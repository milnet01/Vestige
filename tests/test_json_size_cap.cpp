// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_json_size_cap.cpp
/// @brief Phase 10.9 Slice 18 Ts2 — pin `JsonSizeCap` size-cap helpers.
///
/// The cap helpers gate every JSON / text loader against OOM via
/// over-sized inputs (AUDIT H4 / M17–M26). `tests/test_cube_loader_hardening.cpp`
/// claims its 128 MB cap "is delegated to JsonSizeCap, covered by its
/// own tests" — but no JsonSizeCap test file existed before Slice 18.
/// This file closes that claim.

#include <gtest/gtest.h>

#include "utils/json_size_cap.h"

#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using namespace Vestige;

namespace Vestige::Test
{

class JsonSizeCapTest : public ::testing::Test
{
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path()
              / ("vestige_json_size_cap_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    fs::path writeFile(const std::string& name, const std::string& contents)
    {
        const fs::path p = m_dir / name;
        std::ofstream out(p, std::ios::binary);
        out << contents;
        return p;
    }
};

// -- loadTextFileWithSizeCap --

TEST_F(JsonSizeCapTest, LoadTextReturnsContentsForUnderCap)
{
    const fs::path p = writeFile("small.txt", "hello world");
    auto contents = JsonSizeCap::loadTextFileWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/1024);
    ASSERT_TRUE(contents.has_value());
    EXPECT_EQ(*contents, "hello world");
}

TEST_F(JsonSizeCapTest, LoadTextReturnsNulloptForMissingFile)
{
    auto contents = JsonSizeCap::loadTextFileWithSizeCap(
        (m_dir / "does_not_exist.txt").string(),
        "JsonSizeCapTest", /*maxBytes=*/1024);
    EXPECT_FALSE(contents.has_value());
}

TEST_F(JsonSizeCapTest, LoadTextRejectsOverCap)
{
    // Plant a 1 KB file and ask for it with a 128-byte cap.
    const std::string blob(1024, 'A');
    const fs::path p = writeFile("oversized.txt", blob);
    auto contents = JsonSizeCap::loadTextFileWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/128);
    EXPECT_FALSE(contents.has_value())
        << "1 KB file under a 128-byte cap must be rejected";
}

TEST_F(JsonSizeCapTest, LoadTextAcceptsExactlyAtCap)
{
    // Cap is inclusive of the boundary — a file exactly at maxBytes must
    // load. Off-by-one in the cap comparator would fail this.
    const std::string blob(64, 'B');
    const fs::path p = writeFile("at_cap.txt", blob);
    auto contents = JsonSizeCap::loadTextFileWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/64);
    ASSERT_TRUE(contents.has_value());
    EXPECT_EQ(contents->size(), 64u);
}

// -- loadJsonWithSizeCap --

TEST_F(JsonSizeCapTest, LoadJsonParsesValidDocumentUnderCap)
{
    const fs::path p = writeFile("ok.json", R"({"version":1,"name":"test"})");
    auto j = JsonSizeCap::loadJsonWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/1024);
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ((*j)["version"].get<int>(), 1);
    EXPECT_EQ((*j)["name"].get<std::string>(), "test");
}

TEST_F(JsonSizeCapTest, LoadJsonRejectsOverCap)
{
    // 4 KB of padding makes a still-valid JSON over the 1 KB cap.
    std::string padded = "{\"data\":\"";
    padded.append(4096, 'X');
    padded += "\"}";
    const fs::path p = writeFile("oversized.json", padded);
    auto j = JsonSizeCap::loadJsonWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/1024);
    EXPECT_FALSE(j.has_value())
        << "JSON > 1 KB under a 1 KB cap must be rejected before parse";
}

TEST_F(JsonSizeCapTest, LoadJsonRejectsMalformedJson)
{
    const fs::path p = writeFile("bad.json", "{ not valid json ]");
    auto j = JsonSizeCap::loadJsonWithSizeCap(
        p.string(), "JsonSizeCapTest", /*maxBytes=*/1024);
    EXPECT_FALSE(j.has_value());
}

TEST_F(JsonSizeCapTest, LoadJsonReturnsNulloptForMissingFile)
{
    auto j = JsonSizeCap::loadJsonWithSizeCap(
        (m_dir / "missing.json").string(),
        "JsonSizeCapTest", /*maxBytes=*/1024);
    EXPECT_FALSE(j.has_value());
}

// Ts20-CV12: every cap helper tags its diagnostics with the caller-
// supplied context string so an OOM-guard rejection in the log can be
// traced back to the loader that hit it. Pin that the over-cap Error
// entry carries that tag verbatim — the size check fires before parse,
// so the file contents are irrelevant here.
TEST_F(JsonSizeCapTest, OverCapRejectionLogsCallerTag_CV12)
{
    Logger::clearEntries();
    const char* kTag = "CV12_CallerTag";

    const std::string blob(1024, 'Z');
    const fs::path p = writeFile("tagged_oversized.json", blob);
    auto j = JsonSizeCap::loadJsonWithSizeCap(p.string(), kTag, /*maxBytes=*/128);
    ASSERT_FALSE(j.has_value());

    bool found = false;
    for (const auto& entry : Logger::getEntries())
    {
        if (entry.level == LogLevel::Error
            && entry.message.find(kTag) != std::string::npos
            && entry.message.find("cap") != std::string::npos)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "over-cap rejection did not log an Error tagged '"
                       << kTag << "'";
}

}  // namespace Vestige::Test
