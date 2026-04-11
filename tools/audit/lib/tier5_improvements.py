"""Tier 5: Technology detection, improvement research, and domain-specific security queries.

Scans the codebase for known techniques, algorithms, and libraries,
then generates research queries for better approaches, new features,
and domain-specific security vulnerabilities.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .utils import enumerate_files

log = logging.getLogger("audit")


# ---------------------------------------------------------------------------
# Known technology signatures → improvement query templates
# ---------------------------------------------------------------------------
# Each entry: (regex_pattern, file_globs, category, query_template)
# The query_template can use {year} which is replaced with the current year.

TECHNOLOGY_SIGNATURES: list[tuple[str, str, str, str]] = [
    # Rendering techniques
    (r"\bSSAO\b|ssao", "*.cpp,*.h,*.glsl",
     "Rendering", "SSAO screen space ambient occlusion improvements alternatives {year}"),
    (r"\bSSR\b|screen.?space.?reflect", "*.cpp,*.h,*.glsl",
     "Rendering", "screen space reflections SSR improvements techniques {year}"),
    (r"\bPBR\b|physically.?based", "*.cpp,*.h,*.glsl",
     "Rendering", "PBR physically based rendering latest improvements {year}"),
    (r"\bcascaded.?shadow|CSM\b", "*.cpp,*.h,*.glsl",
     "Shadows", "cascaded shadow maps improvements virtual shadow maps {year}"),
    (r"\bPCSS\b|soft.?shadow", "*.cpp,*.h,*.glsl",
     "Shadows", "PCSS soft shadows performance improvements alternatives {year}"),
    (r"\bbloom\b", "*.cpp,*.h,*.glsl",
     "Post-processing", "bloom post-processing efficient techniques dual filtering {year}"),
    (r"\bTAA\b|temporal.?anti", "*.cpp,*.h,*.glsl",
     "Anti-aliasing", "temporal anti-aliasing TAA improvements ghosting reduction {year}"),
    (r"\bSMAA\b", "*.cpp,*.h,*.glsl",
     "Anti-aliasing", "SMAA anti-aliasing latest improvements alternatives {year}"),
    (r"\bdeferred\b.*render|g.?buffer", "*.cpp,*.h",
     "Rendering", "deferred rendering optimization techniques {year}"),
    (r"\binstanc", "*.cpp,*.h",
     "Rendering", "GPU instancing best practices indirect rendering {year}"),
    (r"\bfrustum.?cull", "*.cpp,*.h",
     "Culling", "frustum culling GPU-driven occlusion culling techniques {year}"),

    # Physics / simulation
    (r"\bXPBD\b|position.?based.?dynamics", "*.cpp,*.h",
     "Physics", "XPBD extended position based dynamics improvements stability {year}"),
    (r"\bcloth.?simul", "*.cpp,*.h",
     "Physics", "real-time cloth simulation GPU techniques improvements {year}"),
    (r"\bragdoll\b", "*.cpp,*.h",
     "Physics", "ragdoll physics improvements active ragdoll techniques {year}"),
    (r"\bGerstner\b|ocean.?wave|water.?wave", "*.cpp,*.h,*.glsl",
     "Water", "Gerstner wave ocean rendering improvements FFT water {year}"),
    (r"\bbuoyancy\b", "*.cpp,*.h",
     "Physics", "real-time buoyancy simulation improvements {year}"),

    # Animation
    (r"\bmotion.?match", "*.cpp,*.h",
     "Animation", "motion matching animation latest techniques improvements {year}"),
    (r"\binverse.?kinematic|IK\b", "*.cpp,*.h",
     "Animation", "inverse kinematics IK solver improvements FABRIK CCD {year}"),
    (r"\bblend.?tree|animation.?blend", "*.cpp,*.h",
     "Animation", "animation blending blend tree improvements {year}"),
    (r"\blip.?sync|viseme", "*.cpp,*.h",
     "Animation", "real-time lip sync audio driven facial animation {year}"),
    (r"\bmorph.?target|blend.?shape", "*.cpp,*.h",
     "Animation", "morph target blend shape optimization GPU {year}"),

    # Lighting / GI
    (r"\blight.?probe|spherical.?harmonic|SH\b", "*.cpp,*.h,*.glsl",
     "Lighting", "light probes spherical harmonics improvements irradiance volumes {year}"),
    (r"\bradiosity\b", "*.cpp,*.h",
     "GI", "real-time radiosity global illumination improvements {year}"),
    (r"\benvironment.?map|IBL\b|image.?based.?light", "*.cpp,*.h,*.glsl",
     "Lighting", "IBL image based lighting improvements split sum {year}"),

    # Terrain / foliage
    (r"\bterrain\b.*chunk|heightmap|terrain.?lod", "*.cpp,*.h",
     "Terrain", "terrain rendering LOD techniques GPU tessellation {year}"),
    (r"\bfoliage\b|grass.?render", "*.cpp,*.h,*.glsl",
     "Vegetation", "grass foliage rendering GPU improvements indirect {year}"),
    (r"\bstochastic.?tiling", "*.cpp,*.h,*.glsl",
     "Textures", "stochastic tiling texture improvements alternatives {year}"),
    (r"\btriplanar\b", "*.cpp,*.h,*.glsl",
     "Textures", "triplanar mapping improvements biplanar mapping {year}"),

    # Audio
    (r"\bOpenAL\b|audio.?engine", "*.cpp,*.h",
     "Audio", "game audio engine best practices spatial audio HRTF {year}"),

    # UI / Editor
    (r"\bImGui\b|imgui", "*.cpp,*.h",
     "UI", "Dear ImGui best practices performance tips {year}"),
    (r"\bnode.?editor|visual.?script|node.?graph", "*.cpp,*.h",
     "Editor", "node-based visual scripting editor implementation {year}"),

    # Engine architecture
    (r"\bECS\b|entity.?component.?system", "*.cpp,*.h",
     "Architecture", "ECS entity component system performance data-oriented design {year}"),
    (r"\bevent.?bus|event.?system|publish.?subscribe", "*.cpp,*.h",
     "Architecture", "game engine event system improvements lock-free {year}"),
    (r"\basset.?stream|async.?load|resource.?stream", "*.cpp,*.h",
     "Assets", "game asset streaming async loading best practices {year}"),

    # GPU / compute
    (r"\bcompute.?shader", "*.cpp,*.h,*.glsl,*.comp",
     "GPU", "compute shader optimization techniques workgroup size {year}"),
    (r"\bparticle\b.*system|particle.?emit", "*.cpp,*.h",
     "Particles", "GPU particle system compute shader techniques {year}"),

    # Navigation
    (r"\bRecast\b|\bDetour\b|navmesh|navigation.?mesh", "*.cpp,*.h",
     "Navigation", "Recast Detour navmesh improvements dynamic obstacles {year}"),

    # General C++ / OpenGL
    (r"\bOpenGL\s*4\.[456]", "*.cpp,*.h",
     "Graphics API", "OpenGL 4.6 features DSA bindless textures best practices"),
    (r"\bVulkan\b", "*.cpp,*.h",
     "Graphics API", "Vulkan migration from OpenGL best practices {year}"),
]

# Additional generic queries based on language
LANGUAGE_IMPROVEMENT_QUERIES: dict[str, list[str]] = {
    "cpp": [
        "C++17 C++20 performance improvements game engine {year}",
        "modern C++ game engine architecture best practices {year}",
    ],
    "python": [
        "Python performance optimization best practices {year}",
    ],
    "rust": [
        "Rust game development best practices {year}",
    ],
}


@dataclass
class TechDetection:
    """A detected technology in the codebase."""
    category: str
    pattern: str
    files_matched: int
    query: str


def detect_technologies(config: Config) -> list[TechDetection]:
    """Scan the codebase for known technologies and generate improvement queries."""
    from datetime import datetime
    year = datetime.now().year

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs + config.shader_dirs,
        extensions=config.source_extensions + [".glsl", ".comp"],
        exclude_dirs=config.exclude_dirs,
    )

    # Build a filename → content cache for efficiency
    # (we'll scan the same files for multiple patterns)
    import fnmatch
    file_contents: dict[Path, str] = {}
    for f in all_files:
        try:
            file_contents[f] = f.read_text(errors="replace")
        except OSError:
            pass

    detections: list[TechDetection] = []
    seen_queries: set[str] = set()

    for regex_str, file_glob_str, category, query_template in TECHNOLOGY_SIGNATURES:
        globs = [g.strip() for g in file_glob_str.split(",")]
        regex = re.compile(regex_str, re.IGNORECASE)
        query = query_template.replace("{year}", str(year))

        if query in seen_queries:
            continue

        match_count = 0
        for fpath, content in file_contents.items():
            if not any(fnmatch.fnmatch(fpath.name, g) for g in globs):
                continue
            if regex.search(content):
                match_count += 1

        if match_count > 0:
            seen_queries.add(query)
            detections.append(TechDetection(
                category=category,
                pattern=regex_str,
                files_matched=match_count,
                query=query,
            ))

    # Generate "best practices" queries for the top-used technologies
    # (separate from improvement queries — focused on correct usage, not new features)
    sorted_detections = sorted(detections, key=lambda d: -d.files_matched)
    for det in sorted_detections[:8]:
        # Extract a short tech name from the query for the best practices search
        short_name = det.query.split(" improvements")[0].split(" latest")[0].split(" efficient")[0]
        bp_query = f"{short_name} best practices common mistakes pitfalls {year}"
        if bp_query not in seen_queries:
            seen_queries.add(bp_query)
            detections.append(TechDetection(
                category=f"{det.category} (best practices)",
                pattern=det.pattern,
                files_matched=det.files_matched,
                query=bp_query,
            ))

    # Add language-specific generic queries
    lang_queries = LANGUAGE_IMPROVEMENT_QUERIES.get(config.language, [])
    for q in lang_queries:
        query = q.replace("{year}", str(year))
        if query not in seen_queries:
            seen_queries.add(query)
            detections.append(TechDetection(
                category="General",
                pattern="(language)",
                files_matched=0,
                query=query,
            ))

    log.info("Tech detection: %d technologies found (%d improvement + best practices queries) "
             "across %d files", len(detections), len(seen_queries), len(file_contents))
    return detections


def get_improvement_queries(config: Config) -> list[str]:
    """Detect technologies and return their improvement queries."""
    research_config = config.get("research", default={})
    if not research_config.get("improvements", {}).get("enabled", True):
        return []

    max_queries = research_config.get("improvements", {}).get("max_queries", 10)
    detections = detect_technologies(config)

    # Sort by number of files matched (most used tech first), then cap
    detections.sort(key=lambda d: -d.files_matched)
    return [d.query for d in detections[:max_queries]]


# ---------------------------------------------------------------------------
# Domain-specific security research queries
# ---------------------------------------------------------------------------

# Each entry: (regex_pattern, file_globs, domain_name, list of security queries)
SECURITY_SIGNATURES: list[tuple[str, str, str, list[str]]] = [
    # Terminal emulator
    (r"(?:terminal|vt100|vt220|xterm|ansi.?escape|pty|forkpty|termios)", "*.cpp,*.h",
     "Terminal Emulator", [
         "CVE terminal emulator escape sequence injection {year}",
         "terminal emulator security ANSI escape sequence attack",
         "PTY security file descriptor leak forkpty best practices",
         "site:nvd.nist.gov terminal emulator vulnerability {year}",
     ]),
    # Qt application
    (r"#include\s*<Q(?:Application|Widget|MainWindow|Dialog)", "*.cpp,*.h",
     "Qt Application", [
         "Qt6 security best practices untrusted data {year}",
         "Qt QProcess command injection prevention",
         "Qt application security hardening checklist",
     ]),
    # Lua scripting / plugins
    (r"(?:lua_|luaL_|lua\.h|lauxlib)", "*.cpp,*.h",
     "Lua Scripting", [
         "Lua sandbox escape bytecode vulnerability {year}",
         "Lua 5.4 sandbox security best practices",
         "Lua embedding security getmetatable string metatable",
     ]),
    # Network / sockets
    (r"(?:QTcpSocket|QLocalSocket|AF_UNIX|socket\s*\(|listen\s*\()", "*.cpp,*.h",
     "Network/IPC", [
         "Unix domain socket security permissions race condition",
         "IPC security local privilege escalation {year}",
     ]),
    # Session / data persistence
    (r"(?:serialize|deserialize|QDataStream|qCompress|qUncompress)", "*.cpp,*.h",
     "Data Persistence", [
         "Qt QDataStream deserialization vulnerability",
         "decompression bomb zip bomb detection prevention",
         "session file security path traversal {year}",
     ]),
    # Web server / HTTP
    (r"(?:express|fastify|http\.createServer|flask|django|gin\.Default)", "*.js,*.ts,*.py,*.go",
     "Web Server", [
         "OWASP top 10 {year} web application security",
         "HTTP header injection prevention best practices",
     ]),
    # Cryptography
    (r"(?:openssl|crypto|AES|RSA|SHA256|bcrypt|argon2|pbkdf2)", "*.cpp,*.h,*.py,*.js,*.go",
     "Cryptography", [
         "cryptography best practices deprecated algorithms {year}",
         "CVE OpenSSL vulnerability {year}",
     ]),
    # Database
    (r"(?:sqlite3|mysql|postgres|mongodb|redis|leveldb)", "*.cpp,*.h,*.py,*.js,*.go",
     "Database", [
         "SQL injection prevention parameterized queries best practices",
         "database connection security TLS certificate validation",
     ]),
    # File I/O intensive
    (r"(?:fopen|ifstream|ofstream|QFile|open\s*\(.*O_)", "*.cpp,*.h",
     "File I/O", [
         "TOCTOU race condition file operations C++ prevention",
         "symlink attack prevention file operations {year}",
     ]),
    # Game engine / 3D
    (r"(?:OpenGL|Vulkan|DirectX|glfw|SDL2|render)", "*.cpp,*.h",
     "Game Engine", [
         "game engine security shader injection {year}",
         "OpenGL driver crash denial of service prevention",
     ]),
]


def get_security_queries(config: Config) -> list[str]:
    """Detect application domain and generate targeted security research queries."""
    from datetime import datetime
    year = datetime.now().year

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    # Read file contents
    import fnmatch
    file_contents: dict[Path, str] = {}
    for f in all_files:
        try:
            file_contents[f] = f.read_text(errors="replace")
        except OSError:
            pass

    queries: list[str] = []
    seen_domains: set[str] = set()

    for regex_str, file_glob_str, domain, domain_queries in SECURITY_SIGNATURES:
        globs = [g.strip() for g in file_glob_str.split(",")]
        regex = re.compile(regex_str, re.IGNORECASE)

        match_count = 0
        for fpath, content in file_contents.items():
            if not any(fnmatch.fnmatch(fpath.name, g) for g in globs):
                continue
            if regex.search(content):
                match_count += 1

        if match_count > 0 and domain not in seen_domains:
            seen_domains.add(domain)
            for q in domain_queries:
                expanded = q.replace("{year}", str(year))
                if expanded not in queries:
                    queries.append(expanded)
            log.info("Security domain detected: %s (%d files)", domain, match_count)

    # Cap to avoid excessive queries
    max_queries = config.get("research", "improvements", "max_queries", default=10)
    if len(queries) > max_queries:
        queries = queries[:max_queries]

    return queries
