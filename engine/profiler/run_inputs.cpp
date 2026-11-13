// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file run_inputs.cpp
/// @brief The record of which asset files a run read (3D_E-0972).
#include "profiler/run_inputs.h"

#include "core/logger.h"
#include "utils/sha256.h"

#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <system_error>

namespace Vestige
{

namespace
{

std::mutex s_mutex;
std::set<std::string> s_noted;  // guarded by s_mutex

/// The file's content hash and size. False when it cannot be read.
bool hashFile(const std::string& path, std::string& hex, std::uint64_t& bytes)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    Sha256 hash;
    bytes = 0;
    char buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        hash.update(buffer, got);
        bytes += got;
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    hex = hash.finishHex();
    return ok;
}

} // namespace

void noteRunInput(const std::string& path)
{
    // One spelling per file, so "a/../b" and "b" are the same input.
    std::error_code error;
    std::filesystem::path normal = std::filesystem::absolute(path, error);
    if (error)
    {
        normal = path;
    }
    std::string key = normal.lexically_normal().string();
    std::lock_guard<std::mutex> lock(s_mutex);
    s_noted.insert(std::move(key));
}

std::vector<std::string> notedRunInputs()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return {s_noted.begin(), s_noted.end()};
}

void clearRunInputs()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_noted.clear();
}

std::vector<RunInput> hashRunInputs(const std::vector<std::string>& paths,
                                    const std::filesystem::path& assetRoot)
{
    const std::filesystem::path root = assetRoot.lexically_normal();

    // group name -> (name within the group's hash -> file hash), both sorted.
    std::map<std::string, std::map<std::string, std::string>> groups;
    std::map<std::string, std::uint64_t> groupBytes;
    for (const std::string& path : paths)
    {
        const std::filesystem::path file = std::filesystem::path(path).lexically_normal();
        const std::filesystem::path relative = file.lexically_relative(root);
        if (relative.empty() || *relative.begin() == ".." || !relative.has_parent_path())
        {
            continue;  // outside the asset root, or not in a folder of it
        }
        std::string hex;
        std::uint64_t bytes = 0;
        if (!hashFile(path, hex, bytes))
        {
            Logger::warning("Run inputs: cannot read '" + path + "'; it is left out");
            continue;
        }
        const std::string group = relative.begin()->string();
        groups[group][relative.generic_string()] = hex;
        groupBytes[group] += bytes;
    }

    std::vector<RunInput> inputs;
    for (const auto& [group, files] : groups)
    {
        Sha256 hash;
        for (const auto& [name, hex] : files)
        {
            hash.update(name.data(), name.size() + 1);  // with its terminating zero
            hash.update(hex.data(), hex.size());
            hash.update("\n", 1);
        }
        inputs.push_back({group, hash.finishHex(), groupBytes[group]});
    }
    return inputs;
}

} // namespace Vestige
