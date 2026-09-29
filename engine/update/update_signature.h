// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_signature.h
/// @brief Checks that a downloaded update is the file release.yml signed, for
///        that version (3D_E-0729, spec §4.4).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Vestige::Update
{

using PublicKey = std::array<std::uint8_t, 32>;
using Signature = std::array<std::uint8_t, 64>;

/// @brief Lowercase hex of the BLAKE2b-512 hash of @a size bytes at @a data.
std::string blake2bHex(const std::uint8_t* data, std::size_t size);

/// @brief The exact message tools/sign_release.py signs for one asset:
///
///     vestige-update-v1\n
///     version=<version>\n
///     asset=<asset file name>\n
///     blake2b=<blake2bHex of the asset>\n
///
/// Binding the version stops an old signed build republished under a newer
/// tag from verifying.
std::string signedMessage(std::string_view version, std::string_view assetName,
                          std::string_view blake2bHexDigest);

/// @brief True when @a signature is a valid Ed25519 signature of @a message
///        under @a key.
bool verifySignature(std::string_view message, const Signature& signature,
                     const PublicKey& key);

/// @brief The whole check the installer runs before touching anything:
///        rebuild the message from the offered version, the chosen asset name
///        and these downloaded bytes, and verify it against @a key.
bool verifyDownload(std::string_view version, std::string_view assetName,
                    const std::uint8_t* data, std::size_t size,
                    const Signature& signature, const PublicKey& key);

}  // namespace Vestige::Update
