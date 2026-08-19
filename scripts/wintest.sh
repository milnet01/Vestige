#!/usr/bin/env bash
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
#
# wintest.sh — run the Windows test binaries on the real `wintest` box.
#
# Why this exists (3D_E-0046). `scripts/local-ci.sh --windows` cross-builds with
# msvc-wine and runs ctest under Wine, where every GL test SELF-SKIPS: Wine
# exposes no GL 4.5 context, exactly like the GitHub windows-2022 runner. So
# until this script, no GL test had ever executed on Windows anywhere. The
# `wintest` box is a real Windows 10 machine with a GTX 1050, reachable over ssh
# and already in ~/.ssh/config. It is a RUN box — no cmake, ninja, git or cl —
# so this stages the binaries built here and drives them remotely.
#
# Two traps this script exists to handle. Neither is optional and both are
# silent: each one produces a run that LOOKS clean.
#
#   1. Windows OpenSSH runs its shell in session 0, which has no interactive
#      desktop. glfwInit() succeeds there but glfwCreateWindow() FAILS, so a
#      plain `ssh wintest vestige_tests.exe` reports every GL test skipped and
#      exits 0. The tests must be launched into the logged-on user's session,
#      which is what the scheduled task with /IT below does. Measured: over
#      plain ssh the fixture logs "glfwCreateWindow failed"; through the task it
#      logs "4.5.0 NVIDIA 560.94 on NVIDIA GeForce GTX 1050/PCIe/SSE2". A user
#      must therefore be logged in at the console for a run to work.
#
#   2. Six test data directories are baked into the binary at configure time as
#      absolute paths under ${CMAKE_SOURCE_DIR} — VESTIGE_SHADER_DIR,
#      VESTIGE_FONT_DIR, VESTIGE_LOCALIZATION_DIR, VESTIGE_AUDIO_FIXTURES_DIR,
#      VESTIGE_SCENE_FIXTURES_DIR and VESTIGE_REFERENCE_CASES_DIR. On another
#      machine those paths do not exist and ~90 tests fail on a missing file
#      rather than on behaviour. Rather than teach every consumer to relocate,
#      this MIRRORS the source tree at the matching Windows path: a repo at
#      /mnt/Games/... is staged to C:\mnt\Games\..., so a baked path resolves
#      exactly as it does at home. Windows treats a leading "/" as relative to
#      the current drive, which is what makes the mirror work.
#
# Usage:
#   scripts/wintest.sh                       # stage + run the whole suite
#   scripts/wintest.sh --filter 'IBL*'       # a gtest filter
#   scripts/wintest.sh --no-stage            # reuse what is already on the box
#   scripts/wintest.sh --exe test_tinyexr.exe
#
#   VESTIGE_WINTEST_QUALITY_PRESET=high scripts/wintest.sh   # hold this box to
#                                                            # the dev-rig GPU
#                                                            # perf budgets
#
# It does NOT build. Build first with:  ./scripts/local-ci.sh --windows
#
# Every ssh command below interpolates local variables on purpose — the remote
# is cmd.exe and has none of them.
# shellcheck disable=SC2029
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${VESTIGE_WINTEST_HOST:-wintest}"
DRIVE="${VESTIGE_WINTEST_DRIVE:-C:}"
TASK_NAME='VestigeTests'
# Quality tier this box is entitled to. The GPU perf gates in
# tests/test_fog_benchmark.cpp are design § 8's HIGH-preset figures, measured on
# the RX 6600 dev rig; the GTX 1050 in this box misses the volumetric one by 8%
# (3D_E-0615) and no volumetric budget is published below High, so it is gated
# as medium and the benchmarks report rather than assert. Override to high to
# hold this box to the dev-rig numbers anyway.
QUALITY_PRESET="${VESTIGE_WINTEST_QUALITY_PRESET:-medium}"
BIN_DIR="$REPO_ROOT/build-msvc/bin"

# Source-tree directories the baked VESTIGE_*_DIR defines point into, plus the
# runtime assets the exe loads relative to its own cwd. Everything else in the
# repo (models 1.8 G, textures 581 M, source) is deliberately not staged — no
# test reads it. Add a path here if a test starts needing one.
SOURCE_DATA=(
    assets/shaders
    assets/fonts
    assets/localization
    assets/hdri
    tests/fixtures
    tools/formula_workbench/reference_cases
)
RUNTIME_ASSETS=(shaders fonts hdri localization audio)

EXE="vestige_tests.exe"
FILTER=""
DO_STAGE=1
TIMEOUT_S=1800

while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)   FILTER="$2"; shift 2 ;;
        --exe)      EXE="$2"; shift 2 ;;
        --host)     HOST="$2"; shift 2 ;;
        --no-stage) DO_STAGE=0; shift ;;
        --timeout)  TIMEOUT_S="$2"; shift 2 ;;
        -h|--help)  sed -n '5,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

say() { printf '\n=== %s\n' "$*"; }
die() { printf 'wintest: %s\n' "$*" >&2; exit 1; }

# The mirror: /mnt/Games/Scripts/Linux/Vestige -> C:\mnt\Games\Scripts\Linux\Vestige
MIRROR="${DRIVE}${REPO_ROOT//\//\\}"
MIRROR_FWD="${DRIVE}${REPO_ROOT}"
REMOTE_BIN="$MIRROR\\build-msvc\\bin"

# --- preflight --------------------------------------------------------------
say "preflight — $HOST"
[[ -x "$BIN_DIR/$EXE" ]] || die "$BIN_DIR/$EXE not found. Build it first:
    ./scripts/local-ci.sh --windows"
[[ "$REPO_ROOT" =~ ^[/A-Za-z0-9_.\ -]+$ ]] \
    || die "repo path '$REPO_ROOT' has characters Windows cannot mirror."

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" 'ver' >/dev/null 2>&1 \
    || die "cannot reach '$HOST' over ssh (BatchMode). Check ~/.ssh/config and that the box is up."

# A console session must exist, or every GL test skips however we launch it.
console_session=$(ssh "$HOST" 'powershell -NoProfile -Command "(Get-Process -Name explorer -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty SessionId)"' 2>/dev/null | tr -d '\r ')
[[ -n "$console_session" ]] || die "no interactive desktop on $HOST (no explorer.exe).
Log a user in at the console — GL context creation needs a real session."
echo "  console session: $console_session (ssh runs in session 0; tests are launched into that one)"
echo "  mirror root:     $MIRROR"

# --- stage ------------------------------------------------------------------
if [[ $DO_STAGE -eq 1 ]]; then
    say "stage — mirroring the source tree onto $HOST"
    stage_dir="$(mktemp -d)"
    trap 'rm -rf "$stage_dir"' EXIT

    for rel in "${SOURCE_DATA[@]}"; do
        [[ -d "$REPO_ROOT/$rel" ]] || die "missing source data dir: $rel"
        mkdir -p "$stage_dir/$(dirname "$rel")"
        cp -r "$REPO_ROOT/$rel" "$stage_dir/$rel"
    done

    mkdir -p "$stage_dir/build-msvc/bin/assets"
    cp "$BIN_DIR"/*.exe "$stage_dir/build-msvc/bin/" || die "no .exe in $BIN_DIR"
    # The MSVC redist DLLs local-ci.sh co-locates for Wine are the same ones the
    # real box needs; the UCRT (api-ms-win-crt-*) ships with Windows 10.
    cp "$BIN_DIR"/*.dll "$stage_dir/build-msvc/bin/" 2>/dev/null || true
    for d in "${RUNTIME_ASSETS[@]}"; do
        [[ -d "$BIN_DIR/assets/$d" ]] && cp -r "$BIN_DIR/assets/$d" "$stage_dir/build-msvc/bin/assets/"
    done

    ssh "$HOST" "if exist $MIRROR rmdir /s /q $MIRROR" >/dev/null 2>&1
    ssh "$HOST" "mkdir $MIRROR" >/dev/null 2>&1 || die "could not create $MIRROR on $HOST"
    scp -q -r "$stage_dir"/* "$HOST:$MIRROR_FWD/" || die "scp failed"

    local_n=$(find "$stage_dir" -mindepth 1 | wc -l)
    remote_n=$(ssh "$HOST" "dir /s /b $MIRROR" 2>/dev/null | tr -d '\r' | grep -c .)
    echo "  staged $remote_n entries (local $local_n), $(du -sh "$stage_dir" | cut -f1)"
    [[ "$remote_n" -ge "$local_n" ]] || die "stage incomplete: $remote_n remote vs $local_n local"
fi

# --- run --------------------------------------------------------------------
say "run — $EXE ${FILTER:+--gtest_filter=$FILTER}"
runner="$(mktemp)"
{
    printf '%s\n' '@echo off'
    # cd into the staged bin so cwd-relative asset loads resolve; the baked
    # absolute paths resolve through the mirror regardless of cwd.
    # (Never write `set VAR=value && app` here — cmd puts the trailing SPACE
    # into the value. DOOM_Ants records that trap.)
    printf '%s\n' "cd /d $REMOTE_BIN"
    # Quoted form on purpose — see the trailing-space trap noted above.
    printf '%s\n' "set \"VESTIGE_QUALITY_PRESET=$QUALITY_PRESET\""
    printf '%s\n' "del /q $REMOTE_BIN\\results.txt 2>nul"
    printf '%s\n' "del /q $REMOTE_BIN\\done.txt 2>nul"
    if [[ -n "$FILTER" ]]; then
        printf '%s\n' "$EXE --gtest_filter=$FILTER > $REMOTE_BIN\\results.txt 2>&1"
    else
        printf '%s\n' "$EXE > $REMOTE_BIN\\results.txt 2>&1"
    fi
    printf '%s\n' "echo EXIT=%ERRORLEVEL% > $REMOTE_BIN\\done.txt"
} > "$runner"
scp -q "$runner" "$HOST:$MIRROR_FWD/build-msvc/bin/run.cmd" || die "could not upload runner"
rm -f "$runner"

remote_user=$(ssh "$HOST" 'echo %USERNAME%' 2>/dev/null | tr -d '\r')
# /IT is the whole point: run as the logged-on user, in their interactive
# session, so a GL context can be created. /ST is in the past on purpose — the
# task is never scheduled, only ever started by hand on the next line.
ssh "$HOST" "schtasks /create /tn $TASK_NAME /tr \"$REMOTE_BIN\\run.cmd\" /sc once /st 00:00 /it /ru $remote_user /f" >/dev/null 2>&1 \
    || die "could not create the scheduled task as '$remote_user'"
ssh "$HOST" "schtasks /run /tn $TASK_NAME" >/dev/null 2>&1 || die "could not start the scheduled task"

waited=0
exit_line=""
while [[ $waited -lt $TIMEOUT_S ]]; do
    sleep 10; waited=$((waited + 10))
    exit_line=$(ssh "$HOST" "type $REMOTE_BIN\\done.txt 2>nul" 2>/dev/null | tr -d '\r' | tr -d ' ')
    [[ -n "$exit_line" ]] && break
    printf '  ... %ss\n' "$waited"
done
[[ -n "$exit_line" ]] || die "no completion marker after ${TIMEOUT_S}s — the run hung. Results so far:
    ssh $HOST 'type $REMOTE_BIN\\results.txt'"

out="$REPO_ROOT/build-msvc/wintest-results.txt"
ssh "$HOST" "type $REMOTE_BIN\\results.txt" 2>/dev/null | tr -d '\r' > "$out"

# --- report -----------------------------------------------------------------
say "report"
rc="${exit_line#EXIT=}"
echo "  ran in ~${waited}s, remote exit code $rc"
echo "  full output: $out"
grep -m1 'GLTestEnvironment:' "$out" | sed 's/^/  /'
if ! grep -q 'GLTestEnvironment: [0-9]' "$out"; then
    echo "  WARNING: no GL context was created — every GL test skipped, so a green"
    echo "           run here proves nothing. Is a user logged in at the console?"
fi
grep -E '^\[  (PASSED|FAILED|SKIPPED) ' "$out" | sed 's/^/  /'
grep -E '^\[  FAILED  \] [A-Za-z]' "$out" | sed 's/^/  /' | head -40

if [[ "$rc" != "0" ]] && ! grep -qE '^\[==========\] .* ran\.' "$out"; then
    echo
    echo "  The suite DIED before finishing (exit $rc) — the summary above is partial."
    echo "  Last test started:"
    grep -E '^\[ RUN      \]' "$out" | tail -1 | sed 's/^/    /'
    echo "  Re-run without it:  scripts/wintest.sh --no-stage --filter '-<that test>'"
fi

[[ "$rc" == "0" ]] || exit 1
exit 0
