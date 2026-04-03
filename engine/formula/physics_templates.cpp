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

    def.source = "Common PBR technique; physically based on Snell/Fresnel at water-surface interface";
    return def;
}

} // namespace Vestige
