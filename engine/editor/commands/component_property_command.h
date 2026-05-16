// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file component_property_command.h
/// @brief Generic undoable component-property change.
#pragma once

#include "editor/commands/editor_command.h"

#include <functional>
#include <string>
#include <utility>

namespace Vestige
{

/// @brief Records a before/after snapshot of any copyable component state
/// and applies it through a caller-supplied apply function.
///
/// Phase 10.9 Slice 12 Ed2 — used by water / cloth / rigid-body /
/// emissive-light / material inspector blocks (Material clones into
/// itself via copy-assignment; Cloth applies through simulator setters).
/// Generic so the five inspectors don't each need a near-identical
/// command class.
///
/// `Snapshot` is the component-state value type (e.g. `WaterSurfaceConfig`,
/// `EmissiveLightSnapshot`, `Material`). The apply function rebuilds the
/// component from the snapshot and is the only place that needs to know
/// the per-component getters / setters.
template <typename Snapshot>
class ComponentPropertyCommand : public EditorCommand
{
public:
    using ApplyFn = std::function<void(const Snapshot&)>;

    ComponentPropertyCommand(Snapshot oldState,
                             Snapshot newState,
                             std::string description,
                             ApplyFn apply)
        : m_oldState(std::move(oldState))
        , m_newState(std::move(newState))
        , m_description(std::move(description))
        , m_apply(std::move(apply))
    {
    }

    void execute() override
    {
        if (m_apply)
        {
            m_apply(m_newState);
        }
    }

    void undo() override
    {
        if (m_apply)
        {
            m_apply(m_oldState);
        }
    }

    std::string getDescription() const override
    {
        return m_description;
    }

private:
    Snapshot m_oldState;
    Snapshot m_newState;
    std::string m_description;
    ApplyFn m_apply;
};

} // namespace Vestige
