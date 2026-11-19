// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stream_read.h
/// @brief All-or-nothing binary reads into an existing buffer.
#pragma once

#include <istream>
#include <type_traits>
#include <vector>

namespace Vestige
{

/// @brief Replaces the contents of @a dst with `dst.size()` elements read
///        from @a in, or leaves @a dst untouched when the stream runs short.
///
/// Reading straight into live data leaves it half new and half old when the
/// read stops early (an I/O error, a file cut short after its size was
/// checked).
/// @param got Optional: receives the number of bytes actually read.
/// @return True when every byte was read and @a dst now holds them.
template <typename T>
bool readWholeOrKeep(std::istream& in, std::vector<T>& dst,
                     std::streamsize* got = nullptr)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "readWholeOrKeep reads raw bytes");
    std::vector<T> fresh(dst.size());
    const auto want = static_cast<std::streamsize>(fresh.size() * sizeof(T));
    in.read(reinterpret_cast<char*>(fresh.data()), want);
    if (got != nullptr)
    {
        *got = in.gcount();
    }
    if (in.gcount() != want)
    {
        return false;
    }
    dst.swap(fresh);
    return true;
}

}  // namespace Vestige
