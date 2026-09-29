// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_update_service.cpp
/// @brief 3D_E-0729 UpdateService over a fake transport: the check and its
///        notes, skipped versions, and the verify-before-anything download
///        (INV-2, INV-4, INV-6). No network.

#include <gtest/gtest.h>

#include "update/update_service.h"

#include <monocypher-ed25519.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace Vestige::Update::Test
{
namespace
{

/// Serves canned bodies by URL and records what was asked for.
class FakeTransport final : public IHttpTransport
{
public:
    std::map<std::string, HttpResult> routes;
    std::vector<std::string> requested;

    HttpResult get(const std::string& url, std::size_t maxBytes, const ProgressFn&) override
    {
        requested.push_back(url);
        auto it = routes.find(url);
        if (it == routes.end())
        {
            return HttpResult{404, {}, {}};
        }
        HttpResult r = it->second;
        if (r.body.size() > maxBytes)
        {
            return HttpResult{r.status, {}, "the download is larger than allowed"};
        }
        return r;
    }
};

struct TestKey
{
    std::array<std::uint8_t, 64> secret{};
    PublicKey pub{};
    TestKey()
    {
        std::array<std::uint8_t, 32> seed{};
        seed.fill(3);
        crypto_ed25519_key_pair(secret.data(), pub.data(), seed.data());
    }
    std::string sign(const std::string& version, const std::string& asset,
                     const std::string& data) const
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
        const std::string msg = signedMessage(version, asset, blake2bHex(bytes, data.size()));
        Signature sig{};
        crypto_ed25519_sign(sig.data(), secret.data(),
                            reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
        return std::string(sig.begin(), sig.end());
    }
};

const std::string kAsset = "Vestige-0.1.76-x86_64.AppImage";
const std::string kAssetUrl = "https://github.com/d/" + kAsset;
const std::string kSigUrl = kAssetUrl + ".sig";

std::string latestJson(const std::string& assetUrl = kAssetUrl)
{
    return R"({"tag_name":"v0.1.76","html_url":"https://github.com/r","assets":[)"
           R"({"name":")" + kAsset + R"(","browser_download_url":")" + assetUrl + R"("},)"
           R"({"name":")" + kAsset + R"(.sig","browser_download_url":")" + kSigUrl + R"("}]})";
}

Version installed() { return *parseVersion("0.1.75"); }

FakeTransport offering(const TestKey& key, const std::string& payload,
                       const std::string& sigVersion = "0.1.76")
{
    FakeTransport t;
    t.routes[kReleasesLatestUrl] = {200, latestJson(), {}};
    t.routes[changelogUrl("v0.1.75")] = {200, "# C\n- old\n", {}};
    t.routes[changelogUrl("v0.1.76")] = {200, "# C\n- new\n- old\n", {}};
    t.routes[kAssetUrl] = {200, payload, {}};
    t.routes[kSigUrl] = {200, key.sign(sigVersion, kAsset, payload), {}};
    return t;
}

TEST(UpdateService, OffersTheNewerReleaseWithWhatChanged)
{
    TestKey key;
    auto t = offering(key, "payload");
    UpdateService service(t, installed(), InstallKind::AppImage, key.pub);
    const CheckResult r = service.check("", false);
    ASSERT_EQ(r.status, CheckResult::Status::Available);
    ASSERT_TRUE(r.asset.has_value());
    EXPECT_TRUE(r.notesLoaded);
    EXPECT_EQ(r.notes, std::vector<std::string>{"- new"});
}

TEST(UpdateService, SkippedVersionIsQuietAutomaticallyButOfferedByHand)
{
    TestKey key;
    auto t = offering(key, "payload");
    UpdateService service(t, installed(), InstallKind::AppImage, key.pub);
    EXPECT_EQ(service.check("0.1.76", false).status, CheckResult::Status::UpToDate);
    const CheckResult manual = service.check("0.1.76", true);
    EXPECT_EQ(manual.status, CheckResult::Status::Available);
    EXPECT_TRUE(manual.skipped);
}

TEST(UpdateService, UpToDateAndFailuresAreReported)
{
    TestKey key;
    auto t = offering(key, "payload");
    UpdateService current(t, *parseVersion("0.1.76"), InstallKind::AppImage, key.pub);
    EXPECT_EQ(current.check("", true).status, CheckResult::Status::UpToDate);

    FakeTransport offline;  // every URL 404s
    UpdateService none(offline, installed(), InstallKind::AppImage, key.pub);
    const CheckResult r = none.check("", true);
    EXPECT_EQ(r.status, CheckResult::Status::Failed);
    EXPECT_FALSE(r.error.empty());
}

TEST(UpdateService, NotesMissingStillOffersTheUpdate)
{
    TestKey key;
    auto t = offering(key, "payload");
    t.routes.erase(changelogUrl("v0.1.75"));
    UpdateService service(t, installed(), InstallKind::AppImage, key.pub);
    const CheckResult r = service.check("", false);
    EXPECT_EQ(r.status, CheckResult::Status::Available);
    EXPECT_FALSE(r.notesLoaded);
}

TEST(UpdateService, TarballGetsNotesButNoDownload)
{
    TestKey key;
    auto t = offering(key, "payload");
    UpdateService service(t, installed(), InstallKind::Tarball, key.pub);
    const CheckResult r = service.check("", false);
    EXPECT_EQ(r.status, CheckResult::Status::Available);
    EXPECT_FALSE(r.asset.has_value());
    EXPECT_FALSE(service.download(r, {}).error.empty());
}

// INV-2: bytes come back only when the signature holds for this version.
TEST(UpdateService, DownloadReturnsOnlyVerifiedBytes)
{
    TestKey key;
    auto good = offering(key, "payload");
    UpdateService ok(good, installed(), InstallKind::AppImage, key.pub);
    const DownloadResult d = ok.download(ok.check("", false), {});
    EXPECT_TRUE(d.error.empty()) << d.error;
    EXPECT_EQ(std::string(d.bytes.begin(), d.bytes.end()), "payload");

    auto tampered = offering(key, "payload");
    tampered.routes[kAssetUrl].body = "paylOad";
    UpdateService t1(tampered, installed(), InstallKind::AppImage, key.pub);
    const DownloadResult bad = t1.download(t1.check("", false), {});
    EXPECT_FALSE(bad.error.empty());
    EXPECT_TRUE(bad.bytes.empty());

    // An old build signed for 0.1.75, republished as 0.1.76.
    auto retagged = offering(key, "payload", "0.1.75");
    UpdateService t2(retagged, installed(), InstallKind::AppImage, key.pub);
    EXPECT_TRUE(t2.download(t2.check("", false), {}).bytes.empty());
}

// INV-4: an http:// asset is refused before any request for it.
TEST(UpdateService, HttpAssetIsNeverRequested)
{
    TestKey key;
    auto t = offering(key, "payload");
    t.routes[kReleasesLatestUrl].body = latestJson("http://github.com/d/" + kAsset);
    UpdateService service(t, installed(), InstallKind::AppImage, key.pub);
    const CheckResult r = service.check("", false);
    EXPECT_FALSE(r.asset.has_value());
    for (const auto& url : t.requested)
    {
        EXPECT_EQ(url.rfind("https://", 0), 0u) << url;
    }
}

TEST(UpdateService, OversizedAssetIsRefused)
{
    TestKey key;
    auto t = offering(key, std::string(kMaxAssetBytes + 1, 'x'));
    UpdateService service(t, installed(), InstallKind::AppImage, key.pub);
    const DownloadResult d = service.download(service.check("", false), {});
    EXPECT_FALSE(d.error.empty());
    EXPECT_TRUE(d.bytes.empty());
}

// INV-4: libcurl is reached through one file only.
TEST(UpdateService, OnlyTheTransportIncludesLibcurl)
{
    const std::filesystem::path root = std::filesystem::path(VESTIGE_SHADER_DIR) / ".." / "..";
    const std::regex include(R"(#\s*include\s*[<"]curl/)");
    std::vector<std::string> offenders;
    for (const char* dir : {"engine", "app", "tools"})
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root / dir, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            const auto ext = it->path().extension();
            if (ext != ".cpp" && ext != ".h")
            {
                continue;
            }
            std::ifstream in(it->path());
            std::stringstream buf;
            buf << in.rdbuf();
            if (std::regex_search(buf.str(), include))
            {
                offenders.push_back(it->path().filename().string());
            }
        }
    }
    EXPECT_EQ(offenders, std::vector<std::string>{"http_transport.cpp"});
}

}  // namespace
}  // namespace Vestige::Update::Test

namespace Vestige::Update::Test
{
namespace
{

// Real network, so disabled: run with --gtest_also_run_disabled_tests to
// check libcurl, TLS, GitHub's API and the raw CHANGELOG fetch end to end.
TEST(UpdateServiceLive, DISABLED_ChecksRealGitHub)
{
    CurlTransport transport("Vestige/test");
    UpdateService service(transport, *parseVersion("0.1.70"), InstallKind::Tarball,
                          PublicKey{});
    const CheckResult r = service.check("", true);
    ASSERT_EQ(r.status, CheckResult::Status::Available) << r.error;
    EXPECT_TRUE(r.notesLoaded);
    EXPECT_FALSE(r.notes.empty());
    std::cout << "offered " << toString(r.release.version) << ", " << r.notes.size()
              << " new CHANGELOG lines; first: " << (r.notes.empty() ? "" : r.notes.front())
              << "\n";
}

}  // namespace
}  // namespace Vestige::Update::Test
