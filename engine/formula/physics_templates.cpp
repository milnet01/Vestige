// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_templates.cpp
/// @brief Built-in physics and rendering formula template implementations.
#include "formula/physics_templates.h"

namespace Vestige
{

// Convenience aliases
using E = ExprNode;
using QT = QualityTier;
using VT = FormulaValueType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Shorthand: literal node
static std::unique_ptr<ExprNode> lit(float v) { return E::literal(v); }

/// @brief Shorthand: variable node
static std::unique_ptr<ExprNode> var(const std::string& n) { return E::variable(n); }

/// @brief Shorthand: binary op
static std::unique_ptr<ExprNode> binOp(const std::string& op,
                                        std::unique_ptr<ExprNode> l,
                                        std::unique_ptr<ExprNode> r)
{
    return E::binaryOp(op, std::move(l), std::move(r));
}

/// @brief Shorthand: unary function
static std::unique_ptr<ExprNode> fn(const std::string& name,
                                     std::unique_ptr<ExprNode> arg)
{
    return E::unaryOp(name, std::move(arg));
}

// ---------------------------------------------------------------------------
// createAll
// ---------------------------------------------------------------------------

std::vector<FormulaDefinition> PhysicsTemplates::createAll()
{
    std::vector<FormulaDefinition> all;
    all.push_back(createAerodynamicDrag());
    all.push_back(createStokesDrag());
    all.push_back(createWindDeformation());
    all.push_back(createFresnelSchlick());
    all.push_back(createBeerLambert());
    all.push_back(createGerstnerWave());
    all.push_back(createBuoyancy());
    all.push_back(createCausticDepthFade());
    all.push_back(createWaterAbsorption());
    all.push_back(createInverseSquareFalloff());
    all.push_back(createExponentialFog());
    all.push_back(createHookeSpring());
    all.push_back(createCoulombFriction());
    all.push_back(createTerminalVelocity());
    all.push_back(createWetDarkening());
    // PBR Lighting
    all.push_back(createGGXDistribution());
    all.push_back(createSchlickGeometry());
    all.push_back(createACESTonemap());
    all.push_back(createSpotConeFalloff());
    // Animation
    all.push_back(createEaseInSine());
    all.push_back(createFastNegExp());
    // Post-Processing
    all.push_back(createBloomThreshold());
    all.push_back(createVignette());
    // Camera
    all.push_back(createExposureEV());
    all.push_back(createDofCoC());
    // Terrain
    all.push_back(createHeightBlend());
    all.push_back(createThermalErosion());
    return all;
}

// ---------------------------------------------------------------------------
// Wind formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createAerodynamicDrag()
{
    // F_scalar = 0.5 * Cd * rho * A * vDotN
    // (vec3 direction handled by caller multiplying by surface normal)
    FormulaDefinition def;
    def.name = "aerodynamic_drag";
    def.category = "wind";
    def.description = "Aerodynamic drag force magnitude on a surface panel. "
                      "Multiply result by surface normal for force vector.";
    def.inputs = {
        {"vDotN",       VT::FLOAT, "m/s",   0.0f},
        {"surfaceArea", VT::FLOAT, "m2",    1.0f},
        {"airDensity",  VT::FLOAT, "kg/m3", 1.225f}
    };
    def.output = {VT::FLOAT, "N"};
    def.coefficients = {{"Cd", 0.47f}};

    // 0.5 * Cd * airDensity * surfaceArea * vDotN
    def.expressions[QT::FULL] =
        binOp("*", lit(0.5f),
            binOp("*", var("Cd"),
                binOp("*", var("airDensity"),
                    binOp("*", var("surfaceArea"), var("vDotN")))));

    def.source = "Classical aerodynamics; matches cloth_simulator.cpp implementation";
    return def;
}

FormulaDefinition PhysicsTemplates::createStokesDrag()
{
    // F = 6 * pi * mu * r * v  (low Reynolds number)
    FormulaDefinition def;
    def.name = "stokes_drag";
    def.category = "wind";
    def.description = "Stokes drag for small spherical particles at low Reynolds number. "
                      "Valid for dust, rain drops, small debris.";
    def.inputs = {
        {"velocity", VT::FLOAT, "m/s", 0.0f},
        {"radius",   VT::FLOAT, "m",   0.001f}
    };
    def.output = {VT::FLOAT, "N"};
    def.coefficients = {{"mu", 1.81e-5f}};  // Dynamic viscosity of air at 20C

    // 6 * pi * mu * radius * velocity
    // pi ~ 3.14159265
    def.expressions[QT::FULL] =
        binOp("*", lit(6.0f),
            binOp("*", lit(3.14159265f),
                binOp("*", var("mu"),
                    binOp("*", var("radius"), var("velocity")))));

    // APPROXIMATE: precomputed 6*pi ~ 18.85 — avoids two multiplies.
    // mu * radius * velocity * 18.85
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("mu"),
            binOp("*", var("radius"),
                binOp("*", var("velocity"), lit(18.85f))));

    def.source = "Stokes' law (1851); valid for Re < 1";
    return def;
}

FormulaDefinition PhysicsTemplates::createWindDeformation()
{
    // bend = windSpeed * flexibility * height^2
    FormulaDefinition def;
    def.name = "wind_deformation";
    def.category = "wind";
    def.description = "Wind-driven bending displacement for vegetation. "
                      "Height-squared gives realistic base-anchored deformation.";
    def.inputs = {
        {"windSpeed",   VT::FLOAT, "m/s", 1.0f},
        {"flexibility", VT::FLOAT, "",    1.0f},
        {"height",      VT::FLOAT, "m",   1.0f}
    };
    def.output = {VT::FLOAT, "m"};

    // windSpeed * flexibility * height^2
    def.expressions[QT::FULL] =
        binOp("*", var("windSpeed"),
            binOp("*", var("flexibility"),
                binOp("pow", var("height"), lit(2.0f))));

    def.source = "GPU Gems 3 Ch.6 vegetation animation";
    return def;
}

// ---------------------------------------------------------------------------
// Water formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createFresnelSchlick()
{
    // R = F0 + (1 - F0) * (1 - cosTheta)^5
    FormulaDefinition def;
    def.name = "fresnel_schlick";
    def.category = "water";
    def.description = "Schlick approximation of Fresnel reflectance. "
                      "F0=0.02 for water (n=1.33).";
    def.inputs = {
        {"cosTheta", VT::FLOAT, "", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"F0", 0.02f}};

    // F0 + (1 - F0) * (1 - cosTheta)^5
    def.expressions[QT::FULL] =
        binOp("+", var("F0"),
            binOp("*",
                binOp("-", lit(1.0f), var("F0")),
                binOp("pow",
                    binOp("-", lit(1.0f), var("cosTheta")),
                    lit(5.0f))));

    // APPROXIMATE: power of 3 instead of 5 — less dramatic grazing angle
    // but avoids the full quintic evaluation. Visually ~95% identical.
    def.expressions[QT::APPROXIMATE] =
        binOp("+", var("F0"),
            binOp("*",
                binOp("-", lit(1.0f), var("F0")),
                binOp("pow",
                    binOp("-", lit(1.0f), var("cosTheta")),
                    lit(3.0f))));

    def.source = "Schlick 1994; matches water.frag.glsl implementation";
    return def;
}

FormulaDefinition PhysicsTemplates::createBeerLambert()
{
    // I = I0 * exp(-alpha * depth)
    FormulaDefinition def;
    def.name = "beer_lambert";
    def.category = "water";
    def.description = "Beer-Lambert absorption law. Models exponential light "
                      "attenuation through a medium (water, fog).";
    def.inputs = {
        {"I0",    VT::FLOAT, "",  1.0f},
        {"depth", VT::FLOAT, "m", 0.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"alpha", 0.4f}};

    // I0 * exp(-alpha * depth)
    def.expressions[QT::FULL] =
        binOp("*", var("I0"),
            fn("exp",
                fn("negate",
                    binOp("*", var("alpha"), var("depth")))));

    // APPROXIMATE: linear falloff avoids exp() — max(1 - alpha * depth, 0)
    // Good for shallow depths; diverges at depth > 1/alpha.
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("I0"),
            binOp("max",
                binOp("-", lit(1.0f),
                    binOp("*", var("alpha"), var("depth"))),
                lit(0.0f)));

    def.source = "Beer-Lambert law; matches water.frag.glsl absorption";
    return def;
}

FormulaDefinition PhysicsTemplates::createGerstnerWave()
{
    // y = amplitude * sin(k * x - omega * t + phase)
    FormulaDefinition def;
    def.name = "gerstner_wave";
    def.category = "water";
    def.description = "Single Gerstner wave displacement (vertical component). "
                      "Sum multiple waves for realistic water surface.";
    def.inputs = {
        {"x",     VT::FLOAT, "m", 0.0f},
        {"t",     VT::FLOAT, "s", 0.0f}
    };
    def.output = {VT::FLOAT, "m"};
    def.coefficients = {
        {"amplitude", 0.5f},
        {"k",         1.0f},
        {"omega",     1.0f},
        {"phase",     0.0f}
    };

    // amplitude * sin(k * x - omega * t + phase)
    def.expressions[QT::FULL] =
        binOp("*", var("amplitude"),
            fn("sin",
                binOp("+",
                    binOp("-",
                        binOp("*", var("k"), var("x")),
                        binOp("*", var("omega"), var("t"))),
                    var("phase"))));

    // APPROXIMATE: use cos() for the derivative-like shape, scaled.
    // amplitude * cos(k * x - omega * t + phase) produces a phase-shifted
    // version that's visually similar for surface animation. On many GPUs
    // sin/cos are the same cost, but this tier exists for the workbench
    // to curve-fit a polynomial replacement. The real win is a LUT tier.
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("amplitude"),
            fn("cos",
                binOp("+",
                    binOp("-",
                        binOp("*", var("k"), var("x")),
                        binOp("*", var("omega"), var("t"))),
                    var("phase"))));

    def.source = "Gerstner wave equation; matches water_surface.h Wave config";
    return def;
}

FormulaDefinition PhysicsTemplates::createBuoyancy()
{
    // F = fluidDensity * submergedVolume * g
    FormulaDefinition def;
    def.name = "buoyancy";
    def.category = "water";
    def.description = "Archimedes buoyancy force magnitude. Returns upward force "
                      "on a partially or fully submerged object.";
    def.inputs = {
        {"fluidDensity",   VT::FLOAT, "kg/m3", 997.0f},
        {"submergedVolume", VT::FLOAT, "m3",    1.0f}
    };
    def.output = {VT::FLOAT, "N"};
    def.coefficients = {{"g", 9.81f}};

    // fluidDensity * submergedVolume * g
    def.expressions[QT::FULL] =
        binOp("*", var("fluidDensity"),
            binOp("*", var("submergedVolume"), var("g")));

    def.source = "Archimedes' principle; matches environment_forces.cpp buoyancy";
    return def;
}

FormulaDefinition PhysicsTemplates::createCausticDepthFade()
{
    // fade = saturate(1 - depth / maxDepth)
    // Caustics intensity fades linearly with depth below water surface.
    // FULL tier matches the smoothstep in scene.frag.glsl; APPROXIMATE uses linear.
    FormulaDefinition def;
    def.name = "caustic_depth_fade";
    def.category = "water";
    def.description = "Caustic intensity fade with depth below water surface. "
                      "Returns 1.0 at surface, 0.0 at maxDepth.";
    def.inputs = {
        {"depth",    VT::FLOAT, "m", 0.0f},
        {"maxDepth", VT::FLOAT, "m", 5.0f}
    };
    def.output = {VT::FLOAT, ""};

    // FULL: smoothstep-like — t = saturate(depth/maxDepth), result = 1 - t*t*(3-2*t)
    // We express: t = saturate(depth / maxDepth)
    //             fade = 1 - t * t * (3 - 2 * t)
    auto t = fn("saturate", binOp("/", var("depth"), var("maxDepth")));
    auto t2 = fn("saturate", binOp("/", var("depth"), var("maxDepth")));
    auto t3 = fn("saturate", binOp("/", var("depth"), var("maxDepth")));
    def.expressions[QT::FULL] =
        binOp("-", lit(1.0f),
            binOp("*",
                binOp("*", std::move(t), std::move(t2)),
                binOp("-", lit(3.0f),
                    binOp("*", lit(2.0f), std::move(t3)))));

    // APPROXIMATE: simple linear — saturate(1 - depth / maxDepth)
    def.expressions[QT::APPROXIMATE] =
        fn("saturate",
            binOp("-", lit(1.0f),
                binOp("/", var("depth"), var("maxDepth"))));

    def.source = "Matches scene.frag.glsl smoothstep(0, 5, depthBelowWater) depth fade";
    return def;
}

FormulaDefinition PhysicsTemplates::createWaterAbsorption()
{
    // Per-channel Beer-Lambert with water-specific absorption coefficients.
    // In the shader, this is applied per-channel (R, G, B), but the formula
    // library represents the scalar version. The shader applies it 3 times
    // with different alpha values.
    //
    // result = exp(-absorptionCoeff * thickness)
    FormulaDefinition def;
    def.name = "water_absorption";
    def.category = "water";
    def.description = "Per-channel water light absorption (Beer-Lambert). "
                      "Red light absorbed fastest (0.4), green medium (0.2), "
                      "blue slowest (0.1). Apply per-channel in shader.";
    def.inputs = {
        {"thickness",       VT::FLOAT, "m", 0.0f},
        {"absorptionCoeff", VT::FLOAT, "",  0.4f}
    };
    def.output = {VT::FLOAT, ""};

    // FULL: exp(-absorptionCoeff * thickness)
    def.expressions[QT::FULL] =
        fn("exp",
            fn("negate",
                binOp("*", var("absorptionCoeff"), var("thickness"))));

    // APPROXIMATE: max(1 - absorptionCoeff * thickness, 0) — linear falloff
    def.expressions[QT::APPROXIMATE] =
        binOp("max",
            binOp("-", lit(1.0f),
                binOp("*", var("absorptionCoeff"), var("thickness"))),
            lit(0.0f));

    def.source = "Beer-Lambert with water-specific coefficients from water.frag.glsl";
    return def;
}

// ---------------------------------------------------------------------------
// Lighting formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createInverseSquareFalloff()
{
    // atten = 1 / (constant + linear*d + quadratic*d^2)
    FormulaDefinition def;
    def.name = "inverse_square_falloff";
    def.category = "lighting";
    def.description = "Light attenuation with configurable constant, linear, and "
                      "quadratic terms. Standard OpenGL attenuation model.";
    def.inputs = {
        {"distance", VT::FLOAT, "m", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"constant",  1.0f},
        {"linear",    0.09f},
        {"quadratic", 0.032f}
    };

    // 1 / (constant + linear * d + quadratic * d^2)
    def.expressions[QT::FULL] =
        binOp("/", lit(1.0f),
            binOp("+", var("constant"),
                binOp("+",
                    binOp("*", var("linear"), var("distance")),
                    binOp("*", var("quadratic"),
                        binOp("pow", var("distance"), lit(2.0f))))));

    // APPROXIMATE: drop the linear term — 1 / (1 + quadratic * d^2).
    // Saves one multiply-add per light per pixel. Slightly sharper falloff
    // near the light but identical asymptotic behavior.
    def.expressions[QT::APPROXIMATE] =
        binOp("/", lit(1.0f),
            binOp("+", lit(1.0f),
                binOp("*", var("quadratic"),
                    binOp("pow", var("distance"), lit(2.0f)))));

    def.source = "Standard OpenGL attenuation; matches scene.frag.glsl";
    return def;
}

FormulaDefinition PhysicsTemplates::createExponentialFog()
{
    // visibility = exp(-density * distance)
    FormulaDefinition def;
    def.name = "exponential_fog";
    def.category = "lighting";
    def.description = "Exponential fog visibility factor. Returns [0,1] where "
                      "0 is fully fogged and 1 is fully clear.";
    def.inputs = {
        {"distance", VT::FLOAT, "m", 0.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"density", 0.01f}};

    // exp(-density * distance)
    def.expressions[QT::FULL] =
        fn("exp",
            fn("negate",
                binOp("*", var("density"), var("distance"))));

    // APPROXIMATE: linear falloff — max(1 - density * distance, 0).
    // Avoids exp() entirely. Good for short distances; diverges at
    // distance > 1/density where exp() approaches zero but linear goes negative.
    def.expressions[QT::APPROXIMATE] =
        binOp("max",
            binOp("-", lit(1.0f),
                binOp("*", var("density"), var("distance"))),
            lit(0.0f));

    def.source = "Exponential fog model; common in real-time rendering";
    return def;
}

// ---------------------------------------------------------------------------
// Physics formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createHookeSpring()
{
    // F = -k * (x - restLength)
    FormulaDefinition def;
    def.name = "hooke_spring";
    def.category = "physics";
    def.description = "Hooke's law spring force. Negative sign indicates "
                      "restoring force toward rest length.";
    def.inputs = {
        {"x",          VT::FLOAT, "m", 0.0f},
        {"restLength", VT::FLOAT, "m", 1.0f}
    };
    def.output = {VT::FLOAT, "N"};
    def.coefficients = {{"k", 100.0f}};

    // -k * (x - restLength)
    def.expressions[QT::FULL] =
        fn("negate",
            binOp("*", var("k"),
                binOp("-", var("x"), var("restLength"))));

    // APPROXIMATE: linearized for small displacements — k * abs(x - restLength).
    // Drops the negate (always positive magnitude). Caller applies direction.
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("k"),
            fn("abs",
                binOp("-", var("x"), var("restLength"))));

    def.source = "Hooke's law; used in cloth XPBD solver distance constraints";
    return def;
}

FormulaDefinition PhysicsTemplates::createCoulombFriction()
{
    // Ft_clamped = min(Ft, mu * Fn)
    FormulaDefinition def;
    def.name = "coulomb_friction";
    def.category = "physics";
    def.description = "Coulomb friction model. Clamps tangential force to "
                      "mu * normal_force. Returns the clamped tangential force.";
    def.inputs = {
        {"Ft", VT::FLOAT, "N", 0.0f},
        {"Fn", VT::FLOAT, "N", 0.0f}
    };
    def.output = {VT::FLOAT, "N"};
    def.coefficients = {{"mu", 0.5f}};

    // min(Ft, mu * Fn)
    def.expressions[QT::FULL] =
        binOp("min", var("Ft"),
            binOp("*", var("mu"), var("Fn")));

    def.source = "Coulomb friction; matches cloth_simulator.h friction model";
    return def;
}

FormulaDefinition PhysicsTemplates::createTerminalVelocity()
{
    // v_t = sqrt(2 * m * g / (rho * A * Cd))
    FormulaDefinition def;
    def.name = "terminal_velocity";
    def.category = "physics";
    def.description = "Terminal velocity of a falling object in a fluid. "
                      "Balances gravitational and drag forces.";
    def.inputs = {
        {"mass",       VT::FLOAT, "kg",    1.0f},
        {"area",       VT::FLOAT, "m2",    1.0f},
        {"airDensity", VT::FLOAT, "kg/m3", 1.225f}
    };
    def.output = {VT::FLOAT, "m/s"};
    def.coefficients = {
        {"g",  9.81f},
        {"Cd", 0.47f}
    };

    // sqrt(2 * mass * g / (airDensity * area * Cd))
    def.expressions[QT::FULL] =
        fn("sqrt",
            binOp("/",
                binOp("*", lit(2.0f),
                    binOp("*", var("mass"), var("g"))),
                binOp("*", var("airDensity"),
                    binOp("*", var("area"), var("Cd")))));

    def.source = "Derived from drag equation equilibrium with gravity";
    return def;
}

// ---------------------------------------------------------------------------
// Material formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createWetDarkening()
{
    // darkenedAlbedo = albedo * (1 - wetness * darkFactor)
    FormulaDefinition def;
    def.name = "wet_darkening";
    def.category = "material";
    def.description = "Wet surface albedo darkening. Wet surfaces appear darker "
                      "due to total internal reflection trapping light.";
    def.inputs = {
        {"albedo",  VT::FLOAT, "", 1.0f},
        {"wetness", VT::FLOAT, "", 0.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"darkFactor", 0.5f}};

    // albedo * (1 - wetness * darkFactor)
    def.expressions[QT::FULL] =
        binOp("*", var("albedo"),
            binOp("-", lit(1.0f),
                binOp("*", var("wetness"), var("darkFactor"))));

    // APPROXIMATE: inline default darkFactor=0.5 as literal — albedo * (1 - wetness * 0.5).
    // Avoids one coefficient lookup per evaluation.
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("albedo"),
            binOp("-", lit(1.0f),
                binOp("*", var("wetness"), lit(0.5f))));

    def.source = "Common PBR technique; physically based on Snell/Fresnel at water-surface interface";
    return def;
}

// ---------------------------------------------------------------------------
// PBR Lighting formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createGGXDistribution()
{
    // GGX / Trowbridge-Reitz Normal Distribution Function:
    // D = alpha^2 / (PI * (NdotH^2 * (alpha^2 - 1) + 1)^2)
    // where alpha = roughness^2 (passed as coefficient).
    FormulaDefinition def;
    def.name = "ggx_distribution";
    def.category = "lighting";
    def.description = "GGX / Trowbridge-Reitz normal distribution function (NDF). "
                      "Core of the Cook-Torrance specular BRDF. alpha = roughness^2.";
    def.inputs = {
        {"NdotH", VT::FLOAT, "", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"alpha", 0.25f}};  // roughness=0.5 => alpha=0.25

    // alpha^2 / (PI * (NdotH^2 * (alpha^2 - 1) + 1)^2)
    auto alpha2 = binOp("pow", var("alpha"), lit(2.0f));
    auto NdotH2 = binOp("pow", var("NdotH"), lit(2.0f));
    auto alpha2b = binOp("pow", var("alpha"), lit(2.0f));
    auto denom_inner = binOp("+",
        binOp("*", std::move(NdotH2),
            binOp("-", std::move(alpha2b), lit(1.0f))),
        lit(1.0f));
    auto denom = binOp("*", lit(3.14159265f),
        binOp("pow", std::move(denom_inner), lit(2.0f)));

    def.expressions[QT::FULL] = binOp("/", std::move(alpha2), std::move(denom));

    def.source = "GGX (Trowbridge-Reitz 1975, Walter et al. 2007); "
                 "matches scene.frag.glsl distributionGGX()";
    def.accuracy = 1.0f;
    return def;
}

FormulaDefinition PhysicsTemplates::createSchlickGeometry()
{
    // Schlick-GGX geometry function (one side):
    // G1 = NdotV / (NdotV * (1 - k) + k)
    // For direct lighting: k = (roughness + 1)^2 / 8
    // For IBL: k = roughness^2 / 2
    // Here k is a coefficient so both variants can be studied.
    FormulaDefinition def;
    def.name = "schlick_geometry";
    def.category = "lighting";
    def.description = "Schlick-GGX geometry function (one side of Smith's method). "
                      "k=(roughness+1)^2/8 for direct, k=roughness^2/2 for IBL.";
    def.inputs = {
        {"NdotV", VT::FLOAT, "", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"k", 0.125f}};  // roughness=0.5 => k=(1.5)^2/8=0.28

    // NdotV / (NdotV * (1 - k) + k)
    def.expressions[QT::FULL] =
        binOp("/", var("NdotV"),
            binOp("+",
                binOp("*", var("NdotV"),
                    binOp("-", lit(1.0f), var("k"))),
                var("k")));

    // APPROXIMATE: for k close to 0 (smooth surfaces), G1 ≈ 1.
    // For rough surfaces, a linear approximation: NdotV / (NdotV + k - NdotV*k)
    // simplifies to the same thing — the full formula is already cheap.
    // Instead, offer a clamped version that avoids the division:
    // G1 ≈ saturate(NdotV * (1/k - 1) + 1) — rearranged to avoid div by varying NdotV.
    // Actually, the original is already a single division. Keep FULL only — this formula
    // is here for visualization and curve-fitting, not further simplification.

    def.source = "Schlick 1994, applied to GGX (Karis 2013); "
                 "matches scene.frag.glsl geometrySchlickGGX()";
    def.accuracy = 1.0f;
    return def;
}

FormulaDefinition PhysicsTemplates::createACESTonemap()
{
    // ACES filmic tone mapping curve:
    // f(x) = (x * (2.51*x + 0.03)) / (x * (2.43*x + 0.59) + 0.14)
    // Constants from Krzysztof Narkowicz's ACES fit.
    FormulaDefinition def;
    def.name = "aces_tonemap";
    def.category = "rendering";
    def.description = "ACES filmic tone mapping curve (Narkowicz fit). Maps HDR "
                      "radiance to [0,1] LDR with film-like shoulder and toe.";
    def.inputs = {
        {"x", VT::FLOAT, "", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"a", 2.51f},
        {"b", 0.03f},
        {"c", 2.43f},
        {"d", 0.59f},
        {"e", 0.14f}
    };

    // (x * (a*x + b)) / (x * (c*x + d) + e)
    def.expressions[QT::FULL] =
        binOp("/",
            binOp("*", var("x"),
                binOp("+",
                    binOp("*", var("a"), var("x")),
                    var("b"))),
            binOp("+",
                binOp("*", var("x"),
                    binOp("+",
                        binOp("*", var("c"), var("x")),
                        var("d"))),
                var("e")));

    // APPROXIMATE: Reinhard tone mapping — x / (x + 1).
    // Much simpler (1 add + 1 div), no shoulder/toe shaping but preserves
    // the basic HDR-to-LDR compression. Good for low-quality render targets.
    def.expressions[QT::APPROXIMATE] =
        binOp("/", var("x"),
            binOp("+", var("x"), lit(1.0f)));

    def.source = "Narkowicz 2015 ACES fit; matches scene.frag.glsl acesToneMap(). "
                 "APPROXIMATE = Reinhard 2002";
    def.accuracy = 1.0f;
    return def;
}

FormulaDefinition PhysicsTemplates::createSpotConeFalloff()
{
    // Spot light cone intensity falloff:
    // intensity = saturate((theta - outerCutoff) / (innerCutoff - outerCutoff))
    // theta = dot(lightDir, spotDir) (cosine of angle to spot center).
    FormulaDefinition def;
    def.name = "spot_cone_falloff";
    def.category = "lighting";
    def.description = "Spot light cone intensity falloff. Returns 1.0 inside the "
                      "inner cone, 0.0 outside the outer cone, smooth blend between.";
    def.inputs = {
        {"theta", VT::FLOAT, "", 0.95f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"innerCutoff", 0.9659f},  // cos(15 degrees)
        {"outerCutoff", 0.9063f}   // cos(25 degrees)
    };

    // saturate((theta - outerCutoff) / (innerCutoff - outerCutoff))
    def.expressions[QT::FULL] =
        fn("saturate",
            binOp("/",
                binOp("-", var("theta"), var("outerCutoff")),
                binOp("-", var("innerCutoff"), var("outerCutoff"))));

    // APPROXIMATE: smoothstep-like blend for softer edges.
    // t = saturate((theta - outerCutoff) / (innerCutoff - outerCutoff))
    // result = t * t * (3 - 2 * t)
    // This is slightly more expensive but produces smoother light edges.
    auto t_approx = fn("saturate",
        binOp("/",
            binOp("-", var("theta"), var("outerCutoff")),
            binOp("-", var("innerCutoff"), var("outerCutoff"))));
    auto t2 = fn("saturate",
        binOp("/",
            binOp("-", var("theta"), var("outerCutoff")),
            binOp("-", var("innerCutoff"), var("outerCutoff"))));
    auto t3 = fn("saturate",
        binOp("/",
            binOp("-", var("theta"), var("outerCutoff")),
            binOp("-", var("innerCutoff"), var("outerCutoff"))));
    def.expressions[QT::APPROXIMATE] =
        binOp("*",
            binOp("*", std::move(t_approx), std::move(t2)),
            binOp("-", lit(3.0f),
                binOp("*", lit(2.0f), std::move(t3))));

    def.source = "Standard spot light model; matches scene.frag.glsl spot calculation";
    def.accuracy = 1.0f;
    return def;
}

// ---------------------------------------------------------------------------
// Animation formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createEaseInSine()
{
    // Penner easing: easeInSine(t) = 1 - cos(t * PI/2)
    // One of the few easing functions that uses trigonometry.
    FormulaDefinition def;
    def.name = "ease_in_sine";
    def.category = "animation";
    def.description = "Penner easeInSine easing function. Slow start using cosine. "
                      "Compare FULL (trig) vs APPROXIMATE (polynomial) in the workbench.";
    def.inputs = {
        {"t", VT::FLOAT, "", 0.0f}
    };
    def.output = {VT::FLOAT, ""};

    // FULL: 1 - cos(t * PI/2) = 1 - cos(t * 1.5707963)
    def.expressions[QT::FULL] =
        binOp("-", lit(1.0f),
            fn("cos",
                binOp("*", var("t"), lit(1.5707963f))));

    // APPROXIMATE: polynomial fit — t^2 is the simplest approximation.
    // Max error ~0.09 at t≈0.7. Use the workbench curve fitter to find
    // better coefficients: a*t^2 + b*t^3 with a≈1.23, b≈-0.23 reduces
    // max error to ~0.004.
    def.expressions[QT::APPROXIMATE] =
        binOp("+",
            binOp("*", lit(1.233f),
                binOp("pow", var("t"), lit(2.0f))),
            binOp("*", lit(-0.233f),
                binOp("pow", var("t"), lit(3.0f))));

    def.source = "Robert Penner easing equations; matches easing.cpp easeInSine()";
    def.accuracy = 1.0f;
    return def;
}

FormulaDefinition PhysicsTemplates::createFastNegExp()
{
    // Fast negative exponential approximation used in the spring-damper system.
    // exp(-x) is called per-spring per-frame in trajectory prediction.
    FormulaDefinition def;
    def.name = "fast_neg_exp";
    def.category = "animation";
    def.description = "Negative exponential and its fast rational polynomial "
                      "approximation. Used in spring-damper trajectory prediction. "
                      "APPROXIMATE: Holden's rational polynomial (no exp() call).";
    def.inputs = {
        {"x", VT::FLOAT, "", 0.0f}
    };
    def.output = {VT::FLOAT, ""};

    // FULL: exp(-x)
    def.expressions[QT::FULL] =
        fn("exp", fn("negate", var("x")));

    // APPROXIMATE: 1 / (1 + x + 0.48*x^2 + 0.235*x^3)
    // From Daniel Holden's spring-damper formulation. Avoids exp() entirely.
    // Accurate for x in [0, ~5]; diverges for large x.
    // Use the workbench fitter to optimize coefficients for your range.
    def.expressions[QT::APPROXIMATE] =
        binOp("/", lit(1.0f),
            binOp("+", lit(1.0f),
                binOp("+", var("x"),
                    binOp("+",
                        binOp("*", lit(0.48f),
                            binOp("pow", var("x"), lit(2.0f))),
                        binOp("*", lit(0.235f),
                            binOp("pow", var("x"), lit(3.0f)))))));

    def.source = "Daniel Holden 'Spring-It-On' (2020); "
                 "matches trajectory_predictor.cpp fastNegexp()";
    def.accuracy = 1.0f;
    return def;
}

// ---------------------------------------------------------------------------
// Post-Processing formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createBloomThreshold()
{
    // Soft bloom threshold extraction:
    // result = max(0, luminance - threshold) / (luminance - threshold + knee)
    // Produces a soft knee curve that avoids harsh cutoff at the threshold.
    FormulaDefinition def;
    def.name = "bloom_threshold";
    def.category = "post_processing";
    def.description = "Soft bloom threshold extraction with knee curve. "
                      "Produces smooth transition instead of hard cutoff.";
    def.inputs = {
        {"luminance", VT::FLOAT, "", 1.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"threshold", 1.0f},
        {"knee",      0.5f}
    };

    // max(0, luminance - threshold) / (luminance - threshold + knee)
    def.expressions[QT::FULL] =
        binOp("/",
            binOp("max", lit(0.0f),
                binOp("-", var("luminance"), var("threshold"))),
            binOp("+",
                binOp("-", var("luminance"), var("threshold")),
                var("knee")));

    // APPROXIMATE: hard threshold — max(0, luminance - threshold).
    // Cheaper but produces a harsh cutoff at the threshold boundary.
    def.expressions[QT::APPROXIMATE] =
        binOp("max", lit(0.0f),
            binOp("-", var("luminance"), var("threshold")));

    def.source = "Soft bloom knee from Karis 2014 (UE4 bloom); common in post-processing";
    return def;
}

FormulaDefinition PhysicsTemplates::createVignette()
{
    // Radial vignette darkening:
    // result = 1 - intensity * pow(distance, falloff)
    // distance is radial distance from center [0,1].
    FormulaDefinition def;
    def.name = "vignette";
    def.category = "post_processing";
    def.description = "Radial vignette darkening effect. Returns a multiplier "
                      "[0,1] that darkens pixels further from screen center.";
    def.inputs = {
        {"distance", VT::FLOAT, "", 0.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"intensity", 0.5f},
        {"falloff",   2.0f}
    };

    // 1 - intensity * pow(distance, falloff)
    def.expressions[QT::FULL] =
        binOp("-", lit(1.0f),
            binOp("*", var("intensity"),
                binOp("pow", var("distance"), var("falloff"))));

    // APPROXIMATE: linear version — max(0, 1 - intensity * distance).
    // Avoids pow() entirely; linear falloff instead of curved.
    def.expressions[QT::APPROXIMATE] =
        binOp("max", lit(0.0f),
            binOp("-", lit(1.0f),
                binOp("*", var("intensity"), var("distance"))));

    def.source = "Standard vignette post-processing effect";
    return def;
}

// ---------------------------------------------------------------------------
// Camera formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createExposureEV()
{
    // EV-based exposure multiplier:
    // result = 1 / (pow(2, ev) * 1.2)
    // where 1.2 is the middle-grey calibration coefficient.
    FormulaDefinition def;
    def.name = "exposure_ev";
    def.category = "camera";
    def.description = "EV-based exposure multiplier. Converts exposure value to a "
                      "linear multiplier for HDR rendering. 1.2 = middle-grey calibration.";
    def.inputs = {
        {"ev", VT::FLOAT, "", 0.0f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"calibration", 1.2f}};

    // 1 / (pow(2, ev) * calibration)
    def.expressions[QT::FULL] =
        binOp("/", lit(1.0f),
            binOp("*",
                binOp("pow", lit(2.0f), var("ev")),
                var("calibration")));

    // APPROXIMATE: pow(2, -ev) * 0.833 — precomputed 1/1.2 ~ 0.833.
    // Avoids the division; uses a single pow and multiply.
    def.expressions[QT::APPROXIMATE] =
        binOp("*",
            binOp("pow", lit(2.0f), fn("negate", var("ev"))),
            lit(0.833f));

    def.source = "Standard EV exposure model; matches camera exposure calculations";
    return def;
}

FormulaDefinition PhysicsTemplates::createDofCoC()
{
    // Circle of confusion diameter for depth of field:
    // CoC = abs(aperture * focalLength * (focusDistance - depth)
    //       / (depth * (focusDistance - focalLength)))
    FormulaDefinition def;
    def.name = "dof_coc";
    def.category = "camera";
    def.description = "Circle of confusion (CoC) diameter for depth of field. "
                      "Returns the blur disk size for a given depth.";
    def.inputs = {
        {"depth", VT::FLOAT, "m", 5.0f}
    };
    def.output = {VT::FLOAT, "m"};
    def.coefficients = {
        {"aperture",      2.8f},
        {"focalLength",   0.05f},
        {"focusDistance",  5.0f}
    };

    // abs(aperture * focalLength * (focusDistance - depth)
    //     / (depth * (focusDistance - focalLength)))
    def.expressions[QT::FULL] =
        fn("abs",
            binOp("/",
                binOp("*", var("aperture"),
                    binOp("*", var("focalLength"),
                        binOp("-", var("focusDistance"), var("depth")))),
                binOp("*", var("depth"),
                    binOp("-", var("focusDistance"), var("focalLength")))));

    // APPROXIMATE: linearized — abs(aperture * (focusDistance - depth) / focusDistance).
    // Simpler, accurate near focus distance. Drops focalLength terms.
    def.expressions[QT::APPROXIMATE] =
        fn("abs",
            binOp("/",
                binOp("*", var("aperture"),
                    binOp("-", var("focusDistance"), var("depth"))),
                var("focusDistance")));

    def.source = "Thin-lens CoC equation; standard depth of field model";
    return def;
}

// ---------------------------------------------------------------------------
// Terrain formulas
// ---------------------------------------------------------------------------

FormulaDefinition PhysicsTemplates::createHeightBlend()
{
    // Height-based texture blending (preferred over linear for terrain):
    // result = saturate((heightA - heightB + blendFactor) / blendSharpness)
    FormulaDefinition def;
    def.name = "height_blend";
    def.category = "terrain";
    def.description = "Height-based texture blending for terrain. Produces more "
                      "natural transitions than linear blending by using height data.";
    def.inputs = {
        {"heightA",     VT::FLOAT, "m", 1.0f},
        {"heightB",     VT::FLOAT, "m", 0.0f},
        {"blendFactor", VT::FLOAT, "",  0.5f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {{"blendSharpness", 0.1f}};

    // saturate((heightA - heightB + blendFactor) / blendSharpness)
    def.expressions[QT::FULL] =
        fn("saturate",
            binOp("/",
                binOp("+",
                    binOp("-", var("heightA"), var("heightB")),
                    var("blendFactor")),
                var("blendSharpness")));

    // APPROXIMATE: step function — (heightA + blendFactor > heightB) ? 1.0 : 0.0.
    // Cheapest possible blend; uses conditional on max(0, diff) being non-zero.
    def.expressions[QT::APPROXIMATE] =
        E::conditional(
            binOp("max", lit(0.0f),
                binOp("+",
                    binOp("-", var("heightA"), var("heightB")),
                    var("blendFactor"))),
            lit(1.0f),
            lit(0.0f));

    def.source = "Height-based blending from GPU Gems 3; common terrain rendering technique";
    return def;
}

FormulaDefinition PhysicsTemplates::createThermalErosion()
{
    // Thermal erosion sediment transfer rate:
    // transfer = erosionRate * max(0, slope - talusAngle) * dt
    FormulaDefinition def;
    def.name = "thermal_erosion";
    def.category = "terrain";
    def.description = "Thermal erosion sediment transfer rate. Models material "
                      "sliding down slopes that exceed the talus angle.";
    def.inputs = {
        {"slope", VT::FLOAT, "", 0.0f},
        {"dt",    VT::FLOAT, "s", 0.016f}
    };
    def.output = {VT::FLOAT, ""};
    def.coefficients = {
        {"erosionRate", 0.5f},
        {"talusAngle",  0.577f}   // tan(30 degrees)
    };

    // erosionRate * max(0, slope - talusAngle) * dt
    def.expressions[QT::FULL] =
        binOp("*", var("erosionRate"),
            binOp("*",
                binOp("max", lit(0.0f),
                    binOp("-", var("slope"), var("talusAngle"))),
                var("dt")));

    // APPROXIMATE: clamped linear — erosionRate * saturate(slope - talusAngle) * dt.
    // saturate clamps to [0,1] instead of [0,inf], avoiding large values.
    def.expressions[QT::APPROXIMATE] =
        binOp("*", var("erosionRate"),
            binOp("*",
                fn("saturate",
                    binOp("-", var("slope"), var("talusAngle"))),
                var("dt")));

    def.source = "Musgrave 1989 thermal erosion model; common in procedural terrain generation";
    return def;
}

} // namespace Vestige
