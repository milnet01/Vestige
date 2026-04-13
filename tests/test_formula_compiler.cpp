/// @file test_formula_compiler.cpp
/// @brief Unit tests for the Formula Pipeline FP-3: C++/GLSL code generators,
///        LUT generator, and LUT loader.
#include "formula/codegen_cpp.h"
#include "formula/codegen_glsl.h"
#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"
#include "formula/formula_library.h"
#include "formula/lut_generator.h"
#include "formula/lut_loader.h"
#include "formula/physics_templates.h"
#include "formula/safe_math.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace Vestige;

// ===========================================================================
// CodegenCpp — Name conversion
// ===========================================================================

TEST(CodegenCpp, SnakeToCamelCase)
{
    EXPECT_EQ(CodegenCpp::toCppFunctionName("aerodynamic_drag"), "aerodynamicDrag");
    EXPECT_EQ(CodegenCpp::toCppFunctionName("fresnel_schlick"), "fresnelSchlick");
    EXPECT_EQ(CodegenCpp::toCppFunctionName("beer_lambert"), "beerLambert");
    EXPECT_EQ(CodegenCpp::toCppFunctionName("inverse_square_falloff"), "inverseSquareFalloff");
    EXPECT_EQ(CodegenCpp::toCppFunctionName("simple"), "simple");
}

// ===========================================================================
// CodegenCpp — Type conversions
// ===========================================================================

TEST(CodegenCpp, TypeConversion)
{
    EXPECT_EQ(CodegenCpp::toCppType(FormulaValueType::FLOAT), "float");
    EXPECT_EQ(CodegenCpp::toCppType(FormulaValueType::VEC3), "glm::vec3");
    EXPECT_EQ(CodegenCpp::toCppParamType(FormulaValueType::FLOAT), "float");
    EXPECT_EQ(CodegenCpp::toCppParamType(FormulaValueType::VEC3), "const glm::vec3&");
}

// ===========================================================================
// CodegenCpp — Float literal formatting
// ===========================================================================

TEST(CodegenCpp, FloatLiterals)
{
    // Integer-valued floats get decimal point
    std::string lit1 = CodegenCpp::floatLiteral(1.0f);
    EXPECT_NE(lit1.find('.'), std::string::npos);
    EXPECT_NE(lit1.find('f'), std::string::npos);

    // Small values use scientific notation
    std::string litSmall = CodegenCpp::floatLiteral(1.81e-5f);
    EXPECT_NE(litSmall.find('e'), std::string::npos);
    EXPECT_NE(litSmall.find('f'), std::string::npos);

    // Zero
    std::string litZero = CodegenCpp::floatLiteral(0.0f);
    EXPECT_NE(litZero.find('f'), std::string::npos);
}

// ===========================================================================
// CodegenCpp — Expression emission
// ===========================================================================

TEST(CodegenCpp, EmitLiteral)
{
    auto node = ExprNode::literal(3.14f);
    std::string code = CodegenCpp::emitExpression(*node, {});
    EXPECT_NE(code.find("3.14"), std::string::npos);
    EXPECT_NE(code.find('f'), std::string::npos);
}

TEST(CodegenCpp, EmitVariable)
{
    auto node = ExprNode::variable("windSpeed");
    std::string code = CodegenCpp::emitExpression(*node, {});
    EXPECT_EQ(code, "windSpeed");
}

TEST(CodegenCpp, EmitCoefficientInlined)
{
    auto node = ExprNode::variable("Cd");
    std::map<std::string, float> coeffs = {{"Cd", 0.47f}};
    std::string code = CodegenCpp::emitExpression(*node, coeffs);
    // Should inline the coefficient value, not the variable name
    EXPECT_EQ(code.find("Cd"), std::string::npos);
    EXPECT_NE(code.find("0.47"), std::string::npos);
}

TEST(CodegenCpp, EmitBinaryOps)
{
    auto add = ExprNode::binaryOp("+", ExprNode::variable("a"), ExprNode::variable("b"));
    EXPECT_NE(CodegenCpp::emitExpression(*add, {}).find("+"), std::string::npos);

    auto mul = ExprNode::binaryOp("*", ExprNode::literal(2.0f), ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*mul, {}).find("*"), std::string::npos);

    auto pw = ExprNode::binaryOp("pow", ExprNode::variable("x"), ExprNode::literal(2.0f));
    EXPECT_NE(CodegenCpp::emitExpression(*pw, {}).find("std::pow"), std::string::npos);

    auto mn = ExprNode::binaryOp("min", ExprNode::variable("a"), ExprNode::variable("b"));
    EXPECT_NE(CodegenCpp::emitExpression(*mn, {}).find("std::min"), std::string::npos);
}

TEST(CodegenCpp, EmitUnaryOps)
{
    auto s = ExprNode::unaryOp("sin", ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*s, {}).find("std::sin"), std::string::npos);

    auto neg = ExprNode::unaryOp("negate", ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*neg, {}).find("-"), std::string::npos);

    auto sat = ExprNode::unaryOp("saturate", ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*sat, {}).find("std::clamp"), std::string::npos);
}

TEST(CodegenCpp, EmitConditional)
{
    auto cond = ExprNode::conditional(
        ExprNode::variable("flag"),
        ExprNode::literal(1.0f),
        ExprNode::literal(0.0f));
    std::string code = CodegenCpp::emitExpression(*cond, {});
    EXPECT_NE(code.find("flag"), std::string::npos);
    EXPECT_NE(code.find("?"), std::string::npos);
}

// ===========================================================================
// CodegenCpp — Full function generation
// ===========================================================================

TEST(CodegenCpp, GenerateAerodynamicDrag)
{
    auto formula = PhysicsTemplates::createAerodynamicDrag();
    std::string code = CodegenCpp::generateFunction(formula);

    EXPECT_NE(code.find("aerodynamicDrag"), std::string::npos);
    EXPECT_NE(code.find("inline"), std::string::npos);
    EXPECT_NE(code.find("float"), std::string::npos);
    EXPECT_NE(code.find("vDotN"), std::string::npos);
    EXPECT_NE(code.find("surfaceArea"), std::string::npos);
    EXPECT_NE(code.find("return"), std::string::npos);
    // Coefficient Cd=0.47 should be inlined
    EXPECT_NE(code.find("0.47"), std::string::npos);
    // Variable name "Cd" should NOT appear as parameter
    EXPECT_EQ(code.find("float Cd"), std::string::npos);
}

TEST(CodegenCpp, GenerateHeader)
{
    auto templates = PhysicsTemplates::createAll();
    std::vector<const FormulaDefinition*> ptrs;
    for (const auto& t : templates)
        ptrs.push_back(&t);

    std::string header = CodegenCpp::generateHeader(ptrs);

    EXPECT_NE(header.find("#pragma once"), std::string::npos);
    EXPECT_NE(header.find("namespace Vestige::Formulas"), std::string::npos);
    EXPECT_NE(header.find("aerodynamicDrag"), std::string::npos);
    EXPECT_NE(header.find("fresnelSchlick"), std::string::npos);
    EXPECT_NE(header.find("beerLambert"), std::string::npos);
    EXPECT_NE(header.find("hookeSpring"), std::string::npos);
}

// ===========================================================================
// CodegenGlsl — Expression emission
// ===========================================================================

TEST(CodegenGlsl, EmitVariable)
{
    auto node = ExprNode::variable("windSpeed");
    std::string code = CodegenGlsl::emitExpression(*node, {});
    EXPECT_EQ(code, "windSpeed");
}

TEST(CodegenGlsl, NoStdPrefix)
{
    auto s = ExprNode::unaryOp("sin", ExprNode::variable("x"));
    std::string code = CodegenGlsl::emitExpression(*s, {});
    // GLSL: sin(x), not std::sin(x)
    EXPECT_NE(code.find("sin("), std::string::npos);
    EXPECT_EQ(code.find("std::"), std::string::npos);
}

TEST(CodegenGlsl, NoFSuffix)
{
    auto node = ExprNode::literal(3.14f);
    std::string code = CodegenGlsl::emitExpression(*node, {});
    EXPECT_NE(code.find("3.14"), std::string::npos);
    // GLSL float literals don't have 'f' suffix
    EXPECT_EQ(code.find('f'), std::string::npos);
}

TEST(CodegenGlsl, GlslTypes)
{
    EXPECT_EQ(CodegenGlsl::toGlslType(FormulaValueType::FLOAT), "float");
    EXPECT_EQ(CodegenGlsl::toGlslType(FormulaValueType::VEC3), "vec3");
}

TEST(CodegenGlsl, GenerateFunction)
{
    auto formula = PhysicsTemplates::createFresnelSchlick();
    std::string code = CodegenGlsl::generateFunction(formula);

    EXPECT_NE(code.find("fresnelSchlick"), std::string::npos);
    EXPECT_NE(code.find("float"), std::string::npos);
    EXPECT_NE(code.find("cosTheta"), std::string::npos);
    EXPECT_NE(code.find("return"), std::string::npos);
    EXPECT_NE(code.find("pow("), std::string::npos);
    // Should NOT have C++ artifacts
    EXPECT_EQ(code.find("std::"), std::string::npos);
    EXPECT_EQ(code.find("inline"), std::string::npos);
    EXPECT_EQ(code.find("glm::"), std::string::npos);
}

TEST(CodegenGlsl, GenerateFile)
{
    auto templates = PhysicsTemplates::createAll();
    std::vector<const FormulaDefinition*> ptrs;
    for (const auto& t : templates)
        ptrs.push_back(&t);

    std::string file = CodegenGlsl::generateFile(ptrs);

    EXPECT_NE(file.find("DO NOT EDIT"), std::string::npos);
    EXPECT_NE(file.find("aerodynamicDrag"), std::string::npos);
    EXPECT_NE(file.find("fresnelSchlick"), std::string::npos);
    // AUDIT.md §H12 / FIXPLAN E4: prelude must be emitted before functions.
    EXPECT_NE(file.find("safeDiv"), std::string::npos);
    EXPECT_NE(file.find("safeSqrt"), std::string::npos);
    EXPECT_NE(file.find("safeLog"), std::string::npos);
}

// ===========================================================================
// Safe-math parity: evaluator and codegen must agree on degenerate inputs
// ===========================================================================
//
// AUDIT.md §H12 / FIXPLAN E4. Before: evaluator guarded /0, sqrt(-x),
// log(<=0); codegen emitted bare math. LM fitter validated against safe
// evaluator → emitted shader was NaN. Tests here assert both paths now
// emit or express the safe-math wrappers.

TEST(SafeMathParity, CodegenCppEmitsSafeDivOnDivision)
{
    auto div = ExprNode::binaryOp("/",
        ExprNode::variable("a"), ExprNode::variable("b"));
    std::string code = CodegenCpp::emitExpression(*div, {});
    EXPECT_NE(code.find("SafeMath::safeDiv"), std::string::npos)
        << "Emitted C++ must use safeDiv, not bare /, for parity with "
           "ExpressionEvaluator (AUDIT.md §H12).";
    EXPECT_EQ(code.find(" / "), std::string::npos)
        << "Emitted C++ must not contain bare ' / ' for the division op.";
}

TEST(SafeMathParity, CodegenCppEmitsSafeSqrtAndLog)
{
    auto sq = ExprNode::unaryOp("sqrt", ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*sq, {}).find("SafeMath::safeSqrt"),
              std::string::npos);

    auto lg = ExprNode::unaryOp("log", ExprNode::variable("x"));
    EXPECT_NE(CodegenCpp::emitExpression(*lg, {}).find("SafeMath::safeLog"),
              std::string::npos);
}

TEST(SafeMathParity, CodegenGlslEmitsSafeDivAndHelpers)
{
    auto div = ExprNode::binaryOp("/",
        ExprNode::variable("a"), ExprNode::variable("b"));
    EXPECT_NE(CodegenGlsl::emitExpression(*div, {}).find("safeDiv("),
              std::string::npos);

    auto sq = ExprNode::unaryOp("sqrt", ExprNode::variable("x"));
    EXPECT_NE(CodegenGlsl::emitExpression(*sq, {}).find("safeSqrt("),
              std::string::npos);

    auto lg = ExprNode::unaryOp("log", ExprNode::variable("x"));
    EXPECT_NE(CodegenGlsl::emitExpression(*lg, {}).find("safeLog("),
              std::string::npos);
}

TEST(SafeMathParity, GlslPreludeDefinesAllThreeHelpers)
{
    std::string prelude = CodegenGlsl::safeMathPrelude();
    EXPECT_NE(prelude.find("float safeDiv"), std::string::npos);
    EXPECT_NE(prelude.find("float safeSqrt"), std::string::npos);
    EXPECT_NE(prelude.find("float safeLog"), std::string::npos);
}

TEST(SafeMathParity, EvaluatorAndHelpersAgreeOnDegenerateInputs)
{
    // Evaluator now routes through SafeMath; assert the contract still
    // holds on the exact degenerate inputs that used to diverge.
    ExpressionEvaluator eval;

    // Divide by zero → 0 (not inf/NaN).
    auto div = ExprNode::binaryOp("/",
        ExprNode::literal(1.0f), ExprNode::literal(0.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*div, {}), 0.0f);

    // sqrt(-x) → sqrt(|x|).
    auto sq = ExprNode::unaryOp("sqrt", ExprNode::literal(-9.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*sq, {}), 3.0f);

    // log(-x) → 0.
    auto lg = ExprNode::unaryOp("log", ExprNode::literal(-1.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*lg, {}), 0.0f);

    // log(0) → 0.
    auto lgz = ExprNode::unaryOp("log", ExprNode::literal(0.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*lgz, {}), 0.0f);
}

TEST(SafeMathParity, HelpersMatchEvaluatorPrecisely)
{
    // Direct call into the helper namespace — the point is the *same
    // function body* lives in eval and codegen. The evaluator test above
    // proves eval calls the helpers; this test pins the helpers'
    // concrete contracts.
    EXPECT_FLOAT_EQ(SafeMath::safeDiv(1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SafeMath::safeDiv(6.0f, 3.0f), 2.0f);
    EXPECT_FLOAT_EQ(SafeMath::safeSqrt(-16.0f), 4.0f);
    EXPECT_FLOAT_EQ(SafeMath::safeSqrt(16.0f), 4.0f);
    EXPECT_FLOAT_EQ(SafeMath::safeLog(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SafeMath::safeLog(-5.0f), 0.0f);
    EXPECT_NEAR(SafeMath::safeLog(2.71828f), 1.0f, 1e-4f);
}

// ===========================================================================
// Codegen vs Evaluator — Generated expressions must match interpreted results
// ===========================================================================

class CodegenAccuracy : public ::testing::Test
{
protected:
    ExpressionEvaluator evaluator;
};

TEST_F(CodegenAccuracy, AerodynamicDragExpressionMatches)
{
    auto formula = PhysicsTemplates::createAerodynamicDrag();
    const auto* expr = formula.getExpression();
    ASSERT_NE(expr, nullptr);

    // The generated C++ code should produce the same expression tree structure
    // that, when evaluated with the same inputs, gives the same result.
    // We verify indirectly: the emitted expression should contain the coefficient
    // value (inlined) and the variable names.
    std::string cppCode = CodegenCpp::emitExpression(*expr, formula.coefficients);
    std::string glslCode = CodegenGlsl::emitExpression(*expr, formula.coefficients);

    // Both should inline Cd=0.47
    EXPECT_NE(cppCode.find("0.47"), std::string::npos);
    EXPECT_NE(glslCode.find("0.47"), std::string::npos);

    // Both should reference input variables
    EXPECT_NE(cppCode.find("vDotN"), std::string::npos);
    EXPECT_NE(glslCode.find("vDotN"), std::string::npos);

    // Evaluate with known inputs
    std::unordered_map<std::string, float> vars = {
        {"vDotN", 5.0f},
        {"surfaceArea", 2.0f},
        {"airDensity", 1.225f}
    };
    float evaluated = evaluator.evaluate(*expr, vars, {{"Cd", 0.47f}});
    // 0.5 * 0.47 * 1.225 * 2.0 * 5.0 = 2.87875
    EXPECT_NEAR(evaluated, 2.87875f, 0.001f);
}

TEST_F(CodegenAccuracy, FresnelSchlickExpressionMatches)
{
    auto formula = PhysicsTemplates::createFresnelSchlick();
    const auto* expr = formula.getExpression();
    ASSERT_NE(expr, nullptr);

    std::unordered_map<std::string, float> vars = {{"cosTheta", 0.5f}};
    float evaluated = evaluator.evaluate(*expr, vars, {{"F0", 0.02f}});
    // F0 + (1 - F0) * (1 - 0.5)^5 = 0.02 + 0.98 * 0.03125 = 0.050625
    EXPECT_NEAR(evaluated, 0.050625f, 0.001f);
}

TEST_F(CodegenAccuracy, BeerLambertExpressionMatches)
{
    auto formula = PhysicsTemplates::createBeerLambert();
    const auto* expr = formula.getExpression();
    ASSERT_NE(expr, nullptr);

    std::unordered_map<std::string, float> vars = {{"I0", 1.0f}, {"depth", 2.0f}};
    float evaluated = evaluator.evaluate(*expr, vars, {{"alpha", 0.4f}});
    // exp(-0.4 * 2.0) = exp(-0.8) ~ 0.4493
    EXPECT_NEAR(evaluated, std::exp(-0.8f), 0.001f);
}

// ===========================================================================
// LUT Generator
// ===========================================================================

TEST(LutGenerator, Generate1D)
{
    // Generate a 1D LUT for exp(-alpha * depth), alpha=0.4
    auto formula = PhysicsTemplates::createBeerLambert();

    LutAxisDef axis;
    axis.variableName = "depth";
    axis.minValue = 0.0f;
    axis.maxValue = 10.0f;
    axis.resolution = 64;

    auto result = LutGenerator::generate(formula, {axis}, QualityTier::FULL,
                                          {{"I0", 1.0f}});

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.data.size(), 64u);
    EXPECT_EQ(result.axes.size(), 1u);

    // At depth=0, should be ~1.0 (I0 * exp(0))
    EXPECT_NEAR(result.data[0], 1.0f, 0.01f);

    // At depth=10, should be small (exp(-4.0) ~ 0.018)
    EXPECT_NEAR(result.data[63], std::exp(-4.0f), 0.01f);
}

TEST(LutGenerator, Generate2D)
{
    // 2D LUT for fresnel: varies cosTheta
    auto formula = PhysicsTemplates::createFresnelSchlick();

    // We'll sample over two axes even though formula has 1 input —
    // use F0 as a variable instead of coefficient for this test
    FormulaDefinition modifiedFormula = formula.clone();
    modifiedFormula.inputs.push_back({"F0", FormulaValueType::FLOAT, "", 0.02f});
    modifiedFormula.coefficients.clear();

    LutAxisDef axis1{"cosTheta", 0.0f, 1.0f, 32};
    LutAxisDef axis2{"F0", 0.0f, 0.1f, 16};

    auto result = LutGenerator::generate(modifiedFormula, {axis1, axis2});

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.data.size(), 32u * 16u);
}

TEST(LutGenerator, InvalidAxes)
{
    auto formula = PhysicsTemplates::createBeerLambert();

    // No axes
    auto r1 = LutGenerator::generate(formula, {});
    EXPECT_FALSE(r1.success);

    // Resolution too small
    LutAxisDef badAxis{"depth", 0.0f, 10.0f, 1};
    auto r2 = LutGenerator::generate(formula, {badAxis});
    EXPECT_FALSE(r2.success);
}

TEST(LutGenerator, FnvHashConsistency)
{
    // Same input should always produce same hash
    EXPECT_EQ(LutGenerator::fnv1aHash("depth"), LutGenerator::fnv1aHash("depth"));
    // Different inputs should produce different hashes
    EXPECT_NE(LutGenerator::fnv1aHash("depth"), LutGenerator::fnv1aHash("angle"));
}

// ===========================================================================
// LUT File I/O
// ===========================================================================

class LutFileTest : public ::testing::Test
{
protected:
    std::string tmpPath;

    void SetUp() override
    {
        tmpPath = std::filesystem::temp_directory_path().string() + "/test_lut.vlut";
    }

    void TearDown() override
    {
        std::remove(tmpPath.c_str());
    }
};

TEST_F(LutFileTest, WriteAndReadBack)
{
    auto formula = PhysicsTemplates::createBeerLambert();

    LutAxisDef axis{"depth", 0.0f, 10.0f, 32};
    auto result = LutGenerator::generate(formula, {axis}, QualityTier::FULL,
                                          {{"I0", 1.0f}});
    ASSERT_TRUE(result.success);

    ASSERT_TRUE(LutGenerator::writeToFile(result, tmpPath));

    LutLoader loader;
    ASSERT_TRUE(loader.loadFromFile(tmpPath));

    EXPECT_EQ(loader.dimensions(), 1);
    EXPECT_TRUE(loader.isLoaded());

    // Sample at depth=0 should be ~1.0
    EXPECT_NEAR(loader.sample1D(0.0f), 1.0f, 0.02f);

    // Sample at depth=5 should be ~exp(-2.0)
    EXPECT_NEAR(loader.sample1D(5.0f), std::exp(-2.0f), 0.02f);

    // Sample at depth=10 should be ~exp(-4.0)
    EXPECT_NEAR(loader.sample1D(10.0f), std::exp(-4.0f), 0.02f);
}

TEST_F(LutFileTest, WriteAndRead2D)
{
    // Create a simple 2D formula: x * y
    FormulaDefinition formula;
    formula.name = "test_multiply";
    formula.category = "test";
    formula.description = "x * y";
    formula.inputs = {
        {"x", FormulaValueType::FLOAT, "", 0.0f},
        {"y", FormulaValueType::FLOAT, "", 0.0f}
    };
    formula.output = {FormulaValueType::FLOAT, ""};
    formula.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("*", ExprNode::variable("x"), ExprNode::variable("y"));

    LutAxisDef xAxis{"x", 0.0f, 4.0f, 32};
    LutAxisDef yAxis{"y", 0.0f, 4.0f, 32};

    auto result = LutGenerator::generate(formula, {xAxis, yAxis});
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(LutGenerator::writeToFile(result, tmpPath));

    LutLoader loader;
    ASSERT_TRUE(loader.loadFromFile(tmpPath));
    EXPECT_EQ(loader.dimensions(), 2);

    // Sample at (2, 3) should be ~6.0
    EXPECT_NEAR(loader.sample2D(2.0f, 3.0f), 6.0f, 0.3f);

    // Sample at (0, 0) should be ~0.0
    EXPECT_NEAR(loader.sample2D(0.0f, 0.0f), 0.0f, 0.01f);

    // Sample at (4, 4) should be ~16.0
    EXPECT_NEAR(loader.sample2D(4.0f, 4.0f), 16.0f, 0.01f);
}

// ===========================================================================
// LUT Loader — In-memory loading
// ===========================================================================

TEST(LutLoader, LoadFromResult)
{
    auto formula = PhysicsTemplates::createExponentialFog();

    LutAxisDef axis{"distance", 0.0f, 100.0f, 128};
    auto result = LutGenerator::generate(formula, {axis});
    ASSERT_TRUE(result.success);

    LutLoader loader;
    ASSERT_TRUE(loader.loadFromResult(result));
    EXPECT_EQ(loader.dimensions(), 1);

    // At distance=0, visibility=1.0
    EXPECT_NEAR(loader.sample1D(0.0f), 1.0f, 0.01f);

    // At distance=100, visibility=exp(-0.01*100)=exp(-1)~0.3679
    EXPECT_NEAR(loader.sample1D(100.0f), std::exp(-1.0f), 0.02f);
}

TEST(LutLoader, ClampOutOfRange)
{
    auto formula = PhysicsTemplates::createBeerLambert();

    LutAxisDef axis{"depth", 0.0f, 10.0f, 64};
    auto result = LutGenerator::generate(formula, {axis}, QualityTier::FULL,
                                          {{"I0", 1.0f}});
    ASSERT_TRUE(result.success);

    LutLoader loader;
    ASSERT_TRUE(loader.loadFromResult(result));

    // Out-of-range inputs should clamp to boundary values
    float atMin = loader.sample1D(-5.0f);
    EXPECT_NEAR(atMin, 1.0f, 0.01f);  // Clamped to depth=0

    float atMax = loader.sample1D(20.0f);
    float expectedMax = std::exp(-4.0f);
    EXPECT_NEAR(atMax, expectedMax, 0.02f);  // Clamped to depth=10
}

TEST(LutLoader, EmptyLoaderReturnsZero)
{
    LutLoader loader;
    EXPECT_FALSE(loader.isLoaded());
    EXPECT_FLOAT_EQ(loader.sample1D(5.0f), 0.0f);
    EXPECT_FLOAT_EQ(loader.sample2D(5.0f, 5.0f), 0.0f);
    EXPECT_FLOAT_EQ(loader.sample3D(5.0f, 5.0f, 5.0f), 0.0f);
}

// ===========================================================================
// LUT Loader — Invalid file handling
// ===========================================================================

TEST(LutLoader, RejectInvalidFile)
{
    LutLoader loader;
    EXPECT_FALSE(loader.loadFromFile("/nonexistent/path.vlut"));
}

TEST_F(LutFileTest, RejectCorruptedMagic)
{
    // Write a file with invalid magic
    std::ofstream f(tmpPath, std::ios::binary);
    uint32_t badMagic = 0xDEADBEEF;
    f.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
    f.close();

    LutLoader loader;
    EXPECT_FALSE(loader.loadFromFile(tmpPath));
}

// ===========================================================================
// All Physics Templates — Codegen smoke test
// ===========================================================================

TEST(CodegenSmoke, AllTemplatesGenerateValidCpp)
{
    auto templates = PhysicsTemplates::createAll();
    for (const auto& tmpl : templates)
    {
        std::string code = CodegenCpp::generateFunction(tmpl);
        EXPECT_NE(code.find("inline"), std::string::npos)
            << "Template '" << tmpl.name << "' missing 'inline'";
        EXPECT_NE(code.find("return"), std::string::npos)
            << "Template '" << tmpl.name << "' missing 'return'";
    }
}

TEST(CodegenSmoke, AllTemplatesGenerateValidGlsl)
{
    auto templates = PhysicsTemplates::createAll();
    for (const auto& tmpl : templates)
    {
        std::string code = CodegenGlsl::generateFunction(tmpl);
        EXPECT_EQ(code.find("std::"), std::string::npos)
            << "Template '" << tmpl.name << "' contains 'std::' in GLSL";
        EXPECT_NE(code.find("return"), std::string::npos)
            << "Template '" << tmpl.name << "' missing 'return'";
    }
}

TEST(CodegenSmoke, AllTemplatesGenerateLut1D)
{
    auto templates = PhysicsTemplates::createAll();
    for (const auto& tmpl : templates)
    {
        if (tmpl.inputs.empty())
            continue;

        // Generate a 1D LUT over the first input
        LutAxisDef axis;
        axis.variableName = tmpl.inputs[0].name;
        axis.minValue = tmpl.inputs[0].defaultValue;
        axis.maxValue = tmpl.inputs[0].defaultValue + 10.0f;
        axis.resolution = 16;

        auto result = LutGenerator::generate(tmpl, {axis});
        EXPECT_TRUE(result.success)
            << "LUT generation failed for '" << tmpl.name << "': "
            << result.errorMessage;
    }
}
