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


# --- Input validation guards (FIXPLAN C1a) ---


def test_api_run_rejects_injection_in_base_ref(client, web_app):
    """`base_ref: HEAD; touch /tmp/pwn` must be rejected with 400."""
    response = client.post(
        "/api/run",
        json={
            "project_root": web_app.DEFAULT_ROOT,
            "config_path": web_app.DEFAULT_CONFIG,
            "base_ref": "HEAD; touch /tmp/pwn_c1a",
            "tiers": [1],
        },
    )
    assert response.status_code == 400
    # Confirm the injection did NOT run.
    assert not Path("/tmp/pwn_c1a").exists()


def test_api_run_rejects_backtick_in_base_ref(client, web_app):
    """Backtick and $() command substitution must be rejected."""
    for bad in ("`whoami`", "$(whoami)", "HEAD && rm -rf /"):
        response = client.post(
            "/api/run",
            json={
                "project_root": web_app.DEFAULT_ROOT,
                "config_path": web_app.DEFAULT_CONFIG,
                "base_ref": bad,
                "tiers": [1],
            },
        )
        assert response.status_code == 400, f"expected 400 for {bad!r}"


def test_api_run_accepts_normal_git_refs(client, web_app, monkeypatch):
    """Normal refs like `HEAD~1`, `origin/main`, `v1.2.3`, hashes must pass validation."""
    # Stub AuditSession.start so we don't actually kick off a subprocess.
    from web import audit_bridge
    monkeypatch.setattr(
        audit_bridge.AuditSession, "start",
        lambda self, **kw: None,
    )
    for good in ("HEAD", "HEAD~5", "v1.2.3", "origin/main", "abc123def456"):
        response = client.post(
            "/api/run",
            json={
                "project_root": web_app.DEFAULT_ROOT,
                "config_path": web_app.DEFAULT_CONFIG,
                "base_ref": good,
                "tiers": [1],
            },
        )
        assert response.status_code in (200, 409, 500), (
            f"unexpected {response.status_code} for {good!r}: {response.data!r}"
        )


def test_api_run_rejects_bad_tiers(client, web_app):
    """tiers must be a list of ints in [1,5]; reject strings, floats, huge ints."""
    for bad in ("[1,2,3]", [99], [0], [1.5], "not a list"):
        response = client.post(
            "/api/run",
            json={
                "project_root": web_app.DEFAULT_ROOT,
                "config_path": web_app.DEFAULT_CONFIG,
                "base_ref": "HEAD",
                "tiers": bad,
            },
        )
        assert response.status_code == 400, f"expected 400 for tiers={bad!r}"


# NVD API key shape-validation tests live in test_tier5_nvd.py (same
# _resolve_api_key function); keeping them close to the unit under test.
