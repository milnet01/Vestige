// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stills_file.cpp
/// @brief Stills file parser (--stills).
#include "testing/stills_file.h"

#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Vestige
{

namespace
{

bool isValidName(const std::string& name)
{
    for (const char c : name)
    {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
        if (!ok)
        {
            return false;
        }
    }
    return !name.empty();
}

/// Reads one finite number from the whole of @p token.
bool readNumber(const std::string& token, float& out)
{
    std::istringstream in(token);
    in >> out;
    return in && in.peek() == std::char_traits<char>::eof() && std::isfinite(out);
}

} // namespace

StillsParseResult parseStills(const std::string& text)
{
    StillsParseResult result;
    std::istringstream lines(text);
    std::string line;
    int lineNumber = 0;

    while (std::getline(lines, line))
    {
        ++lineNumber;
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        std::string token;
        while (fields >> token)
        {
            tokens.push_back(token);
        }
        if (tokens.empty() || tokens[0][0] == '#')
        {
            continue;
        }

        const std::string where = "line " + std::to_string(lineNumber) + ": ";
        if (tokens.size() != 6)
        {
            result.error = where + "expected 6 fields (name x y z yaw pitch), found "
                         + std::to_string(tokens.size());
            return result;
        }

        StillSpot spot;
        spot.name = tokens[0];
        if (!isValidName(spot.name))
        {
            result.error = where + "name \"" + spot.name
                         + "\" may hold only letters, digits, '_' and '-'";
            return result;
        }

        std::string yToken = tokens[2];
        if (yToken[0] == '~')
        {
            spot.aboveGround = true;
            yToken.erase(0, 1);
        }

        const std::array<float*, 5> targets = {
            &spot.position.x, &spot.position.y, &spot.position.z, &spot.yaw, &spot.pitch,
        };
        const std::array<std::string, 5> numbers = {
            tokens[1], yToken, tokens[3], tokens[4], tokens[5],
        };
        for (size_t i = 0; i < targets.size(); ++i)
        {
            if (!readNumber(numbers[i], *targets[i]))
            {
                result.error = where + "\"" + tokens[i + 1] + "\" is not a number";
                return result;
            }
        }

        result.spots.push_back(spot);
    }

    if (result.spots.empty())
    {
        result.error = "no camera spots (expected lines of: name x y z yaw pitch)";
    }
    return result;
}

StillsParseResult loadStillsFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        StillsParseResult result;
        result.error = "cannot open " + path;
        return result;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseStills(text.str());
}

} // namespace Vestige
