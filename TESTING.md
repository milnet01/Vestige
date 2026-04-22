# Testing Vestige

**You do not need to be a C++ developer to help test Vestige.** If you have
a computer, a GPU driver newer than ~2022, and ten minutes, you can file
a useful report. Testers are how this project escapes the tunnel vision
of its single maintainer's hardware.

> **Project status — early-stage.** Vestige is pre-1.0. Crashes, visual
> glitches, poor performance, and unclear UI are all in-scope to report.
> See [`ROADMAP.md`](ROADMAP.md) for what is shipped vs. planned.

---

## Who we especially need tests from

The maintainer develops on:

- **GPU:** AMD RX 6600 (RDNA2) on Mesa
- **OS:** openSUSE Tumbleweed (Linux 6.x, Wayland + X11)
- **CPU:** AMD Ryzen 5 5600

Everything else is an open gap. Reports are most valuable when they come
from:

- **NVIDIA** GPUs (any generation — proprietary driver *and* Nouveau)
- **Intel** integrated graphics (Iris Xe, Arc, older HD series)
- **Windows 10 / 11** — GPU vendor and driver version noted
- **Non-openSUSE Linux** — Ubuntu/Fedora/Arch/Debian, X11 *or* Wayland
- **Older AMD** cards (pre-RDNA, Polaris, Vega) — driver quirks differ
- **High-DPI and multi-monitor** setups
- **Xbox / PlayStation controllers** plugged in via USB or Bluetooth

If your setup is in any of those buckets, your report covers ground the
maintainer cannot.

---

## How to get a binary

Pre-built binaries are published as **GitHub Release assets** on the
[Vestige releases page](https://github.com/milnet01/Vestige/releases).
Each release carries:

- `vestige-<version>-linux-x86_64.tar.gz` — Linux tarball, extract and run
- `vestige-<version>-windows-x86_64.zip` — Windows ZIP, extract and run

If no release is published yet (or the current commit is newer than the
latest release and you want to test unreleased work), you can:

1. **Download a CI build artifact.** Every successful CI run on `main`
   attaches the built binaries to the run summary (GitHub Actions → CI
   workflow → pick a green run → *Artifacts*). These expire after
   ~90 days and require a GitHub login.
2. **Build from source.** See
   [`CONTRIBUTING.md` § Building from source](CONTRIBUTING.md#building-from-source).

---

## What to try — 10-minute smoke test

Extract the release archive and launch the editor:

- **Linux:** `./vestige-editor` (or double-click `vestige-editor.sh`)
- **Windows:** double-click `vestige-editor.exe`

Then exercise each of the following. Note anything surprising — crashes,
flickers, low frame rate, misaligned text, controls that don't work:

1. **Launch** — does the editor window open? Does the demo scene render?
2. **Move around** (press **Esc** to enter play mode) — WASD + mouse
   look, Shift to sprint, Esc again to return to editor.
3. **Fullscreen toggle** — press **F12**. Then press it again to go back.
4. **Frame counter** — press **F10**. Does the overlay show ≥60 FPS?
5. **Screenshot** — press **F11**. Does `~/Pictures/Screenshots/` (Linux)
   or your Pictures folder (Windows) get a PNG + a diagnostics JSON?
6. **Tonemapper cycle** — press **F2** a few times. Image should change.
7. **Controller (if you have one)** — plug it in before launching. Does
   analog-stick movement work in play mode?
8. **Resize the window** — drag a corner. Does the view re-stretch
   cleanly, or does it tear / flicker?
9. **Close the editor** — does it shut down cleanly, or hang?

If all nine pass, that's a valuable clean report too — file it as
*"Works on X/Y/Z"* feedback (see below). Negative results are *not* more
valuable than positive ones; coverage is coverage.

---

## What to try — 30-minute deeper pass

Once the smoke test passes, the next layer of bugs hides in these:

- **Editor gizmos** — click an object, drag the translate / rotate /
  scale gizmos. Does the handle track the mouse? Any Z-fighting?
- **Load a different scene** — `File → Open Scene…` and pick any scene
  under the `scenes/` folder in the release.
- **Wireframe toggle** (**F1**) — does the frame rate stay above 60 FPS
  in wireframe? (It should.)
- **POM toggle** (**F4**) — parallax occlusion. Look at surfaces with
  heightmaps; depth should change when toggled.
- **Exposure** — press **[** and **]**. The image should darken / brighten.
- **Long-running stability** — leave the editor open for 10 minutes in
  play mode, walking around. Any memory growth? Visible in Task Manager
  (Windows) or `htop` (Linux). Growth is a leak; file it.

---

## How to report

Vestige uses **two templates** — pick the one that fits:

| You found…                                          | File with                                                                                 |
|-----------------------------------------------------|--------------------------------------------------------------------------------------------|
| A **reproducible** crash, visible glitch, or broken | [Bug report](https://github.com/milnet01/Vestige/issues/new?template=bug_report.md)        |
| feature (steps 1-9 fail)                            |                                                                                            |
| **General impressions**, UX friction, perf notes,   | [Tester feedback](https://github.com/milnet01/Vestige/issues/new?template=tester_feedback.md) |
| or "works great, here's my specs"                   |                                                                                            |
| A **question** about how to test something          | [Discussions](https://github.com/milnet01/Vestige/discussions)                             |

The templates prompt you for the information the maintainer needs —
**OS, GPU vendor and driver version, CPU, Vestige version** — please fill
those in. A bug report without hardware info is often un-actionable;
with it, the maintainer can usually localise the fault in minutes.

**Screenshots and short screen recordings are extremely welcome**, even
if you cannot describe what you're seeing in words. Partial-sighted
testers especially: drop the screenshot in and the maintainer will
describe it back for verification.

### What NOT to report

- **Security vulnerabilities** — do NOT open a public issue. Follow
  [`SECURITY.md`](SECURITY.md) for the private disclosure process.
- **Features** that do not exist yet — check [`ROADMAP.md`](ROADMAP.md)
  first. If it's listed as *Planned*, it's not a bug.
- **Modifications you made to the source** — we cannot triage local
  changes. Please reproduce against a vanilla release build.

---

## Privacy and data collection

Vestige **does not phone home**. The editor writes:

- **Settings** to the OS's per-user config directory (Linux:
  `$XDG_CONFIG_HOME/vestige/`, Windows: `%LOCALAPPDATA%\Vestige\`).
- **Screenshots** and **diagnostic JSON** to your Pictures folder only
  when you press **F11**.
- **Log output** to stderr only (nothing persisted).

Nothing is uploaded. If you file an issue, only the information you
paste into the issue reaches the maintainer.

---

## Where to find the maintainer between reports

- **GitHub Issues** — the canonical place for bugs and feature requests.
- **GitHub Discussions** — casual chat, design questions, "is this a bug?"
- **Pull requests** — if you're a developer and want to fix something
  directly, see [`CONTRIBUTING.md`](CONTRIBUTING.md).

Response cadence is one pass per week (this is a solo-maintained project
with a day job). Issues are triaged oldest-first; a week of silence
means it is queued, not ignored.

---

## Thank you

Every crash report from a GPU the maintainer does not own pays back
multiple hours of otherwise-invisible bug-hunting. Testers are first-class
contributors to this project; if a bug of yours leads to a fix, you'll
be credited in `CHANGELOG.md`.
