---
name: Tester feedback
about: Share general impressions, UX friction, perf notes, or "works great, here's my specs"
title: "[tester] <short summary>"
labels: tester-feedback
assignees: ''
---

> **Not a reproducible bug?** If you have exact steps that always cause
> a crash or glitch, the [Bug report](./bug_report.md) template is a
> better fit. This template is for broader impressions, UX notes, and
> "works / doesn't work on my hardware" reports.
>
> **Security issue?** Stop and follow the private disclosure process in
> [`SECURITY.md`](../../SECURITY.md) — do not file publicly.

## Summary

One or two sentences on what you tested and your overall impression.

## Hardware & OS

- **OS and distribution:** e.g. `Windows 11 23H2`, `Ubuntu 24.04`,
  `Fedora 40`, `macOS` (unsupported but noted)
- **GPU vendor and model:** e.g. `NVIDIA RTX 3060`, `Intel Iris Xe`,
  `AMD RX 6700 XT`
- **GPU driver version:** e.g. `NVIDIA 551.86`, `Mesa 25.0.3`,
  `AMD Adrenalin 24.3.1`
- **CPU:** e.g. `Intel i7-12700K`, `AMD Ryzen 7 7700X`
- **RAM:** e.g. `16 GB`
- **Display resolution and refresh:** e.g. `1920x1080 @ 144 Hz`,
  `3840x2160 @ 60 Hz (scaling 150%)`
- **Controller (if relevant):** e.g. `Xbox Series X/S via USB`,
  `DualSense via Bluetooth`

## Vestige build tested

- **Version / tag / commit:** e.g. `v0.1.3-preview`, `main @ abc1234`,
  CI artifact from run #123
- **Source of binary:** GitHub Release asset / CI artifact / built from
  source

## What worked

List the things that behaved as expected. "Fullscreen toggle fine,
WASD fine, demo scene loads in <5 s" is useful signal.

## What was rough

Things that worked but felt wrong — slow, ugly, confusing, laggy.
Screenshots or short clips very welcome.

## Frame rate observations

If you looked at the F10 overlay, paste the numbers:

- **Editor view at launch:** e.g. `~220 FPS`
- **Play-mode walkaround:** e.g. `~140 FPS`, `dips to ~45 FPS near block X`
- **Wireframe (F1):** e.g. `~260 FPS`

## Suggestions

Anything you'd change about the UX, controls, defaults, or first-time
experience. These are taken seriously even when they don't immediately
make it into the roadmap.

## Anything else

Longer notes, comparisons to other engines you've tested, accessibility
observations, etc.
