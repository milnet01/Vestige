#include "utils/json_size_cap.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/logger.h"

namespace Vestige::JsonSizeCap
{
    std::optional<nlohmann::json> loadJsonWithSizeCap(
        const std::string& path,
        const char* context,
        std::uintmax_t maxBytes,
        bool strict)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        const std::uintmax_t sz = fs::file_size(path, ec);
        if (ec)
        {
            Logger::warning(std::string(context) + ": cannot stat: " + path
                + " (" + ec.message() + ")");
            return std::nullopt;
        }
        if (sz > maxBytes)
        {
            Logger::error(std::string(context) + ": file exceeds "
                + std::to_string(maxBytes) + "-byte cap: " + path
                + " (" + std::to_string(sz) + " bytes)");
            return std::nullopt;
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            Logger::warning(std::string(context) + ": cannot open: " + path);
            return std::nullopt;
        }

        if (strict)
        {
            try
            {
                return nlohmann::json::parse(file);
            }
            catch (const nlohmann::json::parse_error& e)
            {
                Logger::error(std::string(context) + ": JSON parse error in "
                    + path + ": " + e.what());
                return std::nullopt;
            }
        }

        nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
        if (j.is_discarded())
        {
            Logger::warning(std::string(context) + ": invalid JSON: " + path);
            return std::nullopt;
        }
        return j;
    }

    std::optional<std::string> loadTextFileWithSizeCap(
        const std::string& path,
        const char* context,
        std::uintmax_t maxBytes)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        const std::uintmax_t sz = fs::file_size(path, ec);
        if (ec)
        {
            Logger::warning(std::string(context) + ": cannot stat: " + path
                + " (" + ec.message() + ")");
            return std::nullopt;
        }
        if (sz > maxBytes)
        {
            Logger::error(std::string(context) + ": file exceeds "
                + std::to_string(maxBytes) + "-byte cap: " + path
                + " (" + std::to_string(sz) + " bytes)");
            return std::nullopt;
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            Logger::warning(std::string(context) + ": cannot open: " + path);
            return std::nullopt;
        }

        std::string content;
        content.reserve(static_cast<size_t>(sz));
        content.assign((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        return content;
    }
}
