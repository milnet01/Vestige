#!/usr/bin/env python3
"""Flask web interface for the audit tool."""

from __future__ import annotations

import json
import logging
import queue
import sys
from pathlib import Path

from flask import Flask, Response, jsonify, render_template, request

# Ensure lib is importable
AUDIT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(AUDIT_ROOT))

from web.audit_bridge import AuditSession

app = Flask(__name__, template_folder="templates", static_folder="static")
session = AuditSession()

# Default project root (can be overridden via the UI)
DEFAULT_ROOT = str(AUDIT_ROOT.parent.parent)  # /mnt/Storage/Scripts/Linux/3D_Engine
DEFAULT_CONFIG = str(AUDIT_ROOT / "audit_config.yaml")


@app.route("/")
def index():
    """Serve the single-page app."""
    return render_template(
        "index.html",
        default_root=DEFAULT_ROOT,
        default_config=DEFAULT_CONFIG,
    )


@app.route("/api/run", methods=["POST"])
def api_run():
    """Start an audit run."""
    if session.running:
        return jsonify({"error": "Audit already running"}), 409

    data = request.get_json(silent=True) or {}
    project_root = data.get("project_root", DEFAULT_ROOT)
    config_path = data.get("config_path", DEFAULT_CONFIG)
    tiers = data.get("tiers", [1, 2, 3, 4, 5])
    base_ref = data.get("base_ref", "HEAD~1")
    verbose = data.get("verbose", False)

    try:
        session.start(
            config_path=config_path,
            project_root=project_root,
            tiers=tiers,
            base_ref=base_ref,
            verbose=verbose,
        )
        return jsonify({"status": "started"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/stop", methods=["POST"])
def api_stop():
    """Cancel a running audit."""
    if not session.running:
        return jsonify({"error": "No audit running"}), 400
    session.stop()
    return jsonify({"status": "stopping"})


@app.route("/api/events")
def api_events():
    """SSE endpoint — streams audit progress events."""
    def generate():
        while True:
            try:
                event = session.event_queue.get(timeout=30)
                yield f"data: {json.dumps(event, default=str)}\n\n"
                if event.get("type") in ("complete", "error", "cancelled"):
                    break
            except queue.Empty:
                # Keepalive to prevent browser/proxy timeout
                yield ": keepalive\n\n"
            except GeneratorExit:
                break

    return Response(
        generate(),
        mimetype="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
            "Connection": "keep-alive",
        },
    )


@app.route("/api/status")
def api_status():
    """Return current session state (for page reload recovery)."""
    return jsonify(session.get_status())


@app.route("/api/init", methods=["POST"])
def api_init():
    """Auto-generate config for a project."""
    data = request.get_json(silent=True) or {}
    project_root = data.get("project_root", DEFAULT_ROOT)
    output_path = data.get("output_path")

    if not output_path:
        output_path = str(Path(project_root) / "audit_config.yaml")

    try:
        from lib.auto_config import generate_config
        generate_config(Path(project_root), Path(output_path))
        return jsonify({"status": "ok", "config_path": output_path})
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/report")
def api_report():
    """Return the generated markdown report."""
    report_path = Path(DEFAULT_ROOT) / "docs" / "AUTOMATED_AUDIT_REPORT.md"

    # Allow override via query param
    custom = request.args.get("path")
    if custom:
        report_path = Path(custom)

    if not report_path.exists():
        return jsonify({"error": "Report not generated yet"}), 404

    return Response(report_path.read_text(), mimetype="text/plain")


@app.route("/api/config")
def api_config():
    """Return the current YAML config."""
    config_path = request.args.get("path", DEFAULT_CONFIG)
    path = Path(config_path)

    if not path.exists():
        return jsonify({"error": f"Config not found: {config_path}"}), 404

    return Response(path.read_text(), mimetype="text/plain")


if __name__ == "__main__":
    # Suppress verbose library logs
    logging.getLogger("werkzeug").setLevel(logging.WARNING)

    # Setup audit logger
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )

    print("Audit Tool Web UI: http://127.0.0.1:5000")
    app.run(host="127.0.0.1", port=5000, debug=False, threaded=True)
