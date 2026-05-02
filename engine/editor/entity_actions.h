// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file entity_actions.h
/// @brief Standalone editor actions for entity manipulation — duplicate, delete, align, distribute.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Vestige
{

class CommandHistory;
class EditorCommand;
class Entity;
class Scene;
class Selection;

/// @brief Editor-level entity operations (duplicate, delete, transform clipboard).
namespace EntityActions
{

/// @brief Duplicates the entity and all its descendants.
/// The clone gets a Unity-style incremented name, +0.5m X offset, and is auto-selected.
/// @param scene The active scene.
/// @param selection Editor selection (clone is auto-selected).
/// @param entityId ID of the entity to duplicate.
/// @return Pointer to the clone, or nullptr on failure.
Entity* duplicateEntity(Scene& scene, Selection& selection, uint32_t entityId);

/// @brief Deletes all currently selected entities and clears the selection.
/// Prevents deleting the scene root.
/// @param scene The active scene.
/// @param selection Editor selection (cleared after delete).
void deleteSelectedEntities(Scene& scene, Selection& selection);

/// @brief Filter @a ids down to the entities that have no selected ancestor.
///
/// Phase 10.9 Slice 12 Ed3 — when the user multi-selects a parent and one
/// of its descendants and presses Delete, the descendant's `DeleteEntityCommand`
/// would run *after* its parent's removal recursively wiped it; the second
/// command then operates on a freed entity (ID lookup fails silently, undo
/// can't restore the original parent-child topology). Drop descendants from
/// the operation list before composing the command.
///
/// Returned ids preserve their relative order in @a ids, so the rebuild on
/// undo runs in a stable order.
std::vector<uint32_t> filterToRootEntities(Scene& scene,
                                           const std::vector<uint32_t>& ids);

/// @brief Build the delete-command for a multi-selection. Filters to roots
///        per `filterToRootEntities`; returns a single `DeleteEntityCommand`
///        when one root remains, a `CompositeCommand` when many remain, or
///        nullptr when the filtered list is empty (e.g. only the scene root
///        was selected). Caller hands the result to `CommandHistory::execute`.
std::unique_ptr<EditorCommand> buildDeleteCommand(
    Scene& scene, const std::vector<uint32_t>& ids);

/// @brief Generates a Unity-style duplicate name by scanning siblings.
/// "Cube" becomes "Cube (1)", "Cube (1)" becomes "Cube (2)", etc.
/// @param originalName The name to base the duplicate on.
/// @param parent The parent entity whose children are scanned for conflicts.
/// @return The generated name.
std::string generateDuplicateName(const std::string& originalName, const Entity* parent);

/// @brief Stored transform for copy/paste operations.
struct TransformClipboard
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    bool hasData = false;
};

/// @brief Copies an entity's local transform into the clipboard.
/// @param scene The active scene.
/// @param entityId Entity to copy from.
/// @param clipboard Output clipboard.
void copyTransform(Scene& scene, uint32_t entityId, TransformClipboard& clipboard);

/// @brief Pastes a clipboard transform onto an entity.
/// @param scene The active scene.
/// @param entityId Entity to paste onto.
/// @param clipboard The stored transform data.
void pasteTransform(Scene& scene, uint32_t entityId, const TransformClipboard& clipboard);

/// @brief Groups all selected entities under a new empty parent "Group".
/// The group's position is set to the centroid of the selected entities.
/// Each entity's local position is adjusted to preserve its world position.
/// @param scene The active scene.
/// @param selection Editor selection (group entity is auto-selected).
/// @return Pointer to the group entity, or nullptr if fewer than 2 entities selected.
Entity* groupEntities(Scene& scene, Selection& selection);

/// @brief Axis for align/distribute operations.
enum class AlignAxis
{
    X,   ///< Left/right.
    Y,   ///< Top/bottom.
    Z    ///< Front/back.
};

/// @brief Anchor point for alignment operations.
enum class AlignAnchor
{
    MIN,     ///< Align to the minimum (left/bottom/front).
    CENTER,  ///< Align to the center.
    MAX      ///< Align to the maximum (right/top/back).
};

/// @brief Aligns all selected entities along the given axis and anchor.
/// Uses world-space positions. Creates an undoable command.
/// @param scene The active scene.
/// @param selection Current selection (must have 2+ entities).
/// @param history Command history for undo support.
/// @param axis Axis to align along.
/// @param anchor Anchor point (MIN, CENTER, MAX).
void alignEntities(Scene& scene, const Selection& selection,
                   CommandHistory& history, AlignAxis axis, AlignAnchor anchor);

/// @brief Distributes all selected entities evenly along the given axis.
/// Spaces entities equally between the outermost entities' positions.
/// @param scene The active scene.
/// @param selection Current selection (must have 3+ entities).
/// @param history Command history for undo support.
/// @param axis Axis to distribute along.
void distributeEntities(Scene& scene, const Selection& selection,
                        CommandHistory& history, AlignAxis axis);

} // namespace EntityActions

} // namespace Vestige
