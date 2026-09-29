#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Sign release assets for the self-updater, or verify their signatures.

3D_E-0729, docs/specs/3D_E-0729-self-update.md section 4.4. Each asset gets
`<asset>.sig`: a raw 64-byte Ed25519 signature over

    vestige-update-v1\\n
    version=<version>\\n
    asset=<asset file name>\\n
    blake2b=<lowercase hex BLAKE2b-512 of the asset>\\n

which the engine rebuilds and checks with Monocypher (update_signature.cpp).

    sign:    VESTIGE_UPDATE_SIGNING_KEY=<base64 32-byte seed> \\
             sign_release.py --version 0.1.76 FILE...
    verify:  sign_release.py --verify --version 0.1.76 FILE...

--verify reads the public key from engine/update/update_key.h, so a signing
secret that no longer matches the key the engine ships fails the release
(INV-3) instead of publishing updates nobody can install. The secret is read
from the environment only and never printed.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import os
import re
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)

KEY_HEADER = Path(__file__).resolve().parent.parent / "engine" / "update" / "update_key.h"


def signed_message(version: str, asset: Path) -> bytes:
    digest = hashlib.blake2b(asset.read_bytes()).hexdigest()  # 64-byte digest
    return (
        "vestige-update-v1\n"
        f"version={version}\n"
        f"asset={asset.name}\n"
        f"blake2b={digest}\n"
    ).encode()


def engine_public_key() -> Ed25519PublicKey:
    text = KEY_HEADER.read_text()
    match = re.search(r"update-public-key:\s*([0-9a-f]{64})\b", text)
    if not match:
        sys.exit(f"no 'update-public-key: <64 hex>' line in {KEY_HEADER}")
    return Ed25519PublicKey.from_public_bytes(bytes.fromhex(match.group(1)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", required=True, help="release version, no leading v")
    ap.add_argument("--verify", action="store_true", help="check <file>.sig instead of signing")
    ap.add_argument("files", nargs="+", type=Path)
    args = ap.parse_args()
    if args.version.startswith("v"):
        sys.exit("--version takes the version without its leading 'v'")

    if args.verify:
        key = engine_public_key()
        bad = 0
        for f in args.files:
            sig = Path(str(f) + ".sig")
            try:
                key.verify(sig.read_bytes(), signed_message(args.version, f))
                print(f"verified {f.name}")
            except (InvalidSignature, FileNotFoundError, ValueError) as e:
                print(f"FAILED {f.name}: {type(e).__name__}", file=sys.stderr)
                bad += 1
        return 1 if bad else 0

    seed_b64 = os.environ.get("VESTIGE_UPDATE_SIGNING_KEY", "").strip()
    if not seed_b64:
        sys.exit("VESTIGE_UPDATE_SIGNING_KEY is not set")
    seed = base64.b64decode(seed_b64)
    if len(seed) != 32:
        sys.exit("VESTIGE_UPDATE_SIGNING_KEY must be the base64 of a 32-byte seed")
    key = Ed25519PrivateKey.from_private_bytes(seed)
    for f in args.files:
        Path(str(f) + ".sig").write_bytes(key.sign(signed_message(args.version, f)))
        print(f"signed {f.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
