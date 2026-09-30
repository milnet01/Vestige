# Releasing Vestige

Vestige releases **on demand**: when there are fixes, features or improvements
worth giving users, the maintainer says so and one release goes out. There is
no schedule, no release candidate and no draft waiting on a click.

The build and the GitHub Release are made by
[`.github/workflows/release.yml`](.github/workflows/release.yml).

## Cutting a release

All work lands on `main`, and a release is cut from `main`.

1. **Bump the version** in `CMakeLists.txt` (`project(VERSION …)`) and the
   `VERSION` file. `.claude/bump.json` lists both.
2. **Move the changelog**: `## [Unreleased]` in `CHANGELOG.md` becomes
   `## [X.Y.Z] - <date>`, with a fresh empty `[Unreleased]` above it.
3. **Commit, tag and push**: tag the commit `vX.Y.Z` and push the commit and
   the tag together. The pre-push gate (`scripts/local-ci.sh`) runs first.
4. **The tag push starts `release.yml`**, which builds the Linux tarball, the
   AppImage and the Windows zip, signs each, attaches them to a draft release,
   and then publishes that release as **Latest**. Nothing is public until every
   download is attached.

Which number to pick (PATCH or MINOR) is
[`docs/standards/versioning-overrides.md`](docs/standards/versioning-overrides.md)'s
to decide.

## Rebuilding an existing tag

Actions tab → *Release* → *Run workflow* → enter the tag. The run rebuilds that
tag and republishes its release.

## Fixing a bad release

Fix it on `main` and cut the next PATCH release. There are no release branches.

## Safety properties

- **Never half a release** — the Linux and Windows jobs upload to a draft, and
  a separate job publishes it only after both have finished. If either build
  fails, the draft stays private.
- **Every download is signed** — `release.yml` signs the tarball, AppImage and
  zip with the `VESTIGE_UPDATE_SIGNING_KEY` secret (`tools/sign_release.py`)
  and verifies each signature against `engine/update/update_key.h` before
  upload, so a release whose secret and engine key disagree fails instead of
  publishing updates the editor would refuse (3D_E-0729).
- **Update notes are the CHANGELOG** — the editor shows users the CHANGELOG
  lines added between their version's tag and the new one, so what is written
  there is what they read.
- **A tag carrying `-rc` is never Latest** — release candidates are not part
  of this process, but if such a tag is pushed it publishes as a pre-release.

## Versioning note

`project(VERSION …)` and the `VERSION` file carry the version of the most
recent release until the next one bumps them, so a build from source reports
the release it was built on top of. The authoritative version of a release is
its git tag, and a release build is stamped with it.

How releases worked before 2026-09-30 (a weekly train with release branches
and release candidates): `git log -- .github/workflows/release-cadence.yml`.
