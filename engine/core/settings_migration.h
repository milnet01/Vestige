// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_migration.h
/// @brief Schema migration chain for Phase 10 settings.
///
/// A newer engine may add or rename settings fields. Files written
/// by an older build carry `schemaVersion: N` at the root; on load
/// we run `migrate(j)` which walks the chain of per-version
/// functions until `j["schemaVersion"] == kCurrentSchemaVersion`.
///
/// This is the MongoDB "schema-versioning" pattern — older
/// documents coexist with newer ones, migrated lazily on access.
///
/// Adding a new migration (v1 → v2):
///
///   1. Bump `kCurrentSchemaVersion` in `settings.h`.
///   2. Add `void migrate_v1_to_v2(nlohmann::json& j);` below,
///      implement it in `settings_migration.cpp`, and add the
///      `case 1:` arm in `migrate()`.
///   3. Test: write a v1 file, call `migrate`, assert the result
///      has `schemaVersion == 2` and the new field is populated
///      with whatever defaulting rule you chose.
///
/// Migrations must be **idempotent** — running the chain twice on
/// the same JSON produces the same result as running it once — so
/// partial migrations that crash midway don't corrupt the file
/// further on a subsequent attempt.
#pragma once

#include <nlohmann/json_fwd.hpp>

namespace Vestige
{

/// @brief Runs the migration chain in-place on @a j.
///
/// Reads the root `schemaVersion` (defaults to 1 if absent) and
/// invokes per-version migration functions until the version
/// matches `kCurrentSchemaVersion`. Sets
/// `j["schemaVersion"] = kCurrentSchemaVersion` on success.
///
/// @returns true on success (@a j is now at current schema),
///          false if the chain encountered an unknown version
///          (file came from a future build — cannot downgrade).
bool migrate(nlohmann::json& j);

/// @brief v1 → v2: add the `onboarding` section with defaults.
///
/// v2 introduces the first-run-wizard completion state for
/// Phase 10.5 (see `PHASE10_5_FIRST_RUN_WIZARD_DESIGN.md`). A v1
/// file has no `onboarding` block; this migration inserts one
/// with `hasCompletedFirstRun = false`, an empty `completedAt`,
/// and `skipCount = 0`.
///
/// **The legacy `welcome_shown` flag file is NOT read here** —
/// that file lives on the filesystem, not in the JSON tree, so
/// its promotion happens in `Settings::loadFromDisk`'s post-parse
/// hook (`promoteLegacyOnboardingFlag`). Keeping the migration
/// pure-JSON keeps it idempotent and unit-testable without file
/// I/O.
///
/// Idempotent: running twice on the same tree leaves it unchanged.
void migrate_v1_to_v2(nlohmann::json& j);

} // namespace Vestige
