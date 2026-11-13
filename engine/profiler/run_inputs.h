// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file run_inputs.h
/// @brief Which asset files a run read, for the frame log's `inputs` field
///        (3D_E-0972).
///
/// `frameledger compare` refuses two runs unless their `inputs` match
/// (FLED-0001 § 4.2), so that a slowdown is not blamed on code when a texture
/// or a model changed. Each loader notes the file it opens; when the run ends
/// the noted files under the asset root are hashed and grouped by their
/// top-level folder, one `inputs` entry per folder. Only files the run
/// actually read count, so an asset added for another scene does not make two
/// meadow runs differ.
///
/// CPU / GPU placement: CPU file hashing after the last frame; nothing on the
/// GPU.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief One `inputs` entry: a group of files, named after its folder.
struct RunInput
{
    std::string name;         ///< Top-level folder under the asset root ("models").
    std::string sha256;       ///< 64 hex digits over the group's files (see hashRunInputs).
    std::uint64_t bytes = 0;  ///< Total size of the group's files.
};

/// @brief Records that the run read @p path. Any thread; a path noted twice
///        counts once. Call it where a loader opens a file.
void noteRunInput(const std::string& path);

/// @brief The paths noted so far, sorted, each once.
std::vector<std::string> notedRunInputs();

/// @brief Forgets every noted path (a test).
void clearRunInputs();

/// @brief Hashes the noted files that lie in a folder under @p assetRoot and
///        groups them by that first folder ("models", "textures"). A file
///        outside the root, or directly in it, is not an input: settings and
///        saved scenes are read through the same loaders and are not content
///        the engine ships. A group's hash covers each file's path relative to
///        the root and its content hash, in sorted order, so it depends
///        neither on the order the files were read in nor on where the assets
///        are installed. A file that can no longer be read is left out and
///        named in the log. Groups are returned sorted by name.
std::vector<RunInput> hashRunInputs(const std::vector<std::string>& paths,
                                    const std::filesystem::path& assetRoot);

} // namespace Vestige
