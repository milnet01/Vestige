/// @file recent_files.cpp
/// @brief Recent files manager implementation.
#include "editor/recent_files.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
{

void RecentFiles::load()
{
    m_paths.clear();

    fs::path storagePath = getStoragePath();
    if (!fs::exists(storagePath))
    {
        return;
    }

    std::ifstream file(storagePath);
    if (!file.is_open())
    {
        return;
    }

    json data;
    try
    {
        data = json::parse(file);
    }
    catch (const json::parse_error&)
    {
        Logger::warning("RecentFiles: could not parse " + storagePath.string());
        return;
    }

    if (!data.contains("recent_files") || !data["recent_files"].is_array())
    {
        return;
    }

    for (const auto& entry : data["recent_files"])
    {
        if (!entry.is_string())
        {
            continue;
        }

        fs::path p(entry.get<std::string>());

        // Prune entries for files that no longer exist
        if (fs::exists(p))
        {
            m_paths.push_back(p);
        }
    }

    if (m_paths.size() > MAX_ENTRIES)
    {
        m_paths.resize(MAX_ENTRIES);
    }
}

void RecentFiles::save() const
{
    fs::path storagePath = getStoragePath();

    std::error_code ec;
    fs::create_directories(storagePath.parent_path(), ec);
    if (ec)
    {
        Logger::warning("RecentFiles: could not create directory "
                        + storagePath.parent_path().string());
        return;
    }

    json data;
    json paths = json::array();
    for (const auto& p : m_paths)
    {
        paths.push_back(p.string());
    }
    data["recent_files"] = paths;

    std::ofstream file(storagePath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        Logger::warning("RecentFiles: could not write " + storagePath.string());
        return;
    }

    file << data.dump(2);
}

void RecentFiles::addPath(const std::filesystem::path& path)
{
    fs::path absPath = fs::absolute(path);

    // Remove existing duplicate
    m_paths.erase(
        std::remove(m_paths.begin(), m_paths.end(), absPath),
        m_paths.end());

    // Insert at front (most recent first)
    m_paths.insert(m_paths.begin(), absPath);

    if (m_paths.size() > MAX_ENTRIES)
    {
        m_paths.resize(MAX_ENTRIES);
    }

    save();
}

const std::vector<std::filesystem::path>& RecentFiles::getPaths() const
{
    return m_paths;
}

void RecentFiles::removePath(const std::filesystem::path& path)
{
    fs::path absPath = fs::absolute(path);
    m_paths.erase(
        std::remove(m_paths.begin(), m_paths.end(), absPath),
        m_paths.end());
    save();
}

void RecentFiles::clear()
{
    m_paths.clear();
    save();
}

fs::path RecentFiles::getConfigDir()
{
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    fs::path configDir;

    if (xdgConfig && xdgConfig[0] != '\0')
    {
        configDir = fs::path(xdgConfig);
    }
    else
    {
        const char* home = std::getenv("HOME");
        if (home)
        {
            configDir = fs::path(home) / ".config";
        }
        else
        {
            configDir = fs::path("/tmp");
        }
    }

    return configDir / "vestige";
}

fs::path RecentFiles::getStoragePath()
{
    return getConfigDir() / "recent_files.json";
}

} // namespace Vestige
