/// @file physics_templates.h
/// @brief Pre-built formula templates for common physics and rendering equations.
///
/// These templates ship with the engine and represent formulas already scattered
/// across the codebase (cloth drag, Fresnel, Beer-Lambert, etc.). Each template
/// creates a FormulaDefinition with the correct expression tree, typed inputs,
/// coefficients, and source attribution.
#pragma once

#include "formula/formula.h"

#include <vector>

namespace Vestige
{

/// @brief Factory for built-in physics and rendering formula templates.
class PhysicsTemplates
{
public:
    /// @brief Creates all built-in formula templates.
    static std::vector<FormulaDefinition> createAll();

    // -- Individual template factories --------------------------------------
    // Wind
    static FormulaDefinition createAerodynamicDrag();
    static FormulaDefinition createStokesDrag();
    static FormulaDefinition createWindDeformation();

    // Water
    static FormulaDefinition createFresnelSchlick();
    static FormulaDefinition createBeerLambert();
    static FormulaDefinition createGerstnerWave();
    static FormulaDefinition createBuoyancy();
    static FormulaDefinition createCausticDepthFade();
    static FormulaDefinition createWaterAbsorption();

    // Lighting
    static FormulaDefinition createInverseSquareFalloff();
    static FormulaDefinition createExponentialFog();

    // Physics
    static FormulaDefinition createHookeSpring();
    static FormulaDefinition createCoulombFriction();
    static FormulaDefinition createTerminalVelocity();

    // Material
    static FormulaDefinition createWetDarkening();
};

} // namespace Vestige
