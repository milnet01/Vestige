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

VERSION = "1.7.0"

app = Flask(__name__, template_folder="templates", static_folder="static")
session = AuditSession()

# Default project root (can be overridden via the UI)
DEFAULT_ROOT = str(AUDIT_ROOT.parent.parent)  # /mnt/Storage/Scripts/Linux/3D_Engine
DEFAULT_CONFIG = str(AUDIT_ROOT / "audit_config.yaml")
CHANGELOG_PATH = AUDIT_ROOT / "CHANGELOG.md"


@app.route("/")
def index():
    """Serve the single-page app."""
    return render_template(
        "index.html",
        default_root=DEFAULT_ROOT,
        default_config=DEFAULT_CONFIG,
        version=VERSION,
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


@app.route("/api/detect", methods=["POST"])
def api_detect():
    """Detect project settings without writing a config file.

    Returns structured data for the visual config builder.
    """
    data = request.get_json(silent=True) or {}
    project_root = data.get("project_root", DEFAULT_ROOT)
    root = Path(project_root)

    if not root.is_dir():
        return jsonify({"error": f"Not a directory: {project_root}"}), 400

    try:
        from lib.auto_config import detect_project
        detected = detect_project(root)

        # Also check what tools are available
        import shutil
        tools = {
            "cppcheck": shutil.which("cppcheck") is not None,
            "clang_tidy": shutil.which("clang-tidy") is not None,
            "git": shutil.which("git") is not None,
            "cmake": shutil.which("cmake") is not None,
        }
        try:
            import lizard
            tools["lizard"] = True
        except ImportError:
            tools["lizard"] = False

        detected["available_tools"] = tools

        # Check if a config file already exists
        config_path = root / "audit_config.yaml"
        detected["existing_config"] = str(config_path) if config_path.exists() else None

        # Check for audit tool config in tools/audit/
        tools_config = root / "tools" / "audit" / "audit_config.yaml"
        if tools_config.exists():
            detected["existing_config"] = str(tools_config)

        return jsonify(detected)
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


@app.route("/api/config", methods=["PUT"])
def api_config_save():
    """Save updated YAML config."""
    import yaml

    data = request.get_json(silent=True) or {}
    config_path = Path(data.get("path", DEFAULT_CONFIG)).resolve()
    content = data.get("content", "")

    # Security: restrict writes to project root or audit tool directory
    allowed_roots = [Path(DEFAULT_ROOT).resolve(), AUDIT_ROOT.resolve()]
    if not any(str(config_path).startswith(str(r)) for r in allowed_roots):
        return jsonify({"error": "Path outside project directory"}), 403

    # Only allow .yaml/.yml files
    if config_path.suffix not in (".yaml", ".yml"):
        return jsonify({"error": "Only YAML files can be saved"}), 400

    # Validate YAML
    try:
        yaml.safe_load(content)
    except yaml.YAMLError as e:
        return jsonify({"error": f"Invalid YAML: {e}"}), 400

    # Backup before overwriting
    if config_path.exists():
        backup = config_path.with_suffix(".yaml.bak")
        backup.write_text(config_path.read_text())

    config_path.write_text(content)
    return jsonify({"status": "saved"})


@app.route("/api/reports")
def api_reports():
    """List all timestamped audit reports."""
    report_dir = Path(DEFAULT_ROOT) / "docs"
    reports = []
    for f in sorted(report_dir.glob("AUTOMATED_AUDIT_REPORT_*.md"), reverse=True):
        stat = f.stat()
        reports.append({
            "filename": f.name,
            "path": str(f),
            "size": stat.st_size,
            "modified": stat.st_mtime,
        })
    return jsonify({"reports": reports[:50]})


@app.route("/api/reports/<path:filename>")
def api_report_file(filename: str):
    """Return a specific timestamped report."""
    report_dir = Path(DEFAULT_ROOT) / "docs"
    path = (report_dir / filename).resolve()
    try:
        path.relative_to(report_dir.resolve())
    except ValueError:
        return jsonify({"error": "Access denied"}), 403
    if not path.exists():
        return jsonify({"error": "Not found"}), 404
    return Response(path.read_text(), mimetype="text/plain")


@app.route("/api/version")
def api_version():
    """Return version and changelog."""
    changelog = ""
    if CHANGELOG_PATH.exists():
        changelog = CHANGELOG_PATH.read_text()
    return jsonify({"version": VERSION, "changelog": changelog})


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

    print("Audit Tool Web UI: http://127.0.0.1:5800")
    app.run(host="127.0.0.1", port=5800, debug=False, threaded=True)
