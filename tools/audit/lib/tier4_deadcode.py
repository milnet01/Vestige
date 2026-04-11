"""Tier 4: Dead code detection — unused functions, unused includes, unreferenced symbols."""

from __future__ import annotations

import logging
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

# Regex patterns for C++ function declarations and definitions
# Matches: ReturnType FunctionName(params);  or  ReturnType ClassName::FunctionName(params) {
_FUNC_DECL_RE = re.compile(
    r"^\s*"
    r"(?:virtual\s+|static\s+|inline\s+|explicit\s+|friend\s+|constexpr\s+)*"
    r"(?:const\s+)?"
    r"(?:[\w:]+(?:<[^>]*>)?[\s*&]+)"  # return type
    r"(~?\w+)"                         # function name (group 1)
    r"\s*\([^)]*\)"                    # parameters
    r"\s*(?:const|override|noexcept|final|\s)*"
    r"\s*;",                           # declaration ends with ;
    re.MULTILINE,
)

_FUNC_DEF_RE = re.compile(
    r"^\s*"
    r"(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+)*"
    r"(?:const\s+)?"
    r"(?:[\w:]+(?:<[^>]*>)?[\s*&]+)"  # return type
    r"(?:(\w+)::)?"                    # optional ClassName:: (group 1)
    r"(~?\w+)"                         # function name (group 2)
    r"\s*\([^)]*\)"                    # parameters
    r"\s*(?:const|override|noexcept|final|\s)*"
    r"\s*\{",                          # definition opens with {
    re.MULTILINE,
)

# Signal/slot declarations in Qt headers
_QT_SIGNAL_SLOT_RE = re.compile(
    r"^\s*(?:void\s+)"
    r"(\w+)"                           # signal/slot name
    r"\s*\([^)]*\)\s*;",
    re.MULTILINE,
)

# Include directive
_INCLUDE_RE = re.compile(r'^\s*#include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# System/standard includes that should not be flagged
_SYSTEM_INCLUDE_PREFIXES = (
    "Q", "q", "std", "GL/", "gl", "GLFW", "glm", "stb_", "imgui",
    "nlohmann", "ft2build", "freetype", "al.h", "alc.h",
)


@dataclass
class DeadCodeAnalysis:
    """Results of dead code analysis."""
    unused_functions: list[dict[str, Any]] = field(default_factory=list)
    unused_includes: list[dict[str, Any]] = field(default_factory=list)
    total_declarations: int = 0
    total_definitions: int = 0
    total_includes_checked: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "unused_functions": self.unused_functions[:30],
            "unused_includes": self.unused_includes[:30],
            "total_declarations": self.total_declarations,
            "total_definitions": self.total_definitions,
            "total_includes_checked": self.total_includes_checked,
        }


def analyze_dead_code(config: Config) -> tuple[DeadCodeAnalysis, list[Finding]]:
    """Detect unused functions and includes across the codebase."""
    lang = config.language

    if lang in ("cpp", "c"):
        return _analyze_cpp(config)
    elif lang == "python":
        return _analyze_python(config)
    elif lang == "rust":
        return _analyze_rust(config)
    elif lang in ("js", "ts"):
        return _analyze_js_ts(config)
    else:
        # Fallback: try C++ analysis (covers most compiled languages)
        return _analyze_cpp(config)


def _analyze_cpp(config: Config) -> tuple[DeadCodeAnalysis, list[Finding]]:
    """Detect unused functions and includes in C/C++ codebases."""
    result = DeadCodeAnalysis()
    findings: list[Finding] = []

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    if not all_files:
        return result, findings

    header_files = [f for f in all_files if f.suffix in (".h", ".hpp")]
    source_files = [f for f in all_files if f.suffix in (".cpp", ".cxx", ".cc", ".c")]

    log.info("Dead code analysis: %d headers, %d sources", len(header_files), len(source_files))

    # Phase 1: Collect all function declarations from headers
    declarations: dict[str, list[dict]] = defaultdict(list)  # func_name -> [{file, line}]
    for hfile in header_files:
        try:
            content = hfile.read_text(errors="replace")
        except OSError:
            continue

        # Skip signal/slot sections (Qt generates connection code)
        in_signals = False
        in_slots = False
        lines = content.splitlines()

        for i, line in enumerate(lines, start=1):
            stripped = line.strip()
            if stripped in ("signals:", "Q_SIGNALS:"):
                in_signals = True
                in_slots = False
                continue
            if stripped in ("public slots:", "private slots:", "protected slots:",
                           "public Q_SLOTS:", "private Q_SLOTS:", "protected Q_SLOTS:"):
                in_slots = True
                in_signals = False
                continue
            if stripped.startswith(("public:", "private:", "protected:", "};", "class ")):
                in_signals = False
                in_slots = False

            # Skip signal declarations — they're auto-generated by moc
            if in_signals:
                continue

            m = _FUNC_DECL_RE.match(line)
            if m:
                func_name = m.group(1)
                # Skip constructors, destructors, operators, and common Qt overrides
                if (func_name.startswith("~") or
                    func_name.startswith("operator") or
                    func_name in _SKIP_FUNCTIONS):
                    continue
                rel = relative_path(hfile, config.root)
                declarations[func_name].append({
                    "file": rel,
                    "line": i,
                    "is_slot": in_slots,
                })
                result.total_declarations += 1

    # Phase 2: Build a set of all referenced symbols across all source files
    all_content: dict[Path, str] = {}
    all_references: set[str] = set()
    for f in all_files:
        try:
            content = f.read_text(errors="replace")
            all_content[f] = content
        except OSError:
            continue

        # Extract all word-boundary identifiers that could be function calls
        for word in re.findall(r"\b(\w+)\s*\(", content):
            all_references.add(word)
        # Also catch references without parens (function pointers, &ClassName::method, etc.)
        for word in re.findall(r"&\w+::(\w+)", content):
            all_references.add(word)
        # CONNECT macro references
        for word in re.findall(r"(?:SIGNAL|SLOT)\((\w+)", content):
            all_references.add(word)
        # Qt5-style connect references
        for word in re.findall(r"&\w+::(\w+)", content):
            all_references.add(word)

    # Phase 3: Find declarations that are never referenced elsewhere
    for func_name, decl_list in declarations.items():
        if func_name in all_references:
            continue

        # Check if the function is defined anywhere (it might be defined but never called)
        is_defined = False
        for f, content in all_content.items():
            if f.suffix in (".h", ".hpp"):
                continue
            if re.search(rf"\b{re.escape(func_name)}\b", content):
                is_defined = True
                break

        if not is_defined:
            # Declared but never defined or called — strong signal
            for decl in decl_list:
                if decl["is_slot"]:
                    continue  # Slots may be connected dynamically
                result.unused_functions.append({
                    "name": func_name,
                    "file": decl["file"],
                    "line": decl["line"],
                    "reason": "Declared but never defined or referenced",
                })
                findings.append(Finding(
                    file=decl["file"],
                    line=decl["line"],
                    severity=Severity.LOW,
                    category="dead_code",
                    source_tier=4,
                    title=f"Unused function declaration: {func_name}()",
                    detail="Declared in header but never defined or called anywhere",
                    pattern_name="unused_function_decl",
                ))
        else:
            # Defined but the name never appears as a call — possible dead code
            # Only flag if it's not a virtual/override (could be called polymorphically)
            for decl in decl_list:
                if decl["is_slot"]:
                    continue
                # Check the declaration line for virtual/override
                hfile_path = config.root / decl["file"]
                if hfile_path in all_content:
                    hcontent = all_content[hfile_path]
                    hlines = hcontent.splitlines()
                    if decl["line"] <= len(hlines):
                        decl_line = hlines[decl["line"] - 1]
                        if "virtual" in decl_line or "override" in decl_line:
                            continue

                result.unused_functions.append({
                    "name": func_name,
                    "file": decl["file"],
                    "line": decl["line"],
                    "reason": "Defined but never called",
                })
                findings.append(Finding(
                    file=decl["file"],
                    line=decl["line"],
                    severity=Severity.LOW,
                    category="dead_code",
                    source_tier=4,
                    title=f"Potentially unused function: {func_name}()",
                    detail="Defined but never called from any source file",
                    pattern_name="unused_function_def",
                ))

    # Phase 4: Unused includes in source files
    result.unused_includes, include_findings = _detect_unused_includes(
        source_files, all_content, config
    )
    result.total_includes_checked = sum(1 for f in source_files if f in all_content)
    findings.extend(include_findings)

    log.info("Dead code: %d unused functions, %d unused includes",
             len(result.unused_functions), len(result.unused_includes))
    return result, findings


def _detect_unused_includes(
    source_files: list[Path],
    all_content: dict[Path, str],
    config: Config,
) -> tuple[list[dict], list[Finding]]:
    """Detect #include directives in .cpp files where no symbol from the header is used.

    Uses a heuristic: extract the class/type name from the include path,
    then check if that name appears in the source file.
    """
    unused: list[dict] = []
    findings: list[Finding] = []

    for cpp_file in source_files:
        if cpp_file not in all_content:
            continue
        content = all_content[cpp_file]
        rel = relative_path(cpp_file, config.root)

        for m in _INCLUDE_RE.finditer(content):
            include_path = m.group(1)
            line_num = content[:m.start()].count("\n") + 1

            # Skip system/standard includes
            if any(include_path.startswith(p) for p in _SYSTEM_INCLUDE_PREFIXES):
                continue
            if "/" not in include_path and not include_path.endswith((".h", ".hpp")):
                continue

            # Derive expected symbols from the include
            symbols = _symbols_from_include(include_path)
            if not symbols:
                continue

            # Check if any derived symbol appears in the file (excluding the #include line itself)
            content_without_includes = re.sub(r'#include\s*[<"][^>"]+[>"]', "", content)
            found = False
            for sym in symbols:
                if re.search(rf"\b{re.escape(sym)}\b", content_without_includes):
                    found = True
                    break

            if not found:
                unused.append({
                    "file": rel,
                    "line": line_num,
                    "include": include_path,
                    "expected_symbols": symbols,
                })
                findings.append(Finding(
                    file=rel,
                    line=line_num,
                    severity=Severity.LOW,
                    category="dead_code",
                    source_tier=4,
                    title=f"Possibly unused include: {include_path}",
                    detail=f"No references to expected symbols: {', '.join(symbols[:3])}",
                    pattern_name="unused_include",
                ))

    return unused, findings


def _symbols_from_include(include_path: str) -> list[str]:
    """Derive expected symbol names from an include path.

    e.g., "core/event_bus.h" -> ["EventBus", "event_bus"]
         "QTimer" -> ["QTimer"]
         "config.h" -> ["Config", "config"]
    """
    stem = Path(include_path).stem
    symbols = [stem]

    # PascalCase version: event_bus -> EventBus
    parts = stem.split("_")
    if len(parts) > 1:
        pascal = "".join(p.capitalize() for p in parts)
        symbols.append(pascal)
    elif stem[0].islower():
        symbols.append(stem[0].upper() + stem[1:])

    return symbols


# Functions to skip in dead code detection (too common / framework-required)
_SKIP_FUNCTIONS = frozenset({
    # Qt framework
    "paintEvent", "resizeEvent", "keyPressEvent", "keyReleaseEvent",
    "mousePressEvent", "mouseReleaseEvent", "mouseMoveEvent", "mouseDoubleClickEvent",
    "wheelEvent", "closeEvent", "showEvent", "hideEvent", "focusInEvent", "focusOutEvent",
    "changeEvent", "enterEvent", "leaveEvent", "moveEvent", "timerEvent",
    "dragEnterEvent", "dragMoveEvent", "dragLeaveEvent", "dropEvent",
    "contextMenuEvent", "inputMethodEvent", "tabletEvent",
    "event", "eventFilter", "sizeHint", "minimumSizeHint",
    "metaObject", "qt_metacall", "qt_metacast",
    # Common C++ patterns
    "main", "run", "init", "setup", "cleanup", "shutdown",
    "getInstance", "instance", "create", "destroy",
    "begin", "end", "size", "empty", "clear",
    "push", "pop", "top", "front", "back",
    "get", "set", "reset", "update", "render", "draw",
    "toString", "toJson", "fromJson", "serialize", "deserialize",
    "hash", "compare", "equals", "clone", "copy",
    # Google Test
    "SetUp", "TearDown", "SetUpTestSuite", "TearDownTestSuite",
})


# ---------------------------------------------------------------------------
# Python dead code detection
# ---------------------------------------------------------------------------

_PY_DEF_RE = re.compile(r"^(?:def|async\s+def)\s+(\w+)\s*\(", re.MULTILINE)
_PY_IMPORT_RE = re.compile(
    r"^\s*(?:from\s+[\w.]+\s+)?import\s+(.+)$", re.MULTILINE
)
_PY_SKIP_FUNCTIONS = frozenset({
    "__init__", "__str__", "__repr__", "__eq__", "__hash__", "__len__",
    "__enter__", "__exit__", "__call__", "__iter__", "__next__",
    "__getitem__", "__setitem__", "__delitem__", "__contains__",
    "__add__", "__sub__", "__mul__", "__truediv__", "__floordiv__",
    "__lt__", "__le__", "__gt__", "__ge__", "__ne__",
    "__new__", "__del__", "__bool__", "__int__", "__float__",
    "__getattr__", "__setattr__", "__delattr__",
    "setUp", "tearDown", "setUpClass", "tearDownClass",
    "setup_module", "teardown_module",
    "test_", "conftest",
    "main", "run", "app", "create_app",
})


def _analyze_python(config: Config) -> tuple[DeadCodeAnalysis, list[Finding]]:
    """Detect unused functions and imports in Python codebases."""
    result = DeadCodeAnalysis()
    findings: list[Finding] = []

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".py"],
        exclude_dirs=config.exclude_dirs,
    )

    if not all_files:
        return result, findings

    log.info("Dead code analysis (Python): %d files", len(all_files))

    # Collect all function definitions
    definitions: dict[str, list[dict]] = defaultdict(list)
    all_content: dict[Path, str] = {}

    for f in all_files:
        try:
            content = f.read_text(errors="replace")
            all_content[f] = content
        except OSError:
            continue

        rel = relative_path(f, config.root)
        for m in _PY_DEF_RE.finditer(content):
            func_name = m.group(1)
            if func_name.startswith("_") and not func_name.startswith("__"):
                # Private functions — better candidates for dead code
                line_num = content[:m.start()].count("\n") + 1
                definitions[func_name].append({"file": rel, "line": line_num})
                result.total_definitions += 1

    # Find all references
    all_references: set[str] = set()
    for f, content in all_content.items():
        for word in re.findall(r"\b(\w+)\s*\(", content):
            all_references.add(word)
        for word in re.findall(r"\b(\w+)\b", content):
            all_references.add(word)

    # Find unreferenced private functions
    for func_name, def_list in definitions.items():
        if func_name in _PY_SKIP_FUNCTIONS:
            continue
        if func_name.startswith("test_"):
            continue

        # Count references (excluding the def line itself)
        ref_count = 0
        for f, content in all_content.items():
            ref_count += len(re.findall(rf"\b{re.escape(func_name)}\b", content))

        # Subtract definition lines
        ref_count -= len(def_list)

        if ref_count <= 0:
            for defn in def_list:
                result.unused_functions.append({
                    "name": func_name,
                    "file": defn["file"],
                    "line": defn["line"],
                    "reason": "Private function never referenced",
                })
                findings.append(Finding(
                    file=defn["file"],
                    line=defn["line"],
                    severity=Severity.LOW,
                    category="dead_code",
                    source_tier=4,
                    title=f"Potentially unused function: {func_name}()",
                    detail="Private function with no references found",
                    pattern_name="unused_function_def",
                ))

    # Detect unused imports
    _py_unused_imports(all_files, all_content, config, result, findings)

    log.info("Dead code (Python): %d unused functions, %d unused imports",
             len(result.unused_functions), len(result.unused_includes))
    return result, findings


def _py_unused_imports(
    files: list[Path],
    all_content: dict[Path, str],
    config: Config,
    result: DeadCodeAnalysis,
    findings: list[Finding],
) -> None:
    """Detect unused import statements in Python files."""
    for f in files:
        if f not in all_content:
            continue
        content = all_content[f]
        rel = relative_path(f, config.root)

        for m in _PY_IMPORT_RE.finditer(content):
            imports_str = m.group(1).strip()
            line_num = content[:m.start()].count("\n") + 1

            # Parse individual imported names
            for imp in imports_str.split(","):
                imp = imp.strip()
                if " as " in imp:
                    name = imp.split(" as ")[-1].strip()
                else:
                    name = imp.split(".")[-1].strip()

                if not name or name == "*":
                    continue

                # Check if the imported name is used anywhere else in the file
                content_after_import = content[m.end():]
                if not re.search(rf"\b{re.escape(name)}\b", content_after_import):
                    result.unused_includes.append({
                        "file": rel,
                        "line": line_num,
                        "include": name,
                        "expected_symbols": [name],
                    })
                    findings.append(Finding(
                        file=rel,
                        line=line_num,
                        severity=Severity.LOW,
                        category="dead_code",
                        source_tier=4,
                        title=f"Possibly unused import: {name}",
                        detail=f"Imported name '{name}' not referenced after import",
                        pattern_name="unused_import",
                    ))


# ---------------------------------------------------------------------------
# Rust dead code detection
# ---------------------------------------------------------------------------

_RUST_FN_RE = re.compile(r"^\s*(?:pub(?:\(crate\))?\s+)?fn\s+(\w+)\s*[<(]", re.MULTILINE)
_RUST_USE_RE = re.compile(r"^\s*use\s+([\w:]+(?:::\{[^}]+\})?)\s*;", re.MULTILINE)

_RUST_SKIP = frozenset({
    "main", "new", "default", "from", "into", "try_from", "try_into",
    "fmt", "eq", "ne", "lt", "le", "gt", "ge", "hash",
    "clone", "drop", "deref", "deref_mut",
    "next", "poll", "call",
    "setup", "teardown",
})


def _analyze_rust(config: Config) -> tuple[DeadCodeAnalysis, list[Finding]]:
    """Detect unused functions in Rust codebases."""
    result = DeadCodeAnalysis()
    findings: list[Finding] = []

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".rs"],
        exclude_dirs=config.exclude_dirs,
    )

    if not all_files:
        return result, findings

    log.info("Dead code analysis (Rust): %d files", len(all_files))

    definitions: dict[str, list[dict]] = defaultdict(list)
    all_content: dict[Path, str] = {}

    for f in all_files:
        try:
            content = f.read_text(errors="replace")
            all_content[f] = content
        except OSError:
            continue

        rel = relative_path(f, config.root)
        for m in _RUST_FN_RE.finditer(content):
            func_name = m.group(1)
            if func_name not in _RUST_SKIP and not func_name.startswith("test_"):
                line_num = content[:m.start()].count("\n") + 1
                definitions[func_name].append({"file": rel, "line": line_num})
                result.total_definitions += 1

    # Count references
    for func_name, def_list in definitions.items():
        ref_count = 0
        for f, content in all_content.items():
            ref_count += len(re.findall(rf"\b{re.escape(func_name)}\b", content))
        ref_count -= len(def_list)

        if ref_count <= 0:
            for defn in def_list:
                result.unused_functions.append({
                    "name": func_name,
                    "file": defn["file"],
                    "line": defn["line"],
                    "reason": "Function never referenced",
                })
                findings.append(Finding(
                    file=defn["file"],
                    line=defn["line"],
                    severity=Severity.LOW,
                    category="dead_code",
                    source_tier=4,
                    title=f"Potentially unused function: {func_name}()",
                    detail="Defined but never called from any source file",
                    pattern_name="unused_function_def",
                ))

    log.info("Dead code (Rust): %d unused functions", len(result.unused_functions))
    return result, findings


# ---------------------------------------------------------------------------
# JavaScript / TypeScript dead code detection
# ---------------------------------------------------------------------------

_JS_FUNC_RE = re.compile(
    r"(?:export\s+)?(?:async\s+)?function\s+(\w+)\s*\(|"
    r"(?:export\s+)?const\s+(\w+)\s*=\s*(?:async\s+)?\([^)]*\)\s*=>",
    re.MULTILINE,
)
_JS_IMPORT_RE = re.compile(
    r"import\s+(?:\{([^}]+)\}|(\w+))\s+from\s+", re.MULTILINE
)

_JS_SKIP = frozenset({
    "default", "render", "getStaticProps", "getServerSideProps",
    "getStaticPaths", "middleware", "loader", "action",
    "generateMetadata", "generateStaticParams",
    "GET", "POST", "PUT", "DELETE", "PATCH",
    "setup", "teardown", "beforeEach", "afterEach",
    "describe", "it", "test", "expect",
})


def _analyze_js_ts(config: Config) -> tuple[DeadCodeAnalysis, list[Finding]]:
    """Detect unused functions and imports in JS/TS codebases."""
    result = DeadCodeAnalysis()
    findings: list[Finding] = []

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    if not all_files:
        return result, findings

    log.info("Dead code analysis (JS/TS): %d files", len(all_files))

    all_content: dict[Path, str] = {}
    for f in all_files:
        try:
            all_content[f] = f.read_text(errors="replace")
        except OSError:
            pass

    # Detect unused imports
    for f, content in all_content.items():
        rel = relative_path(f, config.root)
        for m in _JS_IMPORT_RE.finditer(content):
            named = m.group(1)
            default_import = m.group(2)
            line_num = content[:m.start()].count("\n") + 1

            names: list[str] = []
            if named:
                for part in named.split(","):
                    part = part.strip()
                    if " as " in part:
                        names.append(part.split(" as ")[-1].strip())
                    elif part:
                        names.append(part)
            elif default_import:
                names.append(default_import)

            content_after = content[m.end():]
            for name in names:
                if name in _JS_SKIP:
                    continue
                if not re.search(rf"\b{re.escape(name)}\b", content_after):
                    result.unused_includes.append({
                        "file": rel,
                        "line": line_num,
                        "include": name,
                        "expected_symbols": [name],
                    })
                    findings.append(Finding(
                        file=rel,
                        line=line_num,
                        severity=Severity.LOW,
                        category="dead_code",
                        source_tier=4,
                        title=f"Possibly unused import: {name}",
                        detail=f"Imported name '{name}' not referenced after import",
                        pattern_name="unused_import",
                    ))

    log.info("Dead code (JS/TS): %d unused imports", len(result.unused_includes))
    return result, findings
