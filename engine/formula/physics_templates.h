// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

    // PBR Lighting
    static FormulaDefinition createGGXDistribution();
    static FormulaDefinition createSchlickGeometry();
    static FormulaDefinition createACESTonemap();
    static FormulaDefinition createSpotConeFalloff();

    // Animation
    static FormulaDefinition createEaseInSine();
    static FormulaDefinition createFastNegExp();

    // Post-Processing
    static FormulaDefinition createBloomThreshold();
    static FormulaDefinition createVignette();

    // Camera
    static FormulaDefinition createExposureEV();
    static FormulaDefinition createDofCoC();

    // Terrain
    static FormulaDefinition createHeightBlend();
    static FormulaDefinition createThermalErosion();

    // Path tracing (DOOM_Ants Workbench requests, 3D_E-0006..0010).
    // Scalar PDFs / weights / transfer functions exported as GLSL for an
    // external Vulkan path tracer; the vector sample-direction routines stay
    // hand-written (no coefficients to fit) — see
    // docs/research/pathtracer_formula_coverage_design.md §2.
    // Sampling PDFs (3D_E-0006)
    static FormulaDefinition createCosineHemispherePdf();
    static FormulaDefinition createGgxVndfPdf();
    static FormulaDefinition createMisPowerHeuristic();
    // Denoiser / temporal accumulation — SVGF family (3D_E-0007)
    static FormulaDefinition createTemporalAlpha();
    static FormulaDefinition createEdgeStoppingDepth();
    static FormulaDefinition createEdgeStoppingNormal();
    static FormulaDefinition createEdgeStoppingLuminance();
    static FormulaDefinition createAdaptiveSampleCount();
    // Russian roulette (3D_E-0008)
    static FormulaDefinition createRrSurvival();
    // sRGB transfer functions (3D_E-0010)
    static FormulaDefinition createSrgbToLinear();
    static FormulaDefinition createLinearToSrgb();
};

} // namespace Vestige
