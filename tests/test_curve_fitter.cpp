/// @file test_curve_fitter.cpp
/// @brief Unit tests for the Levenberg-Marquardt curve fitter.
#include <gtest/gtest.h>

#include "formula/curve_fitter.h"
#include "formula/expression.h"
#include "formula/formula.h"
#include "formula/formula_library.h"
#include "formula/formula_preset.h"
#include "formula/physics_templates.h"

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helper: build a formula definition from a raw expression
// ---------------------------------------------------------------------------

static FormulaDefinition makeFormula(
    const std::string& name,
    std::vector<FormulaInput> inputs,
    std::map<std::string, float> coeffs,
    std::unique_ptr<ExprNode> expr)
{
    FormulaDefinition def;
    def.name = name;
    def.category = "test";
    def.description = "Test formula";
    def.inputs = std::move(inputs);
    def.coefficients = std::move(coeffs);
    def.expressions[QualityTier::FULL] = std::move(expr);
    return def;
}

// Shorthand factories
static auto lit(float v) { return ExprNode::literal(v); }
static auto var(const std::string& n) { return ExprNode::variable(n); }
static auto binOp(const std::string& op, std::unique_ptr<ExprNode> l,
                  std::unique_ptr<ExprNode> r)
{ return ExprNode::binaryOp(op, std::move(l), std::move(r)); }
static auto fn(const std::string& name, std::unique_ptr<ExprNode> arg)
{ return ExprNode::unaryOp(name, std::move(arg)); }

// ---------------------------------------------------------------------------
// CurveFitter tests
// ---------------------------------------------------------------------------

/// Fit y = a * x  (single coefficient, linear relationship)
TEST(CurveFitter, FitLinearSingleCoeff)
{
    // Formula: y = a * x
    auto expr = binOp("*", var("a"), var("x"));
    auto formula = makeFormula("linear",
        {{"x", FormulaValueType::FLOAT, "m", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    // Generate data: y = 3.0 * x
    std::vector<DataPoint> data;
    for (int i = 1; i <= 10; ++i)
    {
        float x = static_cast<float>(i);
        data.push_back({{{"x", x}}, 3.0f * x});
    }

    FitResult result = CurveFitter::fit(formula, data, {{"a", 1.0f}});
    EXPECT_TRUE(result.converged);
    EXPECT_NEAR(result.coefficients["a"], 3.0f, 0.01f);
    EXPECT_GT(result.rSquared, 0.999f);
    EXPECT_LT(result.rmse, 0.01f);
}

/// Fit y = a * x + b  (two coefficients)
TEST(CurveFitter, FitLinearTwoCoeffs)
{
    // Formula: y = a * x + b
    auto expr = binOp("+",
        binOp("*", var("a"), var("x")),
        var("b"));
    auto formula = makeFormula("affine",
        {{"x", FormulaValueType::FLOAT, "", 0.0f}},
        {{"a", 1.0f}, {"b", 0.0f}},
        std::move(expr));

    // Generate data: y = 2.5 * x + 1.0
    std::vector<DataPoint> data;
    for (int i = 0; i < 15; ++i)
    {
        float x = static_cast<float>(i) * 0.5f;
        data.push_back({{{"x", x}}, 2.5f * x + 1.0f});
    }

    FitResult result = CurveFitter::fit(formula, data, {{"a", 1.0f}, {"b", 0.0f}});
    EXPECT_TRUE(result.converged);
    EXPECT_NEAR(result.coefficients["a"], 2.5f, 0.05f);
    EXPECT_NEAR(result.coefficients["b"], 1.0f, 0.05f);
    EXPECT_GT(result.rSquared, 0.999f);
}

/// Fit y = a * x^2  (quadratic)
TEST(CurveFitter, FitQuadratic)
{
    // y = a * x^2
    auto expr = binOp("*", var("a"),
        binOp("pow", var("x"), lit(2.0f)));
    auto formula = makeFormula("quadratic",
        {{"x", FormulaValueType::FLOAT, "", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    // Generate data: y = 0.5 * x^2
    std::vector<DataPoint> data;
    for (int i = 1; i <= 10; ++i)
    {
        float x = static_cast<float>(i);
        data.push_back({{{"x", x}}, 0.5f * x * x});
    }

    FitResult result = CurveFitter::fit(formula, data, {{"a", 1.0f}});
    EXPECT_TRUE(result.converged);
    EXPECT_NEAR(result.coefficients["a"], 0.5f, 0.01f);
    EXPECT_GT(result.rSquared, 0.999f);
}

/// Fit y = a * exp(-b * x)  (exponential decay)
TEST(CurveFitter, FitExponentialDecay)
{
    // y = a * exp(-b * x)
    auto expr = binOp("*", var("a"),
        fn("exp",
            fn("negate", binOp("*", var("b"), var("x")))));
    auto formula = makeFormula("exp_decay",
        {{"x", FormulaValueType::FLOAT, "", 0.0f}},
        {{"a", 1.0f}, {"b", 1.0f}},
        std::move(expr));

    // Generate data: y = 5.0 * exp(-0.3 * x)
    std::vector<DataPoint> data;
    for (int i = 0; i < 20; ++i)
    {
        float x = static_cast<float>(i) * 0.5f;
        data.push_back({{{"x", x}}, 5.0f * std::exp(-0.3f * x)});
    }

    FitResult result = CurveFitter::fit(formula, data, {{"a", 2.0f}, {"b", 0.5f}});
    EXPECT_TRUE(result.converged);
    EXPECT_NEAR(result.coefficients["a"], 5.0f, 0.1f);
    EXPECT_NEAR(result.coefficients["b"], 0.3f, 0.02f);
    EXPECT_GT(result.rSquared, 0.999f);
}

/// Verify statistics are computed correctly
TEST(CurveFitter, ReportsStatistics)
{
    // y = a * x, perfect fit
    auto expr = binOp("*", var("a"), var("x"));
    auto formula = makeFormula("linear",
        {{"x", FormulaValueType::FLOAT, "", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    std::vector<DataPoint> data;
    for (int i = 1; i <= 5; ++i)
    {
        float x = static_cast<float>(i);
        data.push_back({{{"x", x}}, 2.0f * x});
    }

    FitResult result = CurveFitter::fit(formula, data, {{"a", 1.0f}});
    EXPECT_TRUE(result.converged);
    EXPECT_GT(result.rSquared, 0.999f);
    EXPECT_LT(result.rmse, 0.001f);
    EXPECT_LT(result.maxError, 0.001f);
    EXPECT_GT(result.iterations, 0);
}

/// Edge case: empty data
TEST(CurveFitter, EmptyData)
{
    auto expr = binOp("*", var("a"), var("x"));
    auto formula = makeFormula("linear",
        {{"x", FormulaValueType::FLOAT, "", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    FitResult result = CurveFitter::fit(formula, {}, {{"a", 1.0f}});
    EXPECT_FALSE(result.converged);
    EXPECT_FALSE(result.statusMessage.empty());
}

/// Edge case: no coefficients
TEST(CurveFitter, NoCoefficients)
{
    auto expr = binOp("*", var("a"), var("x"));
    auto formula = makeFormula("linear",
        {{"x", FormulaValueType::FLOAT, "", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    std::vector<DataPoint> data = {{{{"x", 1.0f}}, 2.0f}};
    FitResult result = CurveFitter::fit(formula, data, {});
    EXPECT_FALSE(result.converged);
}

/// Edge case: insufficient data (fewer points than coefficients)
TEST(CurveFitter, InsufficientData)
{
    auto expr = binOp("+",
        binOp("*", var("a"), var("x")),
        var("b"));
    auto formula = makeFormula("affine",
        {{"x", FormulaValueType::FLOAT, "", 0.0f}},
        {{"a", 1.0f}, {"b", 0.0f}},
        std::move(expr));

    // Only 1 data point but 2 coefficients
    std::vector<DataPoint> data = {{{{"x", 1.0f}}, 3.0f}};
    FitResult result = CurveFitter::fit(formula, data, {{"a", 1.0f}, {"b", 0.0f}});
    EXPECT_FALSE(result.converged);
    EXPECT_TRUE(result.statusMessage.find("Insufficient") != std::string::npos);
}

/// Fit with noisy data — should still find approximate coefficients
TEST(CurveFitter, FitWithNoisyData)
{
    auto expr = binOp("*", var("a"), var("x"));
    auto formula = makeFormula("linear",
        {{"x", FormulaValueType::FLOAT, "", 1.0f}},
        {{"a", 1.0f}},
        std::move(expr));

    // y = 4.0 * x with ±5% noise
    std::vector<DataPoint> data;
    float noise[] = {1.02f, 0.97f, 1.05f, 0.98f, 1.01f,
                     0.96f, 1.03f, 0.99f, 1.04f, 0.95f};
    for (int i = 1; i <= 10; ++i)
    {
        float x = static_cast<float>(i);
        data.push_back({{{"x", x}}, 4.0f * x * noise[i - 1]});
    }

    // Start closer to true value for noisy data
    FitResult result = CurveFitter::fit(formula, data, {{"a", 3.0f}});
    EXPECT_NEAR(result.coefficients["a"], 4.0f, 0.2f);
    EXPECT_GT(result.rSquared, 0.99f);
}

/// Fit a built-in physics template (Hooke's spring)
TEST(CurveFitter, FitPhysicsTemplate)
{
    FormulaDefinition hooke = PhysicsTemplates::createHookeSpring();

    // Generate data for F = -k * (x - restLength) with k=100, restLength=2.0
    std::vector<DataPoint> data;
    for (int i = 0; i < 15; ++i)
    {
        float x = static_cast<float>(i) * 0.5f;
        float force = -100.0f * (x - 2.0f);
        DataPoint dp;
        dp.variables["x"] = x;
        dp.variables["restLength"] = 2.0f;
        dp.observed = force;
        data.push_back(dp);
    }

    FitResult result = CurveFitter::fit(hooke, data, {{"k", 50.0f}});
    EXPECT_TRUE(result.converged);
    EXPECT_NEAR(result.coefficients["k"], 100.0f, 1.0f);
    EXPECT_GT(result.rSquared, 0.999f);
}

// ---------------------------------------------------------------------------
// FormulaPreset tests
// ---------------------------------------------------------------------------

TEST(FormulaPreset, JsonRoundTrip)
{
    FormulaPreset preset;
    preset.name = "test_preset";
    preset.displayName = "Test Preset";
    preset.category = "test";
    preset.description = "A test preset";
    preset.author = "Unit Test";
    preset.overrides = {
        {"aerodynamic_drag", {{"Cd", 0.6f}}, "Test override"},
    };

    nlohmann::json j = preset.toJson();
    FormulaPreset restored = FormulaPreset::fromJson(j);

    EXPECT_EQ(restored.name, "test_preset");
    EXPECT_EQ(restored.displayName, "Test Preset");
    EXPECT_EQ(restored.category, "test");
    EXPECT_EQ(restored.author, "Unit Test");
    ASSERT_EQ(restored.overrides.size(), 1);
    EXPECT_EQ(restored.overrides[0].formulaName, "aerodynamic_drag");
    EXPECT_FLOAT_EQ(restored.overrides[0].coefficients["Cd"], 0.6f);
}

TEST(FormulaPresetLibrary, RegisterAndFind)
{
    FormulaPresetLibrary lib;
    lib.registerBuiltinPresets();

    EXPECT_GT(lib.count(), 0);

    const FormulaPreset* desert = lib.findByName("realistic_desert");
    ASSERT_NE(desert, nullptr);
    EXPECT_EQ(desert->displayName, "Realistic Desert");
    EXPECT_EQ(desert->category, "environment");
    EXPECT_FALSE(desert->overrides.empty());
}

TEST(FormulaPresetLibrary, FindByCategory)
{
    FormulaPresetLibrary lib;
    lib.registerBuiltinPresets();

    auto envPresets = lib.findByCategory("environment");
    EXPECT_GE(envPresets.size(), 3);  // desert, tropical, arctic, underwater

    auto stylized = lib.findByCategory("stylized");
    EXPECT_GE(stylized.size(), 2);  // anime, painterly
}

TEST(FormulaPresetLibrary, GetCategories)
{
    FormulaPresetLibrary lib;
    lib.registerBuiltinPresets();

    auto cats = lib.getCategories();
    EXPECT_GE(cats.size(), 3);  // environment, stylized, scenario
}

TEST(FormulaPresetLibrary, ApplyPreset)
{
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    FormulaPresetLibrary presetLib;
    presetLib.registerBuiltinPresets();

    const FormulaPreset* desert = presetLib.findByName("realistic_desert");
    ASSERT_NE(desert, nullptr);

    // Check original drag coefficient
    const FormulaDefinition* drag = library.findByName("aerodynamic_drag");
    ASSERT_NE(drag, nullptr);
    float origDrag = drag->coefficients.at("Cd");
    EXPECT_FLOAT_EQ(origDrag, 0.47f);  // Default

    // Apply desert preset
    size_t applied = FormulaPresetLibrary::applyPreset(*desert, library);
    EXPECT_GT(applied, 0);

    // Verify coefficient changed
    drag = library.findByName("aerodynamic_drag");
    ASSERT_NE(drag, nullptr);
    EXPECT_FLOAT_EQ(drag->coefficients.at("Cd"), 0.55f);
}

TEST(FormulaPresetLibrary, RemovePreset)
{
    FormulaPresetLibrary lib;
    lib.registerBuiltinPresets();

    size_t before = lib.count();
    EXPECT_TRUE(lib.removePreset("realistic_desert"));
    EXPECT_EQ(lib.count(), before - 1);
    EXPECT_EQ(lib.findByName("realistic_desert"), nullptr);
}

TEST(FormulaPresetLibrary, JsonPersistence)
{
    FormulaPresetLibrary lib;
    lib.registerBuiltinPresets();

    nlohmann::json j = lib.toJson();
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), lib.count());

    // Reload into fresh library
    FormulaPresetLibrary lib2;
    size_t loaded = lib2.loadFromJson(j);
    EXPECT_EQ(loaded, lib.count());
    EXPECT_EQ(lib2.count(), lib.count());
}

TEST(FormulaPresetLibrary, AllPresetsHaveValidFormulas)
{
    FormulaLibrary formulaLib;
    formulaLib.registerBuiltinTemplates();

    FormulaPresetLibrary presetLib;
    presetLib.registerBuiltinPresets();

    for (const auto* preset : presetLib.getAll())
    {
        for (const auto& ov : preset->overrides)
        {
            const FormulaDefinition* formula =
                formulaLib.findByName(ov.formulaName);
            EXPECT_NE(formula, nullptr)
                << "Preset '" << preset->name << "' references unknown formula '"
                << ov.formulaName << "'";

            // Verify coefficient names are valid
            if (formula)
            {
                for (const auto& [coeffName, val] : ov.coefficients)
                {
                    EXPECT_TRUE(formula->coefficients.count(coeffName) > 0)
                        << "Preset '" << preset->name << "' overrides unknown "
                        << "coefficient '" << coeffName << "' in formula '"
                        << ov.formulaName << "'";
                }
            }
        }
    }
}
