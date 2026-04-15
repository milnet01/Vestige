#!/usr/bin/env bash
# scripts/check_changelog_pair.sh
#
# Pre-commit hook helper. Given a source path prefix plus its CHANGELOG
# + VERSION files, fail the commit if any file under the prefix is
# staged but the CHANGELOG/VERSION pair is not.
#
# Usage:
#   check_changelog_pair.sh <prefix> <changelog_path> <version_path>
#
# Examples:
#   check_changelog_pair.sh tools/audit/ tools/audit/CHANGELOG.md tools/audit/VERSION
#   check_changelog_pair.sh engine/ CHANGELOG.md VERSION

set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "usage: $0 <prefix> <changelog> <version>" >&2
    exit 2
fi

prefix="$1"
changelog="$2"
version="$3"

# Only run when there's something staged. Empty commits / hook-only
# invocations should pass.
staged="$(git diff --cached --name-only --diff-filter=ACMR || true)"
if [[ -z "$staged" ]]; then
    exit 0
fi

# Files staged under the prefix, excluding the CHANGELOG/VERSION
# themselves (otherwise touching ONLY the CHANGELOG would require
# touching the CHANGELOG, which is circular).
affected="$(echo "$staged" \
    | awk -v p="$prefix" -v c="$changelog" -v v="$version" '
        $0 ~ "^"p && $0 != c && $0 != v { print $0 }
      ')"

if [[ -z "$affected" ]]; then
    exit 0  # nothing in the prefix changed — nothing to police.
fi

# At least one file under the prefix changed. CHANGELOG + VERSION
# must also be staged in the same commit.
has_changelog=0
has_version=0
while IFS= read -r f; do
    [[ "$f" == "$changelog" ]] && has_changelog=1
    [[ "$f" == "$version"   ]] && has_version=1
done <<< "$staged"

failed=0
if [[ "$has_changelog" -ne 1 ]]; then
    echo "error: files under '$prefix' changed but '$changelog' was not updated." >&2
    failed=1
fi
# VERSION check is only enforced when a VERSION file exists on disk —
# some tool directories track version inside source (e.g. web/app.py)
# and only require a CHANGELOG bump.
if [[ -f "$version" ]] && [[ "$has_version" -ne 1 ]]; then
    echo "error: files under '$prefix' changed but '$version' was not updated." >&2
    failed=1
fi

if [[ "$failed" -ne 0 ]]; then
    echo "" >&2
    echo "This repo requires CHANGELOG.md (and VERSION if present) to be updated" >&2
    echo "in the same commit as any code change under '$prefix'." >&2
    echo "Affected files:" >&2
    echo "$affected" | sed 's/^/  /' >&2
    echo "" >&2
    echo "Stage the CHANGELOG (and VERSION) updates and re-commit, or run" >&2
    echo "  git commit --no-verify" >&2
    echo "if you're absolutely sure (e.g. typo fix in a comment)." >&2
    exit 1
fi

exit 0
