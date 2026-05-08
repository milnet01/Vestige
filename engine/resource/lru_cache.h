// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lru_cache.h
/// @brief Lightweight LRU cache helpers for ResourceManager (Phase 10.9 Pe5).
///
/// Each helper operates on two parallel containers owned by the calling
/// class:
///
///   `Cache<T>` — `unordered_map<string, CacheEntry<T>>` that stores the
///                cached value plus a list-iterator pointing at its
///                position in the recency list.
///
///   `Order`    — `list<string>` of cache keys, MRU at `front()`, LRU at
///                `back()`.
///
/// The free-function form (rather than a wrapping class) lets a single
/// owner — `ResourceManager` — share recency-tracking logic across three
/// caches (textures / meshes / models) without forcing one container per
/// helper or exposing a thick generic class to the public API.
///
/// Eviction soft-cap: entries with `use_count() > 1` are skipped because
/// they're still held by callers; evicting them would let the next
/// `loadXxx` produce a *second* live shared instance for the same key,
/// breaking the "one engine-wide instance per asset" invariant.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Per-cache value record. The `order` iterator points into the
///        owner's `std::list<std::string>` so the "move to MRU" splice
///        runs in O(1).
template <typename T>
struct CacheEntry
{
    std::shared_ptr<T> value;
    typename std::list<std::string>::iterator order;
};

template <typename T>
using Cache = std::unordered_map<std::string, CacheEntry<T>>;

/// @brief Records a cache hit by splicing @a key (referenced from @a it)
///        to MRU-front of the recency list.
template <typename T>
inline void touchCache(Cache<T>& /*cache*/, std::list<std::string>& order,
                       typename Cache<T>::iterator it)
{
    order.splice(order.begin(), order, it->second.order);
}

/// @brief Inserts a freshly-loaded entry at MRU-front of @a order, then
///        evicts from the LRU tail until `cache.size() <= limit`,
///        skipping entries with `use_count > 1`. The cache size can
///        exceed @a limit when every entry is in use; that's a deliberate
///        soft-cap to preserve the shared-instance invariant.
template <typename T>
inline void insertAndEnforceCache(Cache<T>& cache, std::list<std::string>& order,
                                  const std::string& key,
                                  std::shared_ptr<T> value, size_t limit)
{
    order.push_front(key);
    cache[key] = { std::move(value), order.begin() };

    while (cache.size() > limit)
    {
        auto walk = order.end();
        bool evicted = false;
        while (walk != order.begin())
        {
            --walk;
            auto mapIt = cache.find(*walk);
            if (mapIt != cache.end() && mapIt->second.value.use_count() == 1)
            {
                cache.erase(mapIt);
                order.erase(walk);
                evicted = true;
                break;
            }
        }
        if (!evicted) break;  // Everything's in use — soft-cap exceeded.
    }
}

/// @brief Re-enforces @a limit on a cache that may already be over-cap
///        (e.g. after a setter call tightens the budget).
template <typename T>
inline void enforceCacheLimit(Cache<T>& cache, std::list<std::string>& order,
                              size_t limit)
{
    while (cache.size() > limit)
    {
        auto walk = order.end();
        bool evicted = false;
        while (walk != order.begin())
        {
            --walk;
            auto mapIt = cache.find(*walk);
            if (mapIt != cache.end() && mapIt->second.value.use_count() == 1)
            {
                cache.erase(mapIt);
                order.erase(walk);
                evicted = true;
                break;
            }
        }
        if (!evicted) break;
    }
}

}  // namespace Vestige
