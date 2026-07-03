// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_ir_pool.h
/// @brief AX2 R2 — pure LRU, byte-bounded pool of convolution impulse-response
///        buffers.
#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Device-free bookkeeping for the convolution IR buffers held by
///        `AudioEngine`.
///
/// Holds only opaque OpenAL buffer IDs + their PCM byte sizes — it never calls
/// OpenAL. The owning `AudioEngine` performs the `alGenBuffers` before an
/// `insert` and the `alDeleteBuffers` on the IDs an operation RETURNS. Split
/// out so the eviction + pinning invariants are unit-testable without an audio
/// device (the audio suite runs headless).
///
/// Recency: `m_order` front = most-recently-used. Eviction walks the tail.
///
/// Invariants:
///  - After any `insert` / `setLimit`, total resident bytes ≤ limit, EXCEPT
///    when the only entries left are the pinned (attached) IR and/or the
///    MRU-front entry — neither is ever evicted. The pinned buffer must not be
///    deleted (the convolution extension forbids deleting an attached buffer);
///    the MRU-front is the just-requested IR, so a load always yields a live
///    buffer even when it alone exceeds the limit.
///  - The pinned buffer ID is never returned for deletion.
class ReverbIrPool
{
public:
    /// @brief Default byte ceiling (64 MB, reverb design §7).
    static constexpr std::size_t kDefaultLimitBytes = 64ull * 1024 * 1024;

    /// @brief Look up @a path. On a hit, promote it to MRU and return its
    ///        buffer ID; on a miss, return 0.
    unsigned int find(const std::string& path);

    /// @brief Insert @a buffer (@a bytes big) under @a path as the new MRU
    ///        entry, then evict LRU-tail entries until within the limit. A
    ///        pre-existing entry for @a path is replaced (its old buffer, if
    ///        different, is returned for deletion).
    /// @return The buffer IDs evicted — the caller deletes them. @a buffer is
    ///         never evicted by its own insert.
    std::vector<unsigned int> insert(const std::string& path,
                                     unsigned int buffer, std::size_t bytes);

    /// @brief Pin @a buffer (the IR currently attached to the slot) so it is
    ///        exempt from eviction. Pass 0 to unpin.
    void setPinned(unsigned int buffer) { m_pinned = buffer; }
    unsigned int pinned() const { return m_pinned; }

    /// @brief Set the byte ceiling, evicting down to it.
    /// @return The buffer IDs evicted, for the caller to delete.
    std::vector<unsigned int> setLimit(std::size_t bytes);
    std::size_t limitBytes() const { return m_limitBytes; }

    /// @brief Drop every entry (and unpin).
    /// @return Every buffer ID that was resident, for the caller to delete.
    std::vector<unsigned int> clear();

    std::size_t count() const { return m_entries.size(); }
    std::size_t bytes() const { return m_totalBytes; }

private:
    struct Entry
    {
        unsigned int buffer = 0;
        std::size_t  bytes  = 0;
    };

    std::unordered_map<std::string, Entry> m_entries;
    std::list<std::string> m_order;   ///< front = MRU
    std::size_t  m_totalBytes = 0;
    std::size_t  m_limitBytes = kDefaultLimitBytes;
    unsigned int m_pinned     = 0;

    void touch(const std::string& path);
    void evictInto(std::vector<unsigned int>& out);
};

} // namespace Vestige
