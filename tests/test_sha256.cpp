// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sha256.cpp
/// @brief SHA-256 against known digests. The first three are the FIPS 180-4
///        examples; every expected value was also read from Python's hashlib.
#include "utils/sha256.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace Vestige;

TEST(Sha256Test, MatchesKnownDigests)
{
    EXPECT_EQ(sha256Hex("", 0),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc", 3),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(sha256Hex(twoBlocks.data(), twoBlocks.size()),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, PadsCorrectlyAroundABlockBoundary)
{
    // 55 bytes is the longest message whose padding fits its own block; 64
    // fills a block exactly, so the padding takes a block of its own.
    const std::string a55(55, 'a');
    EXPECT_EQ(sha256Hex(a55.data(), a55.size()),
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    const std::string a64(64, 'a');
    EXPECT_EQ(sha256Hex(a64.data(), a64.size()),
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(Sha256Test, PiecesGiveTheSameDigestAsOneCall)
{
    const std::string million(1000000, 'a');
    Sha256 hash;
    std::size_t offset = 0;
    // Uneven pieces, so a piece ends inside, at and across block boundaries.
    for (std::size_t piece = 1; offset < million.size(); piece = piece * 3 + 1)
    {
        const std::size_t size = std::min(piece, million.size() - offset);
        hash.update(million.data() + offset, size);
        offset += size;
    }
    EXPECT_EQ(hash.finishHex(),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
