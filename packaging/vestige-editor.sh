#!/usr/bin/env bash
# Wrapper script — launches the Vestige editor from whatever prefix the
# binary landed in. Handles two install layouts:
#
#   1. Build tree:     <build>/bin/vestige
#   2. System install: <prefix>/bin/vestige (typical /usr/local/bin)
#
# The editor is the default mode, so this is just `vestige` with any
# arguments forwarded. Separate wrapper exists so desktop launchers and
# menu entries can reference a stable name that signals "editor" to
# users, without us having to maintain two binaries.
set -euo pipefail

# Resolve the real path of this script (follow symlinks).
SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname "${SCRIPT_PATH}")"

# Sibling binary first (build tree + relocatable installs), then PATH.
if [[ -x "${SCRIPT_DIR}/vestige" ]]; then
    exec "${SCRIPT_DIR}/vestige" "$@"
fi

if command -v vestige >/dev/null 2>&1; then
    exec vestige "$@"
fi

echo "vestige-editor: could not locate the 'vestige' binary." >&2
echo "  Looked in: ${SCRIPT_DIR}/vestige, \$PATH" >&2
exit 127
