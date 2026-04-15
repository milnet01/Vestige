#!/usr/bin/env bash
# scripts/final_launch_sweep.sh
#
# One-command final sweep for Vestige's open-source launch, per
# docs/PRE_OPEN_SOURCE_AUDIT.md §11 "One final pre-launch sweep".
#
# Runs, in order:
#   1. gitleaks on the full history (source + --log-opts=--all)
#   2. the repo's own audit tool, full pass (tiers 1-6)
#   3. a clean-slate CMake configure + build + ctest
#
# All three must succeed before flipping the repo public. Any non-zero
# exit aborts the remaining steps — fix and re-run from the top.
#
# Usage:
#   scripts/final_launch_sweep.sh
#
# Optional environment overrides:
#   SKIP_GITLEAKS=1   -- skip step 1 (e.g. if gitleaks isn't installed yet)
#   SKIP_AUDIT=1      -- skip step 2
#   SKIP_BUILD=1      -- skip step 3 (fastest iteration for re-running 1+2)
#   BUILD_DIR=<path>  -- override build directory (default: build)
#   JOBS=<n>          -- override parallel build jobs (default: nproc)
#
# The NVD API key for the audit tool's Tier 5 CVE research is read from
# $NVD_API_KEY if set; unset means Tier 5 falls back to the public
# rate-limited endpoint (still works, just slower).

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${BUILD_DIR:-build}"
jobs="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# ANSI colour helpers (respect NO_COLOR per the convention).
if [[ -t 1 ]] && [[ -z "${NO_COLOR:-}" ]]; then
    c_bold=$'\e[1m'
    c_green=$'\e[32m'
    c_red=$'\e[31m'
    c_yellow=$'\e[33m'
    c_reset=$'\e[0m'
else
    c_bold=''
    c_green=''
    c_red=''
    c_yellow=''
    c_reset=''
fi

step() {
    echo ""
    echo "${c_bold}=== $* ===${c_reset}"
}

ok() {
    echo "${c_green}✓${c_reset} $*"
}

fail() {
    echo "${c_red}✗${c_reset} $*" >&2
    exit 1
}

skip() {
    echo "${c_yellow}⟳${c_reset} $*"
}

# ---------------------------------------------------------------------------
# Step 1 — gitleaks on working tree + full history
# ---------------------------------------------------------------------------
if [[ "${SKIP_GITLEAKS:-0}" == "1" ]]; then
    skip "Step 1/3: gitleaks (SKIP_GITLEAKS=1)"
else
    step "Step 1/3: gitleaks (working tree + full history)"
    if ! command -v gitleaks >/dev/null 2>&1; then
        fail "gitleaks not found on PATH. Install from https://github.com/gitleaks/gitleaks or set SKIP_GITLEAKS=1 to bypass (not recommended for launch)."
    fi
    gitleaks detect \
        --source . \
        --config .gitleaks.toml \
        --log-opts=--all \
        --redact \
        --verbose \
        || fail "gitleaks found leaks. Review output above, remediate, and re-run."
    ok "gitleaks clean."
fi

# ---------------------------------------------------------------------------
# Step 2 — full audit tool pass
# ---------------------------------------------------------------------------
if [[ "${SKIP_AUDIT:-0}" == "1" ]]; then
    skip "Step 2/3: audit tool (SKIP_AUDIT=1)"
else
    step "Step 2/3: audit tool (full pass, tiers 1-6)"
    if ! command -v python3 >/dev/null 2>&1; then
        fail "python3 not found on PATH."
    fi
    if [[ ! -f tools/audit/audit.py ]]; then
        fail "tools/audit/audit.py missing — is this the Vestige repo root?"
    fi
    # No --ci here: the launch sweep runs locally, we want the full
    # human-readable report, not CI annotations.
    python3 tools/audit/audit.py \
        || fail "Audit tool reported findings. Review docs/AUTOMATED_AUDIT_REPORT.md, remediate, and re-run."
    ok "Audit tool clean."
fi

# ---------------------------------------------------------------------------
# Step 3 — clean-slate CMake build + ctest
# ---------------------------------------------------------------------------
if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    skip "Step 3/3: clean build + ctest (SKIP_BUILD=1)"
else
    step "Step 3/3: clean-slate build + ctest (build dir: $build_dir, jobs: $jobs)"
    if [[ -d "$build_dir" ]]; then
        echo "Removing existing $build_dir ..."
        rm -rf "$build_dir"
    fi
    cmake -B "$build_dir" -S . -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        || fail "CMake configure failed."
    cmake --build "$build_dir" -j "$jobs" \
        || fail "Build failed."
    ctest --test-dir "$build_dir" --output-on-failure -j "$jobs" \
        || fail "ctest reported failures."
    ok "Build + tests clean."
fi

# ---------------------------------------------------------------------------
# Done.
# ---------------------------------------------------------------------------
echo ""
echo "${c_bold}${c_green}All pre-launch sweeps passed.${c_reset}"
echo ""
echo "Remaining §11 manual steps (not automatable):"
echo "  - Tag pre-release on Vestige AND VestigeAssets together"
echo "  - Flip both repos private→public in GitHub Settings"
echo "  - Verify fresh clone on a clean machine"
echo "  - Remove -DVESTIGE_FETCH_ASSETS=OFF from .github/workflows/ci.yml"
echo "  - Announce on the chosen channel"
