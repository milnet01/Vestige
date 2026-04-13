/// @file node_type_registry.cpp
/// @brief NodeTypeRegistry implementation.
#include "scripting/node_type_registry.h"

#include <algorithm>

namespace Vestige
{

void NodeTypeRegistry::registerNode(NodeTypeDescriptor descriptor)
{
    m_nodes[descriptor.typeName] = std::move(descriptor);
}

const NodeTypeDescriptor* NodeTypeRegistry::findNode(const std::string& typeName) const
{
    auto it = m_nodes.find(typeName);
    return it != m_nodes.end() ? &it->second : nullptr;
}

std::vector<const NodeTypeDescriptor*> NodeTypeRegistry::getByCategory(
    const std::string& category) const
{
    std::vector<const NodeTypeDescriptor*> result;
    for (const auto& [name, desc] : m_nodes)
    {
        if (desc.category == category)
        {
            result.push_back(&desc);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const NodeTypeDescriptor* a, const NodeTypeDescriptor* b)
              {
                  return a->displayName < b->displayName;
              });

    return result;
}

std::vector<std::string> NodeTypeRegistry::getCategories() const
{
    std::vector<std::string> categories;
    for (const auto& [name, desc] : m_nodes)
    {
        if (std::find(categories.begin(), categories.end(), desc.category) ==
            categories.end())
        {
            categories.push_back(desc.category);
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

bool NodeTypeRegistry::hasNode(const std::string& typeName) const
{
    return m_nodes.count(typeName) > 0;
}

} // namespace Vestige
