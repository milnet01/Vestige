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
# Default mode (LAUNCH_MODE, see below) treats the audit tool's findings
# as ADVISORY — the audit tool always returns non-zero when ANY findings
# exist, which is correct for a developer workflow ("eyeball every new
# finding") but wrong for a launch sweep whose job is "surface
# regressions since the last known-clean baseline." Step 2 aborts only
# on a genuine regression: the build broke, tests fail that didn't fail
# before, or CRITICAL/HIGH finding counts increased since the previous
# trend_snapshot_*.json.
#
# Steps 1 and 3 remain strict — a gitleaks hit OR a build/test failure
# is always a hard abort.
#
# Usage:
#   scripts/final_launch_sweep.sh
#
# Environment overrides:
#   SKIP_GITLEAKS=1   -- skip step 1 (e.g. if gitleaks isn't installed yet)
#   SKIP_AUDIT=1      -- skip step 2
#   SKIP_BUILD=1      -- skip step 3 (fastest iteration for re-running 1+2)
#   STRICT_MODE=1     -- restore the old "any audit finding fails" gate.
#                        Useful for "is everything clean from scratch?"
#                        experiments, but not how a launch sweep is
#                        supposed to run — the audit tool's baseline is
#                        thousands of advisory findings that require
#                        human review, not blanket remediation.
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

warn() {
    echo "${c_yellow}!${c_reset} $*" >&2
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
# Step 2 — audit tool pass (advisory by default, strict under STRICT_MODE=1)
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

    # Capture the set of trend_snapshot files BEFORE this run so we can
    # identify the prior baseline (anything newer is the snapshot this
    # run just wrote). We use an associative array of existing paths.
    declare -A prior_snapshots=()
    for f in docs/trend_snapshot_*.json; do
        [[ -e "$f" ]] || continue
        prior_snapshots["$f"]=1
    done

    # Run the audit. Capture its exit code; we'll classify it after we
    # inspect the snapshot diff in LAUNCH_MODE. The audit always prints
    # its own progress; no extra flags needed.
    set +e
    python3 tools/audit/audit.py
    audit_rc=$?
    set -e

    if [[ "${STRICT_MODE:-0}" == "1" ]]; then
        # Legacy "any-findings fails" behaviour. Used for "does the audit
        # return completely clean?" experiments, not launch sweeps.
        if [[ $audit_rc -ne 0 ]]; then
            fail "STRICT_MODE: audit tool reported findings (exit=$audit_rc). Review docs/AUTOMATED_AUDIT_REPORT.md, remediate, and re-run."
        fi
        ok "Audit tool clean (STRICT_MODE)."
    else
        # Launch-mode — compare this run's snapshot against the most
        # recent PRIOR snapshot and abort only on a real regression.
        current_snapshot=""
        for f in docs/trend_snapshot_*.json; do
            [[ -e "$f" ]] || continue
            if [[ -z "${prior_snapshots[$f]:-}" ]]; then
                current_snapshot="$f"
            fi
        done
        if [[ -z "$current_snapshot" ]]; then
            fail "Expected a fresh trend_snapshot_*.json from this audit run, found none. Check whether audit.py actually completed."
        fi

        # Find the most recent snapshot that existed BEFORE this run.
        prior_snapshot=""
        for f in $(ls -1t docs/trend_snapshot_*.json 2>/dev/null); do
            if [[ -n "${prior_snapshots[$f]:-}" ]]; then
                prior_snapshot="$f"
                break
            fi
        done

        # Classify the run. Any genuine regression → abort. Otherwise →
        # print a summary and continue.
        #
        # We pass the two snapshot paths into a small Python helper
        # embedded inline — keeps JSON parsing out of bash, which is
        # otherwise painful and brittle.
        # Pass ALL pre-run snapshot paths to the classifier so it can
        # find the most recent comparable one (same tier set as the
        # current run, detected via matching by_category key sets). Bash
        # can't cleanly pass an associative array, so serialize as
        # newline-separated paths on stdin after the current path.
        prior_list="$(printf '%s\n' "${!prior_snapshots[@]}" | sort -r)"
        classify_output="$(python3 - "$current_snapshot" <<PY_OUTER
import json, sys

cur_path = sys.argv[1]
prior_candidates = [line for line in """$prior_list""".splitlines() if line.strip()]

with open(cur_path) as fh:
    cur = json.load(fh)

def cat_keys(snap):
    return set((snap.get("by_category") or {}).keys())

# "Core" categories that always appear when Tiers 1-4 ran with any
# non-zero finding output. Other categories are "dynamic" — they
# only appear when the tier produced findings (e.g. "tests" shows
# up only when tests_failed > 0, "feature_coverage" only when thin
# coverage is detected). Comparing on core keys avoids spurious
# "incomparable" results between two full-audit runs that happen
# to differ in which dynamic buckets fired.
CORE_TIER_CATEGORIES = {
    "clang_tidy", "cppcheck",         # Tier 1
    "complexity", "duplication",      # Tier 4
    "dead_code", "refactoring",       # Tier 4
    "cognitive_complexity",           # Tier 4
}

def snapshots_comparable(a, b):
    a_core = cat_keys(a) & CORE_TIER_CATEGORIES
    b_core = cat_keys(b) & CORE_TIER_CATEGORIES
    # Must share at least 5 of 7 core categories. Allows for occasional
    # tier-specific zero-finding runs without false-negatives.
    return len(a_core & b_core) >= 5

prior = None
prior_path = None
for cand_path in prior_candidates:
    try:
        with open(cand_path) as fh:
            cand = json.load(fh)
    except (OSError, json.JSONDecodeError):
        continue
    if snapshots_comparable(cur, cand):
        prior = cand
        prior_path = cand_path
        break

regressions = []
advisories = []

# Build health — any regression is a hard abort. Universal across
# tier sets, so we gate even without a comparable prior.
if not cur.get("build_ok", True):
    regressions.append(
        f"Build broke: warnings={cur.get('build_warnings', '?')}, "
        f"errors={cur.get('build_errors', '?')}"
    )

# Tests — regression is strictly "more failures than before." Also
# universal, so we gate regardless of prior comparability; if no
# prior exists we compare against 0.
cur_tests_failed = int(cur.get("tests_failed", 0))
prior_tests_failed = int(prior.get("tests_failed", 0)) if prior else 0
if cur_tests_failed > prior_tests_failed:
    regressions.append(
        f"Test regressions: {prior_tests_failed} -> {cur_tests_failed} failed "
        f"(+{cur_tests_failed - prior_tests_failed})."
    )
elif cur_tests_failed > 0:
    advisories.append(
        f"{cur_tests_failed} test(s) failing (matches baseline of "
        f"{prior_tests_failed}). Investigate but not a launch blocker."
    )

# Findings by severity — only meaningful against a comparable prior.
# Finding counts from different tier sets can't be directly compared
# (a -t 1 CI snapshot has ~no clang-tidy/cppcheck findings; a full
# 1-6 local run has thousands). If no comparable prior exists, the
# current counts are themselves the new baseline.
by_sev_cur = cur.get("by_severity", {})
if prior:
    by_sev_prior = prior.get("by_severity", {})
    for sev in ("critical", "high"):
        c = int(by_sev_cur.get(sev, 0))
        p = int(by_sev_prior.get(sev, 0))
        if c > p:
            regressions.append(
                f"{sev.upper()} findings: {p} -> {c} (+{c - p} since prior snapshot)."
            )

# Always report the absolute counts so a human can eyeball the baseline.
sev_line = ", ".join(
    f"{k}={by_sev_cur.get(k, 0)}" for k in ("critical", "high", "medium", "low", "info")
)
advisories.append(f"Current finding counts -- {sev_line}")
advisories.append(f"Total findings: {cur.get('finding_count', '?')}")
if prior:
    advisories.append(
        f"Compared against prior snapshot: {prior.get('timestamp', '?')} "
        f"({prior_path})"
    )
else:
    n_prior = len(prior_candidates)
    if n_prior:
        advisories.append(
            f"No comparable prior snapshot found (checked {n_prior}; "
            f"none ran the same tier set). Finding-count gate skipped; "
            f"this run IS the new baseline for future comparisons."
        )
    else:
        advisories.append(
            "No prior snapshot found -- this run IS the baseline."
        )

result = {
    "regressions": regressions,
    "advisories": advisories,
    "has_regression": bool(regressions),
}
print(json.dumps(result))
PY_OUTER
)"

        # Sanity-check the classifier produced output. Empty output
        # means the embedded Python crashed (bash heredoc expansion
        # issue, bad input, etc.); surface the crash rather than
        # letting downstream JSON parsing fail with a confusing trace.
        if [[ -z "$classify_output" ]]; then
            fail "Launch-mode classifier produced no output. Most likely the embedded Python crashed — re-run with 'bash -x scripts/final_launch_sweep.sh' to see the interpolated heredoc, or set STRICT_MODE=1 to bypass classification and gate on audit exit code directly."
        fi

        # Pretty-print the classification lines (reads JSON from stdin
        # via `python3 -c` so nothing in the classifier output can
        # confuse bash quoting. `python3 -` with a heredoc does NOT
        # work here — the heredoc replaces stdin, so json.load(stdin)
        # reads EOF, not the piped JSON.)
        echo ""
        echo "${c_bold}Audit launch-mode classification:${c_reset}"
        printf '%s' "$classify_output" | python3 -c '
import json, sys
data = json.load(sys.stdin)
for msg in data["advisories"]:
    print(f"  . {msg}")
for msg in data["regressions"]:
    print(f"  X REGRESSION: {msg}")
'

        # Extract the pass/fail decision as a separate small invocation.
        has_regression="$(printf '%s' "$classify_output" \
            | python3 -c 'import json,sys; print(json.load(sys.stdin)["has_regression"])')"

        if [[ "$has_regression" == "True" ]]; then
            fail "Audit tool detected regressions vs previous snapshot. See docs/AUTOMATED_AUDIT_REPORT.md and the classification above."
        fi

        # audit_rc != 0 in launch-mode is expected (baseline findings
        # exist). Surface it so it doesn't silently hide a genuine crash.
        if [[ $audit_rc -ne 0 ]]; then
            ok "Audit tool: no regressions vs comparable prior snapshot. Findings exist (exit=$audit_rc) but are baseline — human review recommended via docs/AUTOMATED_AUDIT_REPORT.md before go-live."
        else
            ok "Audit tool clean (no findings)."
        fi
    fi
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
