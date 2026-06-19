// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fog_panel.cpp
/// @brief FogPanel — editor UI over the Phase 10 fog pipeline (slice 11.10).

#include "editor/panels/fog_panel.h"

#include "renderer/fog.h"
#include "renderer/renderer.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>

namespace Vestige
{

// --------------------------------------------------------------------------
// Fog-volume working set (pure — no GL / no ImGui, unit-tested headlessly).
// --------------------------------------------------------------------------

int FogPanel::addVolume(const FogVolume& volume)
{
    m_volumes.push_back(volume);
    m_selectedVolume = static_cast<int>(m_volumes.size()) - 1;
    return m_selectedVolume;
}

bool FogPanel::removeVolume(int index)
{
    if (index < 0 || index >= static_cast<int>(m_volumes.size()))
    {
        return false;
    }
    m_volumes.erase(m_volumes.begin() + index);

    // Keep the selection on the same logical row: shift down when a row above
    // it went away, clear when the selected row itself was removed (mirrors
    // AudioPanel::removeReverbZone).
    if (m_selectedVolume == index)
    {
        m_selectedVolume = -1;
    }
    else if (m_selectedVolume > index)
    {
        --m_selectedVolume;
    }
    return true;
}

// --------------------------------------------------------------------------
// Draw
// --------------------------------------------------------------------------

void FogPanel::draw(Renderer* renderer)
{
    if (!m_open)
    {
        // Re-seed the working set the next time the panel opens.
        m_syncedFromRenderer = false;
        return;
    }

    if (!ImGui::Begin("Fog", &m_open))
    {
        ImGui::End();
        return;
    }

    // Null renderer (or headless): show a hint, no GL touched.
    if (renderer == nullptr)
    {
        ImGui::TextUnformatted("No active renderer.");
        ImGui::End();
        return;
    }

    // Adopt the renderer's current volumes once per open, so volumes authored
    // by scene loading (or a prior session) aren't clobbered by the empty
    // working set. After this the panel is the authority while it stays open.
    if (!m_syncedFromRenderer)
    {
        m_volumes = renderer->fogVolumes();
        m_selectedVolume = -1;
        m_syncedFromRenderer = true;
    }

    if (ImGui::BeginTabBar("FogTabs"))
    {
        if (ImGui::BeginTabItem("Distance"))
        {
            drawDistanceTab(*renderer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Height"))
        {
            drawHeightTab(*renderer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Volumetric"))
        {
            drawVolumetricTab(*renderer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug"))
        {
            drawDebugTab(*renderer);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void FogPanel::drawDistanceTab(Renderer& renderer)
{
    // Curve mode combo (labels from the engine's stable fogModeLabel).
    static const FogMode kModes[] = {
        FogMode::None, FogMode::Linear, FogMode::Exponential, FogMode::ExponentialSquared};
    FogMode mode = renderer.getFogMode();
    if (ImGui::BeginCombo("Mode", fogModeLabel(mode)))
    {
        for (FogMode candidate : kModes)
        {
            const bool selected = (candidate == mode);
            if (ImGui::Selectable(fogModeLabel(candidate), selected))
            {
                renderer.setFogMode(candidate);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    FogParams params = renderer.getFogParams();
    bool changed = false;
    changed |= ImGui::ColorEdit3("Colour", &params.colour.x);
    changed |= ImGui::DragFloat("Start (m)", &params.start, 0.5f, 0.0f, 10000.0f, "%.1f");
    changed |= ImGui::DragFloat("End (m)",   &params.end,   0.5f, 0.0f, 10000.0f, "%.1f");
    changed |= ImGui::DragFloat("Density",   &params.density, 0.001f, 0.0f, 1.0f, "%.4f");
    if (changed)
    {
        renderer.setFogParams(params);
    }
}

void FogPanel::drawHeightTab(Renderer& renderer)
{
    bool heightEnabled = renderer.isHeightFogEnabled();
    if (ImGui::Checkbox("Height fog", &heightEnabled))
    {
        renderer.setHeightFogEnabled(heightEnabled);
    }
    if (heightEnabled)
    {
        HeightFogParams h = renderer.getHeightFogParams();
        bool changed = false;
        changed |= ImGui::ColorEdit3("Height colour", &h.colour.x);
        changed |= ImGui::DragFloat("Fog height (m)",   &h.fogHeight,     0.1f, -1000.0f, 1000.0f, "%.2f");
        changed |= ImGui::DragFloat("Ground density",   &h.groundDensity, 0.001f, 0.0f, 1.0f, "%.4f");
        changed |= ImGui::DragFloat("Height falloff",   &h.heightFalloff, 0.01f, 0.0f, 10.0f, "%.3f");
        changed |= ImGui::DragFloat("Max opacity",      &h.maxOpacity,    0.01f, 0.0f, 1.0f, "%.2f");
        if (changed)
        {
            renderer.setHeightFogParams(h);
        }
    }

    ImGui::Separator();

    bool sunEnabled = renderer.isSunInscatterEnabled();
    if (ImGui::Checkbox("Sun inscatter lobe", &sunEnabled))
    {
        renderer.setSunInscatterEnabled(sunEnabled);
    }
    if (sunEnabled)
    {
        SunInscatterParams s = renderer.getSunInscatterParams();
        bool changed = false;
        changed |= ImGui::ColorEdit3("Sun colour", &s.colour.x);
        changed |= ImGui::DragFloat("Lobe exponent",  &s.exponent,      0.1f, 1.0f, 64.0f, "%.1f");
        changed |= ImGui::DragFloat("Start dist (m)", &s.startDistance, 0.1f, 0.0f, 1000.0f, "%.1f");
        if (changed)
        {
            renderer.setSunInscatterParams(s);
        }
    }
}

void FogPanel::drawVolumetricTab(Renderer& renderer)
{
    // The master volumetric/god-ray enables live in Settings → Accessibility;
    // surface the current state read-only so an artist isn't tuning a layer
    // that is switched off without knowing why.
    const PostProcessAccessibilitySettings& acc = renderer.getPostProcessAccessibility();
    ImGui::Text("Volumetric: %s   God rays: %s",
                acc.volumetricFogEnabled ? "on" : "off",
                acc.godRaysEnabled ? "on" : "off");
    ImGui::TextDisabled("(master toggles live in Settings -> Accessibility)");
    ImGui::Separator();

    // --- Froxel medium + density noise ---
    VolumetricFogParams v = renderer.getVolumetricFogParams();
    bool vChanged = false;
    ImGui::TextUnformatted("Medium");
    vChanged |= ImGui::DragFloat3("Scattering (1/m)", &v.scattering.x, 0.001f, 0.0f, 1.0f, "%.4f");
    vChanged |= ImGui::DragFloat("Extinction (1/m)",  &v.extinction,   0.001f, 0.0f, 1.0f, "%.4f");
    vChanged |= ImGui::DragFloat("Anisotropy (g)",    &v.anisotropy,   0.01f, -0.95f, 0.95f, "%.2f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Density noise");
    vChanged |= ImGui::Checkbox("Noise enabled", &v.noise.enabled);
    if (v.noise.enabled)
    {
        vChanged |= ImGui::DragFloat("Frequency (cyc/m)", &v.noise.frequency, 0.001f, 0.0f, 1.0f, "%.3f");
        vChanged |= ImGui::DragFloat("Strength",          &v.noise.strength,  0.01f, 0.0f, 1.0f, "%.2f");
        vChanged |= ImGui::SliderInt("Octaves",           &v.noise.octaves,   1, 5);
        vChanged |= ImGui::DragFloat3("Wind (m/s)",       &v.noise.windVelocity.x, 0.05f, -10.0f, 10.0f, "%.2f");
    }
    if (vChanged)
    {
        renderer.setVolumetricFogParams(v);
    }

    ImGui::Separator();

    // --- God rays ---
    ImGui::TextUnformatted("God rays (screen-space fallback)");
    GodRayParams g = renderer.getGodRayParams();
    bool gChanged = false;
    gChanged |= ImGui::DragFloat("Intensity",   &g.intensity,  0.01f, 0.0f, 4.0f, "%.2f");
    gChanged |= ImGui::DragFloat("Edge margin", &g.edgeMargin, 0.01f, 0.0f, 1.0f, "%.2f");
    if (gChanged)
    {
        renderer.setGodRayParams(g);
    }

    ImGui::Separator();

    // --- Mist / ground-fog volumes (slice 11.11) ---
    bool volumesChanged = false;
    ImGui::Text("Volumes (%d / %d)", static_cast<int>(m_volumes.size()), MAX_FOG_VOLUMES);
    const bool atCap = static_cast<int>(m_volumes.size()) >= MAX_FOG_VOLUMES;

    if (atCap) ImGui::BeginDisabled();
    if (ImGui::Button("Add Box"))
    {
        FogVolume box;
        box.shape = FogVolumeShape::Box;
        addVolume(box);
        volumesChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Sphere"))
    {
        FogVolume sphere;
        sphere.shape = FogVolumeShape::Sphere;
        addVolume(sphere);
        volumesChanged = true;
    }
    if (atCap) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool haveSelection =
        (m_selectedVolume >= 0 && m_selectedVolume < static_cast<int>(m_volumes.size()));
    if (!haveSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Remove"))
    {
        volumesChanged = removeVolume(m_selectedVolume);
    }
    if (!haveSelection) ImGui::EndDisabled();

    // Volume list.
    if (ImGui::BeginListBox("##fogvolumes", ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing())))
    {
        for (int i = 0; i < static_cast<int>(m_volumes.size()); ++i)
        {
            const char* shapeName =
                (m_volumes[static_cast<size_t>(i)].shape == FogVolumeShape::Sphere) ? "Sphere" : "Box";
            char label[32];
            std::snprintf(label, sizeof(label), "%s %d", shapeName, i);
            if (ImGui::Selectable(label, i == m_selectedVolume))
            {
                m_selectedVolume = i;
            }
        }
        ImGui::EndListBox();
    }

    // Selected-volume editor.
    if (m_selectedVolume >= 0 && m_selectedVolume < static_cast<int>(m_volumes.size()))
    {
        FogVolume& vol = m_volumes[static_cast<size_t>(m_selectedVolume)];
        int shapeIdx = (vol.shape == FogVolumeShape::Sphere) ? 1 : 0;
        if (ImGui::Combo("Shape", &shapeIdx, "Box\0Sphere\0"))
        {
            vol.shape = (shapeIdx == 1) ? FogVolumeShape::Sphere : FogVolumeShape::Box;
            volumesChanged = true;
        }
        volumesChanged |= ImGui::DragFloat3("Center", &vol.center.x, 0.1f, -10000.0f, 10000.0f, "%.2f");
        const char* extentLabel = (vol.shape == FogVolumeShape::Sphere) ? "Radius (.x)" : "Half extents";
        volumesChanged |= ImGui::DragFloat3(extentLabel, &vol.halfExtents.x, 0.05f, 0.0f, 1000.0f, "%.2f");
        volumesChanged |= ImGui::ColorEdit3("Tint", &vol.colour.x);
        volumesChanged |= ImGui::DragFloat("Density (1/m)", &vol.density, 0.01f, 0.0f, 10.0f, "%.3f");
        volumesChanged |= ImGui::DragFloat("Edge softness", &vol.edgeSoftness, 0.01f, 0.0f, 1.0f, "%.2f");
        volumesChanged |= ImGui::DragFloat("Anim speed",    &vol.animSpeed, 0.01f, 0.0f, 10.0f, "%.2f");
    }

    // Push only when something actually changed (the seed-on-open already left
    // the renderer holding this exact set), so we don't copy the vector every
    // frame the tab is merely open.
    if (volumesChanged)
    {
        renderer.setFogVolumes(m_volumes);
    }
}

void FogPanel::drawDebugTab(Renderer& renderer)
{
    const FroxelGridConfig& cfg = renderer.getVolumetricFogConfig();
    ImGui::Text("Froxel grid: %d x %d x %d", cfg.resX, cfg.resY, cfg.resZ);
    ImGui::Text("Froxel range: %.1f - %.1f m", static_cast<double>(cfg.near),
                static_cast<double>(cfg.far));
    ImGui::Separator();

    const int count = static_cast<int>(m_volumes.size());
    ImGui::Text("Active volumes: %d / %d", count, MAX_FOG_VOLUMES);
    if (count > MAX_FOG_VOLUMES)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "Over cap - %d volume(s) will be dropped.", count - MAX_FOG_VOLUMES);
    }
}

} // namespace Vestige
