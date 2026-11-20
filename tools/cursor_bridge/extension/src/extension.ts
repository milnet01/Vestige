// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/**
 * Vestige Cursor Bridge — a VS Code extension that exposes tab-
 * management commands over a local TCP socket so an external tool
 * (e.g. Claude Code, via the companion MCP server) can drive Cursor's
 * editor state: open a file, close unrelated tabs, list open tabs,
 * focus a tab, reveal in the sidebar.
 *
 * Wire protocol: one JSON message per connection, NDJSON (newline-
 * delimited). Each request is `{ id: string, command: string, args: any }`
 * and each response is `{ id: string, ok: boolean, result?: any,
 * error?: string }`. The socket is accepted on `127.0.0.1:PORT` only —
 * no remote exposure by design.
 */
import * as net from 'net';
import * as vscode from 'vscode';

const PORT = 39801;

type CommandHandler = (args: any) => Promise<any>;

/**
 * Get the absolute filesystem path of a tab, or undefined for non-
 * file-backed tabs (walkthroughs, settings editor, diff editors).
 */
function tabPath(tab: vscode.Tab): string | undefined {
  const input = tab.input as { uri?: vscode.Uri } | undefined;
  if (!input || !input.uri) {
    return undefined;
  }
  if (input.uri.scheme !== 'file') {
    return undefined;
  }
  return input.uri.fsPath;
}

const commands: Record<string, CommandHandler> = {
  /** Open or focus a file in a new/existing tab. */
  async ide_open_file(args: { path: string }) {
    if (!args?.path || typeof args.path !== 'string') {
      throw new Error('path required');
    }
    const doc = await vscode.workspace.openTextDocument(args.path);
    await vscode.window.showTextDocument(doc, { preview: false });
    return { path: args.path };
  },

  /** Focus an already-open tab. Fails if the file is not open. */
  async ide_focus(args: { path: string }) {
    if (!args?.path) {
      throw new Error('path required');
    }
    const open = await commands.ide_get_open_tabs({});
    if (!open.paths.includes(args.path)) {
      throw new Error(`file not currently open: ${args.path}`);
    }
    const doc = await vscode.workspace.openTextDocument(args.path);
    await vscode.window.showTextDocument(doc, { preview: false });
    return { path: args.path };
  },

  /**
   * Close every file-backed tab whose path is not in `keep`. `keep` must
   * be given, as an array of strings: a missing one would otherwise mean
   * "keep nothing" and close every tab. An empty array is that request,
   * made on purpose.
   */
  async ide_close_others(args: { keep: string[] }) {
    if (!Array.isArray(args?.keep) || !args.keep.every((p) => typeof p === 'string')) {
      throw new Error('keep required: an array of absolute paths');
    }
    const keep = new Set(args.keep);
    const toClose: vscode.Tab[] = [];
    for (const group of vscode.window.tabGroups.all) {
      for (const tab of group.tabs) {
        const p = tabPath(tab);
        if (!p) {
          continue;  // leave non-file tabs alone
        }
        if (!keep.has(p)) {
          toClose.push(tab);
        }
      }
    }
    if (toClose.length > 0) {
      await vscode.window.tabGroups.close(toClose);
    }
    return { closed: toClose.length };
  },

  /** Synonym for ide_close_others — some callers find it more readable. */
  async ide_close_all_except(args: { keep: string[] }) {
    return commands.ide_close_others(args);
  },

  /** List the absolute paths of every open file-backed tab. */
  async ide_get_open_tabs(_args: {}) {
    const paths: string[] = [];
    for (const group of vscode.window.tabGroups.all) {
      for (const tab of group.tabs) {
        const p = tabPath(tab);
        if (p) {
          paths.push(p);
        }
      }
    }
    return { paths };
  },

  /** Reveal a file in the sidebar explorer (scrolls to it, highlights it). */
  async ide_reveal_in_explorer(args: { path: string }) {
    if (!args?.path) {
      throw new Error('path required');
    }
    const uri = vscode.Uri.file(args.path);
    await vscode.commands.executeCommand('revealInExplorer', uri);
    return { path: args.path };
  },
};

function handleLine(line: string, socket: net.Socket): void {
  let req: any;
  try {
    req = JSON.parse(line);
  } catch (e: any) {
    socket.write(JSON.stringify({ id: null, ok: false, error: `invalid JSON: ${e?.message}` }) + '\n');
    return;
  }

  const id: string = req?.id ?? '';
  const cmd: string = req?.command ?? '';
  // Own keys only: a plain object also answers to inherited names such as
  // `toString`, which are not command handlers.
  const handler = Object.prototype.hasOwnProperty.call(commands, cmd) ? commands[cmd] : undefined;
  if (!handler) {
    socket.write(JSON.stringify({ id, ok: false, error: `unknown command: ${cmd}` }) + '\n');
    return;
  }

  handler(req.args ?? {}).then(
    (result) => socket.write(JSON.stringify({ id, ok: true, result }) + '\n'),
    (err: any) => socket.write(JSON.stringify({ id, ok: false, error: err?.message ?? String(err) }) + '\n')
  );
}

let server: net.Server | undefined;
/** True only while this window's server holds the port. */
let listening = false;

export function activate(context: vscode.ExtensionContext): void {
  server = net.createServer((socket) => {
    let buffer = '';
    socket.on('data', (chunk) => {
      buffer += chunk.toString('utf8');
      let idx: number;
      while ((idx = buffer.indexOf('\n')) >= 0) {
        const line = buffer.substring(0, idx).trim();
        buffer = buffer.substring(idx + 1);
        if (line.length > 0) {
          handleLine(line, socket);
        }
      }
    });
    socket.on('error', () => {
      // Client disconnects mid-request are fine — drop silently.
    });
  });

  // Bind to loopback only — no remote exposure.
  server.listen(PORT, '127.0.0.1', () => {
    listening = true;
    console.log(`[vestige-cursor-bridge] listening on 127.0.0.1:${PORT}`);
  });

  server.on('close', () => {
    listening = false;
  });

  server.on('error', (err: any) => {
    if (err?.code === 'EADDRINUSE') {
      vscode.window.showWarningMessage(
        `Vestige Cursor Bridge: port ${PORT} in use — another Cursor window may already be listening. Only one bridge is needed per port.`
      );
    } else {
      console.error('[vestige-cursor-bridge] server error', err);
    }
  });

  const statusCmd = vscode.commands.registerCommand('vestigeCursorBridge.status', () => {
    vscode.window.showInformationMessage(
      listening
        ? `Vestige Cursor Bridge listening on 127.0.0.1:${PORT}. Use the MCP server to drive it.`
        : `Vestige Cursor Bridge is not listening in this window: port ${PORT} is unavailable, most likely held by another Cursor window's bridge.`
    );
  });
  context.subscriptions.push(statusCmd);
}

export function deactivate(): void {
  if (server) {
    server.close();
    server = undefined;
  }
  listening = false;
}
