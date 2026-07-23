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
#   3. Windows MSVC build+test  ← windows-build-test  [OPT-IN via --windows]
#   4. Tier-1 static audit      ← audit-tool-tier1  (cppcheck + clang-tidy + warnings)
#   5. gitleaks secret scan     ← secret-scan       (full git history)
#
# Stage 3 (Windows/MSVC) is OPT-IN via --windows: it cross-compiles the engine with
# the REAL MSVC toolchain (cl.exe + Windows SDK) under Wine via msvc-wine, then runs
# the compiled gtest suite under Wine — the same compiler ci.yml's windows-build-test
# job uses, so MSVC-only breakage (localtime_r, M_PI, non-noexcept-move-in-vector)
# surfaces before the push instead of 15 min into CI. It's opt-in because it's a
# separate cold build with a different compiler (no ccache-object sharing with the
# Linux stages) that runs the tests through Wine — several extra minutes. Toolchain
# path: $MSVC_WINE_BIN (default ~/tools/msvc-wine/msvc/bin/x64); override to relocate.
# Only the compiled engine suite (vestige_tests) runs under Wine — the host-Python
# data-lint tests (localization/shader lint) are excluded there because the Wine
# cross-compile emulator can't run host Python and mangles their exit codes; those
# already run natively in stages 1-2 and don't exercise MSVC-compiled code.
#
# NOT mirrored: the cmake-compat job (CMake 3.21 + latest). It guards downstream
# users on old CMake; reproducing it needs a second CMake install, more than a
# pre-push gate warrants. It still runs in CI on every push, so the gap is covered
# remotely. If you ever need it locally, install cmake 3.21 in a venv/container and
# run ci.yml's cmake-compat Configure+Build+Test by hand.
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
#   scripts/local-ci.sh             # FULL mirror (Linux + Windows/MSVC + audit +
#                                   # secrets) — the push gate. Windows runs by
#                                   # default so a Windows-only break is caught
#                                   # locally; it SKIPs (→ "not push-verified")
#                                   # only when msvc-wine/wine is absent.
#   scripts/local-ci.sh --no-windows # skip the Windows MSVC stage (Linux-only;
#                                   # reports PARTIAL — not push-verified)
#   scripts/local-ci.sh --quick     # Debug build+test + gitleaks only (fast smoke,
#                                   # NOT push-safe: skips the Tier-1 audit + Windows)
#   scripts/local-ci.sh -j 8        # cap parallel build/test jobs (default: nproc)
#   scripts/local-ci.sh -h          # this help
#
# Exit code: 0 if every run stage passed, 1 if any FAILED. The closing line only
# reads "safe to push" after a FULL mirror (no SKIPped stage, clang-tidy present)
# — a --quick or partial run reports "PARTIAL mirror — NOT push-verified" at
# exit 0 so a smoke green can't be mistaken for the CI push gate. The summary
# lists each stage's result and timing so a failing push shows everything to fix
# in one pass (stages run independently; a build failure skips only its own
# test step, not the other stages).

set -uo pipefail   # deliberately NOT -e: we run all stages and aggregate results

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT" || exit 1

# --- argument parsing -------------------------------------------------------
QUICK=0
# Windows/MSVC is a required CI job, so a full pre-push mirror runs it by default —
# a Linux-only run once let a Windows-only break (a leaked file handle → a
# `remove_all` fault MSVC/Windows rejects but Linux tolerates) reach main. It SKIPs
# cleanly when msvc-wine/wine is absent (→ PARTIAL, not push-verified); --no-windows
# opts out explicitly; --quick drops it for a fast smoke.
WINDOWS=1
JOBS="$(nproc)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)      QUICK=1; WINDOWS=0; shift ;;
        --windows)    WINDOWS=1; shift ;;   # explicit-on (default); kept for muscle memory
        --no-windows) WINDOWS=0; shift ;;
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

# RENDERER PARITY WITH CI (critical for catching GPU-test failures before push).
# GitHub's runners have no GPU, so Mesa falls back to the llvmpipe *software*
# rasterizer. A dev box with a real GPU (radeonsi/NVIDIA) renders GL tests on
# hardware instead — and software vs hardware float precision differs enough that
# a tight GPU-parity tolerance can pass locally yet fail on CI (e.g. the terrain
# GGX parity test: <0.1% drift on radeonsi, ~0.23% on llvmpipe). Forcing software
# rendering here makes local ctest use the *same* renderer as CI, so those
# divergences surface locally. (xvfb alone is NOT enough — on a GPU box Mesa still
# picks the hardware driver under xvfb; the renderer, not the display, is what
# must match.)
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe

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

# --- Windows/MSVC stage (opt-in) --------------------------------------------
# Real MSVC toolchain (cl.exe + Windows SDK) under Wine via msvc-wine. Override
# MSVC_WINE_BIN to relocate the install.
MSVC_WINE_BIN="${MSVC_WINE_BIN:-$HOME/tools/msvc-wine/msvc/bin/x64}"

# Cross-compile the engine with MSVC under Wine and run the compiled gtest suite —
# mirrors ci.yml's windows-build-test. Ninja + cl (no VS generator on Linux);
# CMAKE_SYSTEM_NAME=Windows cross-compiles; the .exe tests run through Wine via
# CMAKE_CROSSCOMPILING_EMULATOR. GL tests self-skip (no GL 4.5 context under Wine),
# same as the GH windows-2022 runner.
build_and_test_msvc() {
    local label="Windows MSVC build+test" start=$SECONDS
    banner "$label — msvc-wine (real MSVC cl.exe under Wine)"
    if [[ ! -x "$MSVC_WINE_BIN/cl" ]]; then
        echo "  msvc-wine not found at $MSVC_WINE_BIN — install it or set MSVC_WINE_BIN."
        record "$label" skip 0; return 0
    fi
    if ! command -v wine >/dev/null 2>&1; then
        echo "  wine not on PATH — needed to run the MSVC test .exe binaries."
        record "$label" skip 0; return 0
    fi
    local msvc_root="${MSVC_WINE_BIN%/bin/x64}"
    # Subshell scopes the MSVC toolchain + cross-compile env so it can't poison the
    # Linux stages' PATH / compiler selection.
    (
        export PATH="$MSVC_WINE_BIN:$PATH" CC=cl CXX=cl
        # Cross-compile hygiene: empty pkg-config registry so host Linux libs (e.g.
        # PipeWire's spa headers → unistd.h) can't leak into the Windows-target build.
        # A native Windows build has none of these; this reproduces that.
        mkdir -p "$REPO_ROOT/.ci-tools/empty-pkgconfig"
        export PKG_CONFIG_LIBDIR="$REPO_ROOT/.ci-tools/empty-pkgconfig" PKG_CONFIG_PATH=""
        # -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded (+CMP0141 NEW): avoid the
        # separate-PDB compiler probe msvc-wine can't do (README "Does it work with
        # CMake?"); keep debug info in the .obj.
        cmake -S . -B build-msvc -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_SYSTEM_NAME=Windows \
            -DCMAKE_POLICY_DEFAULT_CMP0141=NEW \
            -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded \
            -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
            -DVESTIGE_FETCH_ASSETS=OFF || exit 1
        cmake --build build-msvc -j "$JOBS" || exit 1
        # Co-locate the MSVC runtime redist (vcruntime140/msvcp140/...) beside the
        # test .exe so Wine resolves them — WINEPATH alone doesn't chain the
        # inter-DLL deps. Same DLLs vc_redist installs beside a shipped app.
        cp "$msvc_root"/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll build-msvc/bin/ 2>/dev/null || true
        # Only the compiled engine suite runs under Wine; -E excludes the host-Python
        # tests — the Wine emulator mangles their exit code (a passing host-python3
        # test exits 0 but Wine reports non-zero → false ctest failure). They run
        # natively in the Linux stages and don't exercise MSVC-compiled code, so
        # excluding them loses no signal. ANY new host-Python ctest test must be added
        # here (PerfGate joined LocalizationAudit/ShaderLint when 3D_E-0030 landed).
        "${GL_WRAP[@]}" ctest --test-dir build-msvc --output-on-failure -j "$JOBS" \
            -E 'LocalizationAudit|ShaderLint|PerfGate' || exit 1
    )
    local rc=$?
    if [[ $rc -eq 0 ]]; then record "$label" ok $((SECONDS - start)); else record "$label" fail $((SECONDS - start)); fi
    return $rc
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

# --- stage 3: Windows MSVC build + test via msvc-wine (default; --no-windows off) ---
# Runs by default so a Windows-only break is caught before push. When disabled it is
# recorded as SKIP so the summary reports PARTIAL (Windows is a real CI gate — a run
# without it is not push-verified).
if [[ $WINDOWS -eq 1 ]]; then
    build_and_test_msvc || true
else
    record "Windows MSVC build+test" skip 0
fi

# --- stage 4: Tier-1 static audit -------------------------------------------
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

# --- stage 5: gitleaks secret scan ------------------------------------------
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
skipped_stages=()
for i in "${!STAGE_NAMES[@]}"; do
    case "${STAGE_RESULTS[$i]}" in
        ok)   mark="PASS" ;;
        fail) mark="FAIL"; fails=$((fails + 1)) ;;
        skip) mark="SKIP"; skipped_stages+=("${STAGE_NAMES[$i]}") ;;
    esac
    printf '  %-22s %-4s  %3ds\n' "${STAGE_NAMES[$i]}" "$mark" "${STAGE_TIMES[$i]}"
done
hr
if [[ $fails -gt 0 ]]; then
    echo "$fails stage(s) FAILED — fix before pushing (CI would reject this)."
    exit 1
fi

# "Safe to push" is earned ONLY by a full mirror of the CI push gates. A --quick
# run SKIPs the Tier-1 audit (the cppcheck/clang-tidy gate CI enforces) and the
# Release build; a missing clang-tidy makes the audit partial. Any of those means
# a green here does NOT imply a green in CI — the exact trap that let three
# containerOutOfBounds pushes through. Surface it instead of claiming safety.
if [[ ${#skipped_stages[@]} -gt 0 || $CLANG_TIDY_OK -eq 0 ]]; then
    echo "Ran stages passed, but this is a PARTIAL mirror — NOT push-verified."
    [[ ${#skipped_stages[@]} -gt 0 ]] && echo "  skipped: ${skipped_stages[*]}"
    [[ $CLANG_TIDY_OK -eq 0 ]] && echo "  clang-tidy unavailable — audit ran without it."
    echo "  Run ./scripts/local-ci.sh (no --quick, full toolchain) before pushing."
    exit 0
fi
# Reaching here means no stage FAILED and none were SKIPped — so Windows/MSVC
# actually ran and passed (a skip would have taken the PARTIAL branch above).
echo "Full CI mirror incl. Windows/MSVC passed — safe to push. (cmake-compat is still CI-only.)"
exit 0
