// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_key.h
/// @brief The Ed25519 public key every update is checked against (3D_E-0729).
///
/// Its private half is the GitHub Actions secret VESTIGE_UPDATE_SIGNING_KEY,
/// which release.yml signs with. release.yml reads this file to verify its own
/// signatures before upload (INV-3), so a rotated secret without a matching
/// change here fails the release instead of shipping updates nobody can install.
/// Keep the hex on one line inside the quotes: release.yml parses it.
#pragma once

#include "update/update_signature.h"

namespace Vestige::Update
{

// update-public-key: 99a3e26010b67abefb0dc1f9a485f166e9a5e385af06d4e913619aef9c4dfff5
inline constexpr PublicKey kUpdatePublicKey = {
    0x99, 0xa3, 0xe2, 0x60, 0x10, 0xb6, 0x7a, 0xbe, 0xfb, 0x0d, 0xc1, 0xf9, 0xa4, 0x85, 0xf1, 0x66,
    0xe9, 0xa5, 0xe3, 0x85, 0xaf, 0x06, 0xd4, 0xe9, 0x13, 0x61, 0x9a, 0xef, 0x9c, 0x4d, 0xff, 0xf5,
};

}  // namespace Vestige::Update
