#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Whole-program test: a --scene path the engine may not read keeps the demo scene.

Runs the real vestige binary with --scene pointing into a folder it may not
read, the small --material-demo scene, and a one-spot --stills file so it
takes one picture and exits. The engine must log that the path cannot be read
and run the demo scene to the end, not stop at start-up (3D_E-1002).

Usage: locked_scene_keeps_demo.py VESTIGE_BINARY ASSET_DIR WORK_DIR
Needs a display (ctest runs under xvfb-run in CI). Exit 77 means skipped.
"""

import os
import shutil
import stat
import subprocess
import sys

CANNOT_READ = "--scene path cannot be read"
STARTED = "Engine initialized successfully"
FATAL = "Engine initialization failed"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    binary, assets, work = sys.argv[1:]

    if os.geteuid() == 0:
        print("SKIP: root can read a locked folder, so the error cannot be caused")
        return 77

    locked = os.path.join(work, "locked")
    if os.path.isdir(locked):
        os.chmod(locked, stat.S_IRWXU)
    shutil.rmtree(work, ignore_errors=True)
    config = os.path.join(work, "config")
    os.makedirs(locked)
    os.makedirs(config)
    stills = os.path.join(work, "one.stills")
    with open(stills, "w", encoding="utf-8") as f:
        f.write("start 0 1.7 5 -90 0\n")
    scene = os.path.join(locked, "x.scene")
    os.chmod(locked, 0)

    env = dict(os.environ)
    env["VESTIGE_CONFIG_DIR"] = config
    env["XDG_CONFIG_HOME"] = config
    # Under ASan, LeakSanitizer fails the exit code on the GL driver's
    # process-lifetime allocations (an "<unknown module>" report; see
    # tests/lsan_guard.h). This test judges the exit path, not leaks.
    env["ASAN_OPTIONS"] = ":".join(filter(None, [env.get("ASAN_OPTIONS"), "detect_leaks=0"]))
    try:
        run = subprocess.run(
            [binary, "--assets", assets, "--material-demo", "--scene", scene, "--stills", stills],
            cwd=work, env=env, capture_output=True, text=True, timeout=300, check=False)
    finally:
        os.chmod(locked, stat.S_IRWXU)

    console = run.stdout + run.stderr
    print(f"exit code {run.returncode}")

    if FATAL in console or run.returncode != 0:
        print(f"FAIL: start-up ended (exit {run.returncode}) instead of keeping"
              " the demo scene. Console ends:")
        print(console[-4000:])
        return 1
    for needed in (CANNOT_READ, STARTED):
        if needed not in console:
            print(f"FAIL: the console does not say {needed!r}. Console ends:")
            print(console[-4000:])
            return 1
    said = next(line for line in console.splitlines() if CANNOT_READ in line)
    print("console: " + said)
    if scene not in said:
        print("FAIL: the error does not name the --scene path")
        return 1
    print("PASS: the locked --scene path was reported and the demo scene ran")
    return 0


if __name__ == "__main__":
    sys.exit(main())
