// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_smaa.cpp
/// @brief SMAA is the reference implementation, and stays so (3D_E-0631).
///
/// Three contracts:
///  1. Every function the SMAA shaders copy from the reference is identical to
///     external/smaa/SMAA.hlsl, and every reference #define they carry has the
///     reference value. A copy that drifts, or a tweaked preset, fails here.
///  2. Smaa uploads the reference AreaTex and SearchTex bytes unchanged.
///  3. The three shipped passes, run through the real Shader loader and the
///     real Smaa resources, detect a stair-stepped edge, produce blend
///     weights for it, and soften it while leaving flat regions untouched.
#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "renderer/framebuffer.h"
#include "renderer/shader.h"
#include "renderer/smaa.h"

#include <AreaTex.h>
#include <SearchTex.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;
using Vestige::Framebuffer;
using Vestige::FramebufferConfig;
using Vestige::Shader;
using Vestige::Smaa;

namespace
{

const char* const kShaders[] = {
    "smaa_edge.vert.glsl",   "smaa_edge.frag.glsl",
    "smaa_blend.vert.glsl",  "smaa_blend.frag.glsl",
    "smaa_neighborhood.vert.glsl", "smaa_neighborhood.frag.glsl",
};

/// Reads a file as lines with CR and trailing whitespace removed, so the
/// reference's CRLF endings compare equal to the shaders' LF endings.
std::vector<std::string> readLines(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << path;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

/// Top-level function definitions: signature line (`type Name(` at column 0)
/// through the first line that is exactly "}". Keyed by name; overloads are
/// kept in file order.
std::map<std::string, std::vector<std::string>> functionsIn(const std::vector<std::string>& lines,
                                                            size_t first, size_t last)
{
    static const std::regex kSignature(R"(^[A-Za-z0-9_]+ ([A-Za-z0-9_]+)\()");
    std::map<std::string, std::vector<std::string>> out;
    for (size_t i = first; i < last; ++i)
    {
        std::smatch m;
        if (!std::regex_search(lines[i], m, kSignature))
        {
            continue;
        }
        std::string body;
        size_t j = i;
        for (; j < last; ++j)
        {
            body += lines[j] + "\n";
            if (lines[j] == "}")
            {
                break;
            }
        }
        out[m[1].str()].push_back(body);
        i = j;
    }
    return out;
}

std::string shaderPath(const std::string& name)
{
    return std::string(VESTIGE_SHADER_DIR) + "/" + name;
}

std::vector<std::string> referenceLines()
{
    return readLines(std::string(VESTIGE_SMAA_REFERENCE_DIR) + "/SMAA.hlsl");
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Source parity with the vendored reference (no GL needed)
// ---------------------------------------------------------------------------

TEST(SmaaReferenceParity, CopiedFunctionsMatchReference)
{
    const auto ref = referenceLines();
    ASSERT_FALSE(ref.empty());
    const auto refFuncs = functionsIn(ref, 0, ref.size());

    for (const char* name : kShaders)
    {
        SCOPED_TRACE(name);
        const auto lines = readLines(shaderPath(name));

        size_t begin = lines.size();
        size_t end = lines.size();
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (lines[i].find("---- BEGIN verbatim external/smaa/SMAA.hlsl") != std::string::npos) begin = i;
            if (lines[i].find("---- END verbatim external/smaa/SMAA.hlsl") != std::string::npos) end = i;
        }
        ASSERT_LT(begin, end) << "BEGIN/END verbatim markers missing";

        const auto copied = functionsIn(lines, begin + 1, end);
        ASSERT_FALSE(copied.empty()) << "no functions between the markers";

        for (const auto& [fn, bodies] : copied)
        {
            auto it = refFuncs.find(fn);
            ASSERT_NE(it, refFuncs.end()) << fn << " is not a reference SMAA function";
            ASSERT_EQ(bodies.size(), it->second.size())
                << fn << ": copy all of its reference overloads, or none";
            for (size_t k = 0; k < bodies.size(); ++k)
            {
                EXPECT_EQ(bodies[k], it->second[k]) << fn << " differs from external/smaa/SMAA.hlsl";
            }
        }
    }
}

TEST(SmaaReferenceParity, ConfigurationDefinesMatchReference)
{
    const auto ref = referenceLines();
    ASSERT_FALSE(ref.empty());

    // Vestige's own: the porting-layer selector and the uniform name.
    const std::vector<std::string> ownDefines = {
        "#define SMAA_GLSL_4",
        "#define SMAA_RT_METRICS u_rtMetrics",
    };

    for (const char* name : kShaders)
    {
        SCOPED_TRACE(name);
        const auto lines = readLines(shaderPath(name));
        int checked = 0;
        for (const auto& line : lines)
        {
            if (line.rfind("#define SMAA", 0) != 0 && line.rfind("#define SMAATexture", 0) != 0)
            {
                continue;
            }
            if (line.find("vestige") != std::string::npos)
            {
                continue; // smaa_edge.frag.glsl's documented sampling override
            }
            if (std::find(ownDefines.begin(), ownDefines.end(), line) != ownDefines.end())
            {
                continue;
            }
            EXPECT_NE(std::find(ref.begin(), ref.end(), line), ref.end())
                << "not a reference SMAA line: " << line;
            ++checked;
        }
        EXPECT_GT(checked, 10) << "configuration block missing";
    }
}

TEST(SmaaReferenceParity, HighPresetValues)
{
    // The preset is the one smaa.h advertises. A tuned value would still be a
    // reference line (another preset), so pin HIGH explicitly.
    for (const char* name : kShaders)
    {
        SCOPED_TRACE(name);
        const auto lines = readLines(shaderPath(name));
        for (const char* want : {"#define SMAA_THRESHOLD 0.1",
                                 "#define SMAA_MAX_SEARCH_STEPS 16",
                                 "#define SMAA_MAX_SEARCH_STEPS_DIAG 8",
                                 "#define SMAA_CORNER_ROUNDING 25"})
        {
            EXPECT_NE(std::find(lines.begin(), lines.end(), std::string(want)), lines.end())
                << "missing: " << want;
        }
    }
}

// ---------------------------------------------------------------------------
// 2 + 3. GPU: the uploaded tables and the three passes
// ---------------------------------------------------------------------------

namespace
{

class SmaaGpuTest : public GLTestFixture {};

/// Full-screen triangle with the engine quad's attribute layout
/// (location 0 = position, location 1 = texcoord).
struct Triangle
{
    GLuint vao = 0;
    GLuint vbo = 0;
    Triangle()
    {
        const float v[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             3.0f, -1.0f, 2.0f, 0.0f,
            -1.0f,  3.0f, 0.0f, 2.0f,
        };
        glCreateVertexArrays(1, &vao);
        glCreateBuffers(1, &vbo);
        glNamedBufferStorage(vbo, sizeof(v), v, 0);
        glVertexArrayVertexBuffer(vao, 0, vbo, 0, 4 * sizeof(float));
        glEnableVertexArrayAttrib(vao, 0);
        glEnableVertexArrayAttrib(vao, 1);
        glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
        glVertexArrayAttribBinding(vao, 0, 0);
        glVertexArrayAttribBinding(vao, 1, 0);
    }
    ~Triangle()
    {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
    void draw() const
    {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
};

std::vector<glm::vec4> readBack(Framebuffer& fbo, int w, int h)
{
    fbo.bind();
    std::vector<glm::vec4> out(static_cast<size_t>(w) * h);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out.data());
    return out;
}

} // namespace

TEST_F(SmaaGpuTest, UploadsReferenceLookupTables)
{
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;
    Smaa smaa(16, 16);

    std::vector<unsigned char> area(sizeof(areaTexBytes));
    glGetTextureImage(smaa.getAreaTexture(), 0, GL_RG, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(area.size()), area.data());
    EXPECT_EQ(0, std::memcmp(area.data(), areaTexBytes, area.size()))
        << "area texture is not the reference AreaTex";

    std::vector<unsigned char> search(sizeof(searchTexBytes));
    glGetTextureImage(smaa.getSearchTexture(), 0, GL_RED, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(search.size()), search.data());
    EXPECT_EQ(0, std::memcmp(search.data(), searchTexBytes, search.size()))
        << "search texture is not the reference SearchTex";
}

TEST_F(SmaaGpuTest, ThreePassesSoftenAStairSteppedEdge)
{
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;
    constexpr int W = 64;
    constexpr int H = 64;

    Shader edgeShader;
    Shader blendShader;
    Shader neighborShader;
    ASSERT_TRUE(edgeShader.loadFromFiles(shaderPath("smaa_edge.vert.glsl"),
                                         shaderPath("smaa_edge.frag.glsl")));
    ASSERT_TRUE(blendShader.loadFromFiles(shaderPath("smaa_blend.vert.glsl"),
                                          shaderPath("smaa_blend.frag.glsl")));
    ASSERT_TRUE(neighborShader.loadFromFiles(shaderPath("smaa_neighborhood.vert.glsl"),
                                             shaderPath("smaa_neighborhood.frag.glsl")));

    // Linear HDR input, like the engine's resolve FBO: white below a shallow
    // boundary rising one row every 4 columns (long stair-steps).
    FramebufferConfig colorConfig;
    colorConfig.width = W;
    colorConfig.height = H;
    colorConfig.samples = 1;
    colorConfig.hasColorAttachment = true;
    colorConfig.hasDepthAttachment = false;
    colorConfig.isFloatingPoint = true;
    Framebuffer input(colorConfig);
    Framebuffer output(colorConfig);

    std::vector<glm::vec4> img(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const float boundary = static_cast<float>(H) * 0.5f + static_cast<float>(x) * 0.25f;
            img[static_cast<size_t>(y) * W + x] =
                (static_cast<float>(y) > boundary) ? glm::vec4(1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    input.bindColorTexture(0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, img.data());

    Smaa smaa(W, H);
    Triangle tri;
    const glm::vec4 rtMetrics(1.0f / W, 1.0f / H, static_cast<float>(W), static_cast<float>(H));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, W, H);

    // Pass 1: edges
    smaa.getEdgeFbo().bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    edgeShader.use();
    input.bindColorTexture(0);
    edgeShader.setInt("u_colorTexture", 0);
    edgeShader.setVec4("u_rtMetrics", rtMetrics);
    tri.draw();
    const auto edges = readBack(smaa.getEdgeFbo(), W, H);

    // Pass 2: blend weights
    smaa.getBlendFbo().bind();
    glClear(GL_COLOR_BUFFER_BIT);
    blendShader.use();
    smaa.getEdgeFbo().bindColorTexture(0);
    blendShader.setInt("u_edgeTexture", 0);
    glBindTextureUnit(1, smaa.getAreaTexture());
    blendShader.setInt("u_areaTexture", 1);
    glBindTextureUnit(2, smaa.getSearchTexture());
    blendShader.setInt("u_searchTexture", 2);
    blendShader.setVec4("u_rtMetrics", rtMetrics);
    tri.draw();
    const auto weights = readBack(smaa.getBlendFbo(), W, H);

    // Pass 3: neighbourhood blend
    output.bind();
    glClear(GL_COLOR_BUFFER_BIT);
    neighborShader.use();
    input.bindColorTexture(0);
    neighborShader.setInt("u_colorTexture", 0);
    smaa.getBlendFbo().bindColorTexture(1);
    neighborShader.setInt("u_blendTexture", 1);
    neighborShader.setVec4("u_rtMetrics", rtMetrics);
    tri.draw();
    const auto out = readBack(output, W, H);
    Framebuffer::unbind();

    auto at = [&](const std::vector<glm::vec4>& v, int x, int y) {
        return v[static_cast<size_t>(y) * W + x];
    };

    int edgeTexels = 0;
    int weightTexels = 0;
    int softened = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const glm::vec4 e = at(edges, x, y);
            if (e.r > 0.5f || e.g > 0.5f) ++edgeTexels;
            const glm::vec4 wgt = at(weights, x, y);
            if (wgt.r + wgt.g + wgt.b + wgt.a > 0.01f) ++weightTexels;
            const float v = at(out, x, y).r;
            if (v > 0.1f && v < 0.9f) ++softened;
        }
    }

    // Flat regions: no edges and no change.
    EXPECT_EQ(at(edges, 10, 4).r + at(edges, 10, 4).g, 0.0f);
    EXPECT_NEAR(at(out, 10, 4).r, 0.0f, 1e-3f) << "black region changed";
    EXPECT_NEAR(at(out, 10, 60).r, 1.0f, 1e-3f) << "white region changed";

    // The boundary crosses all 64 columns, so it has at least that many edge texels.
    EXPECT_GE(edgeTexels, W) << "edge pass found " << edgeTexels << " texels";
    EXPECT_GT(weightTexels, W / 2) << "blend pass produced " << weightTexels << " weighted texels";
    EXPECT_GT(softened, 20) << "SMAA softened only " << softened << " pixels";
}
