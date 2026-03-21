/// @file paint_scatter_command.h
/// @brief Undo command for scatter object paint/erase strokes.
#pragma once

#include "editor/commands/editor_command.h"
#include "environment/foliage_manager.h"

#include <vector>

namespace Vestige
{

/// @brief Identifies a scatter instance within the chunk grid (for undo support).
struct ScatterInstanceRef
{
    uint64_t chunkKey;
    ScatterInstance instance;
};

/// @brief Undo command for scatter paint/erase strokes.
class PaintScatterCommand : public EditorCommand
{
public:
    PaintScatterCommand(FoliageManager& manager,
                        std::vector<ScatterInstanceRef> added,
                        std::vector<ScatterInstanceRef> removed)
        : m_manager(manager)
        , m_added(std::move(added))
        , m_removed(std::move(removed))
    {
    }

    void execute() override
    {
        // Redo: re-add added, re-remove removed
        for (const auto& ref : m_added)
        {
            int gx, gz;
            FoliageManager::unpackChunkKey(ref.chunkKey, gx, gz);
            m_manager.addScatterDirect(gx, gz, ref.instance);
        }
        for (const auto& ref : m_removed)
        {
            m_manager.removeScatterAt(ref.chunkKey, ref.instance.position);
        }
    }

    void undo() override
    {
        // Remove what was added, restore what was removed
        for (const auto& ref : m_added)
        {
            m_manager.removeScatterAt(ref.chunkKey, ref.instance.position);
        }
        for (const auto& ref : m_removed)
        {
            int gx, gz;
            FoliageManager::unpackChunkKey(ref.chunkKey, gx, gz);
            m_manager.addScatterDirect(gx, gz, ref.instance);
        }
    }

    std::string getDescription() const override
    {
        if (!m_added.empty())
        {
            return "Paint Scatter (" + std::to_string(m_added.size()) + ")";
        }
        return "Erase Scatter (" + std::to_string(m_removed.size()) + ")";
    }

private:
    FoliageManager& m_manager;
    std::vector<ScatterInstanceRef> m_added;
    std::vector<ScatterInstanceRef> m_removed;
};

} // namespace Vestige
