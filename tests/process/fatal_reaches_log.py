#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Whole-program test: a fatal error after the window opens reaches the log file.

Runs the real vestige binary with --scene pointing into a folder it may not
read. Opening the window succeeds; checking the scene path then throws, and the
error must be written to logs/vestige_*.log, not only to the console
(3D_E-0874, lane-02 main.cpp 41).

Usage: fatal_reaches_log.py VESTIGE_BINARY ASSET_DIR WORK_DIR
Needs a display (ctest runs under xvfb-run in CI). Exit 77 means skipped.
"""

import glob
import os
import shutil
import stat
import subprocess
import sys

MARKER = "Engine initialization failed"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    binary, assets, work = sys.argv[1:]

    if os.geteuid() == 0:
        print("SKIP: root can read a locked folder, so the error cannot be caused")
        return 77

    if os.path.isdir(os.path.join(work, "locked")):
        os.chmod(os.path.join(work, "locked"), stat.S_IRWXU)
    shutil.rmtree(work, ignore_errors=True)
    locked = os.path.join(work, "locked")
    config = os.path.join(work, "config")
    os.makedirs(locked)
    os.makedirs(config)
    os.chmod(locked, 0)

    env = dict(os.environ)
    env["VESTIGE_CONFIG_DIR"] = config
    env["XDG_CONFIG_HOME"] = config
    try:
        run = subprocess.run(
            [binary, "--assets", assets, "--scene",
             os.path.join(locked, "x.scene")],
            cwd=work, env=env, capture_output=True, text=True, timeout=120)
    finally:
        os.chmod(locked, stat.S_IRWXU)

    console = run.stdout + run.stderr
    logs = glob.glob(os.path.join(work, "logs", "vestige_*.log"))
    print(f"exit code {run.returncode}; log files: {logs}")

    if MARKER not in console:
        print("FAIL: the program did not report a fatal start-up error. Console:")
        print(console[-4000:])
        return 1
    print("console: " + next(line for line in console.splitlines()
                             if MARKER in line))
    if run.returncode != 1:
        print(f"FAIL: expected exit code 1, got {run.returncode}")
        return 1
    if len(logs) != 1:
        print("FAIL: expected exactly one log file")
        return 1
    with open(logs[0], encoding="utf-8", errors="replace") as f:
        text = f.read()
    if MARKER not in text:
        print("FAIL: the fatal error is on the console but not in the log file."
              " Log ends:")
        print(text[-2000:])
        return 1
    print("PASS: the fatal error is in the log file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
