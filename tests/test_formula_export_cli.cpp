// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_formula_export_cli.cpp
/// @brief 3D_E-0979 and 3D_E-0995 — the Workbench's --export-glsl verb.
///
/// 3D_E-0995: the file --dump-library writes must load in --export-glsl.
/// 3D_E-0979: --prefix P puts P on every emitted name and guard, so two
/// exports compile in one shader; an unknown flag is refused.
///
/// The compile test needs a GL context.

#include "gl_test_fixture.h"
#include "benchmark.h"
#include "formula/codegen_glsl.h"
#include "formula/formula_library.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <glad/gl.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Vestige::FormulaExportCli::Test
{

namespace fs = std::filesystem;

std::optional<int> runExport(std::vector<std::string> args)
{
    args.insert(args.begin(), "formula_workbench");
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    return runExportGlslCli(static_cast<int>(argv.size()), argv.data());
}

std::string slurp(const fs::path& p)
{
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Every identifier-like token in GLSL text, outside `//` comments (the
/// provenance comments quote formula names and descriptions as prose).
std::set<std::string> tokens(const std::string& text)
{
    std::set<std::string> out;
    std::string cur;
    bool inComment = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (inComment)
        {
            inComment = c != '\n';
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            inComment = true;
            if (!cur.empty()) out.insert(cur);
            cur.clear();
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
        {
            cur += c;
        }
        else if (!cur.empty())
        {
            out.insert(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.insert(cur);
    return out;
}

/// A scratch folder removed when the test ends.
struct ScratchDir
{
    fs::path path;
    explicit ScratchDir(const std::string& tag)
        : path(fs::temp_directory_path()
               / ("vestige_" + tag + "_" + Testing::vestigeTestStamp()))
    {
        fs::create_directories(path);
    }
    ~ScratchDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<std::string> builtinFunctionNames()
{
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    std::vector<std::string> names;
    for (const auto* f : library.getAll())
        names.push_back(CodegenGlsl::toGlslFunctionName(f->name));
    return names;
}

const char* const kPreludeNames[] = {"safeDiv", "safeSqrt", "safeLog", "safePow"};

// ---------------------------------------------------------------------------
// 3D_E-0995 — a dumped library exports
// ---------------------------------------------------------------------------

TEST(FormulaExportCli, DumpedLibraryExportsLikeTheBuiltIn_0995)
{
    ScratchDir dir("dump_roundtrip");
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    const fs::path libPath = dir.path / "lib.json";
    std::ofstream(libPath) << dumpLibraryJsonString(library);

    const auto fromDump = runExport(
        {"--export-glsl", libPath.string(), "--out", (dir.path / "dump").string()});
    ASSERT_TRUE(fromDump.has_value());
    ASSERT_EQ(*fromDump, 0) << "the dumped library did not load";

    const auto builtin = runExport({"--export-glsl", "--out", (dir.path / "builtin").string()});
    ASSERT_TRUE(builtin.has_value());
    ASSERT_EQ(*builtin, 0);

    // Same formulas, same expressions, same coefficients, same banner hash.
    EXPECT_EQ(slurp(dir.path / "dump" / "formulas.glsl"),
              slurp(dir.path / "builtin" / "formulas.glsl"));
}

TEST(FormulaExportCli, DumpKeepsTheFieldsItsReadersUse_0995)
{
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    const auto j = nlohmann::json::parse(dumpLibraryJsonString(library));
    ASSERT_TRUE(j.contains("formulas"));
    ASSERT_FALSE(j["formulas"].empty());
    for (const auto& f : j["formulas"])
    {
        for (const char* key : {"name", "category", "description", "inputs",
                                "default_coefficients", "source", "expressions"})
        {
            EXPECT_TRUE(f.contains(key)) << f.value("name", "?") << " lacks " << key;
        }
    }
}

// ---------------------------------------------------------------------------
// 3D_E-0979 — flags
// ---------------------------------------------------------------------------

TEST(FormulaExportCli, UnknownFlagIsRefused_0979)
{
    ScratchDir dir("unknown_flag");
    const auto rc = runExport(
        {"--export-glsl", "--out", (dir.path / "out").string(), "--prefx", "xr_"});
    ASSERT_TRUE(rc.has_value());
    EXPECT_NE(*rc, 0);
    EXPECT_FALSE(fs::exists(dir.path / "out" / "formulas.glsl"));
}

TEST(FormulaExportCli, InvalidPrefixIsRefused_0979)
{
    for (const char* bad : {"gl_", "1x", "a-b", "x__", ""})
    {
        ScratchDir dir("bad_prefix");
        const auto rc = runExport(
            {"--export-glsl", "--out", (dir.path / "out").string(), "--prefix", bad});
        ASSERT_TRUE(rc.has_value());
        EXPECT_NE(*rc, 0) << "prefix '" << bad << "' was accepted";
        EXPECT_FALSE(fs::exists(dir.path / "out" / "formulas.glsl")) << bad;
    }
}

TEST(FormulaExportCli, NoPrefixGivesNoPrefixedName_0979)
{
    ScratchDir dir("no_prefix");
    ASSERT_EQ(runExport({"--export-glsl", "--out", dir.path.string()}).value_or(-1), 0);
    const auto toks = tokens(slurp(dir.path / "formulas.glsl"));
    EXPECT_TRUE(toks.count("safeDiv"));
    EXPECT_TRUE(toks.count("VESTIGE_FORMULA_PRELUDE"));
    EXPECT_TRUE(toks.count("linearToSrgb"));
}

TEST(FormulaExportCli, PrefixReachesEveryNameAndGuard_0979)
{
    ScratchDir dir("prefix");
    ASSERT_EQ(runExport({"--export-glsl", "--out", dir.path.string(),
                         "--prefix", "xr_"}).value_or(-1), 0);

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir.path))
        if (e.path().extension() == ".glsl") files.push_back(e.path());
    ASSERT_GT(files.size(), 1u);

    for (const auto& file : files)
    {
        const auto toks = tokens(slurp(file));
        for (const char* name : kPreludeNames)
            EXPECT_FALSE(toks.count(name)) << file.filename() << " keeps " << name;
        for (const auto& name : builtinFunctionNames())
            EXPECT_FALSE(toks.count(name)) << file.filename() << " keeps " << name;
        EXPECT_FALSE(toks.count("VESTIGE_FORMULA_PRELUDE")) << file.filename();
        for (const auto& t : toks)
        {
            EXPECT_NE(t.rfind("VESTIGE_FORMULA_FN_", 0), 0u)
                << file.filename() << " keeps guard " << t;
        }
        EXPECT_TRUE(toks.count("xr_safeDiv")) << file.filename();
        EXPECT_TRUE(toks.count("xr_VESTIGE_FORMULA_PRELUDE")) << file.filename();
    }

    const auto combined = tokens(slurp(dir.path / "formulas.glsl"));
    EXPECT_TRUE(combined.count("xr_linearToSrgb"));
    EXPECT_TRUE(combined.count("xr_VESTIGE_FORMULA_FN_linearToSrgb"));
}

// ---------------------------------------------------------------------------
// 3D_E-0979 — two exports in one shader
// ---------------------------------------------------------------------------

class FormulaExportCompileTest : public ::Vestige::Test::GLTestFixture
{
};

std::string compileLog(const std::string& source)
{
    const GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    std::string log;
    if (ok != GL_TRUE)
    {
        char buffer[2048] = {};
        glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        log = buffer[0] != '\0' ? buffer : "compile failed with no log";
    }
    glDeleteShader(shader);
    return log;
}

TEST_F(FormulaExportCompileTest, PrefixedAndPlainExportsCompileTogether_0979)
{
    ScratchDir dir("prefix_compile");
    ASSERT_EQ(runExport({"--export-glsl", "--out", (dir.path / "plain").string()})
                  .value_or(-1), 0);
    ASSERT_EQ(runExport({"--export-glsl", "--out", (dir.path / "xr").string(),
                         "--prefix", "xr_"}).value_or(-1), 0);

    const std::string source =
        "#version 450 core\n"
        + slurp(dir.path / "plain" / "formulas.glsl")
        + slurp(dir.path / "xr" / "formulas.glsl")
        + "out vec4 colour;\n"
          "void main() { colour = vec4(linearToSrgb(0.5) + xr_linearToSrgb(0.25)); }\n";
    EXPECT_EQ(compileLog(source), "");
}

} // namespace Vestige::FormulaExportCli::Test
