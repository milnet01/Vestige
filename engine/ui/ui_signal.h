// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_signal.h
/// @brief Lightweight signal/slot template for UI callbacks.
#pragma once

#include <functional>
#include <vector>

namespace Vestige
{

/// @brief Simple signal/slot mechanism for UI event callbacks.
///
/// Usage:
/// @code
///   Signal<int, float> onValueChanged;
///   onValueChanged.connect([](int id, float val) { ... });
///   onValueChanged.emit(42, 3.14f);
/// @endcode
template <typename... Args>
class Signal
{
public:
    using Slot = std::function<void(Args...)>;

    /// @brief Connects a callback slot to this signal.
    /// @param slot The callback to invoke when emit() is called.
    void connect(Slot slot)
    {
        m_slots.push_back(std::move(slot));
    }

    /// @brief Emits the signal, invoking all connected slots.
    /// @param args Arguments forwarded to each slot.
    void emit(Args... args) const
    {
        // Iterate a COPY. A slot may destroy the object that owns this signal:
        // every main-menu button's onClick calls UISystem::applyIntent, which
        // calls setRootScreen / pushModalScreen / popModalScreen, each of which
        // clears the UICanvas and frees the UIElement holding this Signal. With
        // a range-for over m_slots the loop then increments and compares
        // iterators into freed heap, and the std::function was destroyed
        // mid-call. Fires on the first button a player presses, and on the
        // keyboard path too.
        const std::vector<Slot> slots = m_slots;
        for (const auto& slot : slots)
        {
            slot(args...);
        }
    }

    /// @brief Disconnects all slots.
    void disconnectAll()
    {
        m_slots.clear();
    }

    /// @brief Returns the number of connected slots.
    size_t getSlotCount() const { return m_slots.size(); }

private:
    std::vector<Slot> m_slots;
};

} // namespace Vestige
