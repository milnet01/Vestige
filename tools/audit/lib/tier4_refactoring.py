"""Tier 4: Refactoring opportunity analysis — code smell detection."""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

# ---------------------------------------------------------------------------
# Language-specific regex patterns
# ---------------------------------------------------------------------------

# Function signature extraction — captures (func_name, params_string)
_FUNC_SIG_PATTERNS: dict[str, re.Pattern] = {
    "cpp": re.compile(
        r'^\s*(?:(?:static|virtual|inline|explicit|constexpr|const|unsigned|signed|'
        r'void|int|float|double|bool|char|auto|std::\w+|[\w:]+)\s+)*'
        r'[*&]*\s*(\w+)\s*\(([^)]*)\)',
        re.MULTILINE,
    ),
    "python": re.compile(
        r'^\s*(?:async\s+)?def\s+(\w+)\s*\(([^)]*)\)',
        re.MULTILINE,
    ),
    "rust": re.compile(
        r'^\s*(?:pub(?:\([\w:]+\))?\s+)?(?:async\s+)?fn\s+(\w+)\s*(?:<[^>]*>)?\s*\(([^)]*)\)',
        re.MULTILINE,
    ),
    "go": re.compile(
        r'^\s*func\s+(?:\(\w+\s+\*?\w+\)\s+)?(\w+)\s*\(([^)]*)\)',
        re.MULTILINE,
    ),
    "java": re.compile(
        r'^\s*(?:(?:public|private|protected|static|final|abstract|synchronized)\s+)*'
        r'[\w<>\[\],\s]+\s+(\w+)\s*\(([^)]*)\)',
        re.MULTILINE,
    ),
    "javascript": re.compile(
        r'(?:function\s+(\w+)\s*\(([^)]*)\)'
        r'|(\w+)\s*(?::\s*\([^)]*\)\s*=>|=\s*(?:async\s+)?(?:function\s*)?\(([^)]*)\)))',
        re.MULTILINE,
    ),
    "typescript": re.compile(
        r'(?:function\s+(\w+)\s*(?:<[^>]*>)?\s*\(([^)]*)\)'
        r'|(\w+)\s*(?::\s*\([^)]*\)\s*=>|=\s*(?:async\s+)?(?:function\s*)?\(([^)]*)\)))',
        re.MULTILINE,
    ),
}

# Function definition counting (simpler — just counts occurrences)
_FUNC_DEF_PATTERNS: dict[str, re.Pattern] = {
    "cpp": re.compile(
        r'^\s*(?!(?:if|else|for|while|switch|catch|return|throw|case|do)\b)'
        r'(?:[\w:*&<>,\s]+?)\s+\w+\s*\([^)]*\)\s*(?:const|override|noexcept|final|\s)*\{',
        re.MULTILINE,
    ),
    "python": re.compile(r'^\s*(?:async\s+)?def\s+\w+\s*\(', re.MULTILINE),
    "rust": re.compile(r'^\s*(?:pub(?:\([\w:]+\))?\s+)?(?:async\s+)?fn\s+\w+', re.MULTILINE),
    "go": re.compile(r'^\s*func\s+', re.MULTILINE),
    "java": re.compile(
        r'^\s*(?:(?:public|private|protected|static|final|abstract|synchronized)\s+)+'
        r'[\w<>\[\],\s]+\s+\w+\s*\([^)]*\)\s*(?:throws\s+[\w,\s]+)?\s*\{',
        re.MULTILINE,
    ),
    "javascript": re.compile(
        r'(?:function\s+\w+|(?:const|let|var)\s+\w+\s*=\s*(?:async\s+)?(?:function\s*)?\(|'
        r'(?:async\s+)?\w+\s*\([^)]*\)\s*\{)',
        re.MULTILINE,
    ),
    "typescript": re.compile(
        r'(?:function\s+\w+|(?:const|let|var)\s+\w+\s*=\s*(?:async\s+)?(?:function\s*)?\(|'
        r'(?:async\s+)?\w+\s*\([^)]*\)\s*(?::\s*\w+)?\s*\{)',
        re.MULTILINE,
    ),
}

# Map file extensions to language keys
_EXT_TO_LANG: dict[str, str] = {
    ".cpp": "cpp", ".hpp": "cpp", ".cxx": "cpp", ".cc": "cpp",
    ".c": "cpp", ".h": "cpp",
    ".py": "python",
    ".rs": "rust",
    ".go": "go",
    ".java": "java",
    ".js": "javascript", ".jsx": "javascript", ".mjs": "javascript",
    ".ts": "typescript", ".tsx": "typescript",
}


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class RefactoringResult:
    """Results of refactoring opportunity analysis."""
    long_param_lists: list[dict[str, Any]] = field(default_factory=list)
    god_files: list[dict[str, Any]] = field(default_factory=list)
    large_switch_chains: list[dict[str, Any]] = field(default_factory=list)
    deep_nesting: list[dict[str, Any]] = field(default_factory=list)
    similar_signatures: list[dict[str, Any]] = field(default_factory=list)
    total_smells: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "long_param_lists": self.long_param_lists[:20],
            "god_files": self.god_files[:15],
            "large_switch_chains": self.large_switch_chains[:15],
            "deep_nesting": self.deep_nesting[:20],
            "similar_signatures": self.similar_signatures[:15],
            "total_smells": self.total_smells,
        }


# ---------------------------------------------------------------------------
# Detector: Long parameter lists
# ---------------------------------------------------------------------------

def _count_params(param_str: str, lang: str) -> int:
    """Count meaningful parameters in a parameter string."""
    if not param_str.strip():
        return 0

    params = [p.strip() for p in param_str.split(",") if p.strip()]

    if lang == "python":
        params = [p for p in params
                  if p not in ("self", "cls")
                  and not p.startswith("*")
                  and not p.startswith("/")]
    elif lang == "rust":
        params = [p for p in params if p not in ("&self", "&mut self", "self")]

    return len(params)


def _detect_long_params(
    content: str, lang: str, rel_path: str, max_params: int,
) -> list[dict[str, Any]]:
    """Find functions with too many parameters."""
    pattern = _FUNC_SIG_PATTERNS.get(lang)
    if not pattern:
        return []

    results: list[dict[str, Any]] = []

    for m in pattern.finditer(content):
        if lang in ("javascript", "typescript"):
            # JS/TS pattern has alternation groups
            name = m.group(1) or m.group(3) or ""
            params = m.group(2) or m.group(4) or ""
        else:
            name = m.group(1)
            params = m.group(2)

        if not name:
            continue

        count = _count_params(params, lang)
        if count > max_params:
            line_num = content[:m.start()].count("\n") + 1
            results.append({
                "name": name,
                "file": rel_path,
                "line": line_num,
                "params": count,
            })

    return results


# ---------------------------------------------------------------------------
# Detector: God files
# ---------------------------------------------------------------------------

def _detect_god_files(
    content: str, lang: str, rel_path: str, max_functions: int,
) -> dict[str, Any] | None:
    """Detect files with too many function definitions."""
    pattern = _FUNC_DEF_PATTERNS.get(lang)
    if not pattern:
        return None

    matches = pattern.findall(content)
    count = len(matches)

    if count > max_functions:
        return {"file": rel_path, "functions": count}
    return None


# ---------------------------------------------------------------------------
# Detector: Large switch/if-else chains
# ---------------------------------------------------------------------------

_CASE_RE = re.compile(r'^\s*case\s+', re.MULTILINE)
_MATCH_ARM_RE = re.compile(r'^\s*\S+.*=>', re.MULTILINE)  # Rust match arms


def _detect_switch_chains(
    content: str, lang: str, rel_path: str,
    max_cases: int, max_elifs: int,
) -> list[dict[str, Any]]:
    """Detect large switch/case and else-if/elif chains."""
    results: list[dict[str, Any]] = []
    lines = content.splitlines()

    # --- Switch/case detection ---
    if lang != "python":
        case_count = 0
        chain_start = 0

        for i, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("case ") or stripped.startswith("case\t"):
                if case_count == 0:
                    chain_start = i
                case_count += 1
            elif stripped.startswith("default:") or stripped.startswith("default :"):
                pass  # don't reset
            elif case_count > 0 and stripped.startswith("}"):
                if case_count > max_cases:
                    results.append({
                        "file": rel_path,
                        "line": chain_start,
                        "count": case_count,
                        "type": "case labels",
                    })
                case_count = 0

        # Handle unclosed switch at end of file
        if case_count > max_cases:
            results.append({
                "file": rel_path,
                "line": chain_start,
                "count": case_count,
                "type": "case labels",
            })

    # --- Rust match arms ---
    if lang == "rust":
        arm_count = 0
        chain_start = 0
        in_match = False

        for i, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if re.match(r'match\s+', stripped):
                in_match = True
                arm_count = 0
                chain_start = i
            elif in_match and "=>" in stripped:
                arm_count += 1
            elif in_match and stripped.startswith("}"):
                if arm_count > max_cases:
                    results.append({
                        "file": rel_path,
                        "line": chain_start,
                        "count": arm_count,
                        "type": "match arms",
                    })
                in_match = False
                arm_count = 0

    # --- else-if / elif chains ---
    elif_count = 0
    chain_start = 0
    in_elif_body = False

    for i, line in enumerate(lines, start=1):
        stripped = line.lstrip()

        is_elif = False
        if lang == "python":
            is_elif = stripped.startswith("elif ")
        else:
            is_elif = bool(re.match(r'\}?\s*else\s+if\s*[\({]', stripped)
                           or re.match(r'else\s+if\s*[\({]', stripped))

        if is_elif:
            if elif_count == 0:
                chain_start = i
            elif_count += 1
            in_elif_body = True
        elif elif_count > 0:
            # For Python: indented body lines don't break the chain
            # For C-family: lines inside braces don't break the chain
            is_body_line = False
            if lang == "python":
                indent = len(line) - len(line.lstrip()) if line.strip() else 0
                is_body_line = indent > 0 or not line.strip()
            else:
                # In C-family, else-if bodies are inside braces; a new
                # top-level statement (no leading whitespace, not a brace)
                # signals end of the chain
                is_body_line = bool(stripped.startswith("}") or
                                    (in_elif_body and not stripped.startswith("if ")
                                     and not stripped.startswith("else ")
                                     and not stripped.startswith("for ")
                                     and not stripped.startswith("while ")
                                     and not stripped.startswith("switch ")
                                     and not stripped.startswith("return ")
                                     and not stripped.startswith("case ")))

            # Check for chain-ending patterns
            is_else_final = False
            if lang == "python":
                is_else_final = stripped.startswith("else:")
            else:
                is_else_final = bool(re.match(r'\}?\s*else\s*\{', stripped))

            if is_else_final:
                # else at end of elif chain — finalize
                if elif_count > max_elifs:
                    results.append({
                        "file": rel_path,
                        "line": chain_start,
                        "count": elif_count,
                        "type": "else-if chain",
                    })
                elif_count = 0
                in_elif_body = False
            elif not is_body_line:
                if elif_count > max_elifs:
                    results.append({
                        "file": rel_path,
                        "line": chain_start,
                        "count": elif_count,
                        "type": "else-if chain",
                    })
                elif_count = 0
                in_elif_body = False

    # Handle chain at end of file
    if elif_count > max_elifs:
        results.append({
            "file": rel_path,
            "line": chain_start,
            "count": elif_count,
            "type": "else-if chain",
        })

    return results


# ---------------------------------------------------------------------------
# Detector: Deep nesting
# ---------------------------------------------------------------------------

def _detect_deep_nesting(
    content: str, lang: str, rel_path: str, max_depth: int,
) -> list[dict[str, Any]]:
    """Detect functions with excessive nesting depth."""
    results: list[dict[str, Any]] = []

    if lang == "python":
        return _detect_deep_nesting_python(content, rel_path, max_depth)

    # Brace-counting for C-family, Java, Go, Rust, JS, TS
    lines = content.splitlines()
    depth = 0
    func_name = ""
    func_line = 0
    func_base_depth = 0
    max_local_depth = 0
    in_function = False

    # Simple function detection regex
    func_re = _FUNC_SIG_PATTERNS.get(lang)

    for i, line in enumerate(lines, start=1):
        stripped = line.lstrip()

        # Skip string contents (rough approximation)
        code = re.sub(r'"(?:\\.|[^"\\])*"', '""', stripped)
        code = re.sub(r"'(?:\\.|[^'\\])*'", "''", code)

        open_braces = code.count("{")
        close_braces = code.count("}")

        # Detect function start
        if func_re and not in_function and depth <= 1:
            m = func_re.search(stripped)
            if m and "{" in stripped:
                if lang in ("javascript", "typescript"):
                    func_name = m.group(1) or m.group(3) or "anonymous"
                else:
                    func_name = m.group(1) or "unknown"
                func_line = i
                func_base_depth = depth
                max_local_depth = 0
                in_function = True

        depth += open_braces
        depth -= close_braces

        if in_function:
            relative_depth = depth - func_base_depth
            if relative_depth > max_local_depth:
                max_local_depth = relative_depth

            # Function ended
            if depth <= func_base_depth:
                if max_local_depth > max_depth:
                    results.append({
                        "name": func_name,
                        "file": rel_path,
                        "line": func_line,
                        "depth": max_local_depth,
                    })
                in_function = False

        depth = max(depth, 0)  # prevent negative from mismatched braces

    return results


def _detect_deep_nesting_python(
    content: str, rel_path: str, max_depth: int,
) -> list[dict[str, Any]]:
    """Detect deeply nested Python functions via indentation."""
    results: list[dict[str, Any]] = []
    lines = content.splitlines()

    func_name = ""
    func_line = 0
    func_indent = 0
    max_relative_indent = 0
    in_function = False

    for i, line in enumerate(lines, start=1):
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip())

        # Detect function definition
        m = re.match(r'^(\s*)(?:async\s+)?def\s+(\w+)\s*\(', line)
        if m:
            # Save previous function if needed
            if in_function and max_relative_indent > max_depth:
                results.append({
                    "name": func_name,
                    "file": rel_path,
                    "line": func_line,
                    "depth": max_relative_indent,
                })

            func_indent = indent
            func_name = m.group(2)
            func_line = i
            max_relative_indent = 0
            in_function = True
            continue

        if in_function:
            if indent <= func_indent and line.strip():
                # Exited function scope
                if max_relative_indent > max_depth:
                    results.append({
                        "name": func_name,
                        "file": rel_path,
                        "line": func_line,
                        "depth": max_relative_indent,
                    })
                in_function = False
            else:
                # Measure indent in units (4 spaces or 1 tab = 1 level)
                relative = indent - func_indent
                # Detect indent width from first indented line
                indent_unit = 4  # default
                if relative > 0 and relative < 8:
                    indent_unit = relative
                depth_level = relative // max(indent_unit, 1)
                if depth_level > max_relative_indent:
                    max_relative_indent = depth_level

    # Handle function at end of file
    if in_function and max_relative_indent > max_depth:
        results.append({
            "name": func_name,
            "file": rel_path,
            "line": func_line,
            "depth": max_relative_indent,
        })

    return results


# ---------------------------------------------------------------------------
# Detector: Similar function signatures
# ---------------------------------------------------------------------------

def _extract_param_types(param_str: str, lang: str) -> tuple[str, ...] | None:
    """Extract normalized parameter types from a parameter string."""
    if not param_str.strip():
        return None

    params = [p.strip() for p in param_str.split(",") if p.strip()]

    if lang == "python":
        # Python lacks static types in most signatures — skip
        return None

    types: list[str] = []
    for param in params:
        if lang in ("cpp", "java", "javascript", "typescript"):
            # C++/Java: "const std::string& name" -> type is everything before last word
            # Remove default values
            param = param.split("=")[0].strip()
            parts = param.split()
            if len(parts) >= 2:
                # Type is everything except the last token (the name)
                param_type = " ".join(parts[:-1])
                # Normalize: remove const, &, *
                param_type = re.sub(r'\b(const|volatile|mutable)\b', '', param_type)
                param_type = re.sub(r'[&*]', '', param_type).strip()
                if param_type:
                    types.append(param_type)
            elif len(parts) == 1:
                types.append(parts[0])  # Just a type name
        elif lang == "rust":
            # Rust: "name: Type" -> split on ':'
            if ":" in param:
                rust_type = param.split(":", 1)[1].strip()
                rust_type = re.sub(r'^&(?:mut\s+)?', '', rust_type)  # strip references
                if rust_type and rust_type not in ("self",):
                    types.append(rust_type)
        elif lang == "go":
            # Go: "name Type" or "name, name2 Type"
            parts = param.split()
            if parts:
                types.append(parts[-1])  # Last word is the type

    return tuple(types) if types else None


def _detect_similar_signatures(
    all_sigs: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Find functions with identical parameter type lists in different files."""
    # Group by normalized param types
    by_types: dict[tuple[str, ...], list[dict[str, Any]]] = {}
    for sig in all_sigs:
        types = sig.get("types")
        if types and len(types) >= 2:  # Only compare functions with 2+ params
            by_types.setdefault(types, []).append(sig)

    results: list[dict[str, Any]] = []
    for types, sigs in by_types.items():
        if len(sigs) < 2:
            continue

        # Only flag cross-file matches
        files = {s["file"] for s in sigs}
        if len(files) < 2:
            continue

        # Emit pairs (limit to avoid explosion)
        for i in range(min(len(sigs), 5)):
            for j in range(i + 1, min(len(sigs), 5)):
                if sigs[i]["file"] != sigs[j]["file"]:
                    results.append({
                        "name_a": sigs[i]["name"],
                        "file_a": sigs[i]["file"],
                        "line_a": sigs[i]["line"],
                        "name_b": sigs[j]["name"],
                        "file_b": sigs[j]["file"],
                        "line_b": sigs[j]["line"],
                        "param_types": ", ".join(types),
                    })

    return results


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def analyze_refactoring(config: Config) -> tuple[RefactoringResult, list[Finding]]:
    """Detect refactoring opportunities across the project.

    Returns (RefactoringResult, list of Finding).
    """
    result = RefactoringResult()
    findings: list[Finding] = []

    if not config.get("tier4", "refactoring", "enabled", default=True):
        log.info("Tier 4 refactoring: disabled")
        return result, findings

    max_params = config.get("tier4", "refactoring", "max_params", default=5)
    max_funcs = config.get("tier4", "refactoring", "max_functions_per_file", default=15)
    max_cases = config.get("tier4", "refactoring", "max_case_labels", default=7)
    max_elifs = config.get("tier4", "refactoring", "max_elif_chain", default=5)
    max_depth = config.get("tier4", "refactoring", "max_nesting_depth", default=4)
    max_findings = config.get("tier4", "refactoring", "max_findings", default=50)

    # Enumerate source files
    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    log.info("Tier 4 refactoring: analyzing %d files", len(all_files))

    all_signatures: list[dict[str, Any]] = []

    for fpath in all_files:
        try:
            content = fpath.read_text(errors="replace")
        except OSError:
            continue

        ext = fpath.suffix.lower()
        lang = _EXT_TO_LANG.get(ext, "")
        if not lang:
            continue

        rel = relative_path(fpath, config.root)

        # --- Long parameter lists ---
        long_params = _detect_long_params(content, lang, rel, max_params)
        result.long_param_lists.extend(long_params)

        # --- God files ---
        god = _detect_god_files(content, lang, rel, max_funcs)
        if god:
            result.god_files.append(god)

        # --- Switch/if-else chains ---
        chains = _detect_switch_chains(content, lang, rel, max_cases, max_elifs)
        result.large_switch_chains.extend(chains)

        # --- Deep nesting ---
        nesting = _detect_deep_nesting(content, lang, rel, max_depth)
        result.deep_nesting.extend(nesting)

        # --- Collect signatures for cross-file similarity ---
        sig_pattern = _FUNC_SIG_PATTERNS.get(lang)
        if sig_pattern and lang != "python":
            for m in sig_pattern.finditer(content):
                if lang in ("javascript", "typescript"):
                    name = m.group(1) or m.group(3) or ""
                    params = m.group(2) or m.group(4) or ""
                else:
                    name = m.group(1)
                    params = m.group(2)

                if not name:
                    continue

                types = _extract_param_types(params, lang)
                if types:
                    line_num = content[:m.start()].count("\n") + 1
                    all_signatures.append({
                        "name": name,
                        "file": rel,
                        "line": line_num,
                        "types": types,
                    })

    # --- Similar signatures (cross-file) ---
    result.similar_signatures = _detect_similar_signatures(all_signatures)

    # Sort results for consistent output
    result.long_param_lists.sort(key=lambda x: -x["params"])
    result.god_files.sort(key=lambda x: -x["functions"])
    result.large_switch_chains.sort(key=lambda x: -x["count"])
    result.deep_nesting.sort(key=lambda x: -x["depth"])

    # Count total smells
    result.total_smells = (
        len(result.long_param_lists)
        + len(result.god_files)
        + len(result.large_switch_chains)
        + len(result.deep_nesting)
        + len(result.similar_signatures)
    )

    # Generate findings
    finding_count = 0

    for item in result.long_param_lists:
        if finding_count >= max_findings:
            break
        findings.append(Finding(
            file=item["file"], line=item["line"],
            severity=Severity.LOW,
            category="refactoring", source_tier=4,
            title=f"Long parameter list: {item['name']}() has {item['params']} params",
            detail="Consider introducing a parameter object or builder pattern",
            pattern_name="long_param_list",
        ))
        finding_count += 1

    for item in result.god_files:
        if finding_count >= max_findings:
            break
        findings.append(Finding(
            file=item["file"], line=1,
            severity=Severity.MEDIUM,
            category="refactoring", source_tier=4,
            title=f"God file: {item['file']} has {item['functions']} function definitions",
            detail="Consider splitting into smaller, focused modules",
            pattern_name="god_file",
        ))
        finding_count += 1

    for item in result.large_switch_chains:
        if finding_count >= max_findings:
            break
        findings.append(Finding(
            file=item["file"], line=item["line"],
            severity=Severity.LOW,
            category="refactoring", source_tier=4,
            title=f"Large {item['type']}: {item['count']} branches",
            detail="Consider polymorphism, strategy pattern, or lookup table",
            pattern_name="large_switch",
        ))
        finding_count += 1

    for item in result.deep_nesting:
        if finding_count >= max_findings:
            break
        findings.append(Finding(
            file=item["file"], line=item["line"],
            severity=Severity.MEDIUM,
            category="refactoring", source_tier=4,
            title=f"Deep nesting in {item['name']}(): depth {item['depth']}",
            detail="Consider extracting inner logic into helper functions or using early returns",
            pattern_name="deep_nesting",
        ))
        finding_count += 1

    for item in result.similar_signatures:
        if finding_count >= max_findings:
            break
        findings.append(Finding(
            file=item["file_a"], line=item["line_a"],
            severity=Severity.INFO,
            category="refactoring", source_tier=4,
            title=f"Similar signature: {item['name_a']}() matches {item['name_b']}()",
            detail=f"Both take ({item['param_types']}) — consider shared base or template",
            pattern_name="similar_signature",
        ))
        finding_count += 1

    log.info("Tier 4 refactoring: %d smells (%d long params, %d god files, "
             "%d switch chains, %d deep nesting, %d similar sigs)",
             result.total_smells, len(result.long_param_lists),
             len(result.god_files), len(result.large_switch_chains),
             len(result.deep_nesting), len(result.similar_signatures))

    return result, findings
