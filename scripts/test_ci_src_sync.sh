#!/usr/bin/env bash
# Regression test for local-ci.sh's sync_ci_src (3D_E-0732).
#
# A header edited while a gate run is still compiling gets an mtime OLDER than
# the objects that run produces. If the sync then copies the header with that
# mtime, ninja sees objects newer than every input and rebuilds nothing, so the
# next run links stale objects against the new library. The sync must leave a
# changed file newer than anything already built, and must leave an unchanged
# file alone so it does not rebuild.
#
# Runs the real function, extracted from local-ci.sh, on a throwaway repository.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

eval "$(sed -n '/^sync_ci_src() {$/,/^}$/p' "$here/local-ci.sh")"
declare -F sync_ci_src >/dev/null || { echo "FAIL: sync_ci_src not found in local-ci.sh"; exit 1; }

REPO_ROOT="$work/repo"
CI_DIR="$work/ci"
mkdir -p "$REPO_ROOT" "$CI_DIR/debug"
git -C "$REPO_ROOT" init -q
echo "int check(int);" > "$REPO_ROOT/api.h"
echo "unchanged"       > "$REPO_ROOT/other.h"
touch -d '2026-01-01 10:00:00' "$REPO_ROOT/api.h" "$REPO_ROOT/other.h"

sync_ci_src
other_before="$(stat -c %Y "$CI_DIR/src/other.h")"

# The run that started before the edit finishes its object at 10:10.
touch -d '2026-01-01 10:10:00' "$CI_DIR/debug/user.o"
# The edit landed at 10:05, while that run was compiling.
echo "int check(int, bool);" > "$REPO_ROOT/api.h"
touch -d '2026-01-01 10:05:00' "$REPO_ROOT/api.h"

sync_ci_src

fail=0
cmp -s "$REPO_ROOT/api.h" "$CI_DIR/src/api.h" \
    || { echo "FAIL: the edited header was not copied"; fail=1; }
[ "$CI_DIR/src/api.h" -nt "$CI_DIR/debug/user.o" ] \
    || { echo "FAIL: the edited header is not newer than the object built before the edit reached the copy"; fail=1; }
[ "$(stat -c %Y "$CI_DIR/src/other.h")" = "$other_before" ] \
    || { echo "FAIL: an unchanged file was re-stamped (it would rebuild every run)"; fail=1; }

[ "$fail" = 0 ] && echo "PASS: sync_ci_src"
exit "$fail"
