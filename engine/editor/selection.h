// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file selection.h
/// @brief Tracks which entities are selected in the editor.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige
{

class Entity;
class Scene;

/// @brief Manages the set of selected entities in the editor.
class Selection
{
public:
    /// @brief Replaces the selection with a single entity.
    /// @param id Entity ID (0 clears the selection).
    void select(uint32_t id);

    /// @brief Adds an entity to the selection (Shift+click).
    void addToSelection(uint32_t id);

    /// @brief Toggles an entity in/out of the selection (Ctrl+click).
    void toggleSelection(uint32_t id);

    /// @brief Clears the entire selection.
    void clearSelection();

    /// @brief Checks if an entity is selected.
    bool isSelected(uint32_t id) const;

    /// @brief Returns true if anything is selected.
    bool hasSelection() const;

    /// @brief Gets the list of selected entity IDs.
    const std::vector<uint32_t>& getSelectedIds() const;

    /// @brief Gets the primary (most recently selected) entity ID.
    /// Returns 0 if nothing is selected.
    uint32_t getPrimaryId() const;

    /// @brief Resolves the primary selected ID to an Entity pointer.
    /// @param scene The scene to search in.
    /// @return Pointer to the entity, or nullptr.
    Entity* getPrimaryEntity(Scene& scene) const;

private:
    std::vector<uint32_t> m_selectedIds;
};

} // namespace Vestige
