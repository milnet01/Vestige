# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 1 clang-tidy: compile-database sanitising and abandoned-run detection.

Both behaviours here exist because of 3D_E-0637. The build precompiles its
headers with GCC; clang-tidy cannot read a GCC PCH, the project builds with
-Werror, and the resulting diagnostic carries no ``file:line:col:`` prefix --
so every translation unit was abandoned, the parser matched nothing, and the
run reported zero findings and exited clean.
"""

from __future__ import annotations

import json

from lib.tier1_clangtidy import (
    _detect_analysis_failures,
    _write_pch_free_compile_db,
)


def _entry(command: str, file: str = "/repo/a.cpp") -> dict:
    return {"directory": "/repo", "file": file, "command": command,
            "output": "a.o"}


def test_pch_flags_are_stripped_from_the_analysis_database(tmp_path):
    src = tmp_path / "build"
    src.mkdir()
    command = ("/usr/bin/c++ -DFOO -Winvalid-pch -include "
               "/repo/build/CMakeFiles/x.dir/cmake_pch.hxx -O2 -c /repo/a.cpp")
    (src / "compile_commands.json").write_text(json.dumps([_entry(command)]))

    out = _write_pch_free_compile_db(src, tmp_path / "analysis")

    assert out is not None
    written = json.loads((out / "compile_commands.json").read_text())
    got = written[0]["command"]
    assert "-Winvalid-pch" not in got
    assert "cmake_pch.hxx" not in got
    # Only the PCH flags go; every real compile flag survives.
    assert "-DFOO" in got and "-O2" in got and "/repo/a.cpp" in got


def test_an_unrelated_include_flag_is_kept(tmp_path):
    src = tmp_path / "build"
    src.mkdir()
    command = "/usr/bin/c++ -include /repo/forced_prelude.h -c /repo/a.cpp"
    (src / "compile_commands.json").write_text(json.dumps([_entry(command)]))

    out = _write_pch_free_compile_db(src, tmp_path / "analysis")

    written = json.loads((out / "compile_commands.json").read_text())
    assert "forced_prelude.h" in written[0]["command"]


def test_the_arguments_form_is_sanitised_too(tmp_path):
    src = tmp_path / "build"
    src.mkdir()
    entry = {
        "directory": "/repo", "file": "/repo/a.cpp", "output": "a.o",
        "arguments": ["/usr/bin/c++", "-Winvalid-pch", "-include",
                      "/repo/build/cmake_pch.hxx", "-c", "/repo/a.cpp"],
    }
    (src / "compile_commands.json").write_text(json.dumps([entry]))

    out = _write_pch_free_compile_db(src, tmp_path / "analysis")

    args = json.loads((out / "compile_commands.json").read_text())[0]["arguments"]
    assert args == ["/usr/bin/c++", "-c", "/repo/a.cpp"]


def test_a_missing_database_is_reported_rather_than_guessed(tmp_path):
    assert _write_pch_free_compile_db(tmp_path / "nope", tmp_path / "out") is None


def test_an_abandoned_translation_unit_becomes_a_finding():
    # Verbatim clang-tidy output: the error line carries no file:line:col:
    # prefix, which is why the diagnostic parser never saw it.
    output = (
        "28 warnings and 1 error generated.\n"
        "Error while processing /repo/engine/a.cpp.\n"
        "error: precompiled header '/repo/build/cmake_pch.hxx.gch' was ignored "
        "because it is not a clang PCH file [clang-diagnostic-ignored-gch]\n"
        "Found compiler error(s).\n"
    )

    findings = _detect_analysis_failures(output)

    assert findings, "an abandoned run must not look like a clean one"
    assert any("/repo/engine/a.cpp" in (f.title + str(f.file)) for f in findings)


def test_a_silent_zero_becomes_a_finding():
    # clazy's shape: the PCH error is its ENTIRE output and it exits 0, so
    # nothing downstream can tell this from a clean run.
    output = (
        "error: precompiled header '/repo/build/cmake_pch.hxx.gch' was ignored "
        "because it is not a clang PCH file [-Werror,-Wignored-gch]\n"
    )

    findings = _detect_analysis_failures(output)

    assert len(findings) == 1
    assert "not a clang PCH file" in findings[0].title


def test_a_clean_run_produces_no_failure_finding():
    output = ("/repo/a.cpp:10:5: warning: something [bugprone-thing]\n"
              "1 warning generated.\n")
    assert _detect_analysis_failures(output) == []
