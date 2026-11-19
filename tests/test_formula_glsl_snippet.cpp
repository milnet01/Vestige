// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_formula_glsl_snippet.cpp
/// @brief 3D_E-0874 — the Workbench's "Copy as GLSL" text compiles on its own.
///
/// A division is emitted as safeDiv(), which the copied text did not
/// define, so any formula with a '/' failed to compile when pasted.
///
/// Needs a GL context: the test compiles the snippet in a shader.

#include "gl_test_fixture.h"
#include "formula/codegen_glsl.h"
#include "formula/expression.h"
#include "formula/formula.h"

#include <gtest/gtest.h>
#include <glad/gl.h>

#include <map>
#include <string>

namespace Vestige::FormulaGlslSnippet::Test
{

class FormulaGlslSnippetTest : public ::Vestige::Test::GLTestFixture
{
};

/// Compiles `source` as a fragment shader; returns the log, empty on success.
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
        char buffer[1024] = {};
        glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        log = buffer[0] != '\0' ? buffer : "compile failed with no log";
    }
    glDeleteShader(shader);
    return log;
}

FormulaDefinition ratioFormula()
{
    FormulaDefinition formula;
    formula.name = "scaled_ratio";
    formula.description = "k * a / b";
    formula.inputs.push_back({"a", FormulaValueType::FLOAT});
    formula.inputs.push_back({"b", FormulaValueType::FLOAT});
    formula.expressions[QualityTier::FULL] = ExprNode::binaryOp(
        "/",
        ExprNode::binaryOp("*", ExprNode::variable("k"), ExprNode::variable("a")),
        ExprNode::variable("b"));
    return formula;
}

const char* const kMain =
    "out vec4 colour;\n"
    "void main() { colour = vec4(scaledRatio(1.0, 2.0)); }\n";

TEST_F(FormulaGlslSnippetTest, SnippetWithDivisionCompilesWhenPasted_0874)
{
    const FormulaDefinition formula = ratioFormula();
    const std::string snippet = CodegenGlsl::generateSnippet(formula, {{"k", 3.0f}});
    ASSERT_NE(snippet.find("safeDiv("), std::string::npos)
        << "the formula no longer emits safeDiv; pick one that does";

    EXPECT_EQ(compileLog("#version 450 core\n" + snippet + kMain), "");
}

TEST_F(FormulaGlslSnippetTest, TwoPastedSnippetsCompileTogether_0874)
{
    const FormulaDefinition formula = ratioFormula();
    FormulaDefinition other = ratioFormula();
    other.name = "other_ratio";
    const std::string both = CodegenGlsl::generateSnippet(formula, {{"k", 3.0f}})
                           + CodegenGlsl::generateSnippet(other, {{"k", 4.0f}});

    EXPECT_EQ(compileLog("#version 450 core\n" + both + kMain), "");
}

TEST(FormulaGlslSnippet, FormulaWithoutExpressionGivesNothing_0874)
{
    FormulaDefinition formula;
    formula.name = "empty";
    EXPECT_EQ(CodegenGlsl::generateSnippet(formula, {}), "");
}

}  // namespace Vestige::FormulaGlslSnippet::Test
