// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_obj_mtl_warning.cpp
/// @brief Phase 10.9 Slice 5 D4 — pin that ObjLoader emits a single
///        "MTL not supported" warning when usemtl / mtllib is seen,
///        and that the geometry still loads (one-material flatten).

#include <gtest/gtest.h>
#include "utils/obj_loader.h"
#include "core/logger.h"
#include "renderer/mesh.h"  // Vertex

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::ObjMtlWarning::Test
{

class ObjMtlWarningTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_objPath;

    void SetUp() override
    {
        m_root = fs::temp_directory_path()
               / ("vestige_obj_mtl_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root);
        m_objPath = m_root / "test.obj";
        Logger::clearEntries();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    void writeObj(const std::string& body)
    {
        std::ofstream{m_objPath} << body;
    }

    static int countWarningsContaining(const std::string& needle)
    {
        int count = 0;
        for (const auto& e : Logger::getEntries())
        {
            if (e.level == LogLevel::Warning
                && e.message.find(needle) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }
};

TEST_F(ObjMtlWarningTest, UsemtlEmitsOneWarning_D4)
{
    // Two `usemtl` directives split this triangle pair into two materials
    // in spirit; the loader flattens to one. We pin: one warning per load
    // (not one per directive), and the geometry still loads.
    writeObj(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
        "vn 0 0 1\n"
        "usemtl red\n"
        "f 1//1 2//1 3//1\n"
        "usemtl blue\n"
        "f 2//1 4//1 3//1\n"
    );

    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    EXPECT_TRUE(ObjLoader::load(m_objPath.string(), verts, idx));
    EXPECT_FALSE(verts.empty());
    EXPECT_EQ(idx.size(), 6u);  // two triangles
    EXPECT_EQ(countWarningsContaining("MTL not supported"), 1);
}

TEST_F(ObjMtlWarningTest, MtllibEmitsOneWarning_D4)
{
    writeObj(
        "mtllib test.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n"
    );

    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    EXPECT_TRUE(ObjLoader::load(m_objPath.string(), verts, idx));
    EXPECT_EQ(countWarningsContaining("MTL not supported"), 1);
}

TEST_F(ObjMtlWarningTest, NoMtlDirectivesNoWarning_D4)
{
    // Control: a bare OBJ without any MTL directives should not emit
    // the warning.
    writeObj(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n"
    );

    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    EXPECT_TRUE(ObjLoader::load(m_objPath.string(), verts, idx));
    EXPECT_EQ(countWarningsContaining("MTL not supported"), 0);
}

}  // namespace Vestige::ObjMtlWarning::Test
