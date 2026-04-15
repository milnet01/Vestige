---
name: Bug report
about: Report a reproducible problem in the engine or tooling
title: "[bug] <short summary>"
labels: bug
assignees: ''
---

> **Not a security issue, right?** If this might be a security
> vulnerability, **stop** and follow the private disclosure process in
> [`SECURITY.md`](../../SECURITY.md) instead of filing here.

## What happened

A clear, concise description of the bug.

## Expected behaviour

What you expected to happen.

## Repro steps

Minimal steps to reproduce. Reference a specific commit SHA if possible.

1.
2.
3.

## Environment

- **Vestige commit / tag:** e.g. `v0.1.0-preview` or commit `abc1234`
- **OS and distribution:** e.g. `Ubuntu 24.04`, `Windows 11 23H2`,
  `openSUSE Tumbleweed 20260410`
- **GPU and driver:** e.g. `AMD RX 6600 / Mesa 25.0.3`,
  `NVIDIA RTX 3060 / 551.86`
- **Compiler:** e.g. `gcc 14.2`, `clang 18`, `MSVC 19.38`
- **CMake:** `cmake --version`
- **Build type:** Debug / Release / RelWithDebInfo

## Logs, backtraces, screenshots

Paste any relevant log output (fenced in triple backticks) or attach
screenshots / frame captures. For crashes, a backtrace from `gdb` /
`lldb` / Visual Studio is very valuable.

```
(paste logs here)
```

## Anything else

- Have you tried a clean build (`rm -rf build && cmake ...`)?
- Does it happen with the shipped demo scene or only in a custom scene?
- Is this a regression? If so, last-known-good commit?
