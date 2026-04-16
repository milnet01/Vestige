#!/usr/bin/env bash
#
# hook-on-version-edit.sh — PostToolUse hook for the Vestige 3D Engine
# project. When Claude edits CMakeLists.txt or CHANGELOG.md, verify the
# two are still in sync: CMakeLists.txt's `project(... VERSION X.Y.Z)`
# must match the topmost `## [X.Y.Z]` (or `## [Unreleased]`) heading in
# CHANGELOG.md.
#
# Wired in via .claude/settings.json:
#     PostToolUse → matcher Edit|Write → command bash <this script>
#
# Stdin: PostToolUse JSON. Short-circuit unless touched file is one of
# the two we care about.
# Stdout: nothing on sync / non-relevant edits, or a JSON systemMessage
# describing the drift.
# Exit: always 0; never blocks an edit, just informs.
#
# Why a script: keeps settings.json small + diffable, and lets the
# logic live where it can be iterated without re-validating the JSON
# schema.

set -u

PROJECT_ROOT="/mnt/Storage/Scripts/Linux/3D_Engine"

file=$(jq -r '.tool_input.file_path // .tool_response.filePath // empty' 2>/dev/null)
case "$file" in
    */CMakeLists.txt|CMakeLists.txt|*/CHANGELOG.md|CHANGELOG.md) ;;
    *) exit 0 ;;
esac

# Vestige's CMakeLists puts `project(...)` across multiple lines, with
# VERSION on its own indented line. Extract by reading inside the
# project(...) block (from `project(` through the next `)`) and
# grabbing the VERSION token from there. Also guards against false
# positives like `cmake_minimum_required(VERSION 3.20)` which would
# match a flat regex.
cmake_version=$(awk '
    /project\s*\(/ { in_proj=1 }
    in_proj && match($0, /VERSION[ \t]+([0-9]+\.[0-9]+\.[0-9]+)/, m) { print m[1]; exit }
    in_proj && /\)/ { in_proj=0 }
' "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null)
if [ -z "$cmake_version" ]; then
    exit 0  # Can't determine source-of-truth; nothing to compare against.
fi

# Topmost CHANGELOG.md section heading. Accept either `## [X.Y.Z]` (a
# released version) or `## [Unreleased]` (work-in-progress staging).
changelog_top=$(grep -m1 -oE '^##\s+\[[^]]+\]' "$PROJECT_ROOT/CHANGELOG.md" 2>/dev/null \
    | grep -oE '\[[^]]+\]' | tr -d '[]')
if [ -z "$changelog_top" ]; then
    exit 0
fi

# "Unreleased" is always considered in sync — represents pre-release
# accumulation that doesn't pin to a version yet.
if [ "$changelog_top" = "Unreleased" ]; then
    exit 0
fi

if [ "$cmake_version" != "$changelog_top" ]; then
    msg="⚠ Vestige version drift: CMakeLists.txt VERSION=$cmake_version but CHANGELOG.md top section=[$changelog_top]. One of: bump CMakeLists.txt to $changelog_top, add a [$cmake_version] section to CHANGELOG.md, or revert to [Unreleased]."
    jq -n --arg m "$msg" '{systemMessage: $m}'
fi
exit 0
