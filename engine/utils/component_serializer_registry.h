// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file component_serializer_registry.h
/// @brief Registry mapping component type names to JSON read/write
///        callbacks. Replaces the fixed allowlist that silently
///        dropped unregistered component types on scene save/load
///        (ROADMAP Phase 10.9 Slice 1 F3).
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Vestige
{

class Entity;
class ResourceManager;

/// @brief Single-instance registry of component (de)serialisers.
///
/// Each registered entry names a JSON key (e.g. "AudioSource"), a
/// `trySerialize` callback that returns either the component's JSON
/// or `null` when the entity doesn't own that component type, and a
/// `deserialize` callback that creates the component and populates
/// it from JSON.
///
/// `EntitySerializer` iterates the registry on save and dispatches
/// by name on load. Adding a new component type is one registration
/// call instead of two switch-statement edits — and an unregistered
/// type produces a warning rather than silent data loss.
class ComponentSerializerRegistry
{
public:
    using TrySerializeFn =
        std::function<nlohmann::json(const Entity&, const ResourceManager&)>;
    using DeserializeFn =
        std::function<void(const nlohmann::json&, Entity&, ResourceManager&)>;

    struct Entry
    {
        /// @brief JSON key for this component type — also what
        ///        `deserialize` dispatches on.
        std::string typeName;

        /// @brief Returns the component's JSON if the entity owns
        ///        one, otherwise `nlohmann::json()` (i.e. null).
        TrySerializeFn trySerialize;

        /// @brief Creates the component on `entity` and populates
        ///        it from `j`. Called only when the JSON contains a
        ///        block under `typeName`.
        DeserializeFn deserialize;
    };

    /// @brief Returns the process-wide registry.
    static ComponentSerializerRegistry& instance();

    /// @brief Appends an entry. Order is preserved in serialised
    ///        output — the first registered type appears first in
    ///        the components dict.
    void registerEntry(Entry entry);

    /// @brief Lookup for the deserialise path. Returns `nullptr`
    ///        when no entry matches (caller logs a warning so
    ///        unknown-type JSON doesn't fail silently).
    const Entry* findByName(const std::string& typeName) const;

    /// @brief Read-only iteration over all entries in registration
    ///        order. Used by the serialiser to walk every type.
    const std::vector<Entry>& entries() const { return m_entries; }

private:
    ComponentSerializerRegistry() = default;
    std::vector<Entry> m_entries;
};

} // namespace Vestige
