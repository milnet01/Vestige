/// @file entity_actions.cpp
/// @brief EntityActions implementation — duplicate, delete, align, distribute, clipboard.
#include "editor/entity_actions.h"
#include "editor/command_history.h"
#include "editor/commands/align_distribute_command.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Vestige
{

namespace EntityActions
{

std::string generateDuplicateName(const std::string& originalName, const Entity* parent)
{
    // Extract base name: strip trailing " (N)" suffix if present
    std::string baseName = originalName;
    if (originalName.size() > 3 && originalName.back() == ')')
    {
        auto parenOpen = originalName.rfind(" (");
        if (parenOpen != std::string::npos)
        {
            std::string numStr = originalName.substr(
                parenOpen + 2, originalName.size() - parenOpen - 3);
            bool allDigits = !numStr.empty();
            for (char c : numStr)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                {
                    allDigits = false;
                }
            }
            if (allDigits)
            {
                baseName = originalName.substr(0, parenOpen);
            }
        }
    }

    // Find the highest existing suffix number among siblings
    int maxSuffix = 0;
    if (parent)
    {
        for (const auto& child : parent->getChildren())
        {
            const std::string& cn = child->getName();
            if (cn == baseName)
            {
                // Base name without suffix counts as 0
                maxSuffix = std::max(maxSuffix, 0);
                continue;
            }

            // Check if cn matches "baseName (N)"
            if (cn.size() > baseName.size() + 3
                && cn.compare(0, baseName.size(), baseName) == 0
                && cn[baseName.size()] == ' '
                && cn[baseName.size() + 1] == '('
                && cn.back() == ')')
            {
                std::string numStr = cn.substr(
                    baseName.size() + 2, cn.size() - baseName.size() - 3);
                bool allDigits = !numStr.empty();
                for (char c : numStr)
                {
                    if (!std::isdigit(static_cast<unsigned char>(c)))
                    {
                        allDigits = false;
                    }
                }
                if (allDigits)
                {
                    maxSuffix = std::max(maxSuffix, std::stoi(numStr));
                }
            }
        }
    }

    return baseName + " (" + std::to_string(maxSuffix + 1) + ")";
}

Entity* duplicateEntity(Scene& scene, Selection& selection, uint32_t entityId)
{
    Entity* clone = scene.duplicateEntity(entityId);
    if (!clone)
    {
        return nullptr;
    }

    // Apply Unity-style name based on siblings
    clone->setName(generateDuplicateName(clone->getName(), clone->getParent()));

    // Offset +0.5m on X so it doesn't overlap the original exactly
    clone->transform.position.x += 0.5f;

    // Auto-select the clone
    selection.select(clone->getId());

    Logger::info("Duplicated: " + clone->getName());
    return clone;
}

void deleteSelectedEntities(Scene& scene, Selection& selection)
{
    if (!selection.hasSelection())
    {
        return;
    }

    // Copy IDs (selection will be modified during deletion)
    std::vector<uint32_t> ids = selection.getSelectedIds();
    selection.clearSelection();

    int deleted = 0;
    for (uint32_t id : ids)
    {
        if (scene.removeEntity(id))
        {
            ++deleted;
        }
    }

    if (deleted > 0)
    {
        Logger::info("Deleted " + std::to_string(deleted) + " entity(s)");
    }
}

void copyTransform(Scene& scene, uint32_t entityId, TransformClipboard& clipboard)
{
    Entity* entity = scene.findEntityById(entityId);
    if (!entity)
    {
        return;
    }

    clipboard.position = entity->transform.position;
    clipboard.rotation = entity->transform.rotation;
    clipboard.scale = entity->transform.scale;
    clipboard.hasData = true;

    Logger::info("Copied transform from '" + entity->getName() + "'");
}

void pasteTransform(Scene& scene, uint32_t entityId, const TransformClipboard& clipboard)
{
    if (!clipboard.hasData)
    {
        return;
    }

    Entity* entity = scene.findEntityById(entityId);
    if (!entity)
    {
        return;
    }

    entity->transform.position = clipboard.position;
    entity->transform.rotation = clipboard.rotation;
    entity->transform.scale = clipboard.scale;

    // Clear matrix override if present (user is setting explicit TRS values)
    if (entity->transform.hasMatrixOverride())
    {
        entity->transform.clearMatrixOverride();
    }

    Logger::info("Pasted transform to '" + entity->getName() + "'");
}

Entity* groupEntities(Scene& scene, Selection& selection)
{
    if (!selection.hasSelection())
    {
        return nullptr;
    }

    auto ids = selection.getSelectedIds();
    if (ids.size() < 2)
    {
        return nullptr;
    }

    // Compute centroid of selected entities' world positions
    glm::vec3 centroid(0.0f);
    int count = 0;
    std::vector<std::pair<uint32_t, glm::vec3>> entityPositions;

    for (uint32_t id : ids)
    {
        Entity* e = scene.findEntityById(id);
        if (e && e != scene.getRoot())
        {
            glm::vec3 wp = e->getWorldPosition();
            centroid += wp;
            entityPositions.emplace_back(id, wp);
            ++count;
        }
    }

    if (count < 2)
    {
        return nullptr;
    }

    centroid /= static_cast<float>(count);

    // Create group entity at root with position = centroid
    Entity* group = scene.createEntity("Group");
    group->transform.position = centroid;

    // Reparent all selected entities under the group
    for (const auto& [id, worldPos] : entityPositions)
    {
        scene.reparentEntity(id, group->getId());

        // Adjust local position to preserve world position
        // Group has no rotation/scale, so: new_local_pos = old_world_pos - centroid
        Entity* e = scene.findEntityById(id);
        if (e)
        {
            e->transform.position = worldPos - centroid;
        }
    }

    selection.select(group->getId());
    Logger::info("Grouped " + std::to_string(count) + " entities");
    return group;
}

namespace
{

float getAxisValue(const glm::vec3& v, AlignAxis axis)
{
    switch (axis)
    {
        case AlignAxis::X: return v.x;
        case AlignAxis::Y: return v.y;
        case AlignAxis::Z: return v.z;
    }
    return 0.0f;
}

void setAxisValue(glm::vec3& v, AlignAxis axis, float value)
{
    switch (axis)
    {
        case AlignAxis::X: v.x = value; break;
        case AlignAxis::Y: v.y = value; break;
        case AlignAxis::Z: v.z = value; break;
    }
}

const char* axisName(AlignAxis axis)
{
    switch (axis)
    {
        case AlignAxis::X: return "X";
        case AlignAxis::Y: return "Y";
        case AlignAxis::Z: return "Z";
    }
    return "?";
}

const char* anchorName(AlignAnchor anchor)
{
    switch (anchor)
    {
        case AlignAnchor::MIN: return "Min";
        case AlignAnchor::CENTER: return "Center";
        case AlignAnchor::MAX: return "Max";
    }
    return "?";
}

} // anonymous namespace

void alignEntities(Scene& scene, const Selection& selection,
                   CommandHistory& history, AlignAxis axis, AlignAnchor anchor)
{
    auto ids = selection.getSelectedIds();
    if (ids.size() < 2) return;

    // Gather positions
    struct EntityInfo
    {
        uint32_t id;
        glm::vec3 position;
    };
    std::vector<EntityInfo> infos;
    for (uint32_t id : ids)
    {
        Entity* e = scene.findEntityById(id);
        if (e) infos.push_back({id, e->transform.position});
    }
    if (infos.size() < 2) return;

    // Compute the target value based on anchor
    float targetValue = 0.0f;
    switch (anchor)
    {
        case AlignAnchor::MIN:
        {
            targetValue = getAxisValue(infos[0].position, axis);
            for (const auto& info : infos)
            {
                targetValue = std::min(targetValue, getAxisValue(info.position, axis));
            }
            break;
        }
        case AlignAnchor::MAX:
        {
            targetValue = getAxisValue(infos[0].position, axis);
            for (const auto& info : infos)
            {
                targetValue = std::max(targetValue, getAxisValue(info.position, axis));
            }
            break;
        }
        case AlignAnchor::CENTER:
        {
            float sum = 0.0f;
            for (const auto& info : infos)
            {
                sum += getAxisValue(info.position, axis);
            }
            targetValue = sum / static_cast<float>(infos.size());
            break;
        }
    }

    // Build command entries
    std::vector<AlignDistributeCommand::Entry> entries;
    for (const auto& info : infos)
    {
        glm::vec3 newPos = info.position;
        setAxisValue(newPos, axis, targetValue);
        if (newPos != info.position)
        {
            entries.push_back({info.id, info.position, newPos});
        }
    }

    if (entries.empty()) return;

    std::string desc = std::string("Align ") + anchorName(anchor) + " " + axisName(axis);
    history.execute(std::make_unique<AlignDistributeCommand>(scene, desc, std::move(entries)));
    Logger::info(desc + " (" + std::to_string(entries.size()) + " entities)");
}

void distributeEntities(Scene& scene, const Selection& selection,
                        CommandHistory& history, AlignAxis axis)
{
    auto ids = selection.getSelectedIds();
    if (ids.size() < 3) return;

    // Gather positions
    struct EntityInfo
    {
        uint32_t id;
        glm::vec3 position;
        float axisVal;
    };
    std::vector<EntityInfo> infos;
    for (uint32_t id : ids)
    {
        Entity* e = scene.findEntityById(id);
        if (e)
        {
            float val = getAxisValue(e->transform.position, axis);
            infos.push_back({id, e->transform.position, val});
        }
    }
    if (infos.size() < 3) return;

    // Sort by axis value
    std::sort(infos.begin(), infos.end(),
              [](const EntityInfo& a, const EntityInfo& b) { return a.axisVal < b.axisVal; });

    // Compute even spacing between first and last
    float minVal = infos.front().axisVal;
    float maxVal = infos.back().axisVal;
    float spacing = (maxVal - minVal) / static_cast<float>(infos.size() - 1);

    // Build command entries
    std::vector<AlignDistributeCommand::Entry> entries;
    for (size_t i = 1; i + 1 < infos.size(); ++i)
    {
        float newVal = minVal + spacing * static_cast<float>(i);
        glm::vec3 newPos = infos[i].position;
        setAxisValue(newPos, axis, newVal);
        if (newPos != infos[i].position)
        {
            entries.push_back({infos[i].id, infos[i].position, newPos});
        }
    }

    if (entries.empty()) return;

    std::string desc = std::string("Distribute ") + axisName(axis);
    history.execute(std::make_unique<AlignDistributeCommand>(scene, desc, std::move(entries)));
    Logger::info(desc + " (" + std::to_string(entries.size()) + " entities)");
}

} // namespace EntityActions

} // namespace Vestige
