// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_material_set.h
/// @brief PBR ground-material layers for the terrain renderer (Phase 10 / slice A1).
///
/// Holds the four ground layers (grass / rock / dirt / sand) as three
/// `GL_TEXTURE_2D_ARRAY` textures — albedo (sRGB), tangent-space normal (linear),
/// and a packed material map (R=AO, G=Roughness, B=Height) — plus a per-layer world
/// tiling scale. Owned by `TerrainRenderer`; when absent or invalid the renderer keeps
/// the existing flat-colour path (design §4.1). Loading fails **soft**: any decode
/// failure or dimension mismatch leaves `isValid() == false` and creates no GL
/// resources, so callers fall back rather than crash.
#pragma once

#include <glad/gl.h>

#include <array>
#include <string>

namespace Vestige
{

/// @brief One ground layer: three source maps + its world-space tiling scale.
struct TerrainLayerDesc
{
    std::string albedoPath;    ///< sRGB colour.
    std::string normalPath;    ///< linear tangent-space normal (+Z up).
    std::string materialPath;  ///< linear packed R=AO, G=Roughness, B=Height.
    float tiling = 0.15f;      ///< world-units → UV scale (per layer).
};

/// @brief GPU resources for the four PBR ground layers (three 2D texture arrays).
class TerrainMaterialSet
{
public:
    TerrainMaterialSet() = default;
    ~TerrainMaterialSet();

    // Non-copyable (owns GPU resources); movable.
    TerrainMaterialSet(const TerrainMaterialSet&) = delete;
    TerrainMaterialSet& operator=(const TerrainMaterialSet&) = delete;
    TerrainMaterialSet(TerrainMaterialSet&& other) noexcept;
    TerrainMaterialSet& operator=(TerrainMaterialSet&& other) noexcept;

    /// @brief Decodes the 12 source maps and builds the three texture arrays.
    ///
    /// All layers (and all three map types) must share one width×height — the array
    /// requirement. On any decode failure or dimension mismatch the method logs once,
    /// leaves the set invalid, and creates no GL textures. Requires a current GL context.
    ///
    /// @param layers The four ground layers (grass / rock / dirt / sand).
    /// @return True iff a valid, fully-uploaded set was built (`isValid()` thereafter).
    bool load(const std::array<TerrainLayerDesc, 4>& layers);

    /// @brief Binds the three arrays to the given texture units (design §4.2: 6/7/8).
    void bind(int albedoUnit, int normalUnit, int materialUnit) const;

    /// @brief False → the renderer must use the flat-colour fallback path.
    bool isValid() const { return m_valid; }

    /// @brief Per-layer world tiling scales (index order = grass / rock / dirt / sand).
    const std::array<float, 4>& tilings() const { return m_tilings; }

    /// @brief GL handle accessors (0 when invalid) — for tests and renderer wiring.
    GLuint albedoArray() const { return m_albedoArray; }
    GLuint normalArray() const { return m_normalArray; }
    GLuint materialArray() const { return m_materialArray; }

private:
    void release();

    GLuint m_albedoArray = 0;
    GLuint m_normalArray = 0;
    GLuint m_materialArray = 0;
    std::array<float, 4> m_tilings{};
    bool m_valid = false;
};

}  // namespace Vestige
