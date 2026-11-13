// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sha256.cpp
/// @brief SHA-256 as FIPS 180-4 section 6.2 states it.
#include "utils/sha256.h"

#include <algorithm>
#include <cstring>

namespace Vestige
{

namespace
{

/// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the
/// cube roots of the first 64 primes.
constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotateRight(std::uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

Sha256::Sha256()
    : m_state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
{
}

void Sha256::compress(const std::uint8_t* block)
{
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
               | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
               | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
               | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
    for (int i = 0; i < 64; ++i)
    {
        const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + choice + K[i] + w[i];
        const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::update(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_totalBytes += size;
    while (size > 0)
    {
        if (m_blockFill == 0 && size >= m_block.size())
        {
            compress(bytes);  // a whole block needs no copy
            bytes += m_block.size();
            size -= m_block.size();
            continue;
        }
        const std::size_t take = std::min(size, m_block.size() - m_blockFill);
        std::memcpy(m_block.data() + m_blockFill, bytes, take);
        m_blockFill += take;
        bytes += take;
        size -= take;
        if (m_blockFill == m_block.size())
        {
            compress(m_block.data());
            m_blockFill = 0;
        }
    }
}

std::string Sha256::finishHex()
{
    // Padding: one 1 bit, zeros, then the message length in bits as 64 bits,
    // so that the whole is a multiple of 64 bytes.
    const std::uint64_t bitLength = m_totalBytes * 8;
    m_block[m_blockFill++] = 0x80;
    if (m_blockFill > 56)
    {
        std::fill(m_block.begin() + static_cast<std::ptrdiff_t>(m_blockFill), m_block.end(),
                  std::uint8_t{0});
        compress(m_block.data());
        m_blockFill = 0;
    }
    std::fill(m_block.begin() + static_cast<std::ptrdiff_t>(m_blockFill), m_block.begin() + 56,
              std::uint8_t{0});
    for (int i = 0; i < 8; ++i)
    {
        m_block[static_cast<std::size_t>(56 + i)] =
            static_cast<std::uint8_t>(bitLength >> (56 - 8 * i));
    }
    compress(m_block.data());

    static const char DIGITS[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const std::uint32_t word : m_state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            hex.push_back(DIGITS[(word >> shift) & 0xF]);
        }
    }
    return hex;
}

std::string sha256Hex(const void* data, std::size_t size)
{
    Sha256 hash;
    hash.update(data, size);
    return hash.finishHex();
}

} // namespace Vestige
