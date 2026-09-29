// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_service.cpp
#include "update/update_service.h"

#include <algorithm>

namespace Vestige::Update
{

namespace
{

const ProgressFn kNoProgress;

bool isHttps(const std::string& url)
{
    return url.rfind("https://", 0) == 0;
}

}  // namespace

std::string changelogUrl(const std::string& tag)
{
    return "https://raw.githubusercontent.com/milnet01/Vestige/" + tag + "/CHANGELOG.md";
}

UpdateService::UpdateService(IHttpTransport& transport, Version installed, InstallKind kind,
                             PublicKey key)
    : m_transport(transport), m_installed(std::move(installed)), m_kind(kind), m_key(key)
{
}

CheckResult UpdateService::check(const std::string& skippedVersion, bool manual)
{
    CheckResult result;
    const HttpResult latest = m_transport.get(kReleasesLatestUrl, kMaxApiBytes, kNoProgress);
    if (!latest.ok())
    {
        result.error = latest.error.empty()
            ? "GitHub answered with status " + std::to_string(latest.status)
            : latest.error;
        return result;
    }
    auto release = parseLatest(latest.body);
    if (!release)
    {
        result.error = "GitHub's answer could not be read";
        return result;
    }
    result.release = *release;
    if (!isNewer(release->version, m_installed))
    {
        result.status = CheckResult::Status::UpToDate;
        return result;
    }
    result.skipped = !skippedVersion.empty() && toString(release->version) == skippedVersion;
    if (result.skipped && !manual)
    {
        result.status = CheckResult::Status::UpToDate;
        return result;
    }

    result.status = CheckResult::Status::Available;
    if (m_kind == InstallKind::AppImage || m_kind == InstallKind::WindowsZip)
    {
        result.asset = selectAsset(*release, m_kind);
    }

    const HttpResult oldLog = m_transport.get(changelogUrl("v" + toString(m_installed)),
                                              kMaxChangelogBytes, kNoProgress);
    const HttpResult newLog = m_transport.get(changelogUrl(release->tag), kMaxChangelogBytes,
                                              kNoProgress);
    if (oldLog.ok() && newLog.ok())
    {
        result.notes = notesSince(oldLog.body, newLog.body);
        result.notesLoaded = true;
    }
    return result;
}

DownloadResult UpdateService::download(const CheckResult& offer, const ProgressFn& progress)
{
    DownloadResult result;
    if (offer.status != CheckResult::Status::Available || !offer.asset)
    {
        result.error = "there is no download for this kind of install";
        return result;
    }
    const SelectedAsset& chosen = *offer.asset;
    if (!isHttps(chosen.asset.url) || !isHttps(chosen.signature.url))
    {
        result.error = "refused a non-https download";
        return result;
    }

    const HttpResult sig = m_transport.get(chosen.signature.url, 1024, kNoProgress);
    if (!sig.ok())
    {
        result.error = "the signature could not be downloaded: " + sig.error;
        return result;
    }
    if (sig.body.size() != Signature{}.size())
    {
        result.error = "the signature file is malformed";
        return result;
    }
    Signature signature{};
    std::copy(sig.body.begin(), sig.body.end(), signature.begin());

    const HttpResult file = m_transport.get(chosen.asset.url, kMaxAssetBytes, progress);
    if (!file.ok())
    {
        result.error = file.error.empty()
            ? "the download failed with status " + std::to_string(file.status)
            : file.error;
        return result;
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(file.body.data());
    if (!verifyDownload(toString(offer.release.version), chosen.asset.name, data,
                        file.body.size(), signature, m_key))
    {
        result.error = "the download could not be verified, so it was not installed";
        return result;
    }
    result.bytes.assign(data, data + file.body.size());
    return result;
}

}  // namespace Vestige::Update
