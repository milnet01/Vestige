// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_fs_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D2 — pin the tinygltf FsCallbacks sandbox.
///
/// The FsCallbacks override rejects any path tinygltf wants to read that
/// lies outside the .gltf file's parent directory. This closes the
/// confused-deputy / TOCTOU window where the default callbacks would
/// `open()` bytes off disk *before* `resolveUri` got a chance to run on
/// the URI.
///
/// Tests here drive minimal glTF files (asset + buffers + bufferViews +
/// accessors only — no meshes, no images, no GL state) so the load path
/// runs on the headless CI runner. The buffer load is the most sensitive
/// point because tinygltf calls `ReadWholeFile` for every external `.bin`
/// referenced in `buffers[].uri` — exactly the surface the sandbox is
/// supposed to police.

#include <gtest/gtest.h>
#include "utils/gltf_loader.h"
#include "resource/resource_manager.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::GltfFsSandbox::Test
{

class GltfFsSandboxTest : public ::testing::Test
{
protected:
    fs::path m_root;        ///< Per-test scratch root.
    fs::path m_inner;       ///< Sandbox directory (where the .gltf lives).
    fs::path m_outside;     ///< Sibling directory the sandbox must NOT permit.
    fs::path m_gltfPath;    ///< Path to the .gltf inside m_inner.

    void SetUp() override
    {
        Logger::clearEntries();

        m_root = fs::temp_directory_path()
               / ("vestige_gltf_fs_sandbox_test_" + Testing::vestigeTestStamp());

        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root);

        m_inner = m_root / "inner";
        m_outside = m_root / "escape";
        fs::create_directories(m_inner);
        fs::create_directories(m_outside);

        m_gltfPath = m_inner / "test.gltf";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    void writeGltf(const std::string& json)
    {
        std::ofstream{m_gltfPath} << json;
    }

    void writeBinary(const fs::path& path, const std::string& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// @brief Counts ring-buffer warnings whose message contains @a needle.
    static size_t countWarningsContaining(const std::string& needle)
    {
        size_t n = 0;
        for (const auto& e : Logger::getEntries())
        {
            if (e.level == LogLevel::Warning
                && e.message.find(needle) != std::string::npos)
            {
                ++n;
            }
        }
        return n;
    }
};

TEST_F(GltfFsSandboxTest, SiblingBufferUriIsAccepted_D2)
{
    // Positive control: a 12-byte buffer next to the .gltf inside the
    // sandbox loads cleanly. Proves the FsCallbacks override doesn't
    // break the happy path.
    const std::string bufferBytes(12, '\x00');
    writeBinary(m_inner / "data.bin", bufferBytes);

    writeGltf(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "data.bin", "byteLength": 12}],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "n"}]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(countWarningsContaining("rejected read outside gltfDir"), 0u);
}

TEST_F(GltfFsSandboxTest, TraversalBufferUriIsRejectedBeforeRead_D2)
{
    // Place a real "secret" file outside the sandbox — without the D2
    // FsCallbacks override, tinygltf would happily slurp these bytes
    // as a buffer payload. The pin is that the load fails AND a
    // sandbox warning is logged.
    const std::string secret = "VESTIGE_SECRET_DO_NOT_LEAK";
    writeBinary(m_outside / "secret.bin", secret);

    writeGltf(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "../escape/secret.bin", "byteLength": 26}],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "n"}]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    EXPECT_EQ(model, nullptr);
    EXPECT_GE(countWarningsContaining("rejected read outside gltfDir"), 1u);
}

TEST_F(GltfFsSandboxTest, AbsolutePathBufferUriIsRejected_D2)
{
    // Same attack as the traversal case, but using an absolute path.
    // tinygltf joins paths only when the URI is relative; an absolute
    // URI bypasses the join and goes straight to the FS callback. The
    // sandbox must catch it there too.
    const fs::path absSecret = m_outside / "abs_secret.bin";
    writeBinary(absSecret, std::string(8, 'X'));

    // glTF JSON requires the URI to be a string; embed the absolute
    // path. Forward slashes work on POSIX; on Windows the test would
    // need backslash escaping but we skip that here since the engine
    // is Linux-first per CLAUDE.md.
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": ")" + absSecret.string() + R"(", "byteLength": 8}],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "n"}]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    EXPECT_EQ(model, nullptr);
    EXPECT_GE(countWarningsContaining("rejected read outside gltfDir"), 1u);
}

TEST_F(GltfFsSandboxTest, NestedSubdirBufferUriIsAccepted_D2)
{
    // Buffer in a sub-directory below the .gltf. weakly_canonical should
    // place it inside m_inner so the sandbox accepts it.
    const fs::path nestedDir = m_inner / "nested";
    fs::create_directories(nestedDir);
    writeBinary(nestedDir / "data.bin", std::string(4, '\x00'));

    writeGltf(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "nested/data.bin", "byteLength": 4}],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "n"}]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(countWarningsContaining("rejected read outside gltfDir"), 0u);
}

TEST_F(GltfFsSandboxTest, EmbeddedDataUriIsAcceptedWithoutFsCall_D2)
{
    // Data-URIs go through tinygltf's URI decoder, not the FS callback,
    // so they should NOT trigger the sandbox check at all (no warning
    // emitted, load succeeds). This pins the contract that the sandbox
    // only fires for on-disk reads.
    //
    // Eight zero bytes, base64-encoded: "AAAAAAAAAAA="
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "buffers": [{
            "uri": "data:application/octet-stream;base64,AAAAAAAAAAA=",
            "byteLength": 8
        }],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "n"}]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(countWarningsContaining("rejected read outside gltfDir"), 0u);
}

}  // namespace Vestige::GltfFsSandbox::Test
