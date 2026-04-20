// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/**
 * Vestige Cursor Bridge — MCP server.
 *
 * Exposes `ide_*` tools over MCP stdio. Each call forwards an NDJSON
 * request to the companion VS Code extension on `127.0.0.1:39801` and
 * returns the response.
 *
 * The server opens a short-lived TCP connection per tool call rather
 * than a persistent one because (a) the call rate is low, (b) no
 * connection-state complicates retries, and (c) the extension can be
 * reloaded without the MCP server noticing.
 */
import * as net from 'net';
import { randomUUID } from 'crypto';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';

const BRIDGE_HOST = '127.0.0.1';
const BRIDGE_PORT = 39801;
const CALL_TIMEOUT_MS = 5000;

/**
 * Send a single NDJSON command to the extension and resolve with the
 * parsed response. Rejects on connection failure, malformed response,
 * or timeout — caller turns that into an MCP error.
 */
function callBridge(command: string, args: Record<string, unknown>): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = randomUUID();
    const socket = new net.Socket();
    let buffer = '';
    let settled = false;

    const timer = setTimeout(() => {
      if (!settled) {
        settled = true;
        socket.destroy();
        reject(new Error(`cursor-bridge: timeout after ${CALL_TIMEOUT_MS}ms waiting for '${command}'`));
      }
    }, CALL_TIMEOUT_MS);

    socket.connect(BRIDGE_PORT, BRIDGE_HOST, () => {
      socket.write(JSON.stringify({ id, command, args }) + '\n');
    });

    socket.on('data', (chunk) => {
      buffer += chunk.toString('utf8');
      const idx = buffer.indexOf('\n');
      if (idx < 0) {
        return;
      }
      const line = buffer.substring(0, idx);
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      socket.end();

      try {
        const resp = JSON.parse(line);
        if (resp.ok) {
          resolve(resp.result);
        } else {
          reject(new Error(resp.error || 'cursor-bridge: unknown error'));
        }
      } catch (e) {
        reject(new Error(`cursor-bridge: malformed response: ${line}`));
      }
    });

    socket.on('error', (err) => {
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      // Friendly hint for the most common failure — the extension isn't running.
      const hint = (err as any)?.code === 'ECONNREFUSED'
        ? ' (is the Vestige Cursor Bridge extension installed and Cursor running?)'
        : '';
      reject(new Error(`cursor-bridge: ${err.message}${hint}`));
    });
  });
}

const server = new Server(
  { name: 'vestige-cursor-bridge', version: '0.1.0' },
  { capabilities: { tools: {} } }
);

/** Static tool manifest — mirrored against the extension's handler table. */
const TOOLS = [
  {
    name: 'ide_open_file',
    description: 'Open or focus a file in Cursor / VS Code as a non-preview editor tab. Creates a new tab if the file is not already open; focuses the existing tab otherwise.',
    inputSchema: {
      type: 'object',
      properties: { path: { type: 'string', description: 'Absolute filesystem path.' } },
      required: ['path'],
    },
  },
  {
    name: 'ide_focus',
    description: 'Focus an already-open tab by file path. Fails if the file is not currently open — use ide_open_file to open-and-focus in one step.',
    inputSchema: {
      type: 'object',
      properties: { path: { type: 'string' } },
      required: ['path'],
    },
  },
  {
    name: 'ide_close_others',
    description: 'Close every file-backed editor tab whose absolute path is not in `keep`. Non-file tabs (settings, walkthroughs, diff editors) are left alone.',
    inputSchema: {
      type: 'object',
      properties: {
        keep: {
          type: 'array',
          items: { type: 'string' },
          description: 'Absolute paths to keep open.',
        },
      },
      required: ['keep'],
    },
  },
  {
    name: 'ide_close_all_except',
    description: 'Synonym for ide_close_others.',
    inputSchema: {
      type: 'object',
      properties: { keep: { type: 'array', items: { type: 'string' } } },
      required: ['keep'],
    },
  },
  {
    name: 'ide_get_open_tabs',
    description: 'Return the absolute paths of every open file-backed tab in Cursor / VS Code.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'ide_reveal_in_explorer',
    description: 'Scroll the sidebar explorer to a file and highlight it.',
    inputSchema: {
      type: 'object',
      properties: { path: { type: 'string' } },
      required: ['path'],
    },
  },
] as const;

server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: TOOLS.map((t) => ({ ...t })),
}));

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const name = request.params.name;
  const args = (request.params.arguments ?? {}) as Record<string, unknown>;
  const known = TOOLS.find((t) => t.name === name);
  if (!known) {
    return {
      isError: true,
      content: [{ type: 'text', text: `unknown tool: ${name}` }],
    };
  }
  try {
    const result = await callBridge(name, args);
    return {
      content: [{ type: 'text', text: JSON.stringify(result) }],
    };
  } catch (e: any) {
    return {
      isError: true,
      content: [{ type: 'text', text: e?.message ?? String(e) }],
    };
  }
});

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  // Keep stdin open — the SDK's transport handles the lifecycle.
}

main().catch((err) => {
  // eslint-disable-next-line no-console
  console.error('[vestige-cursor-bridge-mcp] fatal', err);
  process.exit(1);
});
