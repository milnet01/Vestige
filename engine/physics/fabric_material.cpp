/// @file fabric_material.cpp
/// @brief Fabric material database with physically-based properties.
///
/// Values derived from:
///   - Kawabata Evaluation System (KES-F) measurements
///   - Textile industry density standards (GSM)
///   - Friction coefficients (Optitex, Engineering Toolbox)
///   - XPBD compliance mapping (Macklin, Muller, Chentanez 2016)
///   - Biblical textile research (Tandfonline 2025 goat hair study)
///
/// Validated against existing Vestige cloth presets (linenCurtain, tentFabric, etc.)
#include "physics/fabric_material.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Material database — ordered by FabricType enum
// ---------------------------------------------------------------------------

static const std::array<FabricMaterial, static_cast<size_t>(FabricType::COUNT)> s_materials = {{
    // Common fabrics (light → heavy)
    {FabricType::CHIFFON,          "Chiffon",          "Ultra-light sheer fabric",
     35.0f,   0.0003f,   0.001f,    0.08f,    0.008f, 0.15f, 2.5f},

    {FabricType::SILK,             "Silk",             "Light smooth fabric",
     80.0f,   0.0002f,   0.0008f,   0.05f,    0.010f, 0.18f, 2.2f},

    {FabricType::COTTON,           "Cotton",           "Medium-weight everyday fabric",
     150.0f,  0.0001f,   0.0005f,   0.015f,   0.020f, 0.35f, 1.5f},

    {FabricType::POLYESTER,        "Polyester",        "Synthetic medium-weight fabric",
     150.0f,  0.00015f,  0.0006f,   0.025f,   0.015f, 0.25f, 1.6f},

    {FabricType::LINEN,            "Linen",            "Natural fabric, low stretch",
     180.0f,  0.00008f,  0.0004f,   0.008f,   0.022f, 0.45f, 1.4f},

    {FabricType::WOOL,             "Wool",             "Warm heavy suiting fabric",
     280.0f,  0.00008f,  0.0004f,   0.010f,   0.025f, 0.40f, 1.2f},

    {FabricType::VELVET,           "Velvet",           "Soft heavy fabric with nap",
     300.0f,  0.0001f,   0.0005f,   0.012f,   0.028f, 0.50f, 1.3f},

    {FabricType::DENIM,            "Denim",            "Stiff heavy twill weave",
     400.0f,  0.00003f,  0.0002f,   0.003f,   0.030f, 0.55f, 1.0f},

    {FabricType::CANVAS,           "Canvas",           "Very stiff heavy fabric",
     450.0f,  0.00002f,  0.00015f,  0.002f,   0.035f, 0.65f, 0.9f},

    {FabricType::LEATHER,          "Leather",          "Animal hide, minimal drape",
     800.0f,  0.000005f, 0.00008f,  0.0005f,  0.045f, 0.85f, 0.7f},

    // Biblical/historical fabrics
    {FabricType::FINE_LINEN,       "Fine Linen",       "Shesh Moshzar — twisted Egyptian linen",
     120.0f,  0.00005f,  0.0003f,   0.005f,   0.020f, 0.40f, 1.5f},

    {FabricType::EMBROIDERED_VEIL, "Embroidered Veil", "Paroketh — linen-wool with dyed yarn",
     220.0f,  0.00004f,  0.00025f,  0.003f,   0.025f, 0.45f, 1.3f},

    {FabricType::GOAT_HAIR,        "Goat Hair",        "Izzim — woven goat hair tent covering",
     1588.0f, 0.00003f,  0.00015f,  0.001f,   0.040f, 0.60f, 0.8f},

    {FabricType::RAM_SKIN,         "Ram Skin",         "Orot Elim — tanned sheepskin dyed red",
     700.0f,  0.000008f, 0.0001f,   0.0003f,  0.045f, 0.80f, 0.6f},

    {FabricType::TACHASH,          "Tachash",          "Orot Techashim — heavy outer covering",
     1200.0f, 0.000005f, 0.00008f,  0.0002f,  0.050f, 0.85f, 0.5f},
}};

// ---------------------------------------------------------------------------
// FabricDatabase implementation
// ---------------------------------------------------------------------------

const FabricMaterial& FabricDatabase::get(FabricType type)
{
    auto idx = static_cast<size_t>(type);
    if (idx >= s_materials.size())
    {
        // Fallback to cotton
        return s_materials[static_cast<size_t>(FabricType::COTTON)];
    }
    return s_materials[idx];
}

const char* FabricDatabase::getName(FabricType type)
{
    return get(type).name;
}

int FabricDatabase::getCount()
{
    return static_cast<int>(FabricType::COUNT);
}

ClothPresetConfig FabricDatabase::toPresetConfig(FabricType type)
{
    const auto& mat = get(type);
    ClothPresetConfig preset;

    // Derive particle mass from fabric density (GSM).
    // Typical grid: 20x20 particles at 0.1m spacing = 2m x 2m = 4m^2
    // Divide by ~5000 to get per-particle mass that feels right.
    // This matches existing presets: linenCurtain(120 GSM) → 0.02, heavyDrape → 0.10.
    preset.solver.particleMass = std::max(0.005f, mat.densityGSM / 5000.0f);

    // Compliance values from material directly
    preset.solver.stretchCompliance = mat.stretchCompliance;
    preset.solver.shearCompliance = mat.shearCompliance;
    preset.solver.bendCompliance = mat.bendCompliance;
    preset.solver.damping = mat.damping;

    // Substeps: stiffer fabrics converge faster, need fewer substeps.
    // Softer fabrics (high bend compliance) need more substeps.
    if (mat.bendCompliance >= 0.05f)
    {
        preset.solver.substeps = 16;  // Very soft (chiffon, silk)
    }
    else if (mat.bendCompliance >= 0.01f)
    {
        preset.solver.substeps = 12;  // Medium (cotton, polyester)
    }
    else if (mat.bendCompliance >= 0.003f)
    {
        preset.solver.substeps = 8;   // Stiff (denim, embroidered veil)
    }
    else
    {
        preset.solver.substeps = 6;   // Very stiff (canvas, leather, tachash)
    }

    // Wind strength: lighter fabrics catch more wind
    if (mat.densityGSM <= 100.0f)
    {
        preset.windStrength = 8.0f;   // Chiffon, silk
    }
    else if (mat.densityGSM <= 250.0f)
    {
        preset.windStrength = 5.0f;   // Cotton, linen, fine linen
    }
    else if (mat.densityGSM <= 500.0f)
    {
        preset.windStrength = 3.0f;   // Wool, denim, canvas
    }
    else
    {
        preset.windStrength = 2.0f;   // Leather, goat hair, tachash
    }

    preset.dragCoefficient = mat.dragCoefficient;

    return preset;
}

bool FabricDatabase::isBiblical(FabricType type)
{
    return type >= FabricType::FINE_LINEN && type < FabricType::COUNT;
}

} // namespace Vestige
