// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_pysr_parser.cpp
/// @brief Unit tests for the PySR-expression → ExprNode parser (W2c).
///
/// The parser powers the "Import as library formula" action in the
/// Workbench's Discover-via-PySR panel. These tests cover the full
/// grammar (literals, variables, arithmetic with precedence, unary
/// functions, power, unary minus, parentheses) and the two rejection
/// paths that matter most for safety: unknown function names (blocked
/// before reaching codegen) and the pysr-only aliases the driver's
/// default op set never emits.

#include <gtest/gtest.h>

#include "formula/expression_eval.h"
#include "pysr_parser.h"

#include <cmath>

using namespace Vestige;
using namespace Vestige::pysr;

namespace
{

// Convenience: parse then evaluate with the given bindings.
// Fails the test if parsing fails.
float parseAndEval(const std::string& src,
                   const ExpressionEvaluator::VariableMap& vars = {})
{
    const ParseResult r = parseExpression(src);
    EXPECT_TRUE(r.error.empty()) << "parse failed: " << r.error;
    EXPECT_NE(r.tree, nullptr);
    ExpressionEvaluator eval;
    return eval.evaluate(*r.tree, vars);
}

} // namespace

// ---------------------------------------------------------------------------
// Happy path — shape checks and value checks
// ---------------------------------------------------------------------------

TEST(PySRParser, LiteralNumber)
{
    const auto r = parseExpression("3.14");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_NE(r.tree, nullptr);
    EXPECT_EQ(r.tree->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(r.tree->value, 3.14f);
    EXPECT_TRUE(r.variables.empty());
}

TEST(PySRParser, ScientificNotation)
{
    EXPECT_FLOAT_EQ(parseAndEval("1.5e-3"), 1.5e-3f);
    EXPECT_FLOAT_EQ(parseAndEval("2e2"),    200.0f);
}

TEST(PySRParser, Variable)
{
    const auto r = parseExpression("x0");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_NE(r.tree, nullptr);
    EXPECT_EQ(r.tree->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(r.tree->name, "x0");
    ASSERT_EQ(r.variables.size(), 1u);
    EXPECT_EQ(r.variables[0], "x0");
}

TEST(PySRParser, ArithmeticPrecedence)
{
    // 1 + 2 * 3 must group multiplication tighter than addition.
    EXPECT_FLOAT_EQ(parseAndEval("1 + 2 * 3"), 7.0f);
    EXPECT_FLOAT_EQ(parseAndEval("(1 + 2) * 3"), 9.0f);
    EXPECT_FLOAT_EQ(parseAndEval("10 - 4 - 2"), 4.0f);  // left-assoc
    EXPECT_FLOAT_EQ(parseAndEval("8 / 4 / 2"), 1.0f);   // left-assoc
}

TEST(PySRParser, UnaryMinusAndPlus)
{
    EXPECT_FLOAT_EQ(parseAndEval("-3"),        -3.0f);
    EXPECT_FLOAT_EQ(parseAndEval("+3"),         3.0f);
    EXPECT_FLOAT_EQ(parseAndEval("-(2 + 1)"),  -3.0f);
    EXPECT_FLOAT_EQ(parseAndEval("1 - -2"),     3.0f);
}

TEST(PySRParser, PowerCaretAndStarStar)
{
    // Both operators must produce a pow BINARY_OP.
    const auto r1 = parseExpression("x0^2");
    ASSERT_TRUE(r1.error.empty()) << r1.error;
    ASSERT_NE(r1.tree, nullptr);
    EXPECT_EQ(r1.tree->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(r1.tree->op, "pow");

    const auto r2 = parseExpression("x0**2");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    EXPECT_EQ(r2.tree->op, "pow");

    EXPECT_FLOAT_EQ(parseAndEval("2^3"),  8.0f);
    EXPECT_FLOAT_EQ(parseAndEval("2**3"), 8.0f);
}

TEST(PySRParser, PowerRightAssociative)
{
    // 2^3^2 = 2^(3^2) = 2^9 = 512, NOT (2^3)^2 = 64.
    EXPECT_FLOAT_EQ(parseAndEval("2^3^2"), 512.0f);
}

TEST(PySRParser, UnaryFunctions)
{
    // Each PySR default unary must land as UNARY_OP with correct name.
    for (const auto& fn : {"cos", "sin", "exp", "log", "sqrt",
                           "abs", "floor", "ceil"})
    {
        const std::string src = std::string(fn) + "(x0)";
        const auto r = parseExpression(src);
        ASSERT_TRUE(r.error.empty()) << "parse failed for " << src
                                     << ": " << r.error;
        ASSERT_NE(r.tree, nullptr);
        EXPECT_EQ(r.tree->type, ExprNodeType::UNARY_OP) << src;
        EXPECT_EQ(r.tree->op, fn) << src;
    }

    EXPECT_FLOAT_EQ(parseAndEval("cos(0)"),     1.0f);
    EXPECT_FLOAT_EQ(parseAndEval("sqrt(16)"),   4.0f);
    EXPECT_FLOAT_EQ(parseAndEval("abs(-2)"),    2.0f);
    EXPECT_FLOAT_EQ(parseAndEval("floor(3.7)"), 3.0f);
}

TEST(PySRParser, NestedFunctions)
{
    // sin(log(e^x)) = sin(x) — classic PySR-style nest.
    const float e = 2.718281828f;
    EXPECT_NEAR(parseAndEval("sin(log(exp(1.0)))"),
                std::sin(1.0f), 1e-5f);
    (void)e;
}

TEST(PySRParser, RealisticEquation)
{
    // Shape PySR would emit for a Gaussian-ish fit:
    //   exp(-(x0 - 1.5)**2) * 0.5
    ExpressionEvaluator::VariableMap vars{{"x0", 1.5f}};
    EXPECT_NEAR(parseAndEval("exp(-(x0 - 1.5)**2) * 0.5", vars),
                0.5f, 1e-5f);
    vars["x0"] = 0.5f;
    EXPECT_NEAR(parseAndEval("exp(-(x0 - 1.5)**2) * 0.5", vars),
                std::exp(-1.0f) * 0.5f, 1e-5f);
}

TEST(PySRParser, CollectsDistinctVariableNames)
{
    const auto r = parseExpression("x0 * x1 + cos(x0) - x2");
    ASSERT_TRUE(r.error.empty()) << r.error;
    // Order is first-seen; x0 appears twice but must only be listed once.
    ASSERT_EQ(r.variables.size(), 3u);
    EXPECT_EQ(r.variables[0], "x0");
    EXPECT_EQ(r.variables[1], "x1");
    EXPECT_EQ(r.variables[2], "x2");
}

TEST(PySRParser, ParenthesisedVariable)
{
    const auto r = parseExpression("((x0))");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_NE(r.tree, nullptr);
    EXPECT_EQ(r.tree->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(r.tree->name, "x0");
}

// ---------------------------------------------------------------------------
// Rejection paths — error must be populated, tree must be null
// ---------------------------------------------------------------------------

TEST(PySRParser, RejectsUnknownFunction)
{
    // ``square`` is a pysr *custom* unary that the driver's default
    // op set never emits. Accepting it would require a codegen-side
    // allowlist change, which we haven't done — so reject.
    const auto r = parseExpression("square(x0)");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_NE(r.error.find("unknown function"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("square"), std::string::npos) << r.error;
}

TEST(PySRParser, RejectsUnbalancedParens)
{
    const auto r = parseExpression("cos(x0");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_NE(r.error.find("expected ')'"), std::string::npos) << r.error;
}

TEST(PySRParser, RejectsTrailingGarbage)
{
    const auto r = parseExpression("x0 + x1 )");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_FALSE(r.error.empty());
}

TEST(PySRParser, RejectsEmptyExpression)
{
    const auto r = parseExpression("");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_FALSE(r.error.empty());
}

TEST(PySRParser, RejectsBareOperator)
{
    const auto r = parseExpression("*x0");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_FALSE(r.error.empty());
}

TEST(PySRParser, RejectsUnexpectedCharacter)
{
    const auto r = parseExpression("x0 & x1");
    EXPECT_EQ(r.tree, nullptr);
    EXPECT_NE(r.error.find("unexpected character"), std::string::npos)
        << r.error;
}

TEST(PySRParser, IsRecognisedFunctionAllowlist)
{
    EXPECT_TRUE(isRecognisedFunction("cos"));
    EXPECT_TRUE(isRecognisedFunction("sqrt"));
    EXPECT_FALSE(isRecognisedFunction("square"));
    EXPECT_FALSE(isRecognisedFunction("system"));   // sanity
    EXPECT_FALSE(isRecognisedFunction(""));
}
