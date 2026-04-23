# Vestige Post-Phase Audit Standards

Mandatory audit process after every completed phase. Comprehensive coverage (full codebase) at low token cost (tools + targeted reads).

---

## 1. When

- **After every phase completion** — no exceptions.
- **Before starting the next phase** — findings must be resolved first.
- Plan must be user-approved before implementation.

---

## 2. Workflow

Five tiers. Do not skip. Each feeds the next.

```
Tier 1: Automated Tools         (zero LLM tokens)
Tier 2: Pattern Grep Scan       (matching lines only)
Tier 3: Changed-File Review     (proportional to phase)
Tier 4: Categorical Sweep       (parallel subagents)
Tier 5: Online Research         (targeted)
  → Findings Report → Plan → User Approval → Implementation
```

---

## 3. Tier 1 — Automated Tools

Run + capture output. Catches mechanical issues for free.

**Compiler warnings.** Clean rebuild, grep for `warning:|error:`. `-Werror` has been on since 2026-04-19 L41 — any warning is a build break. Full hardened flag set lives in `engine/CMakeLists.txt` (see SECURITY.md §6).

**cppcheck.** `cppcheck --enable=all --std=c++17 --suppress=missingIncludeSystem --suppress=unusedFunction -I engine/ -I external/ engine/ app/ tests/`.

**clang-tidy** (if available). `bugprone-*,performance-*,modernize-*,readability-*,cppcoreguidelines-*`.

**Sanitizers.** Debug build with `-fsanitize=address,undefined -fno-omit-frame-pointer`; run test suite. ASan catches leaks/overflow/UAF; UBSan catches UB/integer overflow/null deref.

**Tests.** `ctest --output-on-failure`. Any failure is a finding.

---

## 4. Tier 2 — Pattern Grep Scan

Grep finds anti-patterns across the full codebase — only matching lines read.

### Memory & Safety
| Pattern | Catches |
|---------|---------|
| `\bnew\s+\w+` (non-placement) | Raw `new` |
| `\bdelete\s+` | Raw `delete` |
| `\bNULL\b` | Should be `nullptr` |
| C-style casts `\(.*\*\)\s*\w+` | Should use `static_cast` etc. |
| `strcpy\|strcat\|sprintf\|gets` | Unsafe C string funcs |
| `using namespace` in `.h` | Namespace pollution |

### OpenGL State
| Pattern | Catches |
|---------|---------|
| `glBind*(x)` without matching `glBind*(0)` | State leaks |
| `glEnable(` without `glDisable(` | State not restored |
| `glClipControl\|glDepthFunc\|glCullFace\|glPolygonMode` | Global state change — verify restore |
| `GL_FRAMEBUFFER` without `glCheckFramebufferStatus` | FBO completeness not checked |

### Performance
| Pattern | Catches |
|---------|---------|
| `std::string/vector/map` in render/update loop | Per-frame heap alloc |
| `push_back` in render loop | Dynamic resize in hot path |
| `std::shared_ptr` where unique suffices | Unnecessary refcounting |
| `std::endl` | Unnecessary flush — use `"\n"` |

### Quality
| Pattern | Catches |
|---------|---------|
| `TODO\|FIXME\|HACK\|WORKAROUND\|XXX\|TEMP` | Deferred work |
| `#pragma warning\|NOLINT\|suppress` | Warning suppressions |
| `catch.*\{\s*\}` | Silently swallowed errors |
| `// unused\|// removed\|// old\|// deprecated` | Dead code markers |

### Shader (`*.glsl`)
| Pattern | Catches |
|---------|---------|
| `uniform` without C++ `glUniform\|setUniform` match | Unused/unset uniforms |
| Unbounded `for`/`while` | Potential GPU hang |
| `discard` in fragment | Perf concern — verify |

---

## 5. Tier 3 — Changed-File Review

Read every file modified or created this phase. Checklist per file:

- [ ] Naming, format, include order, comments, class structure (CODING_STANDARDS §1–6)
- [ ] General rules — `const`, `constexpr`, `explicit`, RAII (§7)
- [ ] Shader conventions if applicable (§8)
- [ ] Security checklist (SECURITY §10)
- [ ] No silent failures
- [ ] Logical correctness: does the code do what it claims?
- [ ] Edge cases: empty, zero, nullptr, NaN
- [ ] Performance: hot-path efficiency, unnecessary copies

---

## 6. Tier 4 — Categorical Sweep (parallel subagents)

One subagent per category. Each uses grep/glob to find relevant files, then reads only those. Returns `file:line — issue — severity` bullets.

**Severity:** Critical (crash/data loss) · High (bug/security) · Medium (perf/quality) · Low (style/cleanup). Max 50 findings per agent.

| Agent | Scope |
|-------|-------|
| **A — Bugs & Logic** | Logic errors, off-by-one, race conditions, event bus lifecycle, state-machine correctness, float hazards (NaN, precision, div-by-zero). |
| **B — Memory & Resources** | Resource lifecycles (GPU: VAO/VBO/FBO/textures/shaders), smart-pointer usage, RAII, Rule of Five, buffer-overflow risk in manual indexing, leaks in error/early-return paths. |
| **C — Performance** | Per-frame allocs in render/update, draw-call count + batching, culling effectiveness, state-change count, copy-vs-move, `shared_ptr` overhead, shader complexity. |
| **D — Quality & Standards** | Full CODING_STANDARDS compliance, dead code, include hygiene, const correctness, DRY violations, over-engineered abstractions. |
| **E — Security & Robustness** | SECURITY.md compliance, input validation on external paths, path traversal, GL state safety, driver quirks (AMD/NVIDIA/Intel), unchecked return values. |
| **F — Docs & Tests** | Test coverage gaps, stale ARCHITECTURE/CODING/SECURITY/AUDIT docs vs code, Doxygen on public APIs, test quality (behavior vs compile-only). |
| **G — Shaders** | C++/GLSL uniform sync, GLSL best practices, compile-error handling, precision qualifiers, vertex-attribute layout sync. |
| **H — Scene Spatial Integrity** | Entities not spawning inside colliders; containment; ≥ 5 cm margins; shared dimension constants; no caging; no z-fighting; cloth sizing + collider placement. Cross-ref CODING_STANDARDS §9. |

---

## 7. Tier 5 — Online Research

Targeted research on Tier 1–4 findings.

- **Bugs/vulns:** driver bugs for target GPU, CVEs on current deps, pitfalls for GL features in use.
- **Best practices:** better solutions for Medium+ findings where the fix isn't obvious.
- **Experimental features:** cutting-edge tech relevant to this phase's subsystems, feasible on AMD RX 6600 / GL 4.5+. Append to `docs/EXPERIMENTAL_FEATURES.md`.
- **URL management:** add useful new URLs to project allowlist; remove dead ones.

---

## 8. Findings Report

Compile into one report organized by severity.

```markdown
# Phase [N] Audit Findings
## Summary
- Critical: X | High: X | Medium: X | Low: X
## Critical
1. **[file:line]** Description. Tier: X.
## High / Medium / Low ...
## Experimental Feature Opportunities
## Research URLs Added
```

**Rules:** verify each finding before including; group related fixes; note cross-system vs isolated; estimate complexity (trivial/small/medium/large).

---

## 9. Implementation Planning

**Do not implement before plan approval.**

1. Group findings into logical fix batches.
2. Order by severity (Critical → Low), then by sweep breadth within a tier (most-touched first).
3. Each batch: brief description of change + why.
4. Present to user.
5. Implement one batch at a time; verify (compile + test) between.

**Token efficiency during fix:** mechanical fixes use grep + Edit `replace_all`; logic fixes read only affected function/method; don't re-read files already read this session.

---

## 10. Scaling

- **Tier 1** scales automatically (tools scan everything).
- **Tier 2** scales linearly, cheaply (grep is fast, matches only).
- **Tier 3** scales with phase size, not codebase size.
- **Tier 4** scales via parallelism — add subagents for new subsystems.
- **Tier 5** scales with findings, not codebase.

**> ~50 000 LoC:** split Tier 4 subagents by subsystem (renderer/editor/scene/…). Add Tier 2.5 for project-specific static rules. CI runs Tier 1+2 on every commit.

**> ~100 000 LoC:** Tier 4 uses change-dependency analysis (detailed sweep on files within 2 hops of changes, sample the rest). Maintain `KNOWN_ISSUES.md` to avoid re-discovery. Tier 1 becomes primary defense; manual review focuses on logic + architecture.
