/// @file script_component.cpp
/// @brief ScriptComponent implementation.
#include "scripting/script_component.h"

namespace Vestige
{

void ScriptComponent::addScript(const std::string& graphAssetPath)
{
    m_scriptPaths.push_back(graphAssetPath);
}

void ScriptComponent::removeScript(size_t index)
{
    if (index < m_scriptPaths.size())
    {
        m_scriptPaths.erase(m_scriptPaths.begin() +
                            static_cast<std::ptrdiff_t>(index));
    }
    if (index < m_instances.size())
    {
        m_instances.erase(m_instances.begin() +
                          static_cast<std::ptrdiff_t>(index));
    }
}

} // namespace Vestige
