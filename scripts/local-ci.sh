#!/usr/bin/env bash
# scripts/local-ci.sh — run the GitHub Actions CI pipeline locally before pushing.
#
# Mirrors .github/workflows/ci.yml so a clean local run means a green push,
# turning the manual "build + ctest before pushing" discipline into one command.
#
# Stages (each maps to a CI job in ci.yml):
#   1. Debug   build + ctest   ← linux-build-test (Debug)          [container]
#   2. Release build + ctest   ← linux-build-test (Release)        [container]
#   3. Windows MSVC build+test  ← windows-build-test  (msvc-wine, host)
#   4. Tier-1 static audit      ← audit-tool-tier1  (cppcheck + clang-tidy + warnings) [container]
#   5. gitleaks secret scan     ← secret-scan.yml   (full git history)
#   6. actionlint workflow lint ← workflow-lint     (schema + shellcheck over run: blocks)
#   7. CMake compat, 3.21.0 and latest ← cmake-compat's two matrix legs     [container]
#
# [container] = run in the Ubuntu 24.04 image scripts/ci-container/Containerfile
# builds: GitHub's runner OS and its compiler, CMake, cppcheck and clang-tidy
# versions, with ci.yml's own apt packages. This machine's newer toolchain (GCC
# 16, mold) passed a change GitHub's failed on 2026-09-28; the container is how a
# local pass now means the same tools passed. "The container" section below.
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
# Stage 7 mirrors both cmake-compat matrix legs. `3.21.0` is the engine's declared
# `cmake_minimum_required`, the leg that actually catches regressions (FetchContent
# and policy semantics have tightened before; see CMP0169); `latest` is the newest
# CMake release, resolved as actions-setup-cmake does. Each binary is fetched into
# .ci-tools/ on first use, checksum-verified, and runs that job's Configure+Build+
# Test verbatim in the container. A leg SKIPs (→ "not push-verified") offline, and
# both are N/A on a push that changes no build file (3D_E-0708).
#
# Usage:
#   scripts/local-ci.sh             # FULL mirror (Linux + Windows/MSVC + audit +
#                                   # secrets) — the push gate. Windows runs by
#                                   # default so a Windows-only break is caught
#                                   # locally; it SKIPs (→ "not push-verified")
#                                   # only when msvc-wine/wine is absent.
#   scripts/local-ci.sh --no-windows # skip the Windows MSVC stage (Linux-only;
#                                   # reports PARTIAL — not push-verified)
#   scripts/local-ci.sh --no-cmake-compat # skip the CMake 3.21.0 stage (reports
#                                   # PARTIAL — not push-verified)
#   scripts/local-ci.sh --quick     # Debug build+test + gitleaks only (fast smoke,
#                                   # NOT push-safe: skips the Tier-1 audit + Windows)
#   scripts/local-ci.sh --docs      # documentation-only push: the gitleaks stage
#                                   # alone: secret-scan.yml, the one workflow that
#                                   # reads doc paths; builds, tests, the audit,
#                                   # actionlint and cmake-compat read none of them.
#                                   # .githooks/pre-push selects it (.ants/gate.conf).
#   scripts/local-ci.sh -j 8        # cap parallel build/test jobs (default: nproc)
#
# Every ctest call here passes -LE perf: the wall-clock budget tests are timed
# against the machine and flake on a loaded box, so they run in ci.yml's nightly
# schedule instead (3D_E-0714). Run them by hand with
#   ctest --test-dir build-release -L perf --output-on-failure
#   scripts/local-ci.sh -h          # this help
#
# Exit codes: 0 = FULL mirror passed (safe to push), 1 = a stage FAILED,
# 2 = every run stage passed but the mirror was PARTIAL (a stage SKIPped, or
# clang-tidy was missing) — "NOT push-verified". 2 is distinct from 0 so a caller
# can tell a real green from a smoke green without parsing stdout; .githooks/pre-push
# uses it to warn rather than claim the push was verified. Anything non-zero from a
# `--quick` run is expected — that mode deliberately skips the push gates. The summary
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
# cmake-compat is a required CI job too, so the full mirror runs its 3.21.0 leg by
# default. It SKIPs cleanly (→ PARTIAL) when the pinned toolchain can't be fetched.
CMAKE_COMPAT=1
JOBS="$(nproc)"
DOCS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)      QUICK=1; WINDOWS=0; CMAKE_COMPAT=0; shift ;;
        --docs)       DOCS=1; shift ;;
        --windows)    WINDOWS=1; shift ;;   # explicit-on (default); kept for muscle memory
        --no-windows) WINDOWS=0; shift ;;
        --no-cmake-compat) CMAKE_COMPAT=0; shift ;;
        -j)        JOBS="${2:?-j needs a number}"; shift 2 ;;
        -h|--help) awk 'NR>1 && /^#/{sub(/^# ?/,""); print; next} NR>1{exit}' "$0"; exit 0 ;;
        *) echo "unknown argument: $1 (try -h)" >&2; exit 2 ;;
    esac
done

# --- documentation mode -----------------------------------------------------
# Runs secret-scan.yml's job and nothing else, because it is the only job that
# reads documentation paths. Fails closed: with no gitleaks nothing was checked,
# so exit 2 (PARTIAL), which the push hook treats as a refusal.
if [[ $DOCS -eq 1 ]]; then
    if ! command -v gitleaks >/dev/null 2>&1; then
        echo "local-ci --docs: gitleaks not found — nothing was checked (PARTIAL)." >&2
        exit 2
    fi
    echo ">>> local-ci --docs: gitleaks secret scan (full git history)"
    gitleaks detect --source . --config .gitleaks.toml --redact --exit-code 1 || exit 1
    echo "local-ci --docs: documentation checks passed."
    exit 0
fi

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

# --- the Ubuntu 24.04 CI container -----------------------------------------
# Every Linux stage (1 Debug, 2 Release, 4 Tier-1 audit, 7 CMake compat) runs in
# the image scripts/ci-container/Containerfile builds: GitHub's runner OS with its
# compiler (GCC 13.3), CMake (3.31.6), cppcheck (2.13), clang-tidy (18) and
# ci.yml's own apt packages. On 2026-09-28 this machine's GCC 16 and mold passed
# a change Ubuntu's toolchain failed, so the local run now builds where CI builds.
# Each stage's tree is bind-mounted at build/, so the commands are ci.yml's,
# verbatim. The trees and the compiler cache live OUTSIDE the repository
# ($VESTIGE_CI_DIR), so they never dirty it and the audit never scans them.
# Stage 3 (Windows) stays on the host: msvc-wine is the real MSVC compiler.
CI_DIR="${VESTIGE_CI_DIR:-$(dirname "$REPO_ROOT")/.vestige-ci}"
CI_IMAGE=""
CI_IMAGE_OK=0

# Build the image when its recipe or ci.yml's package list changed (the tag is a
# hash of both). An image build is a few minutes of apt, not a cold compile.
ensure_ci_image() {
    command -v podman >/dev/null 2>&1 || { echo "  podman not found — the Linux stages cannot run." >&2; return 1; }
    local pkgs tag
    pkgs="$(python3 tools/ci_apt_packages.py)" || return 1
    tag="$( { cat scripts/ci-container/Containerfile; printf '%s\n' "$pkgs"; } | sha256sum | cut -c1-12)"
    CI_IMAGE="localhost/vestige-ci-ubuntu24:$tag"
    podman image exists "$CI_IMAGE" && return 0
    echo "  building $CI_IMAGE (ci.yml's packages or the Containerfile changed) ..." >&2
    podman build --pull=missing --build-arg "APT_PACKAGES=$pkgs" -t "$CI_IMAGE" \
        -f scripts/ci-container/Containerfile scripts/ci-container >&2
}

# --init: without an init process, a single-command stage makes xvfb-run the
# container's PID 1, where it waits forever for Xvfb's ready signal (measured
# 2026-09-28: the audit stage sat idle 3 h). The timeout turns any other hang into
# a FAILED stage rather than a push that never ends; it is sized for a cold build.
CI_STAGE_TIMEOUT="${VESTIGE_CI_STAGE_TIMEOUT:-5400}"

# The container builds a COPY holding exactly the files git would carry: tracked
# files plus new ones git does not ignore, as they are in the working tree (so a
# hand run tests uncommitted edits too). Ignored local files stay out, as they
# are absent from GitHub's checkout: on 2026-09-28 the gitignored symlinks in
# assets/models/nature_local/ pointed outside the repo and broke copy_assets in
# the container. Files gone from the tree are removed from the copy. It is
# mounted at the repo's own path, so paths baked in at configure time match a
# host build's.
# The copy compares CONTENT (--checksum) and does not keep mtimes: a changed
# file is stamped with the time it is copied, an unchanged one is left alone.
# Keeping the working tree's mtime is wrong here: a header edited while a run
# is compiling is older than the objects that run writes, so ninja never
# rebuilds them and the next run links stale objects (3D_E-0732, two rejected
# pushes). scripts/test_ci_src_sync.sh pins both halves.
sync_ci_src() {
    local src="$CI_DIR/src" tmp
    tmp="$(mktemp -d)" || return 1
    mkdir -p "$src"
    git -C "$REPO_ROOT" ls-files -z -co --exclude-standard | sort -z > "$tmp/want"
    rsync -rlpgoD --checksum --from0 --ignore-missing-args --files-from="$tmp/want" "$REPO_ROOT/" "$src/" || { rm -rf "$tmp"; return 1; }
    ( cd "$src" && find . \( -type f -o -type l \) -print0 | sed -z 's|^\./||' | sort -z ) > "$tmp/have"
    comm -z -23 "$tmp/have" "$tmp/want" | ( cd "$src" && xargs -0 -r rm -f -- )
    rm -rf "$tmp"
}

in_ci() {  # in_ci <tree-name> <bash command> — run in the container, tree at build/
    mkdir -p "$CI_DIR/$1" "$CI_DIR/ccache"
    timeout -k 30 "$CI_STAGE_TIMEOUT" \
    podman run --rm --init --userns=keep-id --security-opt label=disable \
        -e HOME=/tmp -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=10G \
        -e CCACHE_SLOPPINESS="$CCACHE_SLOPPINESS" \
        -v "$CI_DIR/src:$REPO_ROOT" -v "$CI_DIR/$1:$REPO_ROOT/build" \
        -v "$REPO_ROOT/.ci-tools:$REPO_ROOT/.ci-tools:ro" \
        -v "$CI_DIR/ccache:/ccache" -w "$REPO_ROOT" \
        "$CI_IMAGE" bash -euo pipefail -c "$2"
}

# --- preflight: tool presence + version report ------------------------------
# Guards against the false-green failure mode: a tool missing → its stage
# silently does less → local passes while CI fails. The Linux stages' tools are
# the container's; the host still supplies msvc-wine, gitleaks and actionlint.
CLANG_TIDY_OK=1

ver_line() {  # ver_line <label> <cmd...> -- prints "  label  <first version-ish line>"
    local label="$1"; shift
    if command -v "$1" >/dev/null 2>&1; then
        printf '  %-11s %s\n' "$label" "$("$@" 2>&1 | grep -iE 'version|[0-9]+\.[0-9]+' | head -1 | sed 's/^ *//')"
    else
        printf '  %-11s %s\n' "$label" "!! MISSING"
    fi
}

preflight() {
    banner "preflight — tool versions (Linux stages: the Ubuntu 24.04 CI container)"
    mkdir -p "$REPO_ROOT/.ci-tools"
    if ensure_ci_image && scripts/test_ci_src_sync.sh && sync_ci_src; then
        CI_IMAGE_OK=1
        printf '  %-11s %s\n' "source" "$CI_DIR/src ($(git -C "$REPO_ROOT" ls-files -co --exclude-standard | wc -l) files git would carry)"
        printf '  %-11s %s\n' "image" "$CI_IMAGE"
        podman run --rm "$CI_IMAGE" sh -c '
            printf "  %-11s %s\n" c++ "$(c++ --version | head -1)"
            printf "  %-11s %s\n" cmake "$(cmake --version | head -1)"
            printf "  %-11s %s\n" cppcheck "$(cppcheck --version)"
            printf "  %-11s %s\n" clang-tidy "$(clang-tidy --version | grep -i version | head -1 | sed "s/^ *//")"
            printf "  %-11s %s\n" glslang "$(glslangValidator --version | head -1)"'
        podman run --rm "$CI_IMAGE" sh -c 'command -v clang-tidy' >/dev/null || CLANG_TIDY_OK=0
    else
        echo "  !! CI image or source copy unavailable — every Linux stage will SKIP (NOT push-verified)."
    fi
    command -v gitleaks >/dev/null 2>&1 && ver_line "gitleaks" gitleaks version || printf '  %-11s %s\n' "gitleaks" "!! MISSING (secret-scan stage will SKIP)"
    command -v actionlint >/dev/null 2>&1 && ver_line "actionlint" actionlint --version || printf '  %-11s %s\n' "actionlint" "!! MISSING (workflow-lint stage will SKIP)"
    # actionlint lints each `run:` block by shelling out to the shell linter;
    # without that tool actionlint still exits 0, so report it separately rather
    # than let a green actionlint imply the run: blocks were checked.
    # NB: never begin a comment line with that linter's name + a space — it is
    # parsed as a directive and errors out with SC1072/SC1073.
    command -v shellcheck >/dev/null 2>&1 && ver_line "shellcheck" shellcheck --version || printf '  %-11s %s\n' "shellcheck" "!! MISSING (actionlint would skip all run: blocks)"
}

# ci.yml's Configure step, verbatim.
ci_configure() {  # ci_configure <build-type> — prints the command
    printf '%s' "cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=$1 \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DVESTIGE_USE_MOLD=OFF \
        -DVESTIGE_FETCH_ASSETS=OFF \
        -DVESTIGE_REQUIRE_GLSLANG=ON"
}

# Build + test one configuration in the container: ci.yml's Configure, Build and
# Run tests steps. The tree persists in $CI_DIR, so a push rebuilds only what
# changed, and ccache covers a fresh tree.
build_and_test() {  # build_and_test <stage-label> <tree-name> <build-type>
    local label="$1" tree="$2" cfg="$3" start=$SECONDS
    banner "$label — configure + build + test (Ubuntu 24.04 container)"
    if [[ $CI_IMAGE_OK -eq 0 ]]; then
        record "$label" skip 0; return 0
    fi
    if in_ci "$tree" "$(ci_configure "$cfg")
cmake --build build -j $JOBS
xvfb-run --auto-servernum ctest --test-dir build --output-on-failure -j $JOBS -LE perf"; then
        record "$label" ok $((SECONDS - start)); return 0
    fi
    record "$label" fail $((SECONDS - start)); return 1
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
        # here (PerfGate joined LocalizationAudit/ShaderLint when 3D_E-0030 landed;
        # InputPollAudit joined when 3D_E-0628 landed; ShaderCompile when 3D_E-0638
        # landed).
        #
        # The failure is inverted and therefore easy to misread: a host-Python test
        # that PASSES (exit 0) is reported ***Failed under Wine, while a WILL_FAIL
        # one is reported Passed, because the mangled code happens to match its
        # inverted expectation. So a new audit's negative fixtures go green and only
        # its positive cases go red — measured on InputPollAudit, 3 green / 2 red.
        "${GL_WRAP[@]}" ctest --test-dir build-msvc --output-on-failure -j "$JOBS" -LE perf \
            -E 'LocalizationAudit|ShaderLint|ShaderCompile|PerfGate|InputPollAudit' || exit 1
    )
    local rc=$?
    if [[ $rc -eq 0 ]]; then record "$label" ok $((SECONDS - start)); else record "$label" fail $((SECONDS - start)); fi
    return $rc
}

# --- preflight: build/find the CI image, report versions --------------------
preflight

# --- stage 1: Debug build + test (container tree $CI_DIR/debug) -------------
build_and_test "Debug build+test" debug Debug || true

# --- stage 2: Release build + test (container tree $CI_DIR/release) ---------
if [[ $QUICK -eq 0 ]]; then
    build_and_test "Release build+test" release Release || true
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
# In the container, on stage 1's Debug tree (audit's internal `cmake --build
# build` is a warm no-op there), with CI's cppcheck and clang-tidy versions.
# Flags match ci.yml: --ci sets the exit
# code by severity, --no-color keeps logs clean, --no-tests skips the redundant
# ctest pass (stage 1 already ran it against the same build).
if [[ $QUICK -eq 0 ]]; then
    start=$SECONDS
    banner "Tier-1 audit — cppcheck + clang-tidy + build warnings (Ubuntu 24.04 container)"
    # Flag a partial mirror in the stage name so a green summary can't be
    # mistaken for full parity when clang-tidy was unavailable.
    audit_label="Tier-1 audit"; [[ $CLANG_TIDY_OK -eq 0 ]] && audit_label="Tier-1 audit(no-tidy)"
    if [[ $CI_IMAGE_OK -eq 0 ]]; then
        record "$audit_label" skip 0
    elif in_ci debug "xvfb-run --auto-servernum python3 tools/audit/audit.py -t 1 --ci --no-color --no-tests"; then
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

# --- stage 6: actionlint — GitHub Actions workflow lint ----------------------
# Mirrors ci.yml's `workflow-lint` job. actionlint checks workflow schema, expression
# syntax and action references, and shells each `run:` block out to shellcheck.
# That shell linter is therefore a HARD requirement, not a bonus: without it
# actionlint still exits 0 while silently skipping every `run:` block, which is a
# false pass of exactly the kind this script exists to prevent. Either tool
# missing → SKIP, which downgrades the run to PARTIAL / not push-verified.
start=$SECONDS
banner "actionlint — GitHub Actions workflow lint"
if ! command -v actionlint >/dev/null 2>&1; then
    echo "actionlint not found on PATH — install it to mirror the workflow-lint job." >&2
    echo "  (no distro package on openSUSE; grab the release binary from" >&2
    echo "   https://github.com/rhysd/actionlint/releases into ~/.local/bin)" >&2
    record "actionlint" skip 0
elif ! command -v shellcheck >/dev/null 2>&1; then
    echo "shellcheck not found — actionlint would silently skip every run: block," >&2
    echo "  reporting a green that CI will not reproduce. Install shellcheck." >&2
    record "actionlint" skip 0
elif actionlint; then
    record "actionlint" ok $((SECONDS - start))
else
    record "actionlint" fail $((SECONDS - start))
fi

# --- stage 7: CMake 3.21.0 compat build + test ------------------------------
# Mirrors ci.yml's `cmake-compat` job at its `3.21.0` matrix leg — the version in
# this repo's `cmake_minimum_required`. Downstream users on the declared minimum
# are exactly who this guards, and FetchContent/policy semantics have tightened
# under us before (CMP0169), so a configure that works on cmake 4.x says nothing
# about 3.21. The toolchain is a pinned, checksum-verified Kitware binary kept in
# .ci-tools/ (gitignored) and downloaded once; a box with no network SKIPs the
# stage rather than silently passing. The job's `latest` leg needs no separate
# stage — stage 2 already builds that same Release config with the host cmake.
CMAKE_COMPAT_VERSION="3.21.0"
CMAKE_COMPAT_SHA256="d54ef6909f519740bc85cec07ff54574cd1e061f9f17357d9ace69f61c6291ce"

# Print the path to CMake <version>'s cmake, fetching it into .ci-tools/ on first
# use, checksum-verified. Empty output + non-zero return means "unavailable" —
# the caller SKIPs. The binaries are static, so the container runs them too.
ensure_cmake() {  # ensure_cmake <version> <sha256>
    local version="$1" sha="$2"
    local root="$REPO_ROOT/.ci-tools/cmake-$version"
    local bin="$root/bin/cmake"
    if [[ -x "$bin" ]]; then printf '%s\n' "$bin"; return 0; fi

    local asset="cmake-$version-linux-x86_64.tar.gz"
    local url="https://github.com/Kitware/CMake/releases/download/v$version/$asset"
    local tmp; tmp="$(mktemp -d)" || return 1
    # A failed mktemp would leave tmp empty and send the download to /, so treat
    # it as unavailable rather than writing outside the scratch dir.
    [[ -n "$tmp" && -d "$tmp" ]] || return 1
    # shellcheck disable=SC2064  # expand tmp NOW: it is a local, and is already
    # out of scope by the time the RETURN trap fires.
    trap "rm -rf '$tmp'" RETURN
    echo "  fetching CMake $version (once) ..." >&2
    if ! curl -fsSL --retry 3 -o "$tmp/cmake.tar.gz" "$url"; then
        echo "  download failed (offline?)" >&2; return 1
    fi
    # Checksum before extract: an unverified toolchain is a supply-chain hole, and
    # this follows the pinned-version+sha256 pattern ci.yml uses for actionlint.
    if ! echo "$sha  $tmp/cmake.tar.gz" | sha256sum -c - >/dev/null 2>&1; then
        echo "  !! sha256 mismatch on $asset — refusing to use it" >&2; return 1
    fi
    mkdir -p "$root"
    tar -xzf "$tmp/cmake.tar.gz" -C "$root" --strip-components=1 || return 1
    [[ -x "$bin" ]] || return 1
    printf '%s\n' "$bin"
}

# ci.yml's cmake-compat `latest` leg takes the newest CMake release. Resolve it
# the same way and verify it against the release's own SHA-256 list (the trust
# actions-setup-cmake relies on). Prints "<version> <sha256>"; fails offline.
resolve_latest_cmake() {
    local version sha
    version="$(curl -fsSL --retry 3 https://api.github.com/repos/Kitware/CMake/releases/latest \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"].lstrip("v"))')" || return 1
    [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    sha="$(curl -fsSL --retry 3 "https://github.com/Kitware/CMake/releases/download/v$version/cmake-$version-SHA-256.txt" \
        | awk -v a="cmake-$version-linux-x86_64.tar.gz" '$2 == a {print $1}')" || return 1
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || return 1
    printf '%s %s\n' "$version" "$sha"
}

# Stage 7 can only disagree with stage 2 about what CMake itself reads: the same
# compiler builds the same sources, and no CMakeLists globs, so a new source file
# arrives with a CMakeLists change. So a push touching none of those skips it,
# which GitHub's cmake-compat job still runs (3D_E-0708). ANTS_PUSH_CHANGED comes
# from the push hook; unset or empty means the push is unknown (a hand run), and
# the stage runs.
COMPAT_BUILD_PATHS='(^|/)CMakeLists\.txt$|\.cmake(\.in)?$|^cmake/|^external/|^CMakePresets\.json$|^\.github/workflows/ci\.yml$|^scripts/local-ci\.sh$'
compat_moot=0
if [[ $CMAKE_COMPAT -eq 1 && -n "${ANTS_PUSH_CHANGED:-}" ]] \
        && ! grep -qE "$COMPAT_BUILD_PATHS" <<<"$ANTS_PUSH_CHANGED"; then
    compat_moot=1
fi

# One leg: ci.yml's cmake-compat Configure, Build and Run tests steps with that
# CMake, in the container, in its own tree so its cache never meets another's.
compat_leg() {  # compat_leg <label> <tree-name> <cmake path>
    local label="$1" tree="$2" cm="$3" start=$SECONDS
    if in_ci "$tree" "$cm --version | head -1
$cm -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVESTIGE_USE_MOLD=OFF \
    -DVESTIGE_FETCH_ASSETS=OFF
$cm --build build -j $JOBS
xvfb-run --auto-servernum ${cm%/cmake}/ctest --test-dir build --output-on-failure -j $JOBS -LE perf"; then
        record "$label" ok $((SECONDS - start))
    else
        record "$label" fail $((SECONDS - start))
    fi
}

if [[ $compat_moot -eq 1 ]]; then
    banner "CMake compat (3.21.0 + latest) — not run: no build file in this push"
    record "CMake $CMAKE_COMPAT_VERSION compat" moot 0
    record "CMake latest compat" moot 0
elif [[ $CMAKE_COMPAT -eq 1 && $CI_IMAGE_OK -eq 1 ]]; then
    banner "CMake $CMAKE_COMPAT_VERSION compat — configure + build + test (Release, Ubuntu 24.04 container)"
    if compat_cmake="$(ensure_cmake "$CMAKE_COMPAT_VERSION" "$CMAKE_COMPAT_SHA256")"; then
        compat_leg "CMake $CMAKE_COMPAT_VERSION compat" compat-min "$compat_cmake"
    else
        echo "  CMake $CMAKE_COMPAT_VERSION unavailable — leg SKIPped." >&2
        record "CMake $CMAKE_COMPAT_VERSION compat" skip 0
    fi
    banner "CMake latest compat — configure + build + test (Release, Ubuntu 24.04 container)"
    if latest="$(resolve_latest_cmake)" \
            && latest_cmake="$(ensure_cmake "${latest% *}" "${latest#* }")"; then
        compat_leg "CMake latest compat" compat-latest "$latest_cmake"
    else
        echo "  newest CMake release unavailable (offline?) — leg SKIPped." >&2
        record "CMake latest compat" skip 0
    fi
else
    record "CMake $CMAKE_COMPAT_VERSION compat" skip 0
    record "CMake latest compat" skip 0
fi

# --- summary ----------------------------------------------------------------
echo
banner "local-ci summary"
fails=0
skipped_stages=()
moot_stages=()   # not run because the push cannot affect them; never shown as PASS
for i in "${!STAGE_NAMES[@]}"; do
    case "${STAGE_RESULTS[$i]}" in
        ok)   mark="PASS" ;;
        fail) mark="FAIL"; fails=$((fails + 1)) ;;
        skip) mark="SKIP"; skipped_stages+=("${STAGE_NAMES[$i]}") ;;
        moot) mark="N/A"; moot_stages+=("${STAGE_NAMES[$i]}") ;;
    esac
    printf '  %-24s %-4s  %3ds\n' "${STAGE_NAMES[$i]}" "$mark" "${STAGE_TIMES[$i]}"
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
    exit 2
fi
# Reaching here means no stage FAILED and none were SKIPped — so Windows/MSVC
# actually ran and passed (a skip would have taken the PARTIAL branch above).
if [[ ${#moot_stages[@]} -gt 0 ]]; then
    echo "CI mirror passed for this push — safe to push. Not run, because nothing in"
    echo "the push can change them: ${moot_stages[*]} (GitHub still runs them)."
    exit 0
fi
echo "Full CI mirror passed (Linux Debug+Release, Tier-1 audit and CMake $CMAKE_COMPAT_VERSION +"
echo "latest in the Ubuntu 24.04 container; Windows/MSVC, gitleaks, actionlint) — safe to push."
exit 0
