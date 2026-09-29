// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_signature.cpp
#include "update/update_signature.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

namespace Vestige::Update
{

std::string blake2bHex(const std::uint8_t* data, std::size_t size)
{
    std::array<std::uint8_t, 64> hash{};
    crypto_blake2b(hash.data(), hash.size(), data, size);
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(hash.size() * 2);
    for (std::uint8_t b : hash)
    {
        hex.push_back(kDigits[b >> 4]);
        hex.push_back(kDigits[b & 0x0F]);
    }
    return hex;
}

std::string signedMessage(std::string_view version, std::string_view assetName,
                          std::string_view blake2bHexDigest)
{
    std::string message = "vestige-update-v1\n";
    message.append("version=").append(version).append("\n");
    message.append("asset=").append(assetName).append("\n");
    message.append("blake2b=").append(blake2bHexDigest).append("\n");
    return message;
}

bool verifySignature(std::string_view message, const Signature& signature,
                     const PublicKey& key)
{
    return crypto_ed25519_check(signature.data(), key.data(),
                                reinterpret_cast<const std::uint8_t*>(message.data()),
                                message.size())
        == 0;
}

bool verifyDownload(std::string_view version, std::string_view assetName,
                    const std::uint8_t* data, std::size_t size,
                    const Signature& signature, const PublicKey& key)
{
    return verifySignature(signedMessage(version, assetName, blake2bHex(data, size)),
                           signature, key);
}

}  // namespace Vestige::Update
