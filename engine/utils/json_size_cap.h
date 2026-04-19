// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Vestige::JsonSizeCap
{
    /// @brief Default JSON file-size cap — 256 MB.
    ///
    /// Matches the existing caps in obj_loader / gltf_loader /
    /// scene_serializer. A malicious or corrupt JSON file would otherwise be
    /// parsed into unbounded memory and OOM-kill the process. (AUDIT H4 /
    /// M17–M26.)
    constexpr std::uintmax_t DEFAULT_MAX_BYTES = 256ULL * 1024ULL * 1024ULL;

    /// @brief Opens @p path, enforces a byte cap via ``filesystem::file_size``,
    ///        and parses the contents as JSON.
    ///
    /// On success returns the parsed document. On failure (missing file,
    /// over-cap, bad JSON) logs an error/warning that includes @p context and
    /// returns ``std::nullopt``. Callers decide whether a missing-file case is
    /// fatal.
    ///
    /// @param path      Path to the JSON file on disk.
    /// @param context   Short identifier used in log messages (e.g. the name
    ///                  of the loader class).
    /// @param maxBytes  Upper bound on the file size in bytes.
    /// @param strict    If ``true``, throws are surfaced as parse errors; if
    ///                  ``false`` (default), parse errors are logged as
    ///                  warnings and ``std::nullopt`` is returned.
    std::optional<nlohmann::json> loadJsonWithSizeCap(
        const std::string& path,
        const char* context,
        std::uintmax_t maxBytes = DEFAULT_MAX_BYTES,
        bool strict = false);

    /// @brief Reads a text file in full, enforcing a byte cap first.
    ///
    /// Used for shader sources, skybox equirect paths, and other bounded-size
    /// text assets where the existing read path was ``stream << rdbuf()`` with
    /// no bound. Returns the full file contents on success, or ``std::nullopt``
    /// on stat/open/parse failure.
    ///
    /// @param path      Path to the text file on disk.
    /// @param context   Short identifier used in log messages.
    /// @param maxBytes  Upper bound on the file size in bytes.
    std::optional<std::string> loadTextFileWithSizeCap(
        const std::string& path,
        const char* context,
        std::uintmax_t maxBytes = DEFAULT_MAX_BYTES);
}
