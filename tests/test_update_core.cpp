// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_update_core.cpp
/// @brief 3D_E-0729 self-updater, the pure half: version order (INV-1), the
///        signature check (INV-2), "what changed" notes (INV-6), asset choice
///        (INV-4) and install detection (INV-7). No network, no files.

#include <gtest/gtest.h>

#include "update/notes_markup.h"
#include "update/update_installer.h"
#include "update/update_key.h"
#include "update/update_release.h"
#include "update/update_signature.h"
#include "update/update_version.h"

#include <monocypher-ed25519.h>

#include <cstring>
#include <string>
#include <vector>

namespace Vestige::Update::Test
{
namespace
{

Version v(const char* text)
{
    auto parsed = parseVersion(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(Version{});
}

template <size_t N>
std::array<std::uint8_t, N> fromHex(const std::string& hex)
{
    std::array<std::uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i)
    {
        out[i] = static_cast<std::uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    return out;
}

// ---------------------------------------------------------------- INV-1

TEST(UpdateVersion, OrdersVersionsNumericallyAndPreReleasesBelowTheirRelease)
{
    EXPECT_TRUE(isNewer(v("0.1.10"), v("0.1.9")));          // not string order
    EXPECT_TRUE(isNewer(v("0.2.0"), v("0.1.99")));
    EXPECT_TRUE(isNewer(v("0.1.76"), v("0.1.76-rc.1")));     // release beats its RC
    EXPECT_FALSE(isNewer(v("0.1.75"), v("0.1.76-rc.1")));    // older stable never offered to an RC
    EXPECT_FALSE(isNewer(v("0.1.76"), v("0.1.76")));
    EXPECT_FALSE(isNewer(v("0.1.76-rc.1"), v("0.1.76")));
    EXPECT_TRUE(isNewer(v("v1.0.0"), v("0.9.9")));           // leading v accepted
}

TEST(UpdateVersion, RejectsShapesThisProjectDoesNotTag)
{
    for (const char* bad : {"", "v", "1.2", "1.2.3.4", "1.2.x", "01.2.3", "1.2.3-", "1.2.3-rc 1",
                            "1..3"})
    {
        EXPECT_FALSE(parseVersion(bad).has_value()) << bad;
    }
    EXPECT_EQ(toString(v("v0.1.76-rc.1")), "0.1.76-rc.1");
}

// ---------------------------------------------------------------- INV-2

// Signed by Python's `cryptography` Ed25519 with seed bytes 0..31 — the same
// library and message shape tools/sign_release.py uses in release.yml. This
// is what proves the CI signer and the app agree.
const char* const kFixtureData = "Vestige update fixture\n";
const char* const kFixturePub = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* const kFixtureSig =
    "55ecfe8a5b6eec2f5370f6bdac7bac3ecf044786103148971830a9aabee510db"
    "508554197658b57410fb433c0a229c40b0a1a7cc75dbf9df5a69fe05db914905";

const std::uint8_t* bytes(const char* s)
{
    return reinterpret_cast<const std::uint8_t*>(s);
}

TEST(UpdateSignature, BlakeMatchesPythonHashlib)
{
    EXPECT_EQ(blake2bHex(bytes(kFixtureData), std::strlen(kFixtureData)),
              "0e56f0c9bf7319e0b2371d5643fd30399d6c93d8ea2e867a77d44509c61baec1"
              "d75763e9b3c4a2f57e728937d4fe12c480e0cce58700c1f3407c7f626724dce8");
}

TEST(UpdateSignature, AcceptsTheSignedFileForItsVersionOnly)
{
    const auto key = fromHex<32>(kFixturePub);
    const auto sig = fromHex<64>(kFixtureSig);
    const size_t n = std::strlen(kFixtureData);
    const char* asset = "Vestige-0.1.76-x86_64.AppImage";

    EXPECT_TRUE(verifyDownload("0.1.76", asset, bytes(kFixtureData), n, sig, key));

    std::string flipped = kFixtureData;
    flipped[0] ^= 1;
    EXPECT_FALSE(verifyDownload("0.1.76", asset, bytes(flipped.c_str()), n, sig, key));
    // An old signed build republished under a newer tag must not verify.
    EXPECT_FALSE(verifyDownload("0.1.77", asset, bytes(kFixtureData), n, sig, key));
    EXPECT_FALSE(verifyDownload("0.1.76", "Vestige-0.1.76-windows-x86_64.zip",
                                bytes(kFixtureData), n, sig, key));
}

TEST(UpdateSignature, RejectsASignatureFromAnotherKey)
{
    const auto sig = fromHex<64>(kFixtureSig);
    EXPECT_FALSE(verifyDownload("0.1.76", "Vestige-0.1.76-x86_64.AppImage",
                                bytes(kFixtureData), std::strlen(kFixtureData), sig,
                                kUpdatePublicKey));

    // And a key the engine made itself signs something only it verifies.
    std::array<std::uint8_t, 32> seed{};
    seed.fill(7);
    std::array<std::uint8_t, 64> secret{};
    PublicKey pub{};
    crypto_ed25519_key_pair(secret.data(), pub.data(), seed.data());
    const std::string msg = signedMessage("0.1.76", "a", "b");
    Signature own{};
    crypto_ed25519_sign(own.data(), secret.data(), bytes(msg.c_str()), msg.size());
    EXPECT_TRUE(verifySignature(msg, own, pub));
    EXPECT_FALSE(verifySignature(msg, own, fromHex<32>(kFixturePub)));
}

// ---------------------------------------------------------------- INV-6

TEST(UpdateNotes, ShowsEachAddedLineOnceInTheOfferedOrder)
{
    const std::string installed =
        "# Changelog\n"
        "## [Unreleased]\n"
        "- **Old fix.**\n"
        "\n"
        "## [0.1.70]\n"
        "- Older entry.\n";
    // Two entries added under two headings, and a second copy of a line the
    // installed file already had once.
    const std::string offered =
        "# Changelog\r\n"
        "## [Unreleased]\r\n"
        "### 2026-09-29 Fixed — B\r\n"
        "- **New fix B.**\r\n"
        "- **Old fix.**\r\n"
        "### 2026-09-20 Added — A\r\n"
        "- **New feature A.**\r\n"
        "- **Old fix.**\r\n"
        "\r\n"
        "## [0.1.70]\r\n"
        "- Older entry.\r\n";
    const std::vector<std::string> expected = {
        "### 2026-09-29 Fixed — B", "- **New fix B.**", "### 2026-09-20 Added — A",
        "- **New feature A.**",     "- **Old fix.**",
    };
    EXPECT_EQ(notesSince(installed, offered), expected);
    EXPECT_TRUE(notesSince(offered, offered).empty());
}

TEST(UpdateNotes, MarkupBecomesHeadingsBulletsAndPlainText)
{
    const auto lines = parseNotes({"### 2026-09-29 Fixed — **Undo**", "- **Bold** and `code`",
                                   "  - nested [link](https://x)", "", "plain"});
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[0].kind, NotesLine::Kind::Heading);
    EXPECT_EQ(lines[0].level, 3);
    EXPECT_EQ(lines[0].text, "2026-09-29 Fixed — Undo");
    EXPECT_EQ(lines[1].kind, NotesLine::Kind::Bullet);
    EXPECT_EQ(lines[1].text, "Bold and code");
    EXPECT_EQ(lines[2].indent, 1);
    EXPECT_EQ(lines[2].text, "nested link");
    EXPECT_EQ(lines[3].kind, NotesLine::Kind::Blank);
    EXPECT_EQ(lines[4].kind, NotesLine::Kind::Paragraph);
}

// ---------------------------------------------------------------- INV-4 (asset choice)

const char* const kLatestJson = R"({
  "tag_name": "v0.1.76",
  "html_url": "https://github.com/milnet01/Vestige/releases/tag/v0.1.76",
  "assets": [
    {"name": "Vestige-0.1.76-x86_64.AppImage", "size": 36000000,
     "browser_download_url": "https://github.com/d/Vestige-0.1.76-x86_64.AppImage"},
    {"name": "Vestige-0.1.76-x86_64.AppImage.sig", "size": 64,
     "browser_download_url": "https://github.com/d/Vestige-0.1.76-x86_64.AppImage.sig"},
    {"name": "Vestige-0.1.76-x86_64.AppImage.zsync", "size": 1,
     "browser_download_url": "https://github.com/d/Vestige-0.1.76-x86_64.AppImage.zsync"},
    {"name": "vestige-0.1.76-windows-x86_64.zip", "size": 22000000,
     "browser_download_url": "http://github.com/d/vestige-0.1.76-windows-x86_64.zip"},
    {"name": "vestige-0.1.76-windows-x86_64.zip.sig", "size": 64,
     "browser_download_url": "https://github.com/d/vestige-0.1.76-windows-x86_64.zip.sig"},
    {"name": "vestige-0.1.76-linux-x86_64.tar.gz", "size": 36000000,
     "browser_download_url": "https://github.com/d/vestige-0.1.76-linux-x86_64.tar.gz"}
  ]
})";

TEST(UpdateRelease, PicksTheSignedHttpsAssetForTheInstallKind)
{
    const auto release = parseLatest(kLatestJson);
    ASSERT_TRUE(release.has_value());
    EXPECT_EQ(toString(release->version), "0.1.76");

    const auto appimage = selectAsset(*release, InstallKind::AppImage);
    ASSERT_TRUE(appimage.has_value());
    EXPECT_EQ(appimage->asset.name, "Vestige-0.1.76-x86_64.AppImage");
    EXPECT_EQ(appimage->signature.name, "Vestige-0.1.76-x86_64.AppImage.sig");

    // The zip is served over http:// here: refused before any request.
    EXPECT_FALSE(selectAsset(*release, InstallKind::WindowsZip).has_value());
    // The tarball has no .sig: nothing offered.
    EXPECT_FALSE(selectAsset(*release, InstallKind::Tarball).has_value());
    EXPECT_FALSE(selectAsset(*release, InstallKind::None).has_value());
}

TEST(UpdateRelease, MalformedJsonOrTagOffersNothing)
{
    EXPECT_FALSE(parseLatest("not json").has_value());
    EXPECT_FALSE(parseLatest(R"({"tag_name": "nightly"})").has_value());
    EXPECT_FALSE(parseLatest(R"({"assets": []})").has_value());
}

// ---------------------------------------------------------------- INV-7 (detection)

TEST(UpdateInstallKind, OnlyARealAppImageDetectsAsAppImage)
{
    EXPECT_EQ(detectInstall("", "/x/V.AppImage", "/tmp/.mount_V", "/tmp/.mount_V/usr/bin/vestige"),
              InstallKind::None);  // developer build
    EXPECT_EQ(detectInstall("windows-zip", nullptr, nullptr, "C:/v/vestige.exe"),
              InstallKind::WindowsZip);
    EXPECT_EQ(detectInstall("linux", nullptr, nullptr, "/opt/vestige/bin/vestige"),
              InstallKind::Tarball);
    EXPECT_EQ(detectInstall("linux", "/home/u/Vestige.AppImage", "/tmp/.mount_Vabc",
                            "/tmp/.mount_Vabc/usr/bin/vestige"),
              InstallKind::AppImage);
    // Variables inherited from another AppImage: this executable is elsewhere.
    EXPECT_EQ(detectInstall("linux", "/home/u/Other.AppImage", "/tmp/.mount_Oxyz",
                            "/opt/vestige/bin/vestige"),
              InstallKind::Tarball);
    // A sibling mount sharing a prefix is not "under" it.
    EXPECT_EQ(detectInstall("linux", "/home/u/Other.AppImage", "/tmp/.mount_O",
                            "/tmp/.mount_Oxyz/usr/bin/vestige"),
              InstallKind::Tarball);
}

}  // namespace
}  // namespace Vestige::Update::Test
