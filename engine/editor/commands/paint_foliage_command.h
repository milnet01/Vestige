/// @file paint_foliage_command.h
/// @brief Undo command for foliage paint/erase strokes.
#pragma once

#include "editor/commands/editor_command.h"
#include "environment/foliage_manager.h"

#include <vector>

namespace Vestige
{

/// @brief Undo command that records added and removed foliage instances from a brush stroke.
///
/// On undo: removes added instances, restores removed instances.
/// On redo (execute): restores added instances, removes restored instances.
class PaintFoliageCommand : public EditorCommand
{
public:
    /// @brief Creates a paint foliage command.
    /// @param manager Reference to the foliage manager.
    /// @param added Instances added during the stroke.
    /// @param removed Instances removed during the stroke (eraser).
    PaintFoliageCommand(FoliageManager& manager,
                        std::vector<FoliageInstanceRef> added,
                        std::vector<FoliageInstanceRef> removed)
        : m_manager(manager)
        , m_added(std::move(added))
        , m_removed(std::move(removed))
    {
    }

    void execute() override
    {
        // Already applied during painting — this is called for redo
        m_manager.restoreFoliage(m_added);
        m_manager.removeFoliage(m_removed);
    }

    void undo() override
    {
        // Remove what was added, restore what was removed
        m_manager.removeFoliage(m_added);
        m_manager.restoreFoliage(m_removed);
    }

    std::string getDescription() const override
    {
        if (!m_added.empty() && !m_removed.empty())
        {
            return "Paint/Erase Foliage";
        }
        else if (!m_added.empty())
        {
            return "Paint Foliage (" + std::to_string(m_added.size()) + ")";
        }
        else
        {
            return "Erase Foliage (" + std::to_string(m_removed.size()) + ")";
        }
    }

private:
    FoliageManager& m_manager;
    std::vector<FoliageInstanceRef> m_added;
    std::vector<FoliageInstanceRef> m_removed;
};

} // namespace Vestige
