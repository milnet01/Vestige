# Vestige Post-Phase Audit Standards

This document defines the mandatory audit process that runs after every completed phase. The audit is designed to be **comprehensive** (covering the full codebase) while being **token-efficient** (using automated tools and targeted reads instead of reading every file).

---

## 1. When to Audit

- **After every phase completion** — no exceptions
- **Before starting the next phase** — audit findings must be resolved first
- The audit plan must be reviewed and approved by the user before implementation begins

---

## 2. Audit Workflow

The audit follows a strict order. Each tier feeds findings into the next. **Do not skip tiers.**

```
Tier 1: Automated Tools (zero LLM tokens)
    ↓ findings
Tier 2: Pattern Grep Scan (minimal tokens — matching lines only)
    ↓ findings
Tier 3: Changed-File Deep Review (proportional to phase size)
    ↓ findings
Tier 4: Full-Codebase Categorical Sweep (parallel subagents)
    ↓ findings
Tier 5: Online Research (targeted to findings + experimental features)
    ↓
Findings Report → Plan → User Approval → Implementation
```

---

## 3. Tier 1 — Automated Tool Scanning (Zero LLM Tokens)

Run these tools and capture their output. These catch mechanical issues for free.

### 3.1 Compiler Warnings
```bash
# Clean rebuild with all warnings
cmake --build build --clean-first 2>&1 | grep -E "warning:|error:"
```
The project must compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` and zero warnings.

### 3.2 Static Analysis — cppcheck
```bash
cppcheck --enable=all --std=c++17 --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    -I engine/ -I external/ \
    engine/ app/ tests/ 2>&1
```

### 3.3 Static Analysis — clang-tidy (if available)
```bash
clang-tidy engine/**/*.cpp app/*.cpp \
    -checks='bugprone-*,performance-*,modernize-*,readability-*,cppcoreguidelines-*' \
    -- -std=c++17 -I engine/ -I external/ 2>&1
```

### 3.4 Sanitizer Run
```bash
# Build with sanitizers and run tests
cmake -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" ..
cmake --build build && ./build/tests/vestige_tests
```
AddressSanitizer catches memory leaks, buffer overflows, use-after-free.
UndefinedBehaviorSanitizer catches UB, integer overflow, null dereference.

### 3.5 Test Suite
```bash
cd build && ctest --output-on-failure
```
All tests must pass. Any failure is an audit finding.

---

## 4. Tier 2 — Pattern Grep Scan (Minimal Tokens)

Use `grep` to find known anti-patterns across the full codebase. Only matching lines are read — not entire files.

### 4.1 Memory & Safety Anti-Patterns
| Pattern | What It Catches |
|---------|----------------|
| `\bnew\s+\w+` (in .cpp/.h, excluding placement new) | Raw `new` — should use smart pointers |
| `\bdelete\s+` | Raw `delete` — should use smart pointers |
| `\bNULL\b` | Should be `nullptr` |
| `\(.*\*\)\s*\w+` (C-style casts) | Should use `static_cast` etc. |
| `strcpy\|strcat\|sprintf\|gets` | Unsafe C string functions |
| `using namespace` (in .h files) | Namespace pollution in headers |

### 4.2 OpenGL State Anti-Patterns
| Pattern | What It Catches |
|---------|----------------|
| `glBind.*\(.*[^0]` without matching `glBind.*\(.*0\)` nearby | Unbound GL resources (state leaks) |
| `glEnable\(` without matching `glDisable\(` | GL state not restored |
| `glClipControl\|glDepthFunc\|glCullFace\|glPolygonMode` | Global state changes — verify restore |
| `GL_FRAMEBUFFER` without `glCheckFramebufferStatus` | Framebuffer completeness not verified |

### 4.3 Performance Anti-Patterns
| Pattern | What It Catches |
|---------|----------------|
| `std::string\|std::vector\|std::map` inside render/update loops | Per-frame heap allocations |
| `push_back` in render loop context | Dynamic resizing in hot path |
| `std::shared_ptr` where `unique_ptr` suffices | Unnecessary ref-counting overhead |
| `std::endl` | Unnecessary stream flush — use `"\n"` |

### 4.4 Code Quality
| Pattern | What It Catches |
|---------|----------------|
| `TODO\|FIXME\|HACK\|WORKAROUND\|XXX\|TEMP` | Deferred work that may need resolution |
| `#pragma warning\|NOLINT\|suppress` | Warning suppressions to verify |
| Empty catch blocks `catch.*\{\s*\}` | Silently swallowed errors |
| `// unused\|// removed\|// old\|// deprecated` | Dead code markers |

### 4.5 Shader Anti-Patterns
| Pattern (in .glsl files) | What It Catches |
|--------------------------|----------------|
| `uniform.*` not matched by C++ `glUniform\|setUniform` | Unused or unset shader uniforms |
| Unbounded `for`/`while` loops | Potential GPU hangs |
| `discard` in fragment shaders | Performance concern — verify necessity |

---

## 5. Tier 3 — Changed-File Deep Review

Read and review every file modified or created in the current phase. This is proportional to the phase size.

### Review Checklist (per file)
- [ ] Naming conventions (CODING_STANDARDS.md §1–2)
- [ ] Brace style, indentation, spacing (CODING_STANDARDS.md §3)
- [ ] Include order and hygiene (CODING_STANDARDS.md §4)
- [ ] Comment quality — why, not what (CODING_STANDARDS.md §5)
- [ ] Class structure order (CODING_STANDARDS.md §6)
- [ ] General rules compliance — `const`, `constexpr`, `explicit`, RAII (CODING_STANDARDS.md §7)
- [ ] Shader conventions if applicable (CODING_STANDARDS.md §8)
- [ ] Security checklist (SECURITY.md §10)
- [ ] Error handling — no silent failures
- [ ] Logical correctness — does the code do what it claims?
- [ ] Edge cases — empty inputs, zero-size, nullptr, NaN
- [ ] Performance — hot path efficiency, unnecessary copies

---

## 6. Tier 4 — Full-Codebase Categorical Sweep

Use **parallel subagents**, each responsible for one audit category. Each subagent reads only what's relevant to its category — not every file. Subagents return a concise findings list.

### Category Assignments

**Subagent A — Bugs & Logic Errors**
- Review all systems for logic errors, off-by-one, race conditions
- Check event bus subscription/unsubscription lifecycle
- Verify state machine correctness (render passes, editor modes)
- Check floating-point hazards: NaN propagation, precision loss, divide-by-zero

**Subagent B — Memory & Resource Safety**
- Check all resource lifecycles: create → use → destroy
- GPU resources: VAOs, VBOs, FBOs, textures, shaders
- Smart pointer usage, RAII compliance, Rule of Five
- Buffer overflow risk in any manual indexing
- Memory leak paths (especially in error/early-return branches)

**Subagent C — Performance**
- Per-frame allocations in render/update loops
- Draw call count and batching opportunities
- Frustum culling effectiveness
- Unnecessary state changes in render pipeline
- Copy vs move semantics — unnecessary copies
- std::shared_ptr overhead where std::unique_ptr suffices
- Shader complexity — unnecessary calculations, texture lookups

**Subagent D — Code Quality & Standards**
- Full CODING_STANDARDS.md compliance across all files
- Dead code and unused imports/includes
- Include hygiene — unnecessary includes in headers, missing forward declarations
- Const correctness across all public APIs
- Code duplication — DRY violations
- Function/method ordering within files
- Simplification opportunities — over-engineered abstractions

**Subagent E — Security & Robustness**
- SECURITY.md compliance across all files
- Input validation on all external data paths (file loading, user input)
- Path traversal risks
- OpenGL state safety (error checking, state restoration)
- Driver compatibility issues (AMD/NVIDIA/Intel quirks)
- Error handling completeness — unchecked return values

**Subagent F — Documentation & Tests**
- Test coverage gaps — subsystems with no or minimal tests
- Stale documentation — ARCHITECTURE.md, CODING_STANDARDS.md, SECURITY.md vs actual code
- Missing Doxygen comments on public APIs
- Test quality — are tests testing real behavior or just compiling?

**Subagent G — Shader Audit**
- Shader/C++ uniform sync — every uniform declared must be set, every set must be declared
- GLSL best practices and performance
- Shader compilation error handling
- Precision qualifiers and floating-point safety
- Vertex attribute layout consistency between C++ and GLSL

### Subagent Rules
- Each subagent uses grep/glob to find relevant files, then reads only those
- Each subagent returns findings as a concise bullet list: `file:line — issue — severity`
- Severity levels: **Critical** (crash/data loss), **High** (bug/security), **Medium** (performance/quality), **Low** (style/cleanup)
- Maximum 50 findings per subagent — prioritize by severity

---

## 7. Tier 5 — Online Research

Targeted research based on findings from Tiers 1–4.

### 7.1 Bug & Vulnerability Research
- Search for known issues with specific patterns found (e.g., OpenGL driver bugs for the GPU)
- Search for CVEs in current dependency versions
- Search for known pitfalls with specific OpenGL features in use

### 7.2 Best Practices Research
- Research better solutions for any Medium+ findings where the fix isn't obvious
- Search for engine architecture patterns that solve identified structural issues

### 7.3 Experimental Features Research
- Research cutting-edge features relevant to systems touched in this phase
- Focus on features feasible for the hardware target (AMD RX 6600 / OpenGL 4.5+)
- Examples: new rendering techniques, optimization strategies, tooling improvements
- Document findings in `docs/EXPERIMENTAL_FEATURES.md` (append, don't overwrite)

### 7.4 URL Management
- Add any new useful URLs discovered during research to the project allowlist
- Remove any dead/outdated URLs

---

## 8. Findings Report

After all tiers complete, compile a single **Findings Report** organized by severity.

### Report Format
```markdown
# Phase [N] Audit Findings

## Summary
- Critical: X | High: X | Medium: X | Low: X

## Critical Findings
1. **[file:line]** Description. Tier found: X.

## High Findings
...

## Medium Findings
...

## Low Findings
...

## Experimental Feature Opportunities
...

## Research URLs Added
...
```

### Rules
- Verify each finding before including — false positives waste time
- Group related findings that can be fixed together
- Note which findings affect multiple systems vs. isolated issues
- Estimate fix complexity: trivial (1-line), small (< 10 lines), medium (< 50 lines), large (50+ lines)

---

## 9. Implementation Planning

**Do not implement any fixes before the plan is approved.**

### Planning Rules
1. Group findings into logical fix batches (e.g., "all const-correctness fixes", "all OpenGL state restoration")
2. Order batches by: Critical → High → Medium → Low
3. Within a severity, order by: most files touched → fewest files touched (big sweeps first)
4. Each batch gets a brief description of what changes and why
5. Present the plan to the user for approval before starting
6. Implement one batch at a time, verify (compile + test) after each batch

### Token Efficiency During Implementation
- For mechanical fixes (naming, const, includes): use Edit tool with grep to find all instances, fix in bulk
- For logic fixes: read only the affected function/method, not the whole file
- For cross-cutting fixes: use `replace_all` where the pattern is consistent
- Do not re-read files that haven't changed since the last read in the same session

---

## 10. Full Audit Checklist

This is the complete list of what the audit checks. Categories marked with (A) are primarily caught by automated tools in Tier 1.

### Bugs & Correctness
- [ ] Logic errors and off-by-one bugs
- [ ] Null/dangling pointer dereferences
- [ ] Uninitialized variable usage (A)
- [ ] Integer overflow/underflow (A)
- [ ] Floating-point hazards (NaN, infinity, precision loss, divide-by-zero)
- [ ] Race conditions (if multithreading is used)
- [ ] Event bus lifecycle issues (subscribe without unsubscribe)
- [ ] State machine correctness (render passes, editor modes)
- [ ] Edge cases: empty inputs, zero-size collections, boundary values

### Memory & Resources
- [ ] Memory leaks — especially in error/early-return paths (A)
- [ ] Buffer overflows — unchecked array/vector access (A)
- [ ] Use-after-free / dangling references (A)
- [ ] GPU resource leaks — VAOs, VBOs, FBOs, textures, shaders not freed
- [ ] RAII compliance — all resources wrapped in destructors
- [ ] Rule of Five — classes managing resources must define or delete all five
- [ ] Smart pointer correctness — unique_ptr vs shared_ptr usage
- [ ] Raw new/delete — forbidden except placement new

### Security
- [ ] Input validation on all external data (files, user input, gamepad)
- [ ] Path traversal prevention on file loading
- [ ] Shader compilation/linking status checked
- [ ] File size/format validation before parsing
- [ ] No sensitive data in logs
- [ ] Compiler hardening flags enabled (A)
- [ ] Dependency versions pinned and up-to-date

### Performance
- [ ] Per-frame heap allocations in render/update loops
- [ ] Unnecessary copies — use const& or std::move
- [ ] Draw call optimization — batching, instancing, culling
- [ ] Shader efficiency — unnecessary calculations, redundant texture lookups
- [ ] Unnecessary OpenGL state changes between draw calls
- [ ] std::shared_ptr overhead where unique_ptr suffices
- [ ] Hot path branches — predictable vs unpredictable
- [ ] Cache-friendly data layout (SOA vs AOS where applicable)
- [ ] std::endl vs "\n" (unnecessary flush)

### Code Quality
- [ ] Dead code — unused functions, variables, includes, imports
- [ ] Code duplication — DRY violations
- [ ] Simplification opportunities — over-engineered abstractions
- [ ] Const correctness — methods, parameters, local variables
- [ ] Include hygiene — unnecessary includes, missing forward declarations
- [ ] Function/method ordering within files (CODING_STANDARDS.md §6)
- [ ] Naming convention compliance (CODING_STANDARDS.md §2)
- [ ] Formatting compliance — braces, indentation, spacing (CODING_STANDARDS.md §3)
- [ ] Comment quality — why not what (CODING_STANDARDS.md §5)
- [ ] File header comments present
- [ ] Doxygen on public APIs

### OpenGL Specific
- [ ] GL state saved/restored between render passes
- [ ] Framebuffer completeness checked
- [ ] GL error checking in debug builds
- [ ] Hardware limits queried and respected (GL_MAX_*)
- [ ] Resources unbound after use
- [ ] All declared GLSL samplers have valid textures bound at draw time (Mesa/AMD requirement)
- [ ] Shader uniform sync — every GLSL uniform set from C++ and vice versa
- [ ] Vertex attribute layout matches between C++ and GLSL

### Tests & Documentation
- [ ] All tests pass
- [ ] Test coverage — every subsystem has meaningful tests
- [ ] Test quality — tests verify behavior, not just compilation
- [ ] ARCHITECTURE.md matches current code structure
- [ ] CODING_STANDARDS.md is complete and current
- [ ] SECURITY.md is complete and current
- [ ] AUDIT_STANDARDS.md is complete and current
- [ ] API documentation (Doxygen) on all public interfaces

### Cross-Platform & Compatibility
- [ ] Platform-specific code properly guarded with #ifdef
- [ ] No hardcoded paths (use relative paths or configuration)
- [ ] Driver compatibility — tested patterns work on AMD/NVIDIA/Intel
- [ ] Dependency compatibility — all deps build on Linux and Windows

### Build System
- [ ] CMake configuration correct — no warnings
- [ ] Sanitizer build configuration works
- [ ] Release build produces clean output (no debug symbols leaked)
- [ ] All test targets build and link correctly
- [ ] New files added to CMakeLists.txt

---

## 11. Scaling Strategy

As the codebase grows, the audit scales by:

1. **Tier 1 (automated tools)** scales automatically — tools scan everything regardless of size
2. **Tier 2 (grep patterns)** scales linearly but cheaply — grep is fast and returns only matches
3. **Tier 3 (changed files)** scales with phase size, not codebase size
4. **Tier 4 (categorical sweep)** scales via parallelism — add more subagents for new subsystems
5. **Tier 5 (research)** scales with findings, not codebase size

### When the Codebase Exceeds ~50,000 Lines
- Split Tier 4 subagents further by engine subsystem (e.g., separate agents for renderer, editor, scene, etc.)
- Add a Tier 2.5: static analysis with project-specific custom rules
- Consider adding a CI pipeline that runs Tier 1 + Tier 2 automatically on every commit

### When the Codebase Exceeds ~100,000 Lines
- Tier 4 should use file-change-dependency analysis: only sweep files within 2 hops of changed files for detailed review, sample the rest
- Maintain a `KNOWN_ISSUES.md` to avoid re-discovering the same issues
- Tier 1 automated tools become the primary defense; manual review focuses on logic and architecture
