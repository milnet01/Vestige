// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file atomic_write.cpp
/// @brief Durable, atomic file writes.
#include "utils/atomic_write.h"

#include "core/logger.h"

#include <cstdio>
#include <system_error>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Vestige
{
namespace AtomicWrite
{

namespace
{

#ifdef _WIN32

// Writes @a contents to @a tmpPath. Returns true on success.
// @a outDurable is set to true if the contents reached disk (best-effort);
// on Windows we rely on MoveFileExW(MOVEFILE_WRITE_THROUGH) for the
// final rename step to flush, so this path uses FlushFileBuffers on
// the temp file directly.
bool writeTempFileAndFlush(const fs::path& tmpPath,
                           std::string_view contents)
{
    HANDLE h = ::CreateFileW(
        tmpPath.wstring().c_str(),
        GENERIC_WRITE,
        0,  // no sharing — we own the temp file exclusively
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD bytesWritten = 0;
    BOOL ok = ::WriteFile(
        h,
        contents.data(),
        static_cast<DWORD>(contents.size()),
        &bytesWritten,
        nullptr);
    if (!ok || bytesWritten != contents.size())
    {
        ::CloseHandle(h);
        ::DeleteFileW(tmpPath.wstring().c_str());
        return false;
    }

    // Flush file buffers to disk — Windows analogue of fsync(fd).
    if (!::FlushFileBuffers(h))
    {
        ::CloseHandle(h);
        ::DeleteFileW(tmpPath.wstring().c_str());
        return false;
    }
    ::CloseHandle(h);
    return true;
}

Status renameAtomic(const fs::path& tmpPath, const fs::path& targetPath)
{
    // MOVEFILE_REPLACE_EXISTING — overwrite the target.
    // MOVEFILE_WRITE_THROUGH    — return only after the new name is on disk.
    BOOL ok = ::MoveFileExW(
        tmpPath.wstring().c_str(),
        targetPath.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return ok ? Status::Ok : Status::RenameFailed;
}

#else

// Writes @a contents to @a tmpPath and fsyncs it.
// Returns true on success. Leaves the file in place on failure so
// the caller can decide whether to unlink.
bool writeTempFileAndFlush(const fs::path& tmpPath,
                           std::string_view contents,
                           bool& outFsyncOk)
{
    outFsyncOk = false;
    // O_WRONLY | O_CREAT | O_TRUNC — overwrite any stale .tmp.
    // 0644 — user-readable/writable, group/other read-only.
    int fd = ::open(
        tmpPath.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0)
    {
        return false;
    }

    // Write the full payload. A short write here is vanishingly
    // unlikely for regular files but possible — loop to be safe.
    const char* data = contents.data();
    size_t remaining = contents.size();
    while (remaining > 0)
    {
        ssize_t w = ::write(fd, data, remaining);
        if (w < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ::close(fd);
            ::unlink(tmpPath.c_str());
            return false;
        }
        data += w;
        remaining -= static_cast<size_t>(w);
    }

    // fsync: persist the file's data blocks.
    if (::fsync(fd) != 0)
    {
        // Data not durable; close, unlink, report.
        ::close(fd);
        ::unlink(tmpPath.c_str());
        return false;
    }
    outFsyncOk = true;

    if (::close(fd) != 0)
    {
        // close() can return EIO on NFS etc — treat as write failure.
        ::unlink(tmpPath.c_str());
        return false;
    }
    return true;
}

// Fsyncs the directory containing @a anyFile so the rename is durable.
// Returns true on success.
bool fsyncDirOf(const fs::path& anyFile)
{
    fs::path dirPath = anyFile.parent_path();
    if (dirPath.empty())
    {
        dirPath = ".";
    }
    int dirFd = ::open(dirPath.c_str(), O_RDONLY);
    if (dirFd < 0)
    {
        return false;
    }
    int rc = ::fsync(dirFd);
    ::close(dirFd);
    return rc == 0;
}

#endif

#ifdef VESTIGE_TEST_HOOKS
// Single-shot forced-failure flag. The hook is process-wide because the
// AtomicWrite helper is stateless; tests guard against cross-test
// leakage via `clearForcedFailure()` in TearDown. Not thread-safe — the
// test suite runs sequentially per ctest_discover_tests' default mode.
static bool g_forceFailureArmed = false;
static Status g_forceFailureStatus = Status::TempWriteFailed;
#endif

} // namespace

#ifdef VESTIGE_TEST_HOOKS
namespace TestHooks
{

void forceNextWriteFailure(Status status)
{
    g_forceFailureArmed  = true;
    g_forceFailureStatus = status;
}

void clearForcedFailure()
{
    g_forceFailureArmed  = false;
}

} // namespace TestHooks
#endif

Status writeFile(const fs::path& targetPath, std::string_view contents)
{
#ifdef VESTIGE_TEST_HOOKS
    if (g_forceFailureArmed)
    {
        g_forceFailureArmed = false;
        Logger::warning("AtomicWrite: forced failure hook returning "
                        + std::string(describe(g_forceFailureStatus))
                        + " for " + targetPath.string());
        return g_forceFailureStatus;
    }
#endif

    // Ensure parent dir exists — callers are allowed to pass a path
    // whose directory has never been created.
    std::error_code ec;
    if (!targetPath.parent_path().empty())
    {
        fs::create_directories(targetPath.parent_path(), ec);
        if (ec)
        {
            Logger::warning("AtomicWrite: could not create parent directory "
                            + targetPath.parent_path().string()
                            + ": " + ec.message());
            return Status::TempWriteFailed;
        }
    }

    // Tmp file sits in the same directory as the target so the
    // rename stays within one filesystem (required for atomicity).
    fs::path tmpPath = targetPath;
    tmpPath += ".tmp";

#ifdef _WIN32
    if (!writeTempFileAndFlush(tmpPath, contents))
    {
        return Status::TempWriteFailed;
    }
    Status renameStatus = renameAtomic(tmpPath, targetPath);
    if (renameStatus != Status::Ok)
    {
        // Leave the .tmp in place for post-mortem.
        return renameStatus;
    }
    // On Windows, MOVEFILE_WRITE_THROUGH covers the dir-fsync step.
    return Status::Ok;
#else
    bool fsyncOk = false;
    if (!writeTempFileAndFlush(tmpPath, contents, fsyncOk))
    {
        // Distinguish the two failure modes for logging.
        return fsyncOk ? Status::TempWriteFailed : Status::FsyncFailed;
    }

    // rename(2): atomic on same-filesystem POSIX targets.
    if (::rename(tmpPath.c_str(), targetPath.c_str()) != 0)
    {
        ::unlink(tmpPath.c_str());
        return Status::RenameFailed;
    }

    // fsync the directory entry — without this, a crash can lose
    // the rename even though rename(2) returned success.
    if (!fsyncDirOf(targetPath))
    {
        return Status::DirFsyncFailed;
    }

    return Status::Ok;
#endif
}

const char* describe(Status s)
{
    switch (s)
    {
        case Status::Ok:              return "ok";
        case Status::TempWriteFailed: return "temp-write-failed";
        case Status::FsyncFailed:     return "fsync-failed";
        case Status::RenameFailed:    return "rename-failed";
        case Status::DirFsyncFailed:  return "dir-fsync-failed";
    }
    return "unknown";
}

} // namespace AtomicWrite
} // namespace Vestige
