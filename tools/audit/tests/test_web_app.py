"""Tests for the Flask web UI path-safety helper (AUDIT.md §H1-H3)."""

from __future__ import annotations

from pathlib import Path

import pytest


@pytest.fixture
def web_app():
    """Import the web app module; skip tests if Flask isn't installed."""
    flask = pytest.importorskip("flask")
    from web import app
    return app


def test_is_safe_path_blocks_etc_passwd(web_app):
    """A well-known outside-tree path must be rejected."""
    assert not web_app._is_safe_path(Path("/etc/passwd"))


def test_is_safe_path_blocks_ssh_keys(web_app):
    """SSH private key paths must be rejected."""
    assert not web_app._is_safe_path(Path("/root/.ssh/id_rsa"))
    assert not web_app._is_safe_path(Path("/home/somebody/.ssh/id_rsa"))


def test_is_safe_path_blocks_traversal_via_resolve(web_app):
    """`..` components must resolve to an allowed-root-relative path."""
    attack = Path(web_app.DEFAULT_ROOT) / ".." / ".." / ".." / "etc" / "passwd"
    assert not web_app._is_safe_path(attack)


def test_is_safe_path_allows_project_root_files(web_app):
    """Files inside the project root are allowed."""
    project = Path(web_app.DEFAULT_ROOT) / "docs" / "example.md"
    assert web_app._is_safe_path(project)


def test_is_safe_path_allows_audit_tool_files(web_app):
    """Files inside tools/audit/ are allowed."""
    audit = web_app.AUDIT_ROOT / "audit_config.yaml"
    assert web_app._is_safe_path(audit)


def test_is_safe_path_handles_typeerror_gracefully(web_app):
    """Malformed inputs should not raise; they should return False."""
    # Path("") resolves to cwd — may or may not be safe depending on cwd.
    # We only assert it doesn't crash.
    try:
        web_app._is_safe_path(Path(""))
    except Exception as e:
        pytest.fail(f"_is_safe_path raised on empty path: {e}")


def test_allowed_roots_covers_both_trees(web_app):
    """Helper must report both the engine root and the audit-tool root."""
    roots = web_app._allowed_roots()
    assert Path(web_app.DEFAULT_ROOT).resolve() in roots
    assert web_app.AUDIT_ROOT.resolve() in roots


# --- Flask test client: verify the 403 responses at the HTTP boundary ---


@pytest.fixture
def client(web_app):
    web_app.app.config["TESTING"] = True
    with web_app.app.test_client() as c:
        yield c


def test_api_report_rejects_etc_passwd(client):
    """GET /api/report?path=/etc/passwd must return 403."""
    response = client.get("/api/report?path=/etc/passwd")
    assert response.status_code == 403


def test_api_config_rejects_etc_shadow(client):
    """GET /api/config?path=/etc/shadow must return 403."""
    response = client.get("/api/config?path=/etc/shadow")
    assert response.status_code == 403


def test_api_init_rejects_ssh_overwrite(client):
    """POST /api/init with output_path outside the tree must return 403."""
    response = client.post(
        "/api/init",
        json={
            "project_root": "/tmp/fakeproj",
            "output_path": "/home/somebody/.ssh/authorized_keys",
        },
    )
    assert response.status_code == 403


def test_api_init_rejects_non_yaml_suffix(client, web_app):
    """output_path that isn't .yaml/.yml must return 400."""
    # Land inside allowed root so we hit the suffix check, not the path check.
    tmp_out = str(Path(web_app.DEFAULT_ROOT) / "docs" / "attacker.txt")
    response = client.post(
        "/api/init",
        json={
            "project_root": web_app.DEFAULT_ROOT,
            "output_path": tmp_out,
        },
    )
    assert response.status_code == 400
