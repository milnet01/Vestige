// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "audio/reverb_ir_pool.h"

#include <algorithm>
#include <iterator>

namespace Vestige
{

unsigned int ReverbIrPool::find(const std::string& path)
{
    auto it = m_entries.find(path);
    if (it == m_entries.end())
    {
        return 0;
    }
    touch(path);
    return it->second.buffer;
}

std::vector<unsigned int> ReverbIrPool::insert(const std::string& path,
                                               unsigned int buffer,
                                               std::size_t bytes)
{
    std::vector<unsigned int> evicted;

    // Re-loading the same path replaces its entry; the superseded buffer (if a
    // different ID) is handed back for deletion so accounting stays exact.
    auto existing = m_entries.find(path);
    if (existing != m_entries.end())
    {
        if (existing->second.buffer != buffer)
        {
            evicted.push_back(existing->second.buffer);
        }
        m_totalBytes -= existing->second.bytes;
        m_entries.erase(existing);
        m_order.remove(path);
    }

    m_entries.emplace(path, Entry{buffer, bytes});
    m_order.push_front(path);
    m_totalBytes += bytes;

    evictInto(evicted);
    return evicted;
}

std::vector<unsigned int> ReverbIrPool::setLimit(std::size_t bytes)
{
    m_limitBytes = bytes;
    std::vector<unsigned int> evicted;
    evictInto(evicted);
    return evicted;
}

std::vector<unsigned int> ReverbIrPool::clear()
{
    std::vector<unsigned int> all;
    all.reserve(m_entries.size());
    for (const auto& [path, entry] : m_entries)
    {
        all.push_back(entry.buffer);
    }
    m_entries.clear();
    m_order.clear();
    m_totalBytes = 0;
    m_pinned     = 0;
    return all;
}

void ReverbIrPool::touch(const std::string& path)
{
    auto it = std::find(m_order.begin(), m_order.end(), path);
    if (it != m_order.end())
    {
        m_order.splice(m_order.begin(), m_order, it);
    }
}

void ReverbIrPool::evictInto(std::vector<unsigned int>& out)
{
    // Walk the LRU tail toward the front, deleting the oldest evictable entry
    // each pass, until within the limit. Never touch the MRU-front (the loop
    // stops before `begin()`) nor the pinned buffer. If only those remain,
    // stop even if still over the limit — the caller logs the overflow.
    while (m_totalBytes > m_limitBytes && m_order.size() > 1)
    {
        bool evicted = false;
        for (auto cand = std::prev(m_order.end());
             cand != m_order.begin(); --cand)
        {
            auto entry = m_entries.find(*cand);
            if (entry != m_entries.end() && entry->second.buffer != m_pinned)
            {
                out.push_back(entry->second.buffer);
                m_totalBytes -= entry->second.bytes;
                m_entries.erase(entry);
                m_order.erase(cand);
                evicted = true;
                break;
            }
        }
        if (!evicted)
        {
            break;  // only the pinned + MRU-front entries remain
        }
    }
}

} // namespace Vestige
