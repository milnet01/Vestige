/// @file entity_actions.h
/// @brief Standalone editor actions for entity manipulation — duplicate, delete, transform clipboard.
#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Vestige
{

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

} // namespace EntityActions

} // namespace Vestige
