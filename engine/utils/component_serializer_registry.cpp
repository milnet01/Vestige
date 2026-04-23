// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file component_serializer_registry.cpp
/// @brief ComponentSerializerRegistry implementation.
#include "utils/component_serializer_registry.h"

namespace Vestige
{

ComponentSerializerRegistry& ComponentSerializerRegistry::instance()
{
    static ComponentSerializerRegistry s_instance;
    return s_instance;
}

void ComponentSerializerRegistry::registerEntry(Entry entry)
{
    m_entries.push_back(std::move(entry));
}

const ComponentSerializerRegistry::Entry*
ComponentSerializerRegistry::findByName(const std::string& typeName) const
{
    for (const auto& entry : m_entries)
    {
        if (entry.typeName == typeName)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace Vestige
