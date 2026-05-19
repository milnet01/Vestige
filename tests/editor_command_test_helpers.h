// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file editor_command_test_helpers.h
/// @brief Shared editor-command test fixtures.
///
/// Phase 10.9 Slice 20 Ts20-DU4 extraction: `IncrementCommand` was
/// defined byte-identical across test_command_history.cpp and
/// test_command_history_dirty.cpp. Both now forward to this single
/// canonical definition.
#pragma once

#include "editor/commands/editor_command.h"

#include <string>

namespace Vestige::Testing
{

/// @brief Trivial EditorCommand that adds `delta` (default +1) to an
///        external int on execute(), subtracts it on undo(). Used by
///        CommandHistory tests where the command's semantic action
///        doesn't matter — only that execute/undo are observable.
class IncrementCommand : public EditorCommand
{
public:
    explicit IncrementCommand(int& value, int delta = 1)
        : m_value(value), m_delta(delta) {}

    void execute() override { m_value += m_delta; }
    void undo() override { m_value -= m_delta; }
    std::string getDescription() const override { return "Increment"; }

private:
    int& m_value;
    int m_delta;
};

}  // namespace Vestige::Testing
