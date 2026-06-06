// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "utils/utf8.h"

namespace Vestige::utf8
{

namespace
{
constexpr uint32_t kReplacement = 0xFFFD;

// True iff byte at offset i from pos is a continuation byte (0b10xxxxxx).
inline bool isCont(std::string_view s, size_t pos, size_t i)
{
    return pos + i < s.size() &&
           (static_cast<unsigned char>(s[pos + i]) & 0xC0u) == 0x80u;
}

// Low 6 bits of the continuation byte at offset i.
inline uint32_t contBits(std::string_view s, size_t pos, size_t i)
{
    return static_cast<unsigned char>(s[pos + i]) & 0x3Fu;
}
} // namespace

DecodeResult decodeAt(std::string_view s, size_t pos)
{
    if (pos >= s.size())
    {
        return {kReplacement, 1};
    }

    const unsigned char b0 = static_cast<unsigned char>(s[pos]);

    if (b0 < 0x80u)
    {
        return {b0, 1}; // ASCII fast path
    }

    if ((b0 & 0xE0u) == 0xC0u) // 2-byte lead
    {
        if (!isCont(s, pos, 1))
        {
            return {kReplacement, 1};
        }
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | contBits(s, pos, 1);
        if (cp < 0x80u) // overlong
        {
            return {kReplacement, 1};
        }
        return {cp, 2};
    }

    if ((b0 & 0xF0u) == 0xE0u) // 3-byte lead
    {
        if (!isCont(s, pos, 1) || !isCont(s, pos, 2))
        {
            return {kReplacement, 1};
        }
        const uint32_t cp = ((b0 & 0x0Fu) << 12) |
                            (contBits(s, pos, 1) << 6) |
                            contBits(s, pos, 2);
        if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) // overlong or surrogate
        {
            return {kReplacement, 1};
        }
        return {cp, 3};
    }

    if ((b0 & 0xF8u) == 0xF0u) // 4-byte lead
    {
        if (!isCont(s, pos, 1) || !isCont(s, pos, 2) || !isCont(s, pos, 3))
        {
            return {kReplacement, 1};
        }
        const uint32_t cp = ((b0 & 0x07u) << 18) |
                            (contBits(s, pos, 1) << 12) |
                            (contBits(s, pos, 2) << 6) |
                            contBits(s, pos, 3);
        if (cp < 0x10000u || cp > 0x10FFFFu) // overlong or out of range
        {
            return {kReplacement, 1};
        }
        return {cp, 4};
    }

    // Stray continuation byte (0x80..0xBF) or 5/6-byte lead (0xF8..0xFF).
    return {kReplacement, 1};
}

std::vector<uint32_t> decode(std::string_view s)
{
    std::vector<uint32_t> out;
    for (size_t pos = 0; pos < s.size();)
    {
        const DecodeResult r = decodeAt(s, pos);
        out.push_back(r.codepoint);
        pos += static_cast<size_t>(r.bytesRead); // bytesRead >= 1 guarantees progress
    }
    return out;
}

int encode(uint32_t codepoint, std::string& out)
{
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
    {
        codepoint = kReplacement;
    }

    if (codepoint < 0x80u)
    {
        out.push_back(static_cast<char>(codepoint));
        return 1;
    }
    if (codepoint < 0x800u)
    {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return 2;
    }
    if (codepoint < 0x10000u)
    {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return 3;
    }
    out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return 4;
}

bool isHebrew(uint32_t cp)
{
    return (cp >= 0x0590u && cp <= 0x05FFu) ||  // Hebrew block
           (cp >= 0xFB1Du && cp <= 0xFB4Fu);    // Hebrew Presentation Forms
}

bool isGreek(uint32_t cp)
{
    return (cp >= 0x0370u && cp <= 0x03FFu) ||  // Greek and Coptic
           (cp >= 0x1F00u && cp <= 0x1FFFu);    // Greek Extended (polytonic)
}

} // namespace Vestige::utf8
