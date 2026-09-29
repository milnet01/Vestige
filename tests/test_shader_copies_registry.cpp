// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_shader_copies_registry.cpp
/// @brief Every GLSL function copied into more than one shader is listed here.
///
/// The shader loader has no #include, so shared helpers are copied by hand,
/// and a copy that drifts fails at load time or not at all (CLAUDE.md, coding
/// standards summary). Before 3D_E-0688 nobody could list the copies. This
/// test scans assets/shaders: a function defined in two or more files must
/// have an entry in kCopies, and the files in its `same` group must hold
/// identical text (comments and whitespace ignored). A copy that has to
/// differ goes in `differ` with the reason.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Vestige::Test
{
namespace
{

struct Copy
{
    const char* name;
    std::set<std::string> same;    ///< Must be identical.
    std::set<std::string> differ;  ///< Allowed to differ, for `reason`.
    const char* reason;
};

const std::vector<Copy> kCopies = {
    {"SMAAMovc", {"smaa_blend.frag.glsl", "smaa_neighborhood.frag.glsl"}, {}, ""},
    {"calcGrassShadow", {"grass.frag.glsl"}, {"foliage.frag.glsl"},
     "foliage reads the world position from v_fragPosition, grass from v_worldPos"},
    {"cofactorMatrix",
     {"material_preview.vert.glsl", "model_preview.vert.glsl", "scene.vert.glsl"}, {}, ""},
    {"distributionGGX",
     {"material_preview.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl"},
     {"prefilter.frag.glsl"},
     "the IBL prefilter evaluates D at each mip's exact roughness, with no 0.04 floor"},
    {"fresnelSchlick",
     {"material_preview.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl"}, {}, ""},
    {"geometrySchlickGGX",
     {"material_preview.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl"},
     {"brdf_lut.frag.glsl"},
     "the BRDF LUT uses the IBL remap k = a^2/2; direct light uses k = (r+1)^2/8"},
    {"geometrySmith", {"brdf_lut.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl"}, {}, ""},
    {"getCascadeIndex",
     {"foliage.frag.glsl", "grass.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl",
      "tree_mesh.frag.glsl"}, {}, ""},
    {"giSliceCoord", {"gi_inject.comp.glsl", "scene.frag.glsl"}, {}, ""},
    {"hammersley", {"brdf_lut.frag.glsl", "prefilter.frag.glsl"}, {}, ""},
    {"importanceSampleGGX", {"brdf_lut.frag.glsl", "prefilter.frag.glsl"}, {}, ""},
    {"interleavedGradientNoise",
     {"foliage.frag.glsl", "grass.frag.glsl", "scene.frag.glsl", "terrain.frag.glsl",
      "tree_mesh.frag.glsl", "tree_shadow.frag.glsl"}, {}, ""},
    {"radicalInverse_VdC", {"brdf_lut.frag.glsl", "prefilter.frag.glsl"}, {}, ""},
    {"safeNormalize", {"scene.frag.glsl", "scene.vert.glsl", "tree_mesh.vert.glsl"}, {}, ""},
    {"sliceToViewDepth", {"volumetric_inject.comp.glsl", "volumetric_scatter.comp.glsl"},
     {"gi_inject.comp.glsl"}, "gi_inject takes the slice index as a float; same formula"},
    {"viewPosFromDepth", {"contact_shadows.frag.glsl", "ssao.frag.glsl"}, {}, ""},
    {"waterColumnTint", {"screen_quad.frag.glsl", "water.frag.glsl"}, {}, ""},
};

/// Comments stripped, whitespace collapsed.
std::string normalise(const std::string& text)
{
    static const std::regex lineComment(R"(//[^\n]*)");
    static const std::regex blockComment(R"(/\*[\s\S]*?\*/)");
    static const std::regex space(R"(\s+)");
    std::string s = std::regex_replace(text, blockComment, " ");
    s = std::regex_replace(s, lineComment, " ");
    return std::regex_replace(s, space, " ");
}

/// name -> file -> every definition of that name in the file, in order.
using Definitions = std::map<std::string, std::map<std::string, std::string>>;

Definitions scanShaders()
{
    Definitions defs;
    static const std::regex head(R"(^([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*\()");
    for (const auto& entry : std::filesystem::directory_iterator(VESTIGE_SHADER_DIR))
    {
        if (entry.path().extension() != ".glsl")
            continue;
        std::ifstream in(entry.path());
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string src = buf.str();
        const std::string file = entry.path().filename().string();

        size_t lineStart = 0;
        while (lineStart < src.size())
        {
            const size_t lineEnd = std::min(src.find('\n', lineStart), src.size());
            std::smatch m;
            const std::string line = src.substr(lineStart, lineEnd - lineStart);
            const size_t open = src.find('{', lineStart);
            const size_t semi = src.find(';', lineStart);
            if (std::regex_search(line, m, head) && m[2] != "main"
                && open != std::string::npos && open < semi)
            {
                int depth = 0;
                size_t i = open;
                for (; i < src.size(); ++i)
                {
                    if (src[i] == '{') ++depth;
                    if (src[i] == '}' && --depth == 0) break;
                }
                defs[m[2]][file] += normalise(src.substr(lineStart, i + 1 - lineStart)) + "\n";
                lineStart = i + 1;
                continue;
            }
            lineStart = lineEnd + 1;
        }
    }
    return defs;
}

std::string join(const std::set<std::string>& s)
{
    std::string out;
    for (const auto& f : s) out += (out.empty() ? "" : ", ") + f;
    return out;
}

TEST(ShaderCopies, EveryCopiedFunctionIsRegistered)
{
    const Definitions defs = scanShaders();
    ASSERT_FALSE(defs.empty()) << "no shaders found in " << VESTIGE_SHADER_DIR;
    for (const auto& [name, files] : defs)
    {
        if (files.size() < 2)
            continue;
        std::set<std::string> found;
        for (const auto& kv : files) found.insert(kv.first);
        const std::string wanted = name;  // a structured binding cannot be captured in C++17
        auto it = std::find_if(kCopies.begin(), kCopies.end(),
                               [&wanted](const Copy& c) { return wanted == c.name; });
        ASSERT_NE(it, kCopies.end())
            << name << " is copied into " << join(found)
            << " but has no kCopies entry: add one, identical or with its reason";
        std::set<std::string> listed = it->same;
        listed.insert(it->differ.begin(), it->differ.end());
        EXPECT_EQ(join(listed), join(found)) << name << ": kCopies lists other files";
    }
}

TEST(ShaderCopies, RegisteredCopiesMatch)
{
    const Definitions defs = scanShaders();
    for (const Copy& c : kCopies)
    {
        auto it = defs.find(c.name);
        ASSERT_NE(it, defs.end()) << c.name << " no longer exists: remove its entry";
        EXPECT_GE(it->second.size(), 2u) << c.name << " is no longer copied: remove its entry";
        if (!c.differ.empty())
            EXPECT_STRNE(c.reason, "") << c.name << ": say why the copies differ";

        const std::string* first = nullptr;
        std::string firstFile;
        for (const auto& file : c.same)
        {
            auto body = it->second.find(file);
            ASSERT_NE(body, it->second.end()) << c.name << " is not in " << file;
            if (!first)
            {
                first = &body->second;
                firstFile = file;
                continue;
            }
            EXPECT_EQ(body->second, *first)
                << c.name << " in " << file << " has drifted from " << firstFile;
        }
    }
}

}  // namespace
}  // namespace Vestige::Test
