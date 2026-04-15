// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file place_tree_command.h
/// @brief Undo command for tree placement and removal.
#pragma once

#include "editor/commands/editor_command.h"
#include "environment/foliage_manager.h"

#include <vector>

namespace Vestige
{

/// @brief Identifies a tree instance for undo support.
struct TreeInstanceRef
{
    uint64_t chunkKey;
    TreeInstance instance;
};

/// @brief Undo command for tree placement/removal.
class PlaceTreeCommand : public EditorCommand
{
public:
    PlaceTreeCommand(FoliageManager& manager,
                     std::vector<TreeInstanceRef> added,
                     std::vector<TreeInstanceRef> removed)
        : m_manager(manager)
        , m_added(std::move(added))
        , m_removed(std::move(removed))
    {
    }

    void execute() override
    {
        for (const auto& ref : m_added)
        {
            int gx, gz;
            FoliageManager::unpackChunkKey(ref.chunkKey, gx, gz);
            m_manager.addTreeDirect(gx, gz, ref.instance);
        }
        for (const auto& ref : m_removed)
        {
            m_manager.removeTreeAt(ref.chunkKey, ref.instance.position);
        }
    }

    void undo() override
    {
        for (const auto& ref : m_added)
        {
            m_manager.removeTreeAt(ref.chunkKey, ref.instance.position);
        }
        for (const auto& ref : m_removed)
        {
            int gx, gz;
            FoliageManager::unpackChunkKey(ref.chunkKey, gx, gz);
            m_manager.addTreeDirect(gx, gz, ref.instance);
        }
    }

    std::string getDescription() const override
    {
        if (!m_added.empty())
        {
            return "Place Tree (" + std::to_string(m_added.size()) + ")";
        }
        return "Remove Tree (" + std::to_string(m_removed.size()) + ")";
    }

private:
    FoliageManager& m_manager;
    std::vector<TreeInstanceRef> m_added;
    std::vector<TreeInstanceRef> m_removed;
};

} // namespace Vestige
