// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_panel.h
/// @brief SpritePanel — editor UI for inspecting sprite atlases and
/// attaching them to entities (Phase 9F-6).
///
/// Scope is deliberately focused: load a TexturePacker JSON, list its
/// frames, and let the user assign the loaded atlas to the currently
/// selected entity's SpriteComponent. Full slicing / animation-clip
/// authoring is Phase 18 polish.
#pragma once

#include <memory>
#include <string>

namespace Vestige
{

class Scene;
class Selection;
class SpriteAtlas;

class SpritePanel
{
public:
    SpritePanel() = default;

    /// @brief Toggles the panel's visibility.
    void toggleVisible() { m_visible = !m_visible; }
    bool isVisible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }

    /// @brief Draws the panel. Call inside the ImGui frame.
    /// Null @p scene / @p selection are safe — the panel renders as
    /// "no scene" / "no selection" messaging.
    void draw(Scene* scene, Selection* selection);

    /// @brief Returns the atlas currently loaded in the panel, if any.
    std::shared_ptr<SpriteAtlas> getLoadedAtlas() const { return m_atlas; }

    /// @brief Programmatic load path — bypasses the file dialog so
    /// tests + scripted flows can reach the same post-load state.
    bool loadAtlasFromPath(const std::string& jsonPath);

private:
    bool m_visible = false;
    std::string m_lastLoadedPath;
    std::string m_lastLoadError;
    std::string m_selectedFrame;
    std::shared_ptr<SpriteAtlas> m_atlas;
};

} // namespace Vestige
