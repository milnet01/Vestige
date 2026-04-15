// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pin_id.h
/// @brief Interned pin name → integer ID mapping for hot-path performance.
///
/// The interpreter compares pin names on every pin read, every output write,
/// and every connection lookup. Doing those comparisons on `std::string` keys
/// is the dominant cost in `ScriptContext::readInput`. PinIds replace the
/// strings on every runtime hot path:
///
/// - ScriptInstance's connection indices (`m_outputByNode` / `m_inputByNode`)
///   key on PinId instead of string.
/// - ScriptNodeInstance::outputValues / runtimeState key on PinId.
/// - ScriptContext::readInput / setOutput / triggerOutput accept PinId on the
///   hot path; convenience `const std::string&` overloads remain so test code
///   and editor lookups can stay readable.
///
/// On-disk schema (ScriptGraph, ScriptNodeDef, ScriptConnection) keeps
/// strings — pin names need to survive across engine versions and toolchains.
/// PinIds are assigned at runtime per process and are NOT stable across
/// processes.
///
/// Threading: the intern table is single-threaded by design — the scripting
/// system, editor, and node registration all run on the main thread. If a
/// future phase moves any of these off-thread, this needs a mutex.
#pragma once

#include <cstdint>
#include <string>

namespace Vestige
{

/// @brief Process-local integer handle for an interned pin name.
/// Sequential (NOT a hash) — lookups via id-as-vector-index are O(1) and
/// collision-free. 0 is reserved to mean "invalid / unset" so default
/// constructors produce an explicit not-yet-interned state.
using PinId = uint32_t;

/// @brief Sentinel value for "no pin" (default-constructed).
constexpr PinId INVALID_PIN_ID = 0;

/// @brief Look up (or create) the PinId for the given pin name.
/// Empty string returns INVALID_PIN_ID.
PinId internPin(const std::string& name);

/// @brief Look up the original string for a PinId, for editor display and
/// debug logging. Returns an empty string for INVALID_PIN_ID or unknown ids.
const std::string& pinName(PinId id);

/// @brief Number of distinct pin names currently in the intern table
/// (excluding the reserved index 0). Useful for diagnostics.
size_t internedPinCount();

} // namespace Vestige
