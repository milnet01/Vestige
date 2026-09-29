// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file edit_tracker.h
/// @brief Undo bracketing for blocks of ImGui widgets in the inspector.
#pragma once

#include <imgui.h>

#include <cstdint>
#include <optional>

namespace Vestige
{

/// @brief Phase 10.9 Slice 12 Ed1 — per-widget activation tracker.
///
/// Pre-Ed1 the inspector blocks used
///     `if (changed && ImGui::IsItemDeactivatedAfterEdit())`
/// at the *end* of a multi-widget block. `IsItemDeactivatedAfterEdit()`
/// only queries the most recently submitted item, so drag-release
/// events on every widget except the last were silently dropped from
/// the undo history — releasing a drag on "Rate" recorded nothing,
/// only the final widget's release fired the push.
///
/// This tracker is called immediately after each widget so per-widget
/// activation/deactivation states are aggregated correctly across the
/// whole block. `shouldCommit()` then triggers undo on either:
///   - drag end (anyDeactivated → push at end-of-drag, one entry per drag), or
///   - a change while no tracked widget is active (a combo pick, or a value
///     set by code). A change while a widget is held is part of a gesture
///     and is recorded when it ends; before 3D_E-0722 every frame of a drag
///     pushed its own entry, so one drag took one undo per frame.
///
/// Ed2 extends the same tracker to the water / cloth / rigid-body /
/// emissive-light / material inspectors.
struct EditTracker
{
    bool anyActivated   = false;
    bool anyActive      = false;  ///< A widget is held (mid-drag / pressed).
    bool anyDeactivated = false;
    bool anyEnded       = false;  ///< A widget deactivated, edited or not.
    bool changed        = false;

    /// Track the just-submitted ImGui item.
    void track(bool widgetChanged)
    {
        changed |= widgetChanged;
        if (ImGui::IsItemActivated())             anyActivated   = true;
        if (ImGui::IsItemActive())                anyActive      = true;
        if (ImGui::IsItemDeactivatedAfterEdit())  anyDeactivated = true;
        if (ImGui::IsItemDeactivated())           anyEnded       = true;
    }

    /// True when the block should record a single undo entry this frame.
    bool shouldCommit() const
    {
        return anyDeactivated || (changed && !anyActivated && !anyActive);
    }
};

/// @brief Remembers a block's value from the frame a drag started.
///
/// A drag edits the value live every frame and is recorded once, when it
/// ends. By then the snapshot a block takes at the top of the frame already
/// holds the dragged value, so the undo "before" has to be the snapshot from
/// the frame the drag started (3D_E-0722). Only one ImGui
/// item is active at a time, so one instance per inspector section is enough;
/// @a ownerId resets it if the selection changes mid-drag.
template <typename T>
class DragUndo
{
public:
    /// @brief Call once per block per frame, after its widgets.
    /// @param tracker    The block's tracker for this frame.
    /// @param frameStart The block's value before its widgets ran this frame.
    /// @param ownerId    The entity being edited.
    /// @return The value to record as the undo "before" when the block should
    ///         push an entry this frame; empty otherwise.
    std::optional<T> commitBefore(const EditTracker& tracker, const T& frameStart,
                                  std::uint32_t ownerId)
    {
        if (ownerId != m_ownerId)
        {
            m_origin.reset();
            m_ownerId = ownerId;
        }
        if (tracker.anyActivated && !m_origin)
        {
            m_origin = frameStart;
        }
        if (tracker.shouldCommit())
        {
            T before = m_origin.value_or(frameStart);
            m_origin.reset();
            return before;
        }
        if (tracker.anyEnded)
        {
            m_origin.reset();  // a click that edited nothing
        }
        return std::nullopt;
    }

private:
    std::optional<T> m_origin;
    std::uint32_t m_ownerId = 0;
};

}  // namespace Vestige
