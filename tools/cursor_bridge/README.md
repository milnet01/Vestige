# Cursor Bridge

A two-part local bridge that lets Claude Code drive VS Code / Cursor's
editor state: open a file, close unrelated tabs, list open tabs, focus
a tab, reveal in the explorer. Tab management — not content editing
(that already works through Claude Code's official extension).

## Architecture

```
Claude Code (CLI)
      │
      ▼
  MCP tools ──stdio──► mcp_server/   ──TCP──► extension/ ──► VS Code API
   (ide_open_file,       Node 18+         localhost         (Cursor /
    ide_close_others,                       :39801            VS Code)
    ...)
```

Two independent pieces:

1. **`extension/`** — a VS Code extension (Cursor-compatible because
   Cursor accepts all VS Code extensions). Listens on
   `localhost:39801` (configurable) for JSON-RPC-style commands and
   executes them against the `vscode.*` API.

2. **`mcp_server/`** — a Node MCP server Claude Code attaches to via
   `mcp_config.json`. Each MCP tool it exposes forwards to the
   extension over TCP.

Both are stateless beyond the open socket — nothing persists to disk.

## Exposed MCP tools

| Tool | Arguments | What it does |
|---|---|---|
| `ide_open_file` | `path` (abs) | `vscode.window.showTextDocument`. Creates a new tab (or focuses an existing one). |
| `ide_focus` | `path` (abs) | Like `ide_open_file` but fails if the file isn't already open. |
| `ide_close_others` | `keep[]` (abs paths) | Closes every editor tab whose URI is not in `keep[]`. |
| `ide_close_all_except` | `keep[]` | Synonym for `ide_close_others`. |
| `ide_get_open_tabs` | — | Returns an array of open-tab absolute paths. |
| `ide_reveal_in_explorer` | `path` | `revealInExplorer` — makes the sidebar scroll to the file. |

## Install

### Step 1 — build and sideload the extension

```bash
cd tools/cursor_bridge/extension
npm install
npm run compile
npx vsce package --no-dependencies -o cursor-bridge.vsix
# Then in Cursor: Cmd/Ctrl+Shift+P → "Extensions: Install from VSIX..."
```

(Or for local iteration: open the `extension/` dir in Cursor, hit F5 — it
launches a second Cursor window with the extension running.)

### Step 2 — build the MCP server

```bash
cd tools/cursor_bridge/mcp_server
npm install
npm run compile
```

### Step 3 — register the MCP server with Claude Code

Add to `~/.claude/mcp_config.json` (or project `.mcp.json`):

```json
{
  "mcpServers": {
    "cursor-bridge": {
      "command": "node",
      "args": ["/absolute/path/to/tools/cursor_bridge/mcp_server/dist/index.js"]
    }
  }
}
```

Restart Claude Code. You should see the `ide_*` tools in the deferred
tool list. Call `ide_get_open_tabs` first to confirm the extension is
reachable.

## Config

The extension auto-starts on Cursor launch and listens on port 39801.
To change the port (e.g. if it clashes with something local), edit
`extension/src/extension.ts` → `const PORT` and rebuild.

## Security

The socket binds to `127.0.0.1` only — no remote exposure. The extension
accepts only the six commands above; any other message is rejected.
There is no authentication; anyone with local access to the port can
open files. Run in a trusted environment only.
