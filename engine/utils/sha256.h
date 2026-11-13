// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sha256.h
/// @brief SHA-256 (FIPS 180-4), for naming file contents in a run's record.
///        Not for passwords or signatures: nothing here is constant-time.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Vestige
{

/// @brief Hashes bytes fed in any number of pieces.
class Sha256
{
public:
    Sha256();

    void update(const void* data, std::size_t size);

    /// @brief The digest as 64 lower-case hexadecimal digits. Call once;
    ///        the object is spent afterwards.
    std::string finishHex();

private:
    void compress(const std::uint8_t* block);

    std::array<std::uint32_t, 8> m_state;
    std::array<std::uint8_t, 64> m_block{};
    std::size_t m_blockFill = 0;
    std::uint64_t m_totalBytes = 0;
};

/// @brief SHA-256 of @p size bytes at @p data, as 64 hexadecimal digits.
std::string sha256Hex(const void* data, std::size_t size);

} // namespace Vestige
