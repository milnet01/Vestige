// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fog_panel.h
/// @brief Phase 10 slice 11.10 — editor panel for per-scene fog tuning.
#pragma once

#include "editor/panels/i_panel.h"
#include "renderer/volumetric_fog.h"   // FogVolume / FogVolumeShape

#include <vector>

namespace Vestige
{

class Renderer;

/// @brief Editor panel for authoring all fog layers on the active renderer.
///
/// Hosts four tabs (mirroring the AudioPanel four-tab shape):
///   - **Distance**   — `FogMode` + distance-fog colour/start/end/density.
///   - **Height**     — exponential height fog + the sun-inscatter lobe.
///   - **Volumetric** — froxel medium (scattering/extinction/anisotropy),
///                      density noise, god-ray gain + edge margin, and the
///                      placeable mist / ground-fog volume list (slice 11.11).
///   - **Debug**      — froxel grid dims, volume count vs `MAX_FOG_VOLUMES`,
///                      and the volumetric-active status.
///
/// The renderer is the single source of truth for fog state; the panel reads
/// the current value into each widget and writes it back on edit (the
/// `NavigationPanel` / `AudioPanel` precedent). The only state the panel owns
/// is the working set of fog volumes it edits before pushing them to the
/// renderer. That volume-list management is pure (no GL / no ImGui) so it is
/// unit-testable headlessly — see `tests/test_fog_panel.cpp`.
class FogPanel : public IPanel
{
public:
    const char* displayName() const override { return "Fog"; }

    /// @brief Draws the panel inside its own ImGui window.
    /// @param renderer Live renderer to author. Null → early-return (the
    ///        headless tests never touch GL through this path).
    void draw(Renderer* renderer);

    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }
    void toggleOpen() { m_open = !m_open; }

    // -- Fog-volume working set (pure, GL-free — unit-testable) -----------

    /// @brief Appends @p volume and selects it. @return its index.
    int addVolume(const FogVolume& volume);

    /// @brief Removes the volume at @p index (range-checked; no-op if out of
    ///        range). Selection shifts down to stay on the same logical row,
    ///        and clears when the selected row itself is removed — mirroring
    ///        `AudioPanel::removeReverbZone`. @return true if a volume was removed.
    bool removeVolume(int index);

    /// @brief Selects the volume at @p index (no range check — −1 clears).
    void selectVolume(int index) { m_selectedVolume = index; }

    const std::vector<FogVolume>& volumes() const { return m_volumes; }
    int selectedVolume() const { return m_selectedVolume; }

private:
    void drawDistanceTab(Renderer& renderer);
    void drawHeightTab(Renderer& renderer);
    void drawVolumetricTab(Renderer& renderer);
    void drawDebugTab(Renderer& renderer);

    bool m_open = false;

    // Working set of mist / ground-fog volumes (slice 11.11). The Volumetric
    // tab edits these and pushes them to the renderer via setFogVolumes().
    std::vector<FogVolume> m_volumes;
    int m_selectedVolume = -1;

    // One-shot latch: on the first draw after the panel opens, the working set
    // is seeded from the renderer's current volumes (so scene-loaded volumes
    // are adopted, not clobbered). Reset each frame the panel is closed.
    bool m_syncedFromRenderer = false;
};

} // namespace Vestige
