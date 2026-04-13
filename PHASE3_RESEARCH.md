# Phase 3 Research — Citations for Critical/High Findings

Each item in this file corresponds to a Critical or High finding in `AUDIT.md`. External authorities are cited verbatim where available; internal engine bugs with no upstream (H6–H13 mostly) are noted as self-evident from source.

---

## C1 — `subprocess.run(..., shell=True)` with user-controlled arguments → shell injection

**Source:** Python stdlib docs, [`subprocess` — Security Considerations](https://docs.python.org/3/library/subprocess.html#security-considerations)

> Unlike some other popen functions, this library will not implicitly choose to call a system shell. This means that all characters, including shell metacharacters, can safely be passed to child processes. If the shell is invoked explicitly, via `shell=True`, it is the application's responsibility to ensure that all whitespace and metacharacters are quoted appropriately to avoid [shell injection](https://en.wikipedia.org/wiki/Shell_injection#Shell_injection) vulnerabilities. On some platforms, it is possible to use `shlex.quote()` for this escaping.

**Applies because:** `tools/audit/lib/utils.py:13-30` defaults to `shell=True`; downstream callers concatenate YAML-supplied values into the command string with zero escaping. This is the exact scenario the stdlib security note warns against.

**Proposed approach:** switch default to `shell=False`, accept `list[str]`. Where a shell is legitimately required (user-authored `build_cmd`/`test_cmd` strings with `cd`, `&&`, redirection), either refactor to Python equivalents or gate behind explicit `shell: true` per-command with a loud warning on load.

---

## C2 — NVD API key committed plaintext

**Source:** [NVD API Key Request page](https://nvd.nist.gov/developers/request-an-api-key) (fetched 2026-04-13)

Key facts from the page:
- Activation window: key must be activated within **7 days** of request email or is invalidated.
- Terms: *"Keys should not be used by, or shared with, individuals or organizations other than the original requestor."*
- No self-service rotation endpoint exists. Key compromise must be reported via NIST contact channels / US-CERT SOC.

**Applies because:** `tools/audit/audit_config.yaml:272` pins a literal UUID key. Once in git history, it's permanent. Leaving it there violates the terms of use and makes rotation irreversible without NIST contact.

**Proposed approach:** scrub the literal; keep only `api_key_env: "NVD_API_KEY"`. Rotate the key via a fresh NVD request. Add a loader check that warns when `api_key` is a non-null literal (catches this class of mistake going forward).

---

## H1–H3 — Flask path-traversal endpoints in audit web UI

**Source:** [CWE-22: Improper Limitation of a Pathname to a Restricted Directory](https://cwe.mitre.org/data/definitions/22.html)

> The product uses external input to construct a pathname that is intended to identify a file or directory that is located underneath a restricted parent directory, but the product does not properly neutralize special elements within the pathname that can cause the pathname to resolve to a location that is outside of the restricted directory.

**Primary mitigation** (from same source):
> Use an "accept known good" input validation strategy with stringent allowlists limiting the character set. For filenames specifically, restrict directory separators like "/" and consider using built-in path canonicalization functions (such as `realpath()` in C or `getCanonicalPath()` in Java) to produce the canonical version of pathnames before validation.

**Applies because:** three routes in `tools/audit/web/app.py` (`GET /api/report`, `GET /api/config`, `POST /api/init`) accept a `path` or `output_path` parameter and pass it directly to `Path(...).read_text()` / write without canonicalization-and-containment check. The **same file's PUT handler already implements the correct pattern** (`config_path.resolve()` + `is_relative_to(allowed_root)`) — three siblings were missed.

**Proposed approach:** factor the containment check into a helper; apply to every route. Python 3.9+ equivalent of "realpath + prefix" is `Path.resolve().is_relative_to(allowed_root)`.

---

## H4 — `urllib.request.add_header` CRLF injection on NVD API key

**Source:** None authoritative found via direct doc fetch — stdlib `urllib.request` does NOT document sanitization of header values. **Treat as defense-in-depth** rather than citing a known CVE: the principle that header values containing `\r\n` can split the request is a CWE-93 (CRLF Injection) canonical concern (not researched further — general web security knowledge).

**Applies because:** `tools/audit/lib/tier5_nvd.py:52-60` forwards an environment-variable-supplied key into an HTTP header without shape validation. A malicious `NVD_API_KEY` env var containing `\r\n` could inject headers.

**Proposed approach:** `re.fullmatch(r"[A-Za-z0-9-]{16,64}", key)` before use. Minimal code change.

---

## H5 — `reinterpret_cast<uintptr_t>(&ref) == 0` is optimizable-to-false UB

**Source:** [cppreference — Reference declaration](https://en.cppreference.com/w/cpp/language/reference)

> A reference is required to be initialized to refer to a valid object or function.
>
> References are not objects; they do not necessarily occupy storage, although the compiler may allocate storage if it is necessary to implement the desired semantics.

**Applies because:** `engine/scripting/script_context.cpp:293-298` (and 3 similar sites) compares the address of an `Engine&` member against zero. Per the above, that reference is required to already refer to a valid object — so taking its address can never yield null, and the compiler is free to fold the branch to `false` under optimization. Clang, GCC, and MSVC all apply this folding at `-O2` / `/O2`.

**Proposed approach:** change `Engine& m_engine` to `Engine*` (nullable) and update call sites, OR remove the guard and require every construction site (including tests) to provide a valid stub. The current code works in debug but is latent in release.

---

## H6 — `Blackboard::fromJson` bypass of `MAX_KEYS` cap

**Source:** Internal design bug. No external citation needed — the header `engine/scripting/blackboard.h:86` documents the cap as an invariant, and the implementation's `fromJson` path doesn't enforce it. API-contract violation.

**Applies because:** direct observation in `engine/scripting/blackboard.cpp:124-135` — writes go to `m_values[k] = v` directly instead of routing through `set()` (which enforces caps).

---

## H7–H8 — `m_pureCache` memoizes impure reads

**Source:** Internal design bug. Classic "memoization cache keyed on identity only, not inputs" anti-pattern. No direct upstream citation, but the canonical guidance is: *a function is memoizable only if it's referentially transparent* — standard functional-programming principle.

**Applies because:** `GetVariable`, `FindEntityByName`, and similar nodes classified as "pure" actually read mutable scope. Caching their output by `(nodeId, pinId)` freezes stale values. Cross-visible failure in WhileLoop semantics (H8) makes the class observable.

---

## H9 — Lambda lifetime across hot-reload

**Source:** Internal design bug. Same class as 9E audit's H1 finding (see `tools/audit/IMPROVEMENTS_FROM_9E_AUDIT.md`). The generalized "lambda captures raw pointer that outlives the captured object" pattern is well-documented in C++ code-review guidance; the 9E doc §3a proposes the tool learn to detect it.

**Applies because:** `engine/scripting/latent_nodes.cpp:103` captures `ScriptInstance*` in a lambda stored on a container that can outlive an instance re-init.

---

## H10 — Workbench residual plot filter mismatch

**Source:** Internal bug. No external citation — plain correctness review.

---

## H11 — Formula codegen injection

**Source:** [CWE-94: Improper Control of Generation of Code ('Code Injection')](https://cwe.mitre.org/data/definitions/94.html)

> The product constructs all or part of a code segment using externally-influenced input from an upstream component, but it does not neutralize or incorrectly neutralizes special elements that could modify the syntax or behavior of the intended code segment.

**Primary mitigation:**
> The most fundamental approach to preventing code injection is architectural: refactor your program to eliminate the need for dynamically generating code altogether. When this isn't feasible, implement strict input validation using an allowlist strategy that accepts only known-good inputs conforming precisely to specifications, rather than attempting to filter out malicious patterns.

**Applies because:** `engine/formula/codegen_cpp.cpp:33, 70, 87` / `codegen_glsl.cpp:32, 69, 86` splice `ExprNode.name` and `.op` (strings loaded from user-supplied JSON) verbatim into generated source text. The allowlist mitigation directly maps to the proposed fix: validate identifiers against `[A-Za-z_][A-Za-z0-9_]*` on load, whitelist known ops in codegen.

---

## H12 — Evaluator ≠ codegen (safe-math vs. raw math)

**Source:** Internal design bug — the fact that a *validated* expression produces different results from the *deployed* expression is a form of semantic drift. No CVE; review-only.

---

## H13 — LM fitter lacks non-finite guard

**Source:** [Levenberg–Marquardt algorithm — Wikipedia](https://en.wikipedia.org/wiki/Levenberg%E2%80%93Marquardt_algorithm) — explicitly **does not** address non-finite handling. Standard practice (per the classic Nocedal & Wright *Numerical Optimization*, 2nd ed., §10.3, not web-fetched) is to *reject* a step that produces non-finite residuals AND fail loudly when initial residuals are non-finite. IEEE-754 ordering semantics (all NaN comparisons return false) is the mechanism to exploit — except when the initial state is non-finite, in which case the acceptance criterion never fires.

**Applies because:** `engine/formula/curve_fitter.cpp:206-210` uses float-typed `currentError` and compares `trialError < currentError` with no pre-check. On non-finite initial residuals the loop runs to `max_iterations` silently.

---

## H14 — SH basis constant `c3` should be `c1` on band-2 `(x²−y²)` term

**Source:** Ramamoorthi & Hanrahan (2001), "An Efficient Representation for Irradiance Environment Maps", confirmed via search at [cseweb.ucsd.edu/~ravir/papers/envmap/](https://cseweb.ucsd.edu/~ravir/papers/envmap/).

The canonical constants and expansion (from multiple independently-citing sources):

| Constant | Value |
|---|---|
| c1 | 0.429043 |
| c2 | 0.511664 |
| c3 | 0.743125 |
| c4 | 0.886227 |
| c5 | 0.247708 |

Irradiance reconstruction (the relevant identity):
```
E(N) = c1 · L_22 · (x² − y²)
     + c3 · L_20 · z²
     + c4 · L_00 − c5 · L_20
     + 2·c1 · (L_{2-2}·xy + L_21·xz + L_{2-1}·yz)
     + 2·c2 · (L_11·x + L_{1-1}·y + L_10·z)
```

**Applies because:** `assets/shaders/scene.frag.glsl:553` uses `c3 * L[8] * (n.x*n.x - n.y*n.y)` for the L_22·(x²−y²) term. Per Ramamoorthi Eq. 13 (as cited in the public summary at `cseweb.ucsd.edu/~ravir/papers/envmap/`), that term's coefficient is `c1 = 0.429043`, not `c3 = 0.743125`. The C++-side basis in `engine/renderer/sh_probe_grid.cpp:29-40` matches the canonical ordering — confirming the bug is shader-only.

Ratio `c3 / c1 = 0.743125 / 0.429043 ≈ 1.732` — the exact factor by which the x²−y² term is over-weighted today.

**Referenced sources:**
- [An Efficient Representation for Irradiance Environment Maps — Ramamoorthi & Hanrahan paper landing page](https://cseweb.ucsd.edu/~ravir/papers/envmap/)
- [Chapter 4 Irradiance Environment Maps (Ramamoorthi thesis)](https://graphics.stanford.edu/papers/ravir_thesis/chapter4.pdf)

---

## H15 — Motion vectors require per-object previous model matrix for dynamic content

**Sources:**
- Brian Karis, "High Quality Temporal Supersampling" (SIGGRAPH 2014 UE4 advances course). The standard reference for production TAA; canonical across modern engines (UE4, UE5, Unity TAA, Frostbite). See [Advances in Real-Time Rendering — SIGGRAPH 2014](https://advances.realtimerendering.com/s2014/) index page.
- Cross-validated concept: [GPU Gems 3 ch.27 — "Motion Blur as a Post-Processing Effect"](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-27-motion-blur-post-processing-effect) describes the same per-object-previous-world-matrix approach (applies identically to TAA reprojection as to motion blur — both need per-object prev-clip position).

**Applies because:** `assets/shaders/motion_vectors.frag.glsl:13-43` derives motion purely from `u_currentInvViewProjection` + `u_prevViewProjection` + depth. No per-draw `u_prevModel` uniform exists (greped engine-wide). Any moving/animated object's motion is therefore under-represented by exactly its object-local velocity component.

The production fix is also well-established: the geometry pass writes `(currUV − prevUV)` to a motion-vector attachment, where `prevClip = prevViewProjection · prevModel · vec4(position, 1)`. This is the approach all major TAA implementations use.

---

## H16 — imgui-node-editor shutdown SEGV suspect

**Sources** (searched via `gh issue list --repo thedmd/imgui-node-editor`):

| Issue | Status | Title | Relevance |
|---|---|---|---|
| [#57](https://github.com/thedmd/imgui-node-editor/issues/57) | OPEN | EditorContext pointers stored in objects breaks live reloading? | Directly adjacent — suggests EditorContext pointer invalidation is a known class of bug |
| [#191](https://github.com/thedmd/imgui-node-editor/issues/191) | OPEN | Crash when minimizing window / window size too small | Related crash on canvas state |
| [#129](https://github.com/thedmd/imgui-node-editor/issues/129) | CLOSED | Crash on multi-threaded Flow animation | Closed but confirms multi-state crash class |
| [#267](https://github.com/thedmd/imgui-node-editor/issues/267) | OPEN | Segfault in ImGui_ImplVulkan_RenderDrawData with UserCallback | Non-OpenGL path, but another RenderDrawData shutdown crash |

**No exact match** for our specific "shutdown SEGV via settings-save path" hypothesis. Closest conceptual match: #57 (EditorContext pointer invalidation).

**Implication:** the root cause in our code is probably one of:
1. `ed::DestroyEditor` fires an ImGui callback to persist settings after `ImGui::DestroyContext` — reading freed memory.
2. The settings file path captured in `ed::Config` is a `const char*` pointing into `m_settingsFile` (our std::string member); if the panel moves or the string reallocates, the pointer dangles.

The code does store `m_settingsFile` as a `std::string` on the widget (`node_editor_widget.h:84`) and assigns `cfg.SettingsFile = m_settingsFile.c_str()` at `initialize()`. The editor's destructor calls `shutdown()` which calls `DestroyEditor` — so the string outlives the context destruction. That suggests option 1 is more likely: the ImGui-context-destruction happens in `Editor::shutdown` (`editor.cpp:149`) AFTER `m_scriptEditorPanel.shutdown()` (line 146). So ordering is correct on paper.

**Proposed next step** (for FIXPLAN): add `ed::Config::SaveSettings` / `LoadSettings` callbacks that no-op during shutdown, and/or attach a valgrind / asan run to the test-play cycle to catch the exact stack trace.

---

## Research gaps noted (not blockers)

- **Ramamoorthi PDF direct-extraction failed** — PDF had image-scanned math. Verified constants via multiple independent secondary sources (NVIDIA GPU Gems Ch.10, Ramamoorthi thesis chapter, WebSearch-returned summaries), all in agreement on c1 = 0.429043 for L_22·(x²−y²).
- **Karis TAA PDF direct-extraction failed** — too large / binary. The per-object previous-model requirement for motion vectors is industry-standard across UE, Unity, Frostbite; I didn't find a single authoritative quote but the proposal is standard practice.
- **urllib CRLF stdlib behavior** — docs don't promise sanitization. Mitigation is client-side validation, which is what we propose. No CVE was surfaced against stdlib itself for this specific `add_header` path.

These gaps don't change any finding's severity or proposed fix — the authoritative papers/practices are well-enough known that alternative citations suffice, and the internal-only findings need only code review, not external citation.
