# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.suppress — load_suppressions, filter_suppressed, save_suppression."""

from __future__ import annotations

from pathlib import Path

import pytest

from lib.findings import Finding, Severity
from lib.suppress import filter_suppressed, load_suppressions, save_suppression


# ---------------------------------------------------------------------------
# load_suppressions
# ---------------------------------------------------------------------------


class TestLoadSuppressionsEmpty:
    """load_suppressions with no file or empty file returns an empty set."""

    def test_no_file_returns_empty(self, tmp_path: Path):
        result = load_suppressions(tmp_path)
        assert result == set()

    def test_empty_file_returns_empty(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text("")
        result = load_suppressions(tmp_path)
        assert result == set()

    def test_only_blank_lines(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text("\n\n  \n\n")
        result = load_suppressions(tmp_path)
        assert result == set()


class TestLoadSuppressionsComments:
    """load_suppressions should skip comment lines starting with #."""

    def test_comment_lines_skipped(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text(
            "# This is a comment\n"
            "# Another comment\n"
        )
        result = load_suppressions(tmp_path)
        assert result == set()

    def test_comments_and_keys_mixed(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text(
            "# Header comment\n"
            "abc123def456ab\n"
            "# Middle comment\n"
            "1234567890abcdef\n"
        )
        result = load_suppressions(tmp_path)
        assert result == {"abc123def456ab", "1234567890abcdef"}


class TestLoadSuppressionsInlineAnnotations:
    """Inline annotations after the hash key should be ignored."""

    def test_inline_annotation_stripped(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text(
            "abc123def456ab  # reason: false positive\n"
        )
        result = load_suppressions(tmp_path)
        assert result == {"abc123def456ab"}

    def test_multiple_spaces_before_annotation(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text(
            "abc123def456ab      # lots of spaces\n"
        )
        result = load_suppressions(tmp_path)
        assert result == {"abc123def456ab"}

    def test_tab_separated_annotation(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text(
            "abc123def456ab\t# tab separated\n"
        )
        result = load_suppressions(tmp_path)
        assert result == {"abc123def456ab"}


class TestLoadSuppressionsCustomFilename:
    """load_suppressions should accept a custom filename."""

    def test_custom_filename(self, tmp_path: Path):
        (tmp_path / "custom_suppress.txt").write_text("abc123\n")
        result = load_suppressions(tmp_path, filename="custom_suppress.txt")
        assert result == {"abc123"}


# ---------------------------------------------------------------------------
# filter_suppressed
# ---------------------------------------------------------------------------


class TestFilterSuppressed:
    """filter_suppressed should remove findings whose dedup_key is suppressed."""

    def test_no_suppressions_returns_all(self):
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue"),
        ]
        active, count = filter_suppressed(findings, set())
        assert len(active) == 1
        assert count == 0

    def test_matching_key_removes_finding(self):
        f = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue")
        active, count = filter_suppressed([f], {f.dedup_key})
        assert len(active) == 0
        assert count == 1

    def test_partial_match_keeps_finding(self):
        f = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue")
        active, count = filter_suppressed([f], {"not_a_real_key"})
        assert len(active) == 1
        assert count == 0

    def test_mixed_suppressed_and_active(self):
        f1 = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue1")
        f2 = Finding("b.cpp", 2, Severity.LOW, "cat", 1, "issue2")
        f3 = Finding("c.cpp", 3, Severity.INFO, "cat", 1, "issue3")
        active, count = filter_suppressed([f1, f2, f3], {f2.dedup_key})
        assert len(active) == 2
        assert count == 1
        assert all(f.dedup_key != f2.dedup_key for f in active)

    def test_empty_findings_returns_empty(self):
        active, count = filter_suppressed([], {"abc"})
        assert active == []
        assert count == 0


# ---------------------------------------------------------------------------
# save_suppression
# ---------------------------------------------------------------------------


class TestSaveSuppression:
    """save_suppression should create/append to the suppress file."""

    def test_creates_file_if_missing(self, tmp_path: Path):
        save_suppression(tmp_path, "abc123", annotation="test")
        path = tmp_path / ".audit_suppress"
        assert path.exists()
        content = path.read_text()
        assert "abc123" in content

    def test_appends_to_existing(self, tmp_path: Path):
        (tmp_path / ".audit_suppress").write_text("existing_key  # old\n")
        save_suppression(tmp_path, "new_key", annotation="new")
        content = (tmp_path / ".audit_suppress").read_text()
        assert "existing_key" in content
        assert "new_key" in content

    def test_annotation_included(self, tmp_path: Path):
        save_suppression(tmp_path, "abc123", annotation="false positive in test")
        content = (tmp_path / ".audit_suppress").read_text()
        assert "false positive in test" in content

    def test_header_written_on_creation(self, tmp_path: Path):
        save_suppression(tmp_path, "abc123", annotation="test")
        content = (tmp_path / ".audit_suppress").read_text()
        assert "# Audit finding suppressions" in content

    def test_custom_filename(self, tmp_path: Path):
        save_suppression(tmp_path, "abc123", annotation="test", filename="custom.txt")
        assert (tmp_path / "custom.txt").exists()
