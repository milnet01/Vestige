#!/usr/bin/env bash
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
#
# PreToolUse hook for Bash. Reads a JSON event on stdin describing the
# pending tool call. If the command is one of a small set of known noisy
# producers (audit tool, pytest, cmake --build) AND does not already cap
# its own output (via | tail / | head / >redirect / --quiet / -q /
# --tb=line), the hook exits non-zero with a one-line reminder. Claude
# Code surfaces that stderr back to the model, which then re-runs the
# command with truncation.
#
# Exit codes:
#   0   - allow the command to run unchanged
#   2   - block; stderr is shown to the model
#
# This is a TOKEN-FRUGALITY guardrail, not a safety check. Skipping it
# (e.g. by adding `--no-frugal` would still be safe — there is no
# correctness invariant being protected).
#
# Bypass marker: include the literal string ``# frugal:ok`` anywhere in
# the command (e.g. ``cmake --build . # frugal:ok``) and the hook will
# allow it through. Use when you genuinely need full output.

set -euo pipefail

# Read JSON event from stdin into a variable. jq parses one document at
# a time and exits cleanly on EOF.
event=$(cat)

tool_name=$(printf '%s' "$event" | jq -r '.tool_name // ""')
if [[ "$tool_name" != "Bash" ]]; then
    exit 0
fi

cmd=$(printf '%s' "$event" | jq -r '.tool_input.command // ""')
if [[ -z "$cmd" ]]; then
    exit 0
fi

# Bypass marker — explicit reviewer ack.
if [[ "$cmd" == *"# frugal:ok"* ]]; then
    exit 0
fi

# Heuristic: command ALREADY caps its own output if it contains any of
# these. (Order matters only insofar as the regex is OR-ed.)
already_capped() {
    local c="$1"
    [[ "$c" == *"| tail"* ]] && return 0
    [[ "$c" == *"| head"* ]] && return 0
    [[ "$c" == *"| wc"* ]]   && return 0   # `| wc -l` reduces volume
    [[ "$c" == *"| grep"* ]] && return 0   # filtering reduces volume
    [[ "$c" == *"| jq"* ]]   && return 0
    [[ "$c" == *"> /tmp"* ]] && return 0
    [[ "$c" == *"> /var"* ]] && return 0
    [[ "$c" == *">/tmp"*  ]] && return 0
    [[ "$c" == *" > "*    ]] && return 0   # any redirect to a file
    [[ "$c" == *">>"*     ]] && return 0
    return 1
}

# Pattern: command is a known noisy producer.
is_noisy() {
    local c="$1"
    # The audit tool writes a full report to docs/ — capturing its
    # stdout is almost always wasted context.
    [[ "$c" == *"tools/audit/audit.py"* ]] && return 0
    # pytest prints one line per test by default; -q / --tb=line / a
    # tail pipe all keep this manageable.
    if [[ "$c" == *"pytest"* ]]; then
        # pytest in `-q` mode is fine — only verbose / no-flag runs
        # need the reminder.
        [[ "$c" == *" -q"* ]] && return 1
        [[ "$c" == *" --quiet"* ]] && return 1
        [[ "$c" == *" --tb=line"* ]] && return 1
        return 0
    fi
    # cmake --build emits per-file compile lines; can be hundreds.
    [[ "$c" == *"cmake --build"* ]] && return 0
    # ctest verbose / extra-verbose dumps every test's stdout.
    if [[ "$c" == *"ctest"* ]]; then
        [[ "$c" == *" -V"* ]] && return 0
        [[ "$c" == *" --verbose"* ]] && return 0
        [[ "$c" == *" --extra-verbose"* ]] && return 0
    fi
    return 1
}

if is_noisy "$cmd" && ! already_capped "$cmd"; then
    cat >&2 <<EOF
[frugal-hook] Command is a known noisy producer; please cap its output
to keep context lean. Re-run with one of:

  • append: 2>&1 | tail -200
  • redirect: > /tmp/<name>.log 2>&1   (then read the file selectively)
  • for pytest: add -q or --tb=line
  • for the audit tool: read docs/AUTOMATED_AUDIT_REPORT.md afterwards

Bypass: append the marker '# frugal:ok' to the command if you really
need the full stdout.
EOF
    exit 2
fi

exit 0
