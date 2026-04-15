// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fabric_material.h
/// @brief Physically-based fabric material definitions for cloth simulation.
///
/// Provides named fabric types with real-world properties (derived from KES
/// measurements and textile research) that map directly to ClothConfig parameters.
#pragma once

#include "physics/cloth_presets.h"

#include <cstdint>

namespace Vestige
{

/// @brief Named fabric types with physically-based simulation properties.
///
/// Common fabrics cover the most-used textile types in games.
/// Biblical fabrics are specific to Tabernacle/Temple reconstruction scenes.
enum class FabricType : uint8_t
{
    // Common fabrics (ordered light → heavy)
    CHIFFON,        ///< Ultra-light sheer fabric (35 GSM)
    SILK,           ///< Light smooth fabric (80 GSM)
    COTTON,         ///< Medium-weight everyday fabric (150 GSM)
    POLYESTER,      ///< Synthetic medium-weight fabric (150 GSM)
    LINEN,          ///< Medium-weight natural fabric, low stretch (180 GSM)
    WOOL,           ///< Warm heavy suiting fabric (280 GSM)
    VELVET,         ///< Soft heavy fabric with nap (300 GSM)
    DENIM,          ///< Stiff heavy twill weave (400 GSM)
    CANVAS,         ///< Very stiff heavy fabric (450 GSM)
    LEATHER,        ///< Animal hide, minimal drape (800 GSM)

    // Biblical/historical fabrics
    FINE_LINEN,       ///< Shesh Moshzar — fine twisted Egyptian linen (120 GSM)
    EMBROIDERED_VEIL, ///< Paroketh — linen-wool blend with dyed yarn (220 GSM)
    GOAT_HAIR,        ///< Izzim — woven goat hair tent covering (1588 GSM)
    RAM_SKIN,         ///< Orot Elim — tanned sheepskin dyed red (700 GSM)
    TACHASH,          ///< Orot Techashim — heavy waterproof outer covering (1200 GSM)

    COUNT
};

/// @brief Physical properties for a fabric type.
///
/// Values are calibrated from Kawabata Evaluation System (KES) research,
/// textile industry data, and validated against existing cloth presets.
struct FabricMaterial
{
    FabricType type;
    const char* name;               ///< Display name
    const char* description;        ///< Brief description
    float densityGSM;               ///< Weight in grams per square meter
    float stretchCompliance;        ///< XPBD stretch compliance (lower = stiffer)
    float shearCompliance;          ///< XPBD shear compliance
    float bendCompliance;           ///< XPBD bend compliance (higher = softer folds)
    float damping;                  ///< Velocity damping per substep
    float friction;                 ///< Surface friction coefficient
    float dragCoefficient;          ///< Aerodynamic drag coefficient
};

/// @brief Database of physically-based fabric materials.
///
/// Provides static access to fabric properties and conversion to ClothPresetConfig.
class FabricDatabase
{
public:
    /// @brief Returns the FabricMaterial for a given type.
    static const FabricMaterial& get(FabricType type);

    /// @brief Returns the display name for a fabric type.
    static const char* getName(FabricType type);

    /// @brief Returns the total number of fabric types.
    static int getCount();

    /// @brief Converts a FabricMaterial to a ClothPresetConfig suitable for ClothSimulator.
    /// Derives particleMass from density, computes substeps from stiffness,
    /// and scales wind strength inversely with density.
    static ClothPresetConfig toPresetConfig(FabricType type);

    /// @brief Returns true if the fabric type is a biblical/historical type.
    static bool isBiblical(FabricType type);
};

} // namespace Vestige
