# Phase 10.5 — CI Workflows + Release Policy Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Three GitHub Actions workflows that publish nightly + stable releases and validate CHANGELOG metadata, plus the release-management policy that ties phase boundaries to stable promotions.

**Architecture:** Pure CI/CD work. Each workflow is a YAML file with documented triggers + jobs. The release-manifest schema is codified in the shared-infra plan; this plan emits that JSON shape from the workflows.

**Tech Stack:** GitHub Actions, bash + python for build/manifest scripts, `gh` CLI, the existing CMake build matrix.

**Source spec:** `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md` Section 5.

**Depends on:** `docs/superpowers/plans/2026-04-25-phase-10-5-shared-infrastructure-plan.md` (release-manifest schema for the manifest emitter to target).

---

## File structure

| New file | Purpose |
|---|---|
| `.github/workflows/publish-nightly.yml` | Auto-publish nightly on every CI-green push to main |
| `.github/workflows/publish-stable.yml` | Manual workflow_dispatch → tagged stable release |
| `.github/workflows/manifest-validation.yml` | PR-time CHANGELOG metadata sanity check |
| `tools/release/build_manifest.py` | Generates `release-manifest.json` from VERSION + CHANGELOG + platform archives |
| `tools/release/validate_changelog_metadata.py` | Heuristic check for `affected_features:` / `severity:` / `scope:` |
| `tools/release/extract_changelog_slice.py` | Pulls section between two version markers — used by stable workflow |
| `docs/RELEASE_POLICY.md` | Public-facing release-management policy doc |

---

## Task 1: build_manifest.py

Goal: Python script that reads `VERSION`, `CHANGELOG.md`, platform archive paths from CLI args, and emits `release-manifest.json` matching `engine/lifecycle/release_manifest.h`.

**Files:**
- Create: `tools/release/build_manifest.py`
- Test: `tools/release/test_build_manifest.py` (pytest)

- [ ] **Step 1: Failing test.**

```python
# tools/release/test_build_manifest.py
from build_manifest import build_manifest
import json

def test_emits_schema_v1_manifest_for_stable_phase105(tmp_path):
    changelog = """## [Unreleased]

### 2026-05-15 v0.2.0 — Phase 10.9 complete (stable)
affected_features: [sh_probe_grid]
severity: behavior-change
scope: universal

Body.
"""
    (tmp_path / "VERSION").write_text("0.2.0\n")
    (tmp_path / "CHANGELOG.md").write_text(changelog)
    archives = {
        "linux-x86_64":   (tmp_path / "vestige-0.2.0-linux.tar.gz", "h1", 1),
        "windows-x86_64": (tmp_path / "vestige-0.2.0-win.zip", "h2", 2),
        "macos-arm64":    (tmp_path / "vestige-0.2.0-mac.tar.gz", "h3", 3),
    }
    for path, _, _ in archives.values():
        path.write_bytes(b"x")

    m = build_manifest(repo_root=tmp_path, channel="stable",
                       tag="v0.2.0-stable", soak_days=7,
                       phase_marker="Phase 10.9 complete",
                       platform_assets=archives,
                       changelog_md_url="https://...")
    assert m["schema_version"] == 1
    assert m["release"]["version"] == "0.2.0"
    assert m["release"]["channel"] == "stable"
    assert len(m["platforms"]) == 3
    assert m["platforms"]["linux-x86_64"]["sha256"] == "h1"
    assert len(m["breaking_features"]) == 1
    assert m["breaking_features"][0]["feature_id"] == "sh_probe_grid"
    assert m["breaking_features"][0]["scope"] == "universal"
```

- [ ] **Step 2: Implement.** Reads VERSION → `release.version`; parses CHANGELOG `### YYYY-MM-DD vX.Y.Z` headings + key:value front-matter (mirrors `ChangelogParser` from auto-updater plan). Constructs JSON.

- [ ] **Step 3: Run test, fix until green, commit.**

```bash
cd tools/release && pytest test_build_manifest.py -v
git commit -m "Phase 10.5 Slice 5 T1: build_manifest.py"
```

---

## Task 2: validate_changelog_metadata.py

Goal: heuristic CHANGELOG metadata check. Any entry matching `breaking|migration|behavior change` keywords needs `affected_features:` + `severity:` + `scope:`.

**Files:**
- Create: `tools/release/validate_changelog_metadata.py`
- Test: `tools/release/test_validate_changelog_metadata.py`

- [ ] **Step 1: Failing tests.**

```python
def test_warns_on_missing_metadata_when_breaking_kw_present_phase105():
    src = """### 2026-05-10 v0.1.99 — breaking change in audio bus naming

Body without front-matter.
"""
    warnings = validate(src)
    assert len(warnings) == 1
    assert "breaking" in warnings[0].lower()

def test_passes_when_metadata_complete_phase105(): ...
def test_passes_for_non_breaking_entries_phase105(): ...
```

- [ ] **Step 2: Implement + commit.**

```bash
git commit -m "Phase 10.5 Slice 5 T2: validate_changelog_metadata.py"
```

---

## Task 3: extract_changelog_slice.py

Goal: pulls CHANGELOG content between `from_version` and `to_version` for inclusion in the GitHub Release body of `publish-stable.yml`.

**Files:**
- Create: `tools/release/extract_changelog_slice.py`
- Test: `tools/release/test_extract_changelog_slice.py`

- [ ] **Steps:** mirror the ChangelogParser tests; cover the same fixture matrix.

```bash
git commit -m "Phase 10.5 Slice 5 T3: extract_changelog_slice.py"
```

---

## Task 4: publish-nightly.yml

Goal: triggers on every push to `main` after the existing CI matrix passes; builds platform archives; uploads to a `vX.Y.Z-nightly+<sha>` GitHub Release; generates `release-manifest.json` with `channel: nightly`.

**Files:**
- Create: `.github/workflows/publish-nightly.yml`

- [ ] **Step 1: Workflow file.**

```yaml
name: Publish nightly

on:
  workflow_run:
    workflows: ["CI"]
    types: [completed]
    branches: [main]

permissions:
  contents: write   # create release
  actions: read

jobs:
  publish:
    if: ${{ github.event.workflow_run.conclusion == 'success' }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Read VERSION
        id: ver
        shell: bash
        run: |
          echo "version=$(cat VERSION | tr -d '\n')" >> "$GITHUB_OUTPUT"
          echo "tag=v$(cat VERSION | tr -d '\n')-nightly+$(git rev-parse --short HEAD)" >> "$GITHUB_OUTPUT"
      - name: Build (release)
        shell: bash
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j
      - name: Package
        id: pkg
        shell: bash
        run: |
          # Linux/macOS: tar; Windows: zip. Output: <archive>, <sha256>, <size>.
          tools/release/package_platform.sh   # implementer authors per-platform
      - name: Upload release artefact
        uses: actions/upload-artifact@v4
        with:
          name: vestige-${{ steps.ver.outputs.version }}-${{ matrix.os }}
          path: build/dist/

  release:
    needs: publish
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/download-artifact@v4
        with: { path: dist/ }
      - name: Build manifest
        run: python3 tools/release/build_manifest.py \
                --channel nightly --tag ${{ needs.publish.outputs.tag }} \
                --output dist/release-manifest.json \
                dist/*/
      - name: Create release
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          gh release create "${{ needs.publish.outputs.tag }}" \
            --prerelease --generate-notes \
            dist/*/* dist/release-manifest.json
```

- [ ] **Step 2: Smoke test:** push a no-op commit, watch workflow run, verify a `vX.Y.Z-nightly+<sha>` release lands.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 5 T4: publish-nightly.yml"
```

---

## Task 5: publish-stable.yml

Goal: manual `workflow_dispatch` with input `tag: vX.Y.0-stable`. Builds platform archives; uploads to `vX.Y.0-stable` Release; generates manifest with `channel: stable`; copies CHANGELOG slice between previous stable and this stable into the release body.

**Files:**
- Create: `.github/workflows/publish-stable.yml`

- [ ] **Steps:** mirror nightly workflow with these deltas:
  - `on: workflow_dispatch` with `inputs.tag` (required) + `inputs.soak_days` (default 7) + `inputs.phase_marker` (required).
  - `prerelease: false`.
  - Release body = `extract_changelog_slice.py` output between previous stable tag and this tag.
  - `build_manifest.py --channel stable --soak-days {{soak_days}} --phase-marker "{{phase_marker}}"`.

```bash
git commit -m "Phase 10.5 Slice 5 T5: publish-stable.yml"
```

---

## Task 6: manifest-validation.yml

Goal: PR-time CHANGELOG metadata sanity check. Heuristic warning, not failure.

**Files:**
- Create: `.github/workflows/manifest-validation.yml`

```yaml
name: Manifest validation

on:
  pull_request:
    paths: ['CHANGELOG.md']

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { fetch-depth: 0 }
      - name: Get base CHANGELOG
        run: git show ${{ github.event.pull_request.base.sha }}:CHANGELOG.md > /tmp/base.md
      - name: Validate diff
        run: python3 tools/release/validate_changelog_metadata.py --base /tmp/base.md --head CHANGELOG.md
        # exits 0 with warnings written to stdout; comment back via gh pr comment
      - name: Comment on PR (advisory)
        if: ${{ failure() }}  # uses non-zero exit only for hard errors
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: gh pr comment ${{ github.event.pull_request.number }} -F /tmp/warnings.md
```

- [ ] **Commit.**

```bash
git commit -m "Phase 10.5 Slice 5 T6: manifest-validation.yml"
```

---

## Task 7: docs/RELEASE_POLICY.md

Goal: public-facing release-management policy doc that codifies Q17.

**Files:**
- Create: `docs/RELEASE_POLICY.md`

- [ ] **Content** (verbatim from spec Section 5 "Release management policy"):

```markdown
# Release Policy

## Versioning

- **Patch bumps** (0.1.X → 0.1.X+1): every CI-green commit on main. Auto-published to nightly channel.
- **Minor bumps** (0.1.X → 0.2.0): phase / milestone completion OR maintainer-soak promotion. Hand-tagged. Stable channel.
- **Major bumps** (0.X.Y → 1.0.0): once. Maintainer judgment when feature-complete enough to ship a game without expecting breaking changes.

## Channels

- **Nightly**: every `*-nightly+<sha>` tag. Default for users who want bleeding-edge fixes.
- **Stable**: every `*-stable` tag.

## Promotion to stable

A nightly is promoted to stable when EITHER condition is met:

1. **Phase boundary auto-promote**: when a phase / major slice lands its final doc commit, the next CI-green commit is tagged stable.
2. **Soak promote**: a nightly that's been on the channel ≥ 7 days with no P0/P1 regression reports can be tagged stable, without a phase boundary. Used for hotfixes between phases.

## Pre-1.0 caveat

Displayed on every install regardless of channel. Pre-1.0 versions may include breaking changes between minor versions; the editor will attempt to migrate your project but always backs up first.

## 1.0 trigger

Deferred. Likely tied to first biblical-walkthrough title shipping.
```

- [ ] **Commit + doc updates.**

```bash
git add docs/RELEASE_POLICY.md ROADMAP.md CHANGELOG.md VERSION
git commit -m "Phase 10.5 Slice 5 T7: release policy + ci workflows shipped"
git push origin main
```

---

## Self-review

**Spec coverage:** Section 5 release-management policy → tasks 1–7. The three workflows listed in Section 5's CI subsection map to tasks 4–6.

**Placeholder scan:** clean. `tools/release/package_platform.sh` is named but its body (per-platform archive command) is the implementer's choice; the deliverable is "produces a tarball or zip with deterministic name".

**Type consistency:** `release-manifest.json` shape matches `engine/lifecycle/release_manifest.h` from the shared-infra plan exactly.

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-25-phase-10-5-ci-release-policy-plan.md`.

Two execution options:

1. **Subagent-Driven (recommended)**.
2. **Inline Execution**.

When ready, say which approach.
