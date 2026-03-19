/// @file selection.cpp
/// @brief Selection implementation.
#include "editor/selection.h"
#include "scene/scene.h"

#include <algorithm>

namespace Vestige
{

void Selection::select(uint32_t id)
{
    m_selectedIds.clear();
    if (id != 0)
    {
        m_selectedIds.push_back(id);
    }
}

void Selection::addToSelection(uint32_t id)
{
    if (id == 0)
    {
        return;
    }
    if (!isSelected(id))
    {
        m_selectedIds.push_back(id);
    }
}

void Selection::toggleSelection(uint32_t id)
{
    if (id == 0)
    {
        return;
    }
    auto it = std::find(m_selectedIds.begin(), m_selectedIds.end(), id);
    if (it != m_selectedIds.end())
    {
        m_selectedIds.erase(it);
    }
    else
    {
        m_selectedIds.push_back(id);
    }
}

void Selection::clearSelection()
{
    m_selectedIds.clear();
}

bool Selection::isSelected(uint32_t id) const
{
    return std::find(m_selectedIds.begin(), m_selectedIds.end(), id)
        != m_selectedIds.end();
}

bool Selection::hasSelection() const
{
    return !m_selectedIds.empty();
}

const std::vector<uint32_t>& Selection::getSelectedIds() const
{
    return m_selectedIds;
}

uint32_t Selection::getPrimaryId() const
{
    if (m_selectedIds.empty())
    {
        return 0;
    }
    return m_selectedIds.back();
}

Entity* Selection::getPrimaryEntity(Scene& scene) const
{
    uint32_t id = getPrimaryId();
    if (id == 0)
    {
        return nullptr;
    }
    return scene.findEntityById(id);
}

} // namespace Vestige
