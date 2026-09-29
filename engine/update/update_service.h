// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_service.h
/// @brief Checks GitHub for a newer release, gathers what changed, and
///        downloads and verifies the update (3D_E-0729, spec §4.3-§4.5).
///
/// Synchronous: the editor runs these on a job-system worker and hands the
/// result back with JobSystem::runOnMainThread. Tests drive them directly
/// with a fake IHttpTransport.
#pragma once

#include "update/http_transport.h"
#include "update/update_installer.h"
#include "update/update_release.h"
#include "update/update_signature.h"
#include "update/update_version.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vestige::Update
{

inline constexpr const char* kReleasesLatestUrl =
    "https://api.github.com/repos/milnet01/Vestige/releases/latest";
inline constexpr const char* kReleasesPageUrl =
    "https://github.com/milnet01/Vestige/releases/latest";

inline constexpr std::size_t kMaxApiBytes = std::size_t{256} * 1024;
inline constexpr std::size_t kMaxChangelogBytes = std::size_t{4} * 1024 * 1024;
inline constexpr std::size_t kMaxAssetBytes = std::size_t{200} * 1024 * 1024;

/// @brief The raw CHANGELOG.md at a tag.
std::string changelogUrl(const std::string& tag);

/// @brief What a check found.
struct CheckResult
{
    enum class Status
    {
        UpToDate,   ///< Nothing newer.
        Available,  ///< A newer release; see release / asset / notes.
        Failed,     ///< Could not reach or read GitHub; see error.
    };
    Status status = Status::Failed;
    bool skipped = false;  ///< The offer is the version the user chose to skip.
    ReleaseInfo release;
    std::optional<SelectedAsset> asset;  ///< Empty: notes only (tarball, dev build).
    std::vector<std::string> notes;      ///< CHANGELOG lines added since installed.
    bool notesLoaded = false;
    std::string error;
};

/// @brief A downloaded, verified update.
struct DownloadResult
{
    std::vector<std::uint8_t> bytes;  ///< Exactly the bytes that verified.
    std::string error;                ///< Empty on success.
};

class UpdateService
{
public:
    UpdateService(IHttpTransport& transport, Version installed, InstallKind kind,
                  PublicKey key);

    /// @brief Asks GitHub for the latest stable release.
    /// @param skippedVersion The version the user chose to skip, or empty.
    /// @param manual True for Help > Check for Updates: a skipped version is
    ///        still offered (with `skipped` set); an automatic check reports
    ///        it as UpToDate.
    /// @param progress Returning false cancels (used to stop a check when the
    ///        editor exits).
    ///
    /// An AppImage or Windows-zip install is offered nothing when the release
    /// lacks exactly one https asset and its .sig for that kind (spec §4.6):
    /// the result is UpToDate.
    CheckResult check(const std::string& skippedVersion, bool manual,
                      const ProgressFn& progress = {});

    /// @brief Downloads the offered asset and its signature and verifies them.
    ///        Returns the bytes only if the signature holds for the offered
    ///        version (INV-2).
    DownloadResult download(const CheckResult& offer, const ProgressFn& progress);

private:
    IHttpTransport& m_transport;
    Version m_installed;
    InstallKind m_kind;
    PublicKey m_key;
};

}  // namespace Vestige::Update
