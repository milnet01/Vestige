# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.verified — D3 human-review verification layer."""

from __future__ import annotations

from pathlib import Path

from lib.findings import Finding, Severity
from lib.verified import (
    apply_verified_tags,
    load_verified,
    remove_verified,
    save_verified,
)


# ---------------------------------------------------------------------------
# load_verified — empty / absent files
# ---------------------------------------------------------------------------


class TestLoadVerifiedEmpty:

    def test_no_file_returns_empty(self, tmp_path: Path):
        assert load_verified(tmp_path) == set()

    def test_empty_file_returns_empty(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text("")
        assert load_verified(tmp_path) == set()

    def test_only_blank_lines(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text("\n\n  \n\n")
        assert load_verified(tmp_path) == set()


# ---------------------------------------------------------------------------
# load_verified — comments and annotations
# ---------------------------------------------------------------------------


class TestLoadVerifiedComments:

    def test_comment_lines_skipped(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "# This is a comment\n"
            "# Another comment\n"
        )
        assert load_verified(tmp_path) == set()

    def test_comments_and_keys_mixed(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "# Header comment\n"
            "abc123def456ab\n"
            "# Middle comment\n"
            "1234567890abcdef\n"
        )
        assert load_verified(tmp_path) == {"abc123def456ab", "1234567890abcdef"}


class TestLoadVerifiedInlineAnnotations:

    def test_inline_annotation_stripped(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "abc123def456ab  # confirmed real 2026-04-14\n"
        )
        assert load_verified(tmp_path) == {"abc123def456ab"}

    def test_multiple_spaces_before_annotation(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "abc123def456ab      # lots of spaces\n"
        )
        assert load_verified(tmp_path) == {"abc123def456ab"}

    def test_tab_separated_annotation(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "abc123def456ab\t# tab separated\n"
        )
        assert load_verified(tmp_path) == {"abc123def456ab"}


class TestLoadVerifiedCustomFilename:

    def test_custom_filename(self, tmp_path: Path):
        (tmp_path / "custom_verified.txt").write_text("abc123\n")
        assert load_verified(tmp_path, filename="custom_verified.txt") == {"abc123"}


# ---------------------------------------------------------------------------
# apply_verified_tags
# ---------------------------------------------------------------------------


class TestApplyVerifiedTags:

    def test_empty_verified_set_is_noop(self):
        f = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue")
        count = apply_verified_tags([f], set())
        assert count == 0
        assert f.verified is False

    def test_matching_key_is_tagged(self):
        f = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue")
        count = apply_verified_tags([f], {f.dedup_key})
        assert count == 1
        assert f.verified is True

    def test_non_matching_key_is_not_tagged(self):
        f = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue")
        count = apply_verified_tags([f], {"not_a_real_key"})
        assert count == 0
        assert f.verified is False

    def test_mixed_tagged_and_untagged(self):
        f1 = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "issue1")
        f2 = Finding("b.cpp", 2, Severity.LOW, "cat", 1, "issue2")
        f3 = Finding("c.cpp", 3, Severity.INFO, "cat", 1, "issue3")
        count = apply_verified_tags([f1, f2, f3], {f1.dedup_key, f3.dedup_key})
        assert count == 2
        assert f1.verified is True
        assert f2.verified is False
        assert f3.verified is True

    def test_empty_findings_returns_zero(self):
        assert apply_verified_tags([], {"abc"}) == 0

    def test_verified_does_not_change_severity(self):
        """Verification is a tag — it should never mutate severity."""
        f = Finding("a.cpp", 1, Severity.LOW, "cat", 1, "issue")
        original_severity = f.severity
        apply_verified_tags([f], {f.dedup_key})
        assert f.severity == original_severity

    def test_verified_does_not_remove_from_list(self):
        """apply_verified_tags is a tagger, not a filter."""
        f1 = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "keep")
        f2 = Finding("b.cpp", 2, Severity.LOW, "cat", 1, "also keep")
        findings = [f1, f2]
        apply_verified_tags(findings, {f1.dedup_key})
        # Both findings remain
        assert len(findings) == 2


# ---------------------------------------------------------------------------
# save_verified
# ---------------------------------------------------------------------------


class TestSaveVerified:

    def test_creates_file_if_missing(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="test")
        path = tmp_path / ".audit_verified"
        assert path.exists()
        assert "abc123" in path.read_text()

    def test_appends_to_existing(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text("existing_key  # old\n")
        save_verified(tmp_path, "new_key", annotation="new")
        content = (tmp_path / ".audit_verified").read_text()
        assert "existing_key" in content
        assert "new_key" in content

    def test_annotation_included(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="confirmed real")
        content = (tmp_path / ".audit_verified").read_text()
        assert "confirmed real" in content

    def test_header_written_on_creation(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="test")
        content = (tmp_path / ".audit_verified").read_text()
        assert "# Audit finding verifications" in content
        # Header should explain that verification is a tag, not a filter
        assert "VERIFIED" in content or "tag" in content.lower()

    def test_custom_filename(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="test", filename="custom.txt")
        assert (tmp_path / "custom.txt").exists()

    def test_round_trip_via_load(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="first")
        save_verified(tmp_path, "def456", annotation="second")
        assert load_verified(tmp_path) == {"abc123", "def456"}


# ---------------------------------------------------------------------------
# remove_verified
# ---------------------------------------------------------------------------


class TestRemoveVerified:

    def test_remove_existing_key(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="test")
        assert remove_verified(tmp_path, "abc123") is True
        assert load_verified(tmp_path) == set()

    def test_remove_missing_key_returns_false(self, tmp_path: Path):
        assert remove_verified(tmp_path, "abc123") is False

    def test_remove_from_empty_file_returns_false(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text("")
        assert remove_verified(tmp_path, "abc123") is False

    def test_remove_preserves_other_keys(self, tmp_path: Path):
        save_verified(tmp_path, "abc123", annotation="keep1")
        save_verified(tmp_path, "def456", annotation="remove")
        save_verified(tmp_path, "ghi789", annotation="keep2")
        assert remove_verified(tmp_path, "def456") is True
        remaining = load_verified(tmp_path)
        assert remaining == {"abc123", "ghi789"}

    def test_remove_preserves_comments(self, tmp_path: Path):
        (tmp_path / ".audit_verified").write_text(
            "# Header comment\n"
            "abc123  # keep\n"
            "# middle comment\n"
            "def456  # remove\n"
        )
        remove_verified(tmp_path, "def456")
        content = (tmp_path / ".audit_verified").read_text()
        assert "# Header comment" in content
        assert "# middle comment" in content
        assert "abc123" in content
        assert "def456" not in content


# ---------------------------------------------------------------------------
# Integration: deduplicate preserves the verified flag
# ---------------------------------------------------------------------------


class TestVerifiedSurvivesDedup:
    """D3: a verified tag must not be silently lost when two findings share a dedup_key."""

    def test_verified_on_swap_preserved(self):
        """Lower-tier finding replaces higher-tier; verified tag is
        merged from the discarded one."""
        from lib.findings import deduplicate

        high = Finding("f.cpp", 10, Severity.HIGH, "cat", 3, "dup")
        high.verified = True  # reviewer confirmed on a previous run
        low = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        # low has verified=False by default
        result = deduplicate([high, low])
        assert len(result) == 1
        assert result[0].source_tier == 1  # low tier kept
        assert result[0].verified is True  # tag survived

    def test_verified_on_keep_preserved(self):
        """Higher-tier arrives after a lower-tier — the kept (lower)
        finding absorbs the verified tag from the discarded one."""
        from lib.findings import deduplicate

        low = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        high = Finding("f.cpp", 10, Severity.HIGH, "cat", 3, "dup")
        high.verified = True
        result = deduplicate([low, high])
        assert len(result) == 1
        assert result[0].source_tier == 1
        assert result[0].verified is True

    def test_both_verified_stays_verified(self):
        from lib.findings import deduplicate

        a = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        a.verified = True
        b = Finding("f.cpp", 10, Severity.HIGH, "cat", 3, "dup")
        b.verified = True
        result = deduplicate([a, b])
        assert result[0].verified is True

    def test_neither_verified_stays_false(self):
        from lib.findings import deduplicate

        a = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        b = Finding("f.cpp", 10, Severity.HIGH, "cat", 3, "dup")
        result = deduplicate([a, b])
        assert result[0].verified is False
