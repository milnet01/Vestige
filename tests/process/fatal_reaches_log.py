#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Whole-program test: a fatal error after the window opens reaches the log file.

Runs the real vestige binary with the hidden --test-fail-after-window flag:
initialize() throws once the window is open, and the error must be written to
logs/vestige_*.log, not only to the console (3D_E-0874, lane-02 main.cpp 41).
No real bad input fails there since 3D_E-1002, hence the flag.

Usage: fatal_reaches_log.py VESTIGE_BINARY ASSET_DIR WORK_DIR
Needs a display (ctest runs under xvfb-run in CI).
"""

import glob
import os
import shutil
import subprocess
import sys

MARKER = "Engine initialization failed"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    binary, assets, work = sys.argv[1:]

    shutil.rmtree(work, ignore_errors=True)
    config = os.path.join(work, "config")
    os.makedirs(config)

    env = dict(os.environ)
    env["VESTIGE_CONFIG_DIR"] = config
    env["XDG_CONFIG_HOME"] = config
    run = subprocess.run(
        [binary, "--assets", assets, "--test-fail-after-window"],
        cwd=work, env=env, capture_output=True, text=True, timeout=120, check=False)

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
