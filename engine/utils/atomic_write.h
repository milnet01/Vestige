// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file atomic_write.h
/// @brief Durable, atomic file writes.
///
/// Implements the write-temp-fsync-rename-fsync-dir dance that
/// POSIX requires for crash-safe file replacement. A crash mid-write
/// leaves either the old file intact or the new file fully written —
/// never a truncated hybrid. Matches the algorithm documented by
/// `write-file-atomic` (npm) and `atomic-write-file`, and called out
/// by Calvin Loncaric's "How to Durably Write a File on POSIX"
/// (https://calvin.loncaric.us/articles/CreateFile.html).
///
/// Steps:
///   1. Write payload to `<target>.tmp` in the same directory.
///      (Same-directory is required so `rename(2)` stays atomic —
///      cross-filesystem rename falls back to copy+unlink.)
///   2. `fsync(tmp_fd)` — flush file contents to disk.
///   3. `rename(<target>.tmp, <target>)` — atomic on POSIX for
///      same-filesystem renames.
///   4. `fsync(dir_fd)` — persist the directory entry itself, so the
///      rename survives a power loss after the call returns.
///
/// Step 4 is the subtle one: `rename(2)` alone does not guarantee
/// the directory entry has been flushed, so a crash between
/// `rename` returning and the next checkpoint can leave the old
/// file after reboot.
///
/// Windows path uses `MoveFileExW(MOVEFILE_REPLACE_EXISTING |
/// MOVEFILE_WRITE_THROUGH)` which is documented to be atomic on
/// NTFS and synchronously flushed.
///
/// Slice 13.1 ships this for Settings. Save-games and any future
/// persistence layer should use the same helper — duplicating the
/// dance is the shortest path to a corrupted-file bug report.
#pragma once

#include <filesystem>
#include <string_view>

namespace Vestige
{
namespace AtomicWrite
{

/// @brief Result of an atomic write attempt.
enum class Status
{
    Ok,                 ///< Wrote, fsync'd, renamed, fsync'd directory.
    TempWriteFailed,    ///< Could not open / write the .tmp file.
    FsyncFailed,        ///< fsync on the .tmp file returned an error.
    RenameFailed,       ///< rename(.tmp, target) failed. .tmp is left for inspection.
    DirFsyncFailed,     ///< rename succeeded but directory fsync failed — target exists but durability is not guaranteed.
};

/// @brief Writes @a contents to @a targetPath atomically and durably.
///
/// Creates parent directories if missing. Does *not* throw on I/O
/// failure — returns a Status so callers can decide logging policy
/// (Settings logs a warning and falls back to in-memory state).
/// Overwrites an existing file. If the function returns anything
/// other than `Ok`, the target is either unchanged (old contents
/// still there) or the write was durable but the directory entry
/// may not have been flushed.
///
/// @param targetPath Absolute or relative path to the final file.
/// @param contents   Bytes to write — may contain NULs.
/// @returns Status code; `Ok` on success.
Status writeFile(const std::filesystem::path& targetPath,
                 std::string_view contents);

/// @brief Human-readable label for a Status, for logging.
const char* describe(Status s);

#ifdef VESTIGE_TEST_HOOKS
/// @brief Test-only hooks. Gated behind `VESTIGE_TEST_HOOKS` so the
///        symbols are unreachable in production builds. Tests use these
///        to pin the rollback contract (Phase 10.9 Slice 12 Ed11
///        scene-envelope atomicity depends on it: a forced write
///        failure must leave the target file unchanged).
namespace TestHooks
{

/// @brief Causes the next call to `writeFile` to return @a status
///        without touching the filesystem. The flag self-clears after
///        firing once. Subsequent writes behave normally.
void forceNextWriteFailure(Status status);

/// @brief Resets any pending forced failure. Call from test TearDown
///        to avoid leaking state between tests.
void clearForcedFailure();

} // namespace TestHooks
#endif

} // namespace AtomicWrite
} // namespace Vestige
