# Releasing Vestige

Vestige ships on a **weekly release train** with a one-week release-candidate
(RC) bake. The cadence is automated by
[`.github/workflows/release-cadence.yml`](.github/workflows/release-cadence.yml),
which delegates the actual artifact build + GitHub Release to
[`.github/workflows/release.yml`](.github/workflows/release.yml).

## The model in one picture

```
main  ──●──●──●──●──●──●──●──●──●──●──●──▶   (trunk: where all features land)
         \                  \
          \ Wed: cut         \ Wed: cut
           ▼                  ▼
   release/0.1.61      release/0.1.62        (short-lived stabilization branches)
   v0.1.61-rc.1        v0.1.62-rc.1
        │                   │
   (1 week bake)       (1 week bake)
        ▼                   ▼
   v0.1.61 (final)    v0.1.62 (final)
```

- **You always work on `main`.** Commit / merge features there as normal.
- Each **Wednesday 09:00 UTC** the cadence workflow:
  1. **Promotes** last week's RC to a **full release** — it tags the RC's
     `release/X.Y.Z` branch HEAD as `vX.Y.Z`. That build is published as a
     **draft**: go to the repo's *Releases* page and click **Publish** (the one
     manual gate on a public release).
  2. **Cuts** a new RC — creates `release/X.Y.(Z+1)` from current `main` and tags
     `vX.Y.(Z+1)-rc.1`. That build **auto-publishes as a pre-release** for testers.
- Versions step by **patch** while pre-1.0 (0.1.61 → 0.1.62 → …). They are
  derived from existing git tags, so nothing commits a version bump to `main`.

## Why release branches (and not a dev branch)

Features keep landing on `main` all week. If a release were cut straight from
`main` at hotfix time, it would drag in every half-finished feature since the RC.
Putting each release on its own `release/X.Y.Z` branch means the baking RC is
**frozen** except for deliberate hotfixes — new `main` work can't pollute it.

## Hotfixing an RC

When a bug is found in the current RC (or you need a fix in the next release
without the rest of `main`'s new features):

1. Commit the fix to the **active release branch** and push it:
   ```bash
   git fetch origin
   git switch release/0.1.62          # the active RC's branch
   git cherry-pick <fix-commit>       # or commit the fix directly
   git push origin release/0.1.62
   ```
2. Run the **Release cadence** workflow manually with **`mode=hotfix`**
   (Actions tab → Release cadence → Run workflow). It will:
   - re-tag the branch as the next RC (`v0.1.62-rc.2`) and re-publish the
     pre-release with the fix, and
   - **back-merge** `release/0.1.62` into `main` so next week's RC inherits the
     fix. (If that merge conflicts, the job fails loudly with instructions — the
     RC is still published; resolve the merge into `main` by hand.)
3. The fix promotes to the full release on the normal Wednesday.

## Manual / off-schedule release

- **Start the cadence now (don't wait for Wednesday):** Actions tab → *Release
  cadence* → *Run workflow* → `mode=cadence`.
- **Build a specific existing tag:** Actions tab → *Release* → *Run workflow* →
  enter the tag, or just push a `vX.Y.Z` tag (a `…-rc.N` tag publishes a
  pre-release; a bare `vX.Y.Z` tag publishes a draft final).

## Safety properties

- **One Publish click** gates every public final release (drafts), while RCs flow
  to testers automatically.
- **No personal access token** — the cadence calls `release.yml` directly
  (`workflow_call`), so it never relies on a token-pushed tag triggering another
  workflow.
- **Re-runs are safe** — tags/branches already created make a re-run fail rather
  than double-publish.
- **Hotfixes never lose their fix** — promotion tags the release-branch HEAD
  (RC + hotfixes), and hotfixes are back-merged to `main`.

## Versioning note

`VERSION` / the CMake `project(VERSION …)` is the development baseline and the
floor the cadence bumps from. The authoritative version of any release is its git
tag.
