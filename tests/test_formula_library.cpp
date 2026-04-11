/// @file test_formula_library.cpp
/// @brief Unit tests for the Formula Pipeline FP-2: expression tree, evaluator,
///        formula definitions, library, and built-in physics templates.
#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"
#include "formula/formula_library.h"
#include "formula/physics_templates.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace Vestige;

// ===========================================================================
// ExprNode — Factory & Clone
// ===========================================================================

TEST(ExprNode, LiteralCreation)
{
    auto node = ExprNode::literal(3.14f);
    EXPECT_EQ(node->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(node->value, 3.14f);
    EXPECT_TRUE(node->children.empty());
}

TEST(ExprNode, VariableCreation)
{
    auto node = ExprNode::variable("windSpeed");
    EXPECT_EQ(node->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(node->name, "windSpeed");
}

TEST(ExprNode, BinaryOpCreation)
{
    auto node = ExprNode::binaryOp("+",
        ExprNode::literal(1.0f),
        ExprNode::literal(2.0f));
    EXPECT_EQ(node->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(node->op, "+");
    EXPECT_EQ(node->children.size(), 2u);
}

TEST(ExprNode, UnaryOpCreation)
{
    auto node = ExprNode::unaryOp("sin", ExprNode::variable("x"));
    EXPECT_EQ(node->type, ExprNodeType::UNARY_OP);
    EXPECT_EQ(node->op, "sin");
    EXPECT_EQ(node->children.size(), 1u);
}

TEST(ExprNode, ConditionalCreation)
{
    auto node = ExprNode::conditional(
        ExprNode::variable("flag"),
        ExprNode::literal(1.0f),
        ExprNode::literal(0.0f));
    EXPECT_EQ(node->type, ExprNodeType::CONDITIONAL);
    EXPECT_EQ(node->children.size(), 3u);
}

TEST(ExprNode, DeepClone)
{
    // Build: sin(x + 2.0)
    auto original = ExprNode::unaryOp("sin",
        ExprNode::binaryOp("+",
            ExprNode::variable("x"),
            ExprNode::literal(2.0f)));

    auto cloned = original->clone();

    // Verify structure matches
    EXPECT_EQ(cloned->type, ExprNodeType::UNARY_OP);
    EXPECT_EQ(cloned->op, "sin");
    ASSERT_EQ(cloned->children.size(), 1u);
    EXPECT_EQ(cloned->children[0]->type, ExprNodeType::BINARY_OP);
    ASSERT_EQ(cloned->children[0]->children.size(), 2u);
    EXPECT_EQ(cloned->children[0]->children[0]->name, "x");
    EXPECT_FLOAT_EQ(cloned->children[0]->children[1]->value, 2.0f);

    // Verify independence (modifying original doesn't affect clone)
    original->op = "cos";
    EXPECT_EQ(cloned->op, "sin");
}

TEST(ExprNode, UsesVariable)
{
    auto expr = ExprNode::binaryOp("*",
        ExprNode::variable("x"),
        ExprNode::binaryOp("+",
            ExprNode::variable("y"),
            ExprNode::literal(1.0f)));

    EXPECT_TRUE(expr->usesVariable("x"));
    EXPECT_TRUE(expr->usesVariable("y"));
    EXPECT_FALSE(expr->usesVariable("z"));
}

TEST(ExprNode, CollectVariables)
{
    auto expr = ExprNode::binaryOp("*",
        ExprNode::variable("a"),
        ExprNode::binaryOp("+",
            ExprNode::variable("b"),
            ExprNode::variable("a")));  // 'a' appears twice

    std::vector<std::string> vars;
    expr->collectVariables(vars);

    EXPECT_EQ(vars.size(), 2u);  // 'a' not duplicated
    EXPECT_NE(std::find(vars.begin(), vars.end(), "a"), vars.end());
    EXPECT_NE(std::find(vars.begin(), vars.end(), "b"), vars.end());
}

// ===========================================================================
// ExprNode — JSON round-trip
// ===========================================================================

TEST(ExprNodeJson, LiteralRoundTrip)
{
    auto original = ExprNode::literal(42.5f);
    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    EXPECT_EQ(restored->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(restored->value, 42.5f);
}

TEST(ExprNodeJson, VariableRoundTrip)
{
    auto original = ExprNode::variable("temperature");
    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    EXPECT_EQ(restored->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(restored->name, "temperature");
}

TEST(ExprNodeJson, BinaryOpRoundTrip)
{
    auto original = ExprNode::binaryOp("pow",
        ExprNode::variable("x"),
        ExprNode::literal(2.0f));
    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    EXPECT_EQ(restored->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(restored->op, "pow");
    ASSERT_EQ(restored->children.size(), 2u);
    EXPECT_EQ(restored->children[0]->name, "x");
    EXPECT_FLOAT_EQ(restored->children[1]->value, 2.0f);
}

TEST(ExprNodeJson, UnaryOpRoundTrip)
{
    auto original = ExprNode::unaryOp("exp",
        ExprNode::variable("depth"));
    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    EXPECT_EQ(restored->type, ExprNodeType::UNARY_OP);
    EXPECT_EQ(restored->op, "exp");
    ASSERT_EQ(restored->children.size(), 1u);
    EXPECT_EQ(restored->children[0]->name, "depth");
}

TEST(ExprNodeJson, ConditionalRoundTrip)
{
    auto original = ExprNode::conditional(
        ExprNode::variable("flag"),
        ExprNode::literal(10.0f),
        ExprNode::literal(20.0f));
    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    EXPECT_EQ(restored->type, ExprNodeType::CONDITIONAL);
    ASSERT_EQ(restored->children.size(), 3u);
    EXPECT_EQ(restored->children[0]->name, "flag");
    EXPECT_FLOAT_EQ(restored->children[1]->value, 10.0f);
    EXPECT_FLOAT_EQ(restored->children[2]->value, 20.0f);
}

TEST(ExprNodeJson, ComplexExpressionRoundTrip)
{
    // Build: 0.5 * Cd * airDensity * surfaceArea * vDotN
    auto original =
        ExprNode::binaryOp("*", ExprNode::literal(0.5f),
            ExprNode::binaryOp("*", ExprNode::variable("Cd"),
                ExprNode::binaryOp("*", ExprNode::variable("airDensity"),
                    ExprNode::binaryOp("*",
                        ExprNode::variable("surfaceArea"),
                        ExprNode::variable("vDotN")))));

    auto json = original->toJson();
    auto restored = ExprNode::fromJson(json);

    // Verify full structure survived round-trip
    EXPECT_EQ(restored->type, ExprNodeType::BINARY_OP);
    EXPECT_FLOAT_EQ(restored->children[0]->value, 0.5f);

    auto& r1 = restored->children[1];
    EXPECT_EQ(r1->children[0]->name, "Cd");

    auto& r2 = r1->children[1];
    EXPECT_EQ(r2->children[0]->name, "airDensity");

    auto& r3 = r2->children[1];
    EXPECT_EQ(r3->children[0]->name, "surfaceArea");
    EXPECT_EQ(r3->children[1]->name, "vDotN");
}

TEST(ExprNodeJson, InvalidJsonThrows)
{
    // String instead of number or object
    nlohmann::json j = "invalid";
    EXPECT_THROW(ExprNode::fromJson(j), std::runtime_error);
}

// ===========================================================================
// ExpressionEvaluator
// ===========================================================================

TEST(ExprEval, LiteralValue)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::literal(7.5f);
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 7.5f);
}

TEST(ExprEval, VariableLookup)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::variable("x");
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {{"x", 3.0f}}), 3.0f);
}

TEST(ExprEval, UndefinedVariableThrows)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::variable("missing");
    EXPECT_THROW(eval.evaluate(*node, {}), std::runtime_error);
}

TEST(ExprEval, Addition)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::binaryOp("+",
        ExprNode::literal(3.0f),
        ExprNode::literal(4.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 7.0f);
}

TEST(ExprEval, Subtraction)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::binaryOp("-",
        ExprNode::literal(10.0f),
        ExprNode::literal(4.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 6.0f);
}

TEST(ExprEval, Multiplication)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::binaryOp("*",
        ExprNode::literal(3.0f),
        ExprNode::literal(5.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 15.0f);
}

TEST(ExprEval, DivisionSafe)
{
    ExpressionEvaluator eval;
    // Normal division
    auto node = ExprNode::binaryOp("/",
        ExprNode::literal(10.0f),
        ExprNode::literal(4.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 2.5f);

    // Division by zero returns 0
    auto divZero = ExprNode::binaryOp("/",
        ExprNode::literal(10.0f),
        ExprNode::literal(0.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*divZero, {}), 0.0f);
}

TEST(ExprEval, Power)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::binaryOp("pow",
        ExprNode::literal(2.0f),
        ExprNode::literal(8.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 256.0f);
}

TEST(ExprEval, MinMax)
{
    ExpressionEvaluator eval;
    auto minNode = ExprNode::binaryOp("min",
        ExprNode::literal(3.0f),
        ExprNode::literal(7.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*minNode, {}), 3.0f);

    auto maxNode = ExprNode::binaryOp("max",
        ExprNode::literal(3.0f),
        ExprNode::literal(7.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*maxNode, {}), 7.0f);
}

TEST(ExprEval, UnaryFunctions)
{
    ExpressionEvaluator eval;
    const float pi = 3.14159265f;

    // sin(pi/2) = 1
    auto sinNode = ExprNode::unaryOp("sin", ExprNode::literal(pi / 2.0f));
    EXPECT_NEAR(eval.evaluate(*sinNode, {}), 1.0f, 1e-5f);

    // cos(0) = 1
    auto cosNode = ExprNode::unaryOp("cos", ExprNode::literal(0.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*cosNode, {}), 1.0f);

    // sqrt(9) = 3
    auto sqrtNode = ExprNode::unaryOp("sqrt", ExprNode::literal(9.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*sqrtNode, {}), 3.0f);

    // abs(-5) = 5
    auto absNode = ExprNode::unaryOp("abs", ExprNode::literal(-5.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*absNode, {}), 5.0f);

    // exp(0) = 1
    auto expNode = ExprNode::unaryOp("exp", ExprNode::literal(0.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*expNode, {}), 1.0f);

    // log(1) = 0
    auto logNode = ExprNode::unaryOp("log", ExprNode::literal(1.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*logNode, {}), 0.0f);

    // floor(3.7) = 3
    auto floorNode = ExprNode::unaryOp("floor", ExprNode::literal(3.7f));
    EXPECT_FLOAT_EQ(eval.evaluate(*floorNode, {}), 3.0f);

    // ceil(3.2) = 4
    auto ceilNode = ExprNode::unaryOp("ceil", ExprNode::literal(3.2f));
    EXPECT_FLOAT_EQ(eval.evaluate(*ceilNode, {}), 4.0f);

    // negate(5) = -5
    auto negNode = ExprNode::unaryOp("negate", ExprNode::literal(5.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*negNode, {}), -5.0f);

    // saturate(1.5) = 1.0
    auto satNode = ExprNode::unaryOp("saturate", ExprNode::literal(1.5f));
    EXPECT_FLOAT_EQ(eval.evaluate(*satNode, {}), 1.0f);

    // saturate(-0.5) = 0.0
    auto satNode2 = ExprNode::unaryOp("saturate", ExprNode::literal(-0.5f));
    EXPECT_FLOAT_EQ(eval.evaluate(*satNode2, {}), 0.0f);
}

TEST(ExprEval, ConditionalTrue)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::conditional(
        ExprNode::literal(1.0f),   // non-zero = true
        ExprNode::literal(10.0f),  // then
        ExprNode::literal(20.0f)); // else
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 10.0f);
}

TEST(ExprEval, ConditionalFalse)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::conditional(
        ExprNode::literal(0.0f),   // zero = false
        ExprNode::literal(10.0f),
        ExprNode::literal(20.0f));
    EXPECT_FLOAT_EQ(eval.evaluate(*node, {}), 20.0f);
}

TEST(ExprEval, CoefficientLookup)
{
    ExpressionEvaluator eval;
    // Expression: Cd * area
    auto node = ExprNode::binaryOp("*",
        ExprNode::variable("Cd"),
        ExprNode::variable("area"));

    ExpressionEvaluator::VariableMap vars = {{"area", 2.0f}};
    std::unordered_map<std::string, float> coeffs = {{"Cd", 0.47f}};

    EXPECT_NEAR(eval.evaluate(*node, vars, coeffs), 0.94f, 1e-5f);
}

TEST(ExprEval, VariableOverridesCoefficient)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::variable("x");

    ExpressionEvaluator::VariableMap vars = {{"x", 5.0f}};
    std::unordered_map<std::string, float> coeffs = {{"x", 10.0f}};

    // Variable takes precedence
    EXPECT_FLOAT_EQ(eval.evaluate(*node, vars, coeffs), 5.0f);
}

TEST(ExprEval, ComplexExpression)
{
    ExpressionEvaluator eval;
    // Fresnel-Schlick: F0 + (1 - F0) * (1 - cosTheta)^5
    auto expr =
        ExprNode::binaryOp("+", ExprNode::variable("F0"),
            ExprNode::binaryOp("*",
                ExprNode::binaryOp("-", ExprNode::literal(1.0f), ExprNode::variable("F0")),
                ExprNode::binaryOp("pow",
                    ExprNode::binaryOp("-", ExprNode::literal(1.0f), ExprNode::variable("cosTheta")),
                    ExprNode::literal(5.0f))));

    ExpressionEvaluator::VariableMap vars = {{"cosTheta", 1.0f}};
    std::unordered_map<std::string, float> coeffs = {{"F0", 0.02f}};

    // At cosTheta=1: (1-1)^5 = 0, so result = F0 = 0.02
    EXPECT_NEAR(eval.evaluate(*expr, vars, coeffs), 0.02f, 1e-6f);

    // At cosTheta=0: (1-0)^5 = 1, so result = F0 + (1-F0)*1 = 1.0
    vars["cosTheta"] = 0.0f;
    EXPECT_NEAR(eval.evaluate(*expr, vars, coeffs), 1.0f, 1e-6f);
}

TEST(ExprEval, UnknownBinaryOpThrows)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::binaryOp("bogus",
        ExprNode::literal(1.0f),
        ExprNode::literal(2.0f));
    EXPECT_THROW(eval.evaluate(*node, {}), std::runtime_error);
}

TEST(ExprEval, UnknownUnaryOpThrows)
{
    ExpressionEvaluator eval;
    auto node = ExprNode::unaryOp("bogus", ExprNode::literal(1.0f));
    EXPECT_THROW(eval.evaluate(*node, {}), std::runtime_error);
}

// ===========================================================================
// ExpressionEvaluator — Validation
// ===========================================================================

TEST(ExprEvalValidation, AllVariablesDeclared)
{
    auto expr = ExprNode::binaryOp("*",
        ExprNode::variable("x"),
        ExprNode::variable("Cd"));

    std::vector<FormulaInput> inputs = {{"x", FormulaValueType::FLOAT, "m", 0.0f}};
    std::vector<std::string> coeffs = {"Cd"};
    std::string error;

    EXPECT_TRUE(ExpressionEvaluator::validate(*expr, inputs, coeffs, error));
}

TEST(ExprEvalValidation, UndeclaredVariable)
{
    auto expr = ExprNode::variable("unknown");

    std::vector<FormulaInput> inputs = {{"x", FormulaValueType::FLOAT, "m", 0.0f}};
    std::vector<std::string> coeffs;
    std::string error;

    EXPECT_FALSE(ExpressionEvaluator::validate(*expr, inputs, coeffs, error));
    EXPECT_NE(error.find("unknown"), std::string::npos);
}

// ===========================================================================
// FormulaDefinition — JSON round-trip
// ===========================================================================

TEST(FormulaDefinition, JsonRoundTrip)
{
    FormulaDefinition def;
    def.name = "test_formula";
    def.category = "physics";
    def.description = "Test formula for unit tests";
    def.inputs = {
        {"x", FormulaValueType::FLOAT, "m", 0.0f},
        {"y", FormulaValueType::VEC3, "m/s", 1.0f}
    };
    def.output = {FormulaValueType::FLOAT, "N"};
    def.coefficients = {{"k", 100.0f}, {"d", 0.5f}};
    def.source = "unit test";
    def.accuracy = 0.95f;

    def.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("*", ExprNode::variable("k"), ExprNode::variable("x"));
    def.expressions[QualityTier::APPROXIMATE] =
        ExprNode::variable("x");

    // Serialize
    auto json = def.toJson();

    // Deserialize
    auto restored = FormulaDefinition::fromJson(json);

    EXPECT_EQ(restored.name, "test_formula");
    EXPECT_EQ(restored.category, "physics");
    EXPECT_EQ(restored.description, "Test formula for unit tests");
    EXPECT_EQ(restored.inputs.size(), 2u);
    EXPECT_EQ(restored.inputs[0].name, "x");
    EXPECT_EQ(restored.inputs[0].type, FormulaValueType::FLOAT);
    EXPECT_EQ(restored.inputs[1].type, FormulaValueType::VEC3);
    EXPECT_EQ(restored.output.type, FormulaValueType::FLOAT);
    EXPECT_EQ(restored.output.unit, "N");
    EXPECT_FLOAT_EQ(restored.coefficients.at("k"), 100.0f);
    EXPECT_FLOAT_EQ(restored.coefficients.at("d"), 0.5f);
    EXPECT_EQ(restored.source, "unit test");
    EXPECT_FLOAT_EQ(restored.accuracy, 0.95f);

    // Verify both tiers survived
    EXPECT_TRUE(restored.hasTier(QualityTier::FULL));
    EXPECT_TRUE(restored.hasTier(QualityTier::APPROXIMATE));
    EXPECT_FALSE(restored.hasTier(QualityTier::LUT));
}

TEST(FormulaDefinition, CloneIsIndependent)
{
    FormulaDefinition def;
    def.name = "original";
    def.category = "test";
    def.expressions[QualityTier::FULL] = ExprNode::literal(42.0f);

    auto copy = def.clone();
    EXPECT_EQ(copy.name, "original");

    // Modify original
    def.name = "modified";
    EXPECT_EQ(copy.name, "original");
}

TEST(FormulaDefinition, GetExpressionFallback)
{
    FormulaDefinition def;
    def.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);

    // Requesting APPROXIMATE falls back to FULL
    const ExprNode* expr = def.getExpression(QualityTier::APPROXIMATE);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(expr->value, 1.0f);
}

TEST(FormulaDefinition, SingleExpressionJsonFormat)
{
    // Test the "expression" (singular) JSON format used in the design doc
    nlohmann::json j = {
        {"name", "simple"},
        {"category", "test"},
        {"description", "A simple test"},
        {"inputs", nlohmann::json::array({
            {{"name", "x"}, {"type", "float"}, {"unit", "m"}}
        })},
        {"output", {{"type", "float"}, {"unit", "m"}}},
        {"expression", {{"var", "x"}}},
        {"coefficients", {{"k", 1.0f}}}
    };

    auto def = FormulaDefinition::fromJson(j);
    EXPECT_EQ(def.name, "simple");
    EXPECT_TRUE(def.hasTier(QualityTier::FULL));
}

// ===========================================================================
// QualityTier / FormulaValueType string conversion
// ===========================================================================

TEST(EnumConversion, QualityTierRoundTrip)
{
    EXPECT_STREQ(qualityTierToString(QualityTier::FULL), "full");
    EXPECT_STREQ(qualityTierToString(QualityTier::APPROXIMATE), "approximate");
    EXPECT_STREQ(qualityTierToString(QualityTier::LUT), "lut");

    EXPECT_EQ(qualityTierFromString("full"), QualityTier::FULL);
    EXPECT_EQ(qualityTierFromString("approximate"), QualityTier::APPROXIMATE);
    EXPECT_EQ(qualityTierFromString("lut"), QualityTier::LUT);
    EXPECT_EQ(qualityTierFromString("unknown"), QualityTier::FULL);
}

TEST(EnumConversion, ValueTypeRoundTrip)
{
    EXPECT_STREQ(formulaValueTypeToString(FormulaValueType::FLOAT), "float");
    EXPECT_STREQ(formulaValueTypeToString(FormulaValueType::VEC3), "vec3");

    EXPECT_EQ(formulaValueTypeFromString("float"), FormulaValueType::FLOAT);
    EXPECT_EQ(formulaValueTypeFromString("vec2"), FormulaValueType::VEC2);
    EXPECT_EQ(formulaValueTypeFromString("vec3"), FormulaValueType::VEC3);
    EXPECT_EQ(formulaValueTypeFromString("vec4"), FormulaValueType::VEC4);
    EXPECT_EQ(formulaValueTypeFromString("unknown"), FormulaValueType::FLOAT);
}

// ===========================================================================
// FormulaLibrary
// ===========================================================================

TEST(FormulaLibrary, RegisterAndLookup)
{
    FormulaLibrary lib;
    FormulaDefinition def;
    def.name = "test";
    def.category = "physics";
    def.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);

    lib.registerFormula(std::move(def));

    EXPECT_EQ(lib.count(), 1u);
    EXPECT_NE(lib.findByName("test"), nullptr);
    EXPECT_EQ(lib.findByName("nonexistent"), nullptr);
}

TEST(FormulaLibrary, OverwriteByName)
{
    FormulaLibrary lib;

    FormulaDefinition def1;
    def1.name = "test";
    def1.description = "version 1";
    def1.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(def1));

    FormulaDefinition def2;
    def2.name = "test";
    def2.description = "version 2";
    def2.expressions[QualityTier::FULL] = ExprNode::literal(2.0f);
    lib.registerFormula(std::move(def2));

    EXPECT_EQ(lib.count(), 1u);
    EXPECT_EQ(lib.findByName("test")->description, "version 2");
}

TEST(FormulaLibrary, RemoveFormula)
{
    FormulaLibrary lib;
    FormulaDefinition def;
    def.name = "removable";
    def.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(def));

    EXPECT_TRUE(lib.removeFormula("removable"));
    EXPECT_EQ(lib.count(), 0u);
    EXPECT_FALSE(lib.removeFormula("nonexistent"));
}

TEST(FormulaLibrary, FindByCategory)
{
    FormulaLibrary lib;

    FormulaDefinition water1;
    water1.name = "fresnel";
    water1.category = "water";
    water1.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(water1));

    FormulaDefinition water2;
    water2.name = "beer_lambert";
    water2.category = "water";
    water2.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(water2));

    FormulaDefinition physics1;
    physics1.name = "hooke";
    physics1.category = "physics";
    physics1.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(physics1));

    auto waterFormulas = lib.findByCategory("water");
    EXPECT_EQ(waterFormulas.size(), 2u);

    auto physicsFormulas = lib.findByCategory("physics");
    EXPECT_EQ(physicsFormulas.size(), 1u);

    auto emptyCategory = lib.findByCategory("empty");
    EXPECT_TRUE(emptyCategory.empty());
}

TEST(FormulaLibrary, GetCategories)
{
    FormulaLibrary lib;

    FormulaDefinition d1;
    d1.name = "a"; d1.category = "water";
    d1.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(d1));

    FormulaDefinition d2;
    d2.name = "b"; d2.category = "physics";
    d2.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(d2));

    FormulaDefinition d3;
    d3.name = "c"; d3.category = "water";  // Duplicate category
    d3.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(d3));

    auto cats = lib.getCategories();
    EXPECT_EQ(cats.size(), 2u);
    // Sorted alphabetically
    EXPECT_EQ(cats[0], "physics");
    EXPECT_EQ(cats[1], "water");
}

TEST(FormulaLibrary, ClearRemovesAll)
{
    FormulaLibrary lib;
    FormulaDefinition def;
    def.name = "test";
    def.expressions[QualityTier::FULL] = ExprNode::literal(1.0f);
    lib.registerFormula(std::move(def));
    EXPECT_EQ(lib.count(), 1u);

    lib.clear();
    EXPECT_EQ(lib.count(), 0u);
}

TEST(FormulaLibrary, JsonRoundTrip)
{
    FormulaLibrary lib;

    FormulaDefinition d1;
    d1.name = "formula_a";
    d1.category = "test";
    d1.description = "First formula";
    d1.inputs = {{"x", FormulaValueType::FLOAT, "m", 0.0f}};
    d1.output = {FormulaValueType::FLOAT, "m"};
    d1.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("*", ExprNode::literal(2.0f), ExprNode::variable("x"));
    lib.registerFormula(std::move(d1));

    FormulaDefinition d2;
    d2.name = "formula_b";
    d2.category = "test";
    d2.expressions[QualityTier::FULL] = ExprNode::literal(42.0f);
    lib.registerFormula(std::move(d2));

    // Serialize
    auto json = lib.toJson();
    EXPECT_TRUE(json.is_array());
    EXPECT_EQ(json.size(), 2u);

    // Deserialize into fresh library
    FormulaLibrary lib2;
    size_t loaded = lib2.loadFromJson(json);
    EXPECT_EQ(loaded, 2u);
    EXPECT_EQ(lib2.count(), 2u);
    EXPECT_NE(lib2.findByName("formula_a"), nullptr);
    EXPECT_NE(lib2.findByName("formula_b"), nullptr);
}

TEST(FormulaLibrary, LoadSingleObjectJson)
{
    FormulaLibrary lib;
    nlohmann::json j = {
        {"name", "single"},
        {"category", "test"},
        {"expression", 42.0}
    };

    size_t loaded = lib.loadFromJson(j);
    EXPECT_EQ(loaded, 1u);
    EXPECT_NE(lib.findByName("single"), nullptr);
}

// ===========================================================================
// Built-in Physics Templates
// ===========================================================================

TEST(PhysicsTemplates, CreateAllReturnsExpectedCount)
{
    auto all = PhysicsTemplates::createAll();
    EXPECT_EQ(all.size(), 27u);

    // Each has a name, category, and FULL expression
    for (const auto& def : all)
    {
        EXPECT_FALSE(def.name.empty()) << "Formula missing name";
        EXPECT_FALSE(def.category.empty()) << def.name << " missing category";
        EXPECT_TRUE(def.hasTier(QualityTier::FULL))
            << def.name << " missing FULL expression";
    }
}

TEST(PhysicsTemplates, RegisterBuiltinTemplates)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();
    EXPECT_EQ(lib.count(), 27u);

    // Spot-check original formulas
    EXPECT_NE(lib.findByName("aerodynamic_drag"), nullptr);
    EXPECT_NE(lib.findByName("fresnel_schlick"), nullptr);
    EXPECT_NE(lib.findByName("beer_lambert"), nullptr);
    EXPECT_NE(lib.findByName("hooke_spring"), nullptr);
    EXPECT_NE(lib.findByName("wet_darkening"), nullptr);
    // Spot-check new formulas
    EXPECT_NE(lib.findByName("ggx_distribution"), nullptr);
    EXPECT_NE(lib.findByName("schlick_geometry"), nullptr);
    EXPECT_NE(lib.findByName("aces_tonemap"), nullptr);
    EXPECT_NE(lib.findByName("spot_cone_falloff"), nullptr);
    EXPECT_NE(lib.findByName("ease_in_sine"), nullptr);
    EXPECT_NE(lib.findByName("fast_neg_exp"), nullptr);
}

TEST(PhysicsTemplates, CategoriesAreCorrect)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto cats = lib.getCategories();
    // Should have: animation, camera, lighting, material, physics,
    //              post_processing, rendering, terrain, water, wind
    EXPECT_EQ(cats.size(), 10u);
}

TEST(PhysicsTemplates, AerodynamicDragEvaluates)
{
    auto def = PhysicsTemplates::createAerodynamicDrag();
    ExpressionEvaluator eval;

    // vDotN=10, area=2, airDensity=1.225, Cd=0.47
    // Expected: 0.5 * 0.47 * 1.225 * 2 * 10 = 5.7575
    ExpressionEvaluator::VariableMap vars = {
        {"vDotN", 10.0f},
        {"surfaceArea", 2.0f},
        {"airDensity", 1.225f}
    };
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    float result = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(result, 5.7575f, 0.01f);
}

TEST(PhysicsTemplates, FresnelSchlickEvaluates)
{
    auto def = PhysicsTemplates::createFresnelSchlick();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At normal incidence (cosTheta=1): R = F0 = 0.02
    ExpressionEvaluator::VariableMap vars = {{"cosTheta", 1.0f}};
    float r1 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(r1, 0.02f, 1e-5f);

    // At grazing angle (cosTheta=0): R = 1.0
    vars["cosTheta"] = 0.0f;
    float r2 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(r2, 1.0f, 1e-5f);
}

TEST(PhysicsTemplates, BeerLambertEvaluates)
{
    auto def = PhysicsTemplates::createBeerLambert();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At depth=0: I = I0 = 1.0
    ExpressionEvaluator::VariableMap vars = {{"I0", 1.0f}, {"depth", 0.0f}};
    float i0 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(i0, 1.0f, 1e-5f);

    // At depth=5m with alpha=0.4: I = exp(-2.0) ~ 0.1353
    vars["depth"] = 5.0f;
    float i5 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(i5, std::exp(-2.0f), 0.001f);
}

TEST(PhysicsTemplates, HookeSpringEvaluates)
{
    auto def = PhysicsTemplates::createHookeSpring();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Extension: x=1.5m, rest=1.0m, k=100 -> F = -100 * 0.5 = -50
    ExpressionEvaluator::VariableMap vars = {{"x", 1.5f}, {"restLength", 1.0f}};
    float f = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(f, -50.0f, 0.01f);

    // Compression: x=0.5m -> F = -100 * -0.5 = 50
    vars["x"] = 0.5f;
    float fc = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(fc, 50.0f, 0.01f);
}

TEST(PhysicsTemplates, TerminalVelocityEvaluates)
{
    auto def = PhysicsTemplates::createTerminalVelocity();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // mass=1kg, area=0.01m2, airDensity=1.225
    // vt = sqrt(2*1*9.81 / (1.225*0.01*0.47)) = sqrt(19.62/0.005758) = sqrt(3407.4) ~ 58.4
    ExpressionEvaluator::VariableMap vars = {
        {"mass", 1.0f},
        {"area", 0.01f},
        {"airDensity", 1.225f}
    };
    float vt = eval.evaluate(*def.getExpression(), vars, coeffs);
    float expected = std::sqrt(2.0f * 1.0f * 9.81f / (1.225f * 0.01f * 0.47f));
    EXPECT_NEAR(vt, expected, 0.1f);
}

TEST(PhysicsTemplates, WetDarkeningEvaluates)
{
    auto def = PhysicsTemplates::createWetDarkening();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Dry surface: wetness=0 -> albedo unchanged
    ExpressionEvaluator::VariableMap vars = {{"albedo", 0.8f}, {"wetness", 0.0f}};
    float dry = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(dry, 0.8f, 1e-5f);

    // Fully wet: wetness=1, darkFactor=0.5 -> albedo * 0.5 = 0.4
    vars["wetness"] = 1.0f;
    float wet = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(wet, 0.4f, 1e-5f);
}

TEST(PhysicsTemplates, GerstnerWaveEvaluates)
{
    auto def = PhysicsTemplates::createGerstnerWave();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At x=0, t=0 with defaults: amplitude*sin(0-0+0) = 0.5*sin(0) = 0
    ExpressionEvaluator::VariableMap vars = {{"x", 0.0f}, {"t", 0.0f}};
    float y0 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(y0, 0.0f, 1e-5f);
}

TEST(PhysicsTemplates, TemplateJsonRoundTrip)
{
    // Verify all templates survive JSON round-trip
    auto all = PhysicsTemplates::createAll();
    for (const auto& def : all)
    {
        auto json = def.toJson();
        auto restored = FormulaDefinition::fromJson(json);

        EXPECT_EQ(restored.name, def.name) << "Name mismatch for " << def.name;
        EXPECT_EQ(restored.category, def.category) << "Category mismatch for " << def.name;
        EXPECT_EQ(restored.inputs.size(), def.inputs.size())
            << "Input count mismatch for " << def.name;
        EXPECT_TRUE(restored.hasTier(QualityTier::FULL))
            << "Missing FULL tier after round-trip for " << def.name;
    }
}

// ===========================================================================
// New formula evaluation tests
// ===========================================================================

TEST(PhysicsTemplates, GGXDistributionEvaluates)
{
    auto def = PhysicsTemplates::createGGXDistribution();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At NdotH=1 (specular peak): denom_inner = alpha^2, so
    // D = alpha^2 / (PI * (alpha^2)^2) = 1 / (PI * alpha^2)
    ExpressionEvaluator::VariableMap vars = {{"NdotH", 1.0f}};
    float peak = eval.evaluate(*def.getExpression(), vars, coeffs);
    float a2 = 0.25f * 0.25f;  // alpha=0.25 => a2=0.0625
    float expectedPeak = 1.0f / (3.14159265f * a2);  // ~5.093
    EXPECT_NEAR(peak, expectedPeak, 0.01f);

    // At NdotH=0 (perpendicular): denom_inner = 0*(a2-1)+1 = 1
    // D = alpha^2 / (PI * 1) = alpha^2 / PI
    vars["NdotH"] = 0.0f;
    float perp = eval.evaluate(*def.getExpression(), vars, coeffs);
    float expectedPerp = a2 / 3.14159265f;  // ~0.0199
    EXPECT_NEAR(perp, expectedPerp, 0.001f);

    // With rougher surface (alpha=1.0, NdotH=1): D = 1/(PI*1) = 1/PI
    coeffs["alpha"] = 1.0f;
    vars["NdotH"] = 1.0f;
    float rough = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(rough, 1.0f / 3.14159265f, 0.001f);
}

TEST(PhysicsTemplates, SchlickGeometryEvaluates)
{
    auto def = PhysicsTemplates::createSchlickGeometry();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At NdotV=1: G1 = 1 / (1*(1-k)+k) = 1 / 1 = 1
    ExpressionEvaluator::VariableMap vars = {{"NdotV", 1.0f}};
    float g1 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(g1, 1.0f, 1e-5f);

    // At NdotV=0: G1 = 0 / (0*(1-k)+k) = 0
    vars["NdotV"] = 0.0f;
    float g0 = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(g0, 0.0f, 1e-5f);

    // At NdotV=0.5, k=0.125: G1 = 0.5 / (0.5*0.875 + 0.125) = 0.5 / 0.5625
    vars["NdotV"] = 0.5f;
    float gm = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(gm, 0.5f / 0.5625f, 0.001f);
}

TEST(PhysicsTemplates, ACESTonemapEvaluates)
{
    auto def = PhysicsTemplates::createACESTonemap();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At x=0: numerator=0, result=0
    ExpressionEvaluator::VariableMap vars = {{"x", 0.0f}};
    float dark = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(dark, 0.0f, 1e-5f);

    // At x=1.0: (1*(2.51+0.03))/(1*(2.43+0.59)+0.14) = 2.54/3.16 ~ 0.8038
    vars["x"] = 1.0f;
    float mid = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(mid, 2.54f / 3.16f, 0.01f);

    // APPROXIMATE (Reinhard): x/(x+1), at x=1 should be 0.5
    float reinhard = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(reinhard, 0.5f, 1e-5f);
}

TEST(PhysicsTemplates, SpotConeFalloffEvaluates)
{
    auto def = PhysicsTemplates::createSpotConeFalloff();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Inside inner cone (theta > innerCutoff): intensity = 1.0
    ExpressionEvaluator::VariableMap vars = {{"theta", 0.98f}};
    float inside = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(inside, 1.0f, 0.01f);

    // Outside outer cone (theta < outerCutoff): intensity = 0.0
    vars["theta"] = 0.85f;
    float outside = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(outside, 0.0f, 0.01f);

    // Midway between cutoffs: should be ~0.5
    float midTheta = (0.9659f + 0.9063f) / 2.0f;
    vars["theta"] = midTheta;
    float mid = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(mid, 0.5f, 0.05f);
}

TEST(PhysicsTemplates, EaseInSineEvaluates)
{
    auto def = PhysicsTemplates::createEaseInSine();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At t=0: 1 - cos(0) = 0
    ExpressionEvaluator::VariableMap vars = {{"t", 0.0f}};
    float start = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(start, 0.0f, 1e-5f);

    // At t=1: 1 - cos(PI/2) = 1 - 0 = 1
    vars["t"] = 1.0f;
    float end = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(end, 1.0f, 1e-4f);

    // At t=0.5: 1 - cos(PI/4) = 1 - 0.7071 ~ 0.2929
    vars["t"] = 0.5f;
    float mid = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(mid, 1.0f - std::cos(0.5f * 1.5707963f), 0.001f);

    // APPROXIMATE should be close but not identical
    float approx = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approx, mid, 0.05f);  // Within 5% at t=0.5
}

TEST(PhysicsTemplates, FastNegExpEvaluates)
{
    auto def = PhysicsTemplates::createFastNegExp();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At x=0: exp(-0) = 1.0
    ExpressionEvaluator::VariableMap vars = {{"x", 0.0f}};
    float zero = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(zero, 1.0f, 1e-5f);

    // At x=1: exp(-1) ~ 0.3679
    vars["x"] = 1.0f;
    float one = eval.evaluate(*def.getExpression(), vars, coeffs);
    EXPECT_NEAR(one, std::exp(-1.0f), 0.001f);

    // APPROXIMATE at x=1: 1/(1+1+0.48+0.235) = 1/2.715 ~ 0.3683
    float approx = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approx, one, 0.01f);  // Within 1% at x=1

    // APPROXIMATE at x=0 should also be 1.0
    vars["x"] = 0.0f;
    float approxZero = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxZero, 1.0f, 1e-5f);
}

// ===========================================================================
// Existing formulas — APPROXIMATE tier tests
// ===========================================================================

TEST(PhysicsTemplates, InverseSquareFalloffApproximate)
{
    auto def = PhysicsTemplates::createInverseSquareFalloff();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At distance=0: FULL = 1/constant = 1.0, APPROX = 1/(1+0) = 1.0
    ExpressionEvaluator::VariableMap vars = {{"distance", 0.0f}};
    float full0 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx0 = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full0, 1.0f, 0.01f);
    EXPECT_NEAR(approx0, 1.0f, 0.01f);

    // At distance=5: both should be < 1 and approximately similar
    vars["distance"] = 5.0f;
    float full5 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx5 = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_GT(full5, 0.0f);
    EXPECT_GT(approx5, 0.0f);
    EXPECT_LT(full5, 1.0f);
    EXPECT_LT(approx5, 1.0f);
}

TEST(PhysicsTemplates, ExponentialFogApproximate)
{
    auto def = PhysicsTemplates::createExponentialFog();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At distance=0: both should be 1.0
    ExpressionEvaluator::VariableMap vars = {{"distance", 0.0f}};
    float full0 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx0 = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full0, 1.0f, 1e-5f);
    EXPECT_NEAR(approx0, 1.0f, 1e-5f);

    // At distance=50: FULL = exp(-0.5) ~ 0.607, APPROX = max(0.5, 0) = 0.5
    vars["distance"] = 50.0f;
    float full50 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx50 = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full50, std::exp(-0.5f), 0.01f);
    EXPECT_NEAR(approx50, 0.5f, 1e-5f);
}

TEST(PhysicsTemplates, GerstnerWaveApproximate)
{
    auto def = PhysicsTemplates::createGerstnerWave();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At x=0, t=0: FULL = sin(0)=0, APPROX = cos(0)=1*amplitude
    ExpressionEvaluator::VariableMap vars = {{"x", 0.0f}, {"t", 0.0f}};
    float full = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full, 0.0f, 1e-5f);
    EXPECT_NEAR(approx, 0.5f, 1e-5f);  // amplitude * cos(0) = 0.5
}

// ===========================================================================
// New APPROXIMATE tiers for existing FULL-only templates
// ===========================================================================

TEST(PhysicsTemplates, StokesDragApproximate)
{
    auto def = PhysicsTemplates::createStokesDrag();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // velocity=2.0, radius=0.001, mu=1.81e-5
    // FULL: 6 * pi * 1.81e-5 * 0.001 * 2.0 = 6*3.14159*1.81e-5*0.001*2
    // APPROX: 1.81e-5 * 0.001 * 2.0 * 18.85
    ExpressionEvaluator::VariableMap vars = {
        {"velocity", 2.0f},
        {"radius", 0.001f}
    };
    float full = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);

    // Both should be very close (18.85 ~ 6*pi = 18.8496)
    EXPECT_NEAR(full, approx, full * 0.01f);
    EXPECT_GT(full, 0.0f);
}

TEST(PhysicsTemplates, HookeSpringApproximate)
{
    auto def = PhysicsTemplates::createHookeSpring();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Extension: x=1.5, restLength=1.0, k=100
    // FULL: -100 * (1.5 - 1.0) = -50
    // APPROX: 100 * abs(1.5 - 1.0) = 50 (always positive magnitude)
    ExpressionEvaluator::VariableMap vars = {{"x", 1.5f}, {"restLength", 1.0f}};
    float full = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full, -50.0f, 0.01f);
    EXPECT_NEAR(approx, 50.0f, 0.01f);

    // Compression: x=0.5
    // FULL: -100 * (0.5 - 1.0) = 50
    // APPROX: 100 * abs(0.5 - 1.0) = 50
    vars["x"] = 0.5f;
    float fullComp = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approxComp = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(fullComp, 50.0f, 0.01f);
    EXPECT_NEAR(approxComp, 50.0f, 0.01f);
}

TEST(PhysicsTemplates, WetDarkeningApproximate)
{
    auto def = PhysicsTemplates::createWetDarkening();
    EXPECT_TRUE(def.hasTier(QualityTier::APPROXIMATE));

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // With default darkFactor=0.5, FULL and APPROXIMATE should match exactly
    ExpressionEvaluator::VariableMap vars = {{"albedo", 0.8f}, {"wetness", 1.0f}};
    float full = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approx = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(full, 0.4f, 1e-5f);
    EXPECT_NEAR(approx, 0.4f, 1e-5f);

    // Dry surface: both should return albedo unchanged
    vars["wetness"] = 0.0f;
    float fullDry = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float approxDry = eval.evaluate(*def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(fullDry, 0.8f, 1e-5f);
    EXPECT_NEAR(approxDry, 0.8f, 1e-5f);
}

// ===========================================================================
// Post-Processing formula templates
// ===========================================================================

TEST(PhysicsTemplates, BloomThresholdExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* bloom = lib.findByName("bloom_threshold");
    ASSERT_NE(bloom, nullptr);
    EXPECT_EQ(bloom->category, "post_processing");
    EXPECT_TRUE(bloom->hasTier(QualityTier::FULL));
    EXPECT_TRUE(bloom->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, BloomThresholdEvaluates)
{
    auto def = PhysicsTemplates::createBloomThreshold();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Below threshold: luminance=0.5, threshold=1.0 -> max(0, -0.5) = 0
    // FULL: 0 / (-0.5 + 0.5) = 0/0 -> safe div returns 0
    ExpressionEvaluator::VariableMap vars = {{"luminance", 0.5f}};
    float below = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(below, 0.0f, 0.01f);

    // Above threshold: luminance=2.0, threshold=1.0
    // FULL: max(0, 1.0) / (1.0 + 0.5) = 1.0 / 1.5 ~ 0.667
    vars["luminance"] = 2.0f;
    float above = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(above, 1.0f / 1.5f, 0.01f);

    // APPROXIMATE (hard threshold): max(0, 2.0 - 1.0) = 1.0
    float approxAbove = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxAbove, 1.0f, 0.01f);

    // APPROXIMATE below threshold: max(0, 0.5 - 1.0) = 0
    vars["luminance"] = 0.5f;
    float approxBelow = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxBelow, 0.0f, 0.01f);
}

TEST(PhysicsTemplates, VignetteExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* vig = lib.findByName("vignette");
    ASSERT_NE(vig, nullptr);
    EXPECT_EQ(vig->category, "post_processing");
    EXPECT_TRUE(vig->hasTier(QualityTier::FULL));
    EXPECT_TRUE(vig->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, VignetteEvaluates)
{
    auto def = PhysicsTemplates::createVignette();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At center (distance=0): result = 1 - 0.5 * 0^2 = 1.0
    ExpressionEvaluator::VariableMap vars = {{"distance", 0.0f}};
    float center = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(center, 1.0f, 1e-5f);

    // At edge (distance=1): result = 1 - 0.5 * 1^2 = 0.5
    vars["distance"] = 1.0f;
    float edge = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(edge, 0.5f, 1e-5f);

    // APPROXIMATE at center: max(0, 1 - 0.5*0) = 1.0
    vars["distance"] = 0.0f;
    float approxCenter = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxCenter, 1.0f, 1e-5f);

    // APPROXIMATE at edge: max(0, 1 - 0.5*1) = 0.5
    vars["distance"] = 1.0f;
    float approxEdge = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxEdge, 0.5f, 1e-5f);
}

// ===========================================================================
// Camera formula templates
// ===========================================================================

TEST(PhysicsTemplates, ExposureEVExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* exp = lib.findByName("exposure_ev");
    ASSERT_NE(exp, nullptr);
    EXPECT_EQ(exp->category, "camera");
    EXPECT_TRUE(exp->hasTier(QualityTier::FULL));
    EXPECT_TRUE(exp->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, ExposureEVEvaluates)
{
    auto def = PhysicsTemplates::createExposureEV();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At EV=0: 1 / (pow(2,0) * 1.2) = 1 / 1.2 ~ 0.8333
    ExpressionEvaluator::VariableMap vars = {{"ev", 0.0f}};
    float ev0 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(ev0, 1.0f / 1.2f, 0.01f);

    // At EV=1: 1 / (2 * 1.2) = 1 / 2.4 ~ 0.4167
    vars["ev"] = 1.0f;
    float ev1 = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(ev1, 1.0f / 2.4f, 0.01f);

    // APPROXIMATE at EV=0: pow(2, 0) * 0.833 = 0.833
    vars["ev"] = 0.0f;
    float approx0 = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approx0, 0.833f, 0.01f);

    // APPROXIMATE at EV=1: pow(2, -1) * 0.833 = 0.5 * 0.833 = 0.4165
    vars["ev"] = 1.0f;
    float approx1 = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approx1, 0.5f * 0.833f, 0.01f);
}

TEST(PhysicsTemplates, DofCoCExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* dof = lib.findByName("dof_coc");
    ASSERT_NE(dof, nullptr);
    EXPECT_EQ(dof->category, "camera");
    EXPECT_TRUE(dof->hasTier(QualityTier::FULL));
    EXPECT_TRUE(dof->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, DofCoCEvaluates)
{
    auto def = PhysicsTemplates::createDofCoC();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // At focus distance (depth=5.0): CoC = 0 (in focus)
    ExpressionEvaluator::VariableMap vars = {{"depth", 5.0f}};
    float atFocus = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(atFocus, 0.0f, 0.01f);

    // Out of focus (depth=3.0):
    // abs(2.8 * 0.05 * (5.0 - 3.0) / (3.0 * (5.0 - 0.05)))
    // = abs(2.8 * 0.05 * 2.0 / (3.0 * 4.95))
    // = abs(0.28 / 14.85) ~ 0.01886
    vars["depth"] = 3.0f;
    float outOfFocus = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    float expected = std::abs(2.8f * 0.05f * 2.0f / (3.0f * 4.95f));
    EXPECT_NEAR(outOfFocus, expected, 0.001f);

    // APPROXIMATE at focus distance: abs(2.8 * 0 / 5.0) = 0
    vars["depth"] = 5.0f;
    float approxFocus = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxFocus, 0.0f, 0.01f);

    // APPROXIMATE out of focus: abs(2.8 * (5.0-3.0) / 5.0) = abs(5.6/5) = 1.12
    vars["depth"] = 3.0f;
    float approxOut = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxOut, std::abs(2.8f * 2.0f / 5.0f), 0.01f);
}

// ===========================================================================
// Terrain formula templates
// ===========================================================================

TEST(PhysicsTemplates, HeightBlendExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* hb = lib.findByName("height_blend");
    ASSERT_NE(hb, nullptr);
    EXPECT_EQ(hb->category, "terrain");
    EXPECT_TRUE(hb->hasTier(QualityTier::FULL));
    EXPECT_TRUE(hb->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, HeightBlendEvaluates)
{
    auto def = PhysicsTemplates::createHeightBlend();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // heightA clearly higher: saturate((1.0 - 0.0 + 0.5) / 0.1) = saturate(15) = 1.0
    ExpressionEvaluator::VariableMap vars = {
        {"heightA", 1.0f}, {"heightB", 0.0f}, {"blendFactor", 0.5f}
    };
    float aHigher = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(aHigher, 1.0f, 0.01f);

    // heightB clearly higher: saturate((0.0 - 1.0 + 0.0) / 0.1) = saturate(-10) = 0.0
    vars = {{"heightA", 0.0f}, {"heightB", 1.0f}, {"blendFactor", 0.0f}};
    float bHigher = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(bHigher, 0.0f, 0.01f);

    // APPROXIMATE step function: heightA + blendFactor > heightB -> 1.0
    vars = {{"heightA", 1.0f}, {"heightB", 0.0f}, {"blendFactor", 0.5f}};
    float approxA = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxA, 1.0f, 0.01f);

    // APPROXIMATE: heightA + blendFactor < heightB -> 0.0
    vars = {{"heightA", 0.0f}, {"heightB", 1.0f}, {"blendFactor", 0.0f}};
    float approxB = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxB, 0.0f, 0.01f);
}

TEST(PhysicsTemplates, ThermalErosionExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* te = lib.findByName("thermal_erosion");
    ASSERT_NE(te, nullptr);
    EXPECT_EQ(te->category, "terrain");
    EXPECT_TRUE(te->hasTier(QualityTier::FULL));
    EXPECT_TRUE(te->hasTier(QualityTier::APPROXIMATE));
}

TEST(PhysicsTemplates, ThermalErosionEvaluates)
{
    auto def = PhysicsTemplates::createThermalErosion();
    ExpressionEvaluator eval;

    std::unordered_map<std::string, float> coeffs(
        def.coefficients.begin(), def.coefficients.end());

    // Below talus angle: slope=0.3, talusAngle=0.577 -> max(0, -0.277) = 0
    ExpressionEvaluator::VariableMap vars = {{"slope", 0.3f}, {"dt", 0.016f}};
    float below = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(below, 0.0f, 1e-5f);

    // Above talus angle: slope=1.0, talusAngle=0.577
    // transfer = 0.5 * max(0, 1.0 - 0.577) * 0.016 = 0.5 * 0.423 * 0.016 ~ 0.003384
    vars["slope"] = 1.0f;
    float above = eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
    EXPECT_NEAR(above, 0.5f * 0.423f * 0.016f, 0.001f);

    // APPROXIMATE below talus: saturate(0.3 - 0.577) = saturate(-0.277) = 0
    vars["slope"] = 0.3f;
    float approxBelow = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxBelow, 0.0f, 1e-5f);

    // APPROXIMATE above talus: saturate(1.0 - 0.577) = saturate(0.423) = 0.423
    // transfer = 0.5 * 0.423 * 0.016 ~ 0.003384 (same as FULL for slope < 1+talus)
    vars["slope"] = 1.0f;
    float approxAbove = eval.evaluate(
        *def.getExpression(QualityTier::APPROXIMATE), vars, coeffs);
    EXPECT_NEAR(approxAbove, 0.5f * 0.423f * 0.016f, 0.001f);
}
