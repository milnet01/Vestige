/// @file test_quality_manager.cpp
/// @brief Unit tests for FormulaQualityManager and water formula quality tiers.
#include <gtest/gtest.h>

#include "formula/quality_manager.h"
#include "formula/expression_eval.h"
#include "formula/formula_library.h"
#include "formula/physics_templates.h"

using namespace Vestige;

/// Helper: convert std::map coefficients to std::unordered_map for evaluator
static std::unordered_map<std::string, float> toUnordered(const std::map<std::string, float>& m)
{
    return {m.begin(), m.end()};
}

// ---------------------------------------------------------------------------
// FormulaQualityManager tests
// ---------------------------------------------------------------------------

TEST(FormulaQualityManager, DefaultGlobalTierIsFull)
{
    FormulaQualityManager mgr;
    EXPECT_EQ(mgr.getGlobalTier(), QualityTier::FULL);
}

TEST(FormulaQualityManager, SetGlobalTier)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr.getGlobalTier(), QualityTier::APPROXIMATE);
}

TEST(FormulaQualityManager, CategoryOverride)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::FULL);
    mgr.setCategoryTier("water", QualityTier::LUT);

    EXPECT_EQ(mgr.getEffectiveTier("water"), QualityTier::LUT);
    EXPECT_EQ(mgr.getEffectiveTier("wind"), QualityTier::FULL);
    EXPECT_TRUE(mgr.hasCategoryOverride("water"));
    EXPECT_FALSE(mgr.hasCategoryOverride("wind"));
}

TEST(FormulaQualityManager, ClearOverride)
{
    FormulaQualityManager mgr;
    mgr.setCategoryTier("water", QualityTier::LUT);
    EXPECT_TRUE(mgr.hasCategoryOverride("water"));

    mgr.clearCategoryOverride("water");
    EXPECT_FALSE(mgr.hasCategoryOverride("water"));
    EXPECT_EQ(mgr.getEffectiveTier("water"), QualityTier::FULL);
}

TEST(FormulaQualityManager, ClearAllOverrides)
{
    FormulaQualityManager mgr;
    mgr.setCategoryTier("water", QualityTier::LUT);
    mgr.setCategoryTier("wind", QualityTier::APPROXIMATE);
    mgr.clearAllOverrides();
    EXPECT_FALSE(mgr.hasCategoryOverride("water"));
    EXPECT_FALSE(mgr.hasCategoryOverride("wind"));
}

TEST(FormulaQualityManager, JsonRoundTrip)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::APPROXIMATE);
    mgr.setCategoryTier("water", QualityTier::FULL);
    mgr.setCategoryTier("lighting", QualityTier::LUT);

    nlohmann::json j = mgr.toJson();

    FormulaQualityManager mgr2;
    mgr2.fromJson(j);

    EXPECT_EQ(mgr2.getGlobalTier(), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr2.getEffectiveTier("water"), QualityTier::FULL);
    EXPECT_EQ(mgr2.getEffectiveTier("lighting"), QualityTier::LUT);
    EXPECT_EQ(mgr2.getEffectiveTier("wind"), QualityTier::APPROXIMATE);
}

TEST(FormulaQualityManager, JsonWithNoOverrides)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::LUT);

    nlohmann::json j = mgr.toJson();
    EXPECT_FALSE(j.contains("categoryOverrides"));

    FormulaQualityManager mgr2;
    mgr2.fromJson(j);
    EXPECT_EQ(mgr2.getGlobalTier(), QualityTier::LUT);
}

TEST(FormulaQualityManager, MultipleCategoryOverrides)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::FULL);
    mgr.setCategoryTier("water", QualityTier::APPROXIMATE);
    mgr.setCategoryTier("wind", QualityTier::LUT);
    mgr.setCategoryTier("lighting", QualityTier::APPROXIMATE);

    EXPECT_EQ(mgr.getOverrides().size(), 3);
    EXPECT_EQ(mgr.getEffectiveTier("water"), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr.getEffectiveTier("wind"), QualityTier::LUT);
    EXPECT_EQ(mgr.getEffectiveTier("lighting"), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr.getEffectiveTier("physics"), QualityTier::FULL);
}

// ---------------------------------------------------------------------------
// New water formula templates
// ---------------------------------------------------------------------------

TEST(WaterFormulas, CausticDepthFadeExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* fade = lib.findByName("caustic_depth_fade");
    ASSERT_NE(fade, nullptr);
    EXPECT_EQ(fade->category, "water");
    EXPECT_TRUE(fade->hasTier(QualityTier::FULL));
    EXPECT_TRUE(fade->hasTier(QualityTier::APPROXIMATE));
}

TEST(WaterFormulas, CausticDepthFadeFullTier)
{
    FormulaDefinition fade = PhysicsTemplates::createCausticDepthFade();
    ExpressionEvaluator eval;

    // At surface (depth=0), fade should be 1.0
    float atSurface = eval.evaluate(*fade.getExpression(QualityTier::FULL),
                                     {{"depth", 0.0f}, {"maxDepth", 5.0f}},
                                     toUnordered(fade.coefficients));
    EXPECT_NEAR(atSurface, 1.0f, 0.01f);

    // At max depth, fade should be 0.0
    float atMax = eval.evaluate(*fade.getExpression(QualityTier::FULL),
                                {{"depth", 5.0f}, {"maxDepth", 5.0f}},
                                toUnordered(fade.coefficients));
    EXPECT_NEAR(atMax, 0.0f, 0.01f);

    // At half depth, should be between 0 and 1
    float atHalf = eval.evaluate(*fade.getExpression(QualityTier::FULL),
                                  {{"depth", 2.5f}, {"maxDepth", 5.0f}},
                                  toUnordered(fade.coefficients));
    EXPECT_GT(atHalf, 0.0f);
    EXPECT_LT(atHalf, 1.0f);
}

TEST(WaterFormulas, CausticDepthFadeApproxTier)
{
    FormulaDefinition fade = PhysicsTemplates::createCausticDepthFade();
    ExpressionEvaluator eval;

    // APPROXIMATE: linear fade
    float atSurface = eval.evaluate(*fade.getExpression(QualityTier::APPROXIMATE),
                                     {{"depth", 0.0f}, {"maxDepth", 5.0f}},
                                     toUnordered(fade.coefficients));
    EXPECT_NEAR(atSurface, 1.0f, 0.01f);

    float atMax = eval.evaluate(*fade.getExpression(QualityTier::APPROXIMATE),
                                {{"depth", 5.0f}, {"maxDepth", 5.0f}},
                                toUnordered(fade.coefficients));
    EXPECT_NEAR(atMax, 0.0f, 0.01f);

    // Linear: at half depth should be exactly 0.5
    float atHalf = eval.evaluate(*fade.getExpression(QualityTier::APPROXIMATE),
                                  {{"depth", 2.5f}, {"maxDepth", 5.0f}},
                                  toUnordered(fade.coefficients));
    EXPECT_NEAR(atHalf, 0.5f, 0.01f);
}

TEST(WaterFormulas, WaterAbsorptionExists)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    const FormulaDefinition* abs = lib.findByName("water_absorption");
    ASSERT_NE(abs, nullptr);
    EXPECT_EQ(abs->category, "water");
    EXPECT_TRUE(abs->hasTier(QualityTier::FULL));
    EXPECT_TRUE(abs->hasTier(QualityTier::APPROXIMATE));
}

TEST(WaterFormulas, WaterAbsorptionFullTier)
{
    FormulaDefinition absorption = PhysicsTemplates::createWaterAbsorption();
    ExpressionEvaluator eval;

    // At zero thickness, no absorption
    float noAbsorption = eval.evaluate(*absorption.getExpression(QualityTier::FULL),
                                        {{"thickness", 0.0f}, {"absorptionCoeff", 0.4f}},
                                        toUnordered(absorption.coefficients));
    EXPECT_NEAR(noAbsorption, 1.0f, 0.01f);

    // At some depth, should attenuate
    float attenuated = eval.evaluate(*absorption.getExpression(QualityTier::FULL),
                                      {{"thickness", 2.0f}, {"absorptionCoeff", 0.4f}},
                                      toUnordered(absorption.coefficients));
    float expected = std::exp(-0.4f * 2.0f);
    EXPECT_NEAR(attenuated, expected, 0.01f);
}

TEST(WaterFormulas, WaterAbsorptionApproxMatchesAtLowDepth)
{
    FormulaDefinition absorption = PhysicsTemplates::createWaterAbsorption();
    ExpressionEvaluator eval;

    // At shallow depth, linear approx should be close to exponential
    float fullResult = eval.evaluate(*absorption.getExpression(QualityTier::FULL),
                                      {{"thickness", 0.5f}, {"absorptionCoeff", 0.4f}},
                                      toUnordered(absorption.coefficients));
    float approxResult = eval.evaluate(*absorption.getExpression(QualityTier::APPROXIMATE),
                                        {{"thickness", 0.5f}, {"absorptionCoeff", 0.4f}},
                                        toUnordered(absorption.coefficients));
    // At shallow depth (0.5m), should agree within ~5%
    EXPECT_NEAR(approxResult, fullResult, 0.05f);
}

// ---------------------------------------------------------------------------
// APPROXIMATE tiers for existing formulas
// ---------------------------------------------------------------------------

TEST(WaterFormulas, FresnelApproxTierExists)
{
    FormulaDefinition fresnel = PhysicsTemplates::createFresnelSchlick();
    EXPECT_TRUE(fresnel.hasTier(QualityTier::APPROXIMATE));
}

TEST(WaterFormulas, FresnelApproxVsFull)
{
    FormulaDefinition fresnel = PhysicsTemplates::createFresnelSchlick();
    ExpressionEvaluator eval;

    // At cosTheta=1 (looking straight at surface), both should return F0
    float fullDirect = eval.evaluate(*fresnel.getExpression(QualityTier::FULL),
                                      {{"cosTheta", 1.0f}}, toUnordered(fresnel.coefficients));
    float approxDirect = eval.evaluate(*fresnel.getExpression(QualityTier::APPROXIMATE),
                                        {{"cosTheta", 1.0f}}, toUnordered(fresnel.coefficients));
    EXPECT_NEAR(fullDirect, 0.02f, 0.01f);
    EXPECT_NEAR(approxDirect, 0.02f, 0.01f);

    // At cosTheta=0 (grazing angle), both should return 1.0
    float fullGrazing = eval.evaluate(*fresnel.getExpression(QualityTier::FULL),
                                       {{"cosTheta", 0.0f}}, toUnordered(fresnel.coefficients));
    float approxGrazing = eval.evaluate(*fresnel.getExpression(QualityTier::APPROXIMATE),
                                         {{"cosTheta", 0.0f}}, toUnordered(fresnel.coefficients));
    EXPECT_NEAR(fullGrazing, 1.0f, 0.01f);
    EXPECT_NEAR(approxGrazing, 1.0f, 0.01f);

    // At cosTheta=0.5 (45 degrees), approximate should be within 10%
    float fullMid = eval.evaluate(*fresnel.getExpression(QualityTier::FULL),
                                   {{"cosTheta", 0.5f}}, toUnordered(fresnel.coefficients));
    float approxMid = eval.evaluate(*fresnel.getExpression(QualityTier::APPROXIMATE),
                                     {{"cosTheta", 0.5f}}, toUnordered(fresnel.coefficients));
    EXPECT_NEAR(approxMid, fullMid, 0.1f);
}

TEST(WaterFormulas, BeerLambertApproxTierExists)
{
    FormulaDefinition beer = PhysicsTemplates::createBeerLambert();
    EXPECT_TRUE(beer.hasTier(QualityTier::APPROXIMATE));
}

TEST(WaterFormulas, BeerLambertApproxClamps)
{
    FormulaDefinition beer = PhysicsTemplates::createBeerLambert();
    ExpressionEvaluator eval;

    // At very deep, APPROXIMATE should clamp to 0 (not go negative)
    float deepApprox = eval.evaluate(*beer.getExpression(QualityTier::APPROXIMATE),
                                      {{"I0", 1.0f}, {"depth", 10.0f}},
                                      toUnordered(beer.coefficients));
    EXPECT_GE(deepApprox, 0.0f);
}

// ---------------------------------------------------------------------------
// Formula library integration
// ---------------------------------------------------------------------------

TEST(WaterFormulas, AllWaterFormulasInLibrary)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto waterFormulas = lib.findByCategory("water");
    // Should have: fresnel_schlick, beer_lambert, gerstner_wave, buoyancy,
    //              caustic_depth_fade, water_absorption
    EXPECT_GE(waterFormulas.size(), 6);
}

TEST(WaterFormulas, QualityTierFallback)
{
    FormulaDefinition wave = PhysicsTemplates::createGerstnerWave();

    // Gerstner wave now has an APPROXIMATE tier (cos-based phase shift)
    EXPECT_TRUE(wave.hasTier(QualityTier::APPROXIMATE));
    const ExprNode* expr = wave.getExpression(QualityTier::APPROXIMATE);
    EXPECT_NE(expr, nullptr);

    // LUT tier still doesn't exist — should fall back to FULL
    EXPECT_FALSE(wave.hasTier(QualityTier::LUT));
    const ExprNode* lutExpr = wave.getExpression(QualityTier::LUT);
    EXPECT_NE(lutExpr, nullptr);  // Falls back to FULL
}

// ---------------------------------------------------------------------------
// New category: post_processing — quality manager override tests
// ---------------------------------------------------------------------------

TEST(FormulaQualityManager, PostProcessingCategoryOverride)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::FULL);
    mgr.setCategoryTier("post_processing", QualityTier::APPROXIMATE);

    EXPECT_EQ(mgr.getEffectiveTier("post_processing"), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr.getEffectiveTier("water"), QualityTier::FULL);
    EXPECT_TRUE(mgr.hasCategoryOverride("post_processing"));
}

TEST(FormulaQualityManager, PostProcessingInLibrary)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto ppFormulas = lib.findByCategory("post_processing");
    EXPECT_EQ(ppFormulas.size(), 2);

    // Verify both templates exist
    EXPECT_NE(lib.findByName("bloom_threshold"), nullptr);
    EXPECT_NE(lib.findByName("vignette"), nullptr);
}

// ---------------------------------------------------------------------------
// New category: camera — quality manager override tests
// ---------------------------------------------------------------------------

TEST(FormulaQualityManager, CameraCategoryOverride)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::FULL);
    mgr.setCategoryTier("camera", QualityTier::LUT);

    EXPECT_EQ(mgr.getEffectiveTier("camera"), QualityTier::LUT);
    EXPECT_EQ(mgr.getEffectiveTier("terrain"), QualityTier::FULL);
    EXPECT_TRUE(mgr.hasCategoryOverride("camera"));
}

TEST(FormulaQualityManager, CameraInLibrary)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto camFormulas = lib.findByCategory("camera");
    EXPECT_EQ(camFormulas.size(), 2);

    EXPECT_NE(lib.findByName("exposure_ev"), nullptr);
    EXPECT_NE(lib.findByName("dof_coc"), nullptr);
}

// ---------------------------------------------------------------------------
// New category: terrain — quality manager override tests
// ---------------------------------------------------------------------------

TEST(FormulaQualityManager, TerrainCategoryOverride)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::APPROXIMATE);
    mgr.setCategoryTier("terrain", QualityTier::FULL);

    EXPECT_EQ(mgr.getEffectiveTier("terrain"), QualityTier::FULL);
    EXPECT_EQ(mgr.getEffectiveTier("camera"), QualityTier::APPROXIMATE);
    EXPECT_TRUE(mgr.hasCategoryOverride("terrain"));
}

TEST(FormulaQualityManager, TerrainInLibrary)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto terrainFormulas = lib.findByCategory("terrain");
    EXPECT_EQ(terrainFormulas.size(), 2);

    EXPECT_NE(lib.findByName("height_blend"), nullptr);
    EXPECT_NE(lib.findByName("thermal_erosion"), nullptr);
}

// ---------------------------------------------------------------------------
// All new categories in JSON round-trip
// ---------------------------------------------------------------------------

TEST(FormulaQualityManager, NewCategoriesJsonRoundTrip)
{
    FormulaQualityManager mgr;
    mgr.setGlobalTier(QualityTier::FULL);
    mgr.setCategoryTier("post_processing", QualityTier::APPROXIMATE);
    mgr.setCategoryTier("camera", QualityTier::LUT);
    mgr.setCategoryTier("terrain", QualityTier::APPROXIMATE);

    nlohmann::json j = mgr.toJson();

    FormulaQualityManager mgr2;
    mgr2.fromJson(j);

    EXPECT_EQ(mgr2.getEffectiveTier("post_processing"), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr2.getEffectiveTier("camera"), QualityTier::LUT);
    EXPECT_EQ(mgr2.getEffectiveTier("terrain"), QualityTier::APPROXIMATE);
    EXPECT_EQ(mgr2.getEffectiveTier("water"), QualityTier::FULL);
}
