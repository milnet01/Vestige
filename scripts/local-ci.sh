#!/usr/bin/env bash
# scripts/local-ci.sh — run the GitHub Actions CI pipeline locally before pushing.
#
# Mirrors .github/workflows/ci.yml so a clean local run means a green push,
# turning the manual "build + ctest before pushing" discipline into one command.
#
# Stages (each maps to a CI job in ci.yml):
#   1. Debug   build + ctest   ← linux-build-test (Debug)
#   2. Release build + ctest   ← linux-build-test (Release)  [runs the Release-only
#                                  perf benchmarks that are skipped in Debug]
#   3. Tier-1 static audit      ← audit-tool-tier1  (cppcheck + clang-tidy + warnings)
#   4. gitleaks secret scan     ← secret-scan       (full git history)
#
# NOT mirrored: the cmake-compat job (CMake 3.21 + latest) and the Windows/MSVC
# build. cmake-compat guards downstream users on old CMake; reproducing it needs
# a second CMake install, more than a pre-push gate warrants. Windows needs a
# Windows host. Both still run in CI on every push, so the gap is covered
# remotely. If you ever need cmake-compat locally, install cmake 3.21 in a
# venv/container and run ci.yml's cmake-compat Configure+Build+Test by hand.
#
# clang-tidy IS part of the Tier-1 audit and its `error`-level findings GATE CI
# (Severity.HIGH). The audit silently DISABLES clang-tidy when no `clang-tidy` is
# on PATH — so a missing binary makes the local audit a false-green (it passes
# while CI's clang-tidy reddens the push). The preflight below self-heals a
# versioned `clang-tidy-NN` into a repo-local shim and, failing that, WARNS
# loudly. Install it if missing (openSUSE: `zypper in clang-tools`).
#
# VERSION-DRIFT CAVEAT: local static-analysis tools are usually newer than CI's
# ubuntu-24.04 apt versions (CI: cppcheck ~2.13, clang-tidy ~18; a dev box often
# runs 2.21 / 22). Findings are version-sensitive, so a clean local audit is a
# strong signal but not a guarantee for every version-specific finding. CI gates
# on HIGH/CRITICAL only, which is stable in practice. The preflight prints both
# so drift is visible. (A Rule-5a follow-up could pin CI to the latest tools to
# close the drift entirely — tracked separately.)
#
# Usage:
#   scripts/local-ci.sh            # full mirror (all 4 stages) — the push-safety gate
#   scripts/local-ci.sh --quick    # Debug build+test + gitleaks only (fast smoke)
#   scripts/local-ci.sh -j 8        # cap parallel build/test jobs (default: nproc)
#   scripts/local-ci.sh -h          # this help
#
# Exit code: 0 if every run stage passed, 1 otherwise. The summary at the end
# lists each stage's result and timing so a failing push shows everything to fix
# in one pass (stages run independently; a build failure skips only its own
# test step, not the other stages).

set -uo pipefail   # deliberately NOT -e: we run all stages and aggregate results

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT" || exit 1

# --- argument parsing -------------------------------------------------------
QUICK=0
JOBS="$(nproc)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)   QUICK=1; shift ;;
        -j)        JOBS="${2:?-j needs a number}"; shift 2 ;;
        -h|--help) awk 'NR>1 && /^#/{sub(/^# ?/,""); print; next} NR>1{exit}' "$0"; exit 0 ;;
        *) echo "unknown argument: $1 (try -h)" >&2; exit 2 ;;
    esac
done

# --- CI parity knobs --------------------------------------------------------
# Match ci.yml's env: lets ccache cache PCH-using translation units (a PCH bakes
# in preprocessor defines and can capture __DATE__/__TIME__; without these,
# every PCH TU is treated as uncacheable and warm builds go cold).
export CCACHE_SLOPPINESS=pch_defines,time_macros

# CI runs headless under xvfb so GL-touching tests have a display. Locally we
# usually have a real one — wrap with xvfb-run only if it exists, else rely on
# the live DISPLAY/WAYLAND session, and warn if neither is available.
if command -v xvfb-run >/dev/null 2>&1; then
    GL_WRAP=(xvfb-run --auto-servernum)
elif [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    GL_WRAP=()
else
    echo "WARNING: no xvfb-run and no DISPLAY/WAYLAND_DISPLAY — GL tests may fail." >&2
    GL_WRAP=()
fi

# --- result tracking --------------------------------------------------------
STAGE_NAMES=()
STAGE_RESULTS=()
STAGE_TIMES=()

record() {  # record <name> <ok|fail|skip> <seconds>
    STAGE_NAMES+=("$1"); STAGE_RESULTS+=("$2"); STAGE_TIMES+=("$3")
}

hr()    { printf '%s\n' "------------------------------------------------------------"; }
banner(){ hr; printf '>>> %s\n' "$1"; hr; }

# --- preflight: tool presence + version parity report -----------------------
# Guards against the false-green failure mode: a static-analysis tool missing
# locally → the audit silently skips it → local passes while CI fails.
CLANG_TIDY_OK=1

# Self-heal a missing plain `clang-tidy` from the highest-versioned
# `clang-tidy-NN` on the system (openSUSE ships only versioned binaries). The
# shim lives in a gitignored repo-local dir prepended to PATH for this run only.
ensure_clang_tidy() {
    command -v clang-tidy >/dev/null 2>&1 && return 0
    local best="" cand n newest=-1 resolved
    for cand in /usr/bin/clang-tidy-* $(compgen -c clang-tidy- 2>/dev/null); do
        n="${cand##*clang-tidy-}"; [[ "$n" =~ ^[0-9]+$ ]] || continue
        resolved="$(command -v "$cand" 2>/dev/null || { [[ -x "$cand" ]] && echo "$cand"; })"
        [[ -n "$resolved" ]] || continue
        if (( n > newest )); then newest="$n"; best="$resolved"; fi
    done
    [[ -n "$best" ]] || return 1
    mkdir -p "$REPO_ROOT/.ci-tools/bin"
    ln -sf "$best" "$REPO_ROOT/.ci-tools/bin/clang-tidy"
    export PATH="$REPO_ROOT/.ci-tools/bin:$PATH"
    command -v clang-tidy >/dev/null 2>&1
}

ver_line() {  # ver_line <label> <cmd...> -- prints "  label  <first version-ish line>"
    local label="$1"; shift
    if command -v "$1" >/dev/null 2>&1; then
        printf '  %-11s %s\n' "$label" "$("$@" 2>&1 | grep -iE 'version|[0-9]+\.[0-9]+' | head -1 | sed 's/^ *//')"
    else
        printf '  %-11s %s\n' "$label" "!! MISSING"
    fi
}

preflight() {
    banner "preflight — local tool versions (CI = ubuntu-24.04 apt: cppcheck ~2.13, clang-tidy ~18)"
    ensure_clang_tidy || CLANG_TIDY_OK=0
    ver_line "cmake"      cmake --version
    ver_line "ninja"      ninja --version
    ver_line "cppcheck"   cppcheck --version
    ver_line "clang-tidy" clang-tidy --version
    command -v gitleaks >/dev/null 2>&1 && ver_line "gitleaks" gitleaks version || printf '  %-11s %s\n' "gitleaks" "!! MISSING (secret-scan stage will SKIP)"
    if [[ $CLANG_TIDY_OK -eq 0 ]]; then
        echo
        echo "  !! clang-tidy NOT found — the Tier-1 audit will SKIP it. clang-tidy"
        echo "  !! 'error' findings GATE CI, so this local run is NOT a full mirror."
        echo "  !! Install it (openSUSE: zypper in clang-tools) before trusting a green."
    fi
}

# Configure a build dir with the exact flags from ci.yml's Configure step.
configure() {  # configure <dir> <build-type>
    cmake -S . -B "$1" -G Ninja \
        -DCMAKE_BUILD_TYPE="$2" \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DVESTIGE_FETCH_ASSETS=OFF
}

# Build + test one configuration. Reuses an existing build dir (warm/incremental);
# ccache makes a fresh dir nearly as fast at the object level.
build_and_test() {  # build_and_test <stage-label> <dir> <build-type>
    local label="$1" dir="$2" cfg="$3" start=$SECONDS
    banner "$label — configure + build + test"
    if ! configure "$dir" "$cfg"; then
        record "$label" fail $((SECONDS - start)); return 1
    fi
    if ! cmake --build "$dir" -j "$JOBS"; then
        record "$label" fail $((SECONDS - start)); return 1
    fi
    # --output-on-failure -j matches ci.yml's Run tests step.
    if ! "${GL_WRAP[@]}" ctest --test-dir "$dir" --output-on-failure -j "$JOBS"; then
        record "$label" fail $((SECONDS - start)); return 1
    fi
    record "$label" ok $((SECONDS - start)); return 0
}

# --- preflight: report versions + self-heal clang-tidy ----------------------
preflight

# --- stage 1: Debug build + test (reuses the dev build/ dir) ----------------
build_and_test "Debug build+test" build Debug || true

# --- stage 2: Release build + test (own dir; runs Release-only perf gates) --
if [[ $QUICK -eq 0 ]]; then
    build_and_test "Release build+test" build-release Release || true
else
    record "Release build+test" skip 0
fi

# --- stage 3: Tier-1 static audit -------------------------------------------
# Reuses the Debug build/ dir (audit's internal `cmake --build build` is a warm
# no-op when build/ is already built). Flags match ci.yml: --ci sets the exit
# code by severity, --no-color keeps logs clean, --no-tests skips the redundant
# ctest pass (stage 1 already ran it against the same build).
if [[ $QUICK -eq 0 ]]; then
    start=$SECONDS
    banner "Tier-1 audit — cppcheck + clang-tidy + build warnings"
    # Flag a partial mirror in the stage name so a green summary can't be
    # mistaken for full parity when clang-tidy was unavailable.
    audit_label="Tier-1 audit"; [[ $CLANG_TIDY_OK -eq 0 ]] && audit_label="Tier-1 audit(no-tidy)"
    if "${GL_WRAP[@]}" python3 tools/audit/audit.py -t 1 --ci --no-color --no-tests; then
        record "$audit_label" ok $((SECONDS - start))
    else
        record "$audit_label" fail $((SECONDS - start))
    fi
else
    record "Tier-1 audit" skip 0
fi

# --- stage 4: gitleaks secret scan ------------------------------------------
start=$SECONDS
banner "gitleaks — secret scan (full git history)"
if command -v gitleaks >/dev/null 2>&1; then
    if gitleaks detect --source . --config .gitleaks.toml --redact --exit-code 1; then
        record "gitleaks" ok $((SECONDS - start))
    else
        record "gitleaks" fail $((SECONDS - start))
    fi
else
    echo "gitleaks not found on PATH — install it to mirror the secret-scan job." >&2
    record "gitleaks" skip 0
fi

# --- summary ----------------------------------------------------------------
echo
banner "local-ci summary"
fails=0
for i in "${!STAGE_NAMES[@]}"; do
    case "${STAGE_RESULTS[$i]}" in
        ok)   mark="PASS" ;;
        fail) mark="FAIL"; fails=$((fails + 1)) ;;
        skip) mark="SKIP" ;;
    esac
    printf '  %-22s %-4s  %3ds\n' "${STAGE_NAMES[$i]}" "$mark" "${STAGE_TIMES[$i]}"
done
hr
if [[ $fails -eq 0 ]]; then
    echo "All run stages passed — safe to push."
    exit 0
else
    echo "$fails stage(s) FAILED — fix before pushing (CI would reject this)."
    exit 1
fi
