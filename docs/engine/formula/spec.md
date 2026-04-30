# Subsystem Specification — `engine/formula`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/formula` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.5.0+` (Formula Pipeline FP-1…FP-6 shipped per ROADMAP "Formula Pipeline — COMPLETE") |

---

## 1. Purpose

`engine/formula` is the runtime half of the Formula Pipeline — the cross-cutting numerical-design infrastructure that CLAUDE.md Rule 6 names as the canonical home for tuning constants. It owns the Abstract Syntax Tree (AST) that represents every fittable formula in the engine, the tree-walking evaluator the Formula Workbench uses for tool-time evaluation and Levenberg-Marquardt (LM) curve fitting, the C++ and OpenGL Shading Language (GLSL) code generators that turn fitted formulas into zero-overhead inline functions and shader snippets, the **VLUT** (binary lookup-table format; magic `'VLUT'`) generator/loader pair for O(1) sampled approximations, the `FormulaQualityManager` that lets the renderer dial Full / Approximate / LUT tiers per category, and the `FormulaPreset` system that bundles coefficient overrides into named visual styles ("Realistic Desert", "Underwater", "Biblical Tabernacle"). It exists as its own subsystem because every other subsystem — water, cloth, foliage, particles, lighting, post-process — needs to read fitted coefficients without depending on the workbench (which lives in `tools/`); pushing the AST into any one of those subsystems would force the rest to depend on a sibling. For the engine's primary use case (architectural walkthroughs of biblical structures) this subsystem is what keeps the Tabernacle's caustic depth fade, Beer-Lambert water absorption, and Schlick-Fresnel reflections sharing a single source-of-truth coefficient table that survives a rebuild without anyone hand-editing a magic constant.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `ExprNode` AST (5 node types: literal / variable / binary-op / unary-op / conditional), JavaScript Object Notation (JSON) round-trip, identifier + operator allowlist | Lexer / parser for human-typed expressions — Workbench uses `tools/formula_workbench/pysr_parser.h` for symbolic-regression import |
| `ExpressionEvaluator` — tree-walking scalar evaluator with variable + coefficient bindings | Vector-valued evaluation (`vec3` formulas evaluated component-wise by the caller — by design, see §10) |
| `FormulaDefinition` (typed inputs, `FormulaOutput`, per-tier expression trees, named coefficients, provenance) and `FormulaLibrary` (named registry, JSON load/save, category index, built-in templates) | Persisted user-settings layer for selected presets — that's `engine/core/settings.h` `FormulaPresetSettings` |
| `CodegenCpp` / `CodegenGlsl` — emit inline C++ headers and GLSL function snippets from the same AST; coefficient inlining; `safe_math.h` parity prelude | Build-system wiring that invokes the generators (CMake target `formula_codegen` lives in `tools/`); shader compilation (`engine/renderer/shader.h`) |
| `LutGenerator` — sample expression over 1-3 axes, write VLUT binary; `LutLoader` — runtime VLUT load + 1D/2D/3D linear interpolation | GPU-side 3D-texture LUT path — that's `engine/renderer/`'s job (LUT tier is currently CPU-sampled per call; GPU upload flagged in §15 Open Q) |
| `FormulaQualityManager` — global tier + per-category overrides, JSON persistence | Hardware-aware automatic tier selection (Renderer reads the manager and picks the tier; this subsystem only stores intent) |
| `FormulaPreset` + `FormulaPresetLibrary` — coefficient bundles, built-in 9 styles | Preset UI / dropdowns — `engine/editor/` |
| `CurveFitter` — LM with central-difference Jacobian, weighted variant, R² / Root-Mean-Square Error (RMSE) reporting | Symbolic regression (PySR), train/test splitting, residual plots — all live in `tools/formula_workbench/` |
| `SensitivityAnalyzer` — central-difference partial derivatives, normalised sensitivity ranking | Bayesian / Markov-Chain Monte Carlo (MCMC) parameter inference — out of scope; LM is sufficient for ≤ 20 coefficients |
| `NodeGraph` — visual-editor data structures (`Node` / `Port` / `Connection`), DAG cycle check, `ExprNode` round-trip | ImGui rendering — `tools/formula_workbench/formula_node_editor_panel.h` |
| `PhysicsTemplates` — 28 built-in formula factory functions (drag, Fresnel, Beer-Lambert, GGX, ACES, …) | Per-formula physical justification / derivation — that's `docs/research/formula_pipeline_design.md` |
| `FormulaBenchmark` — chrono-driven per-tier timing + Full-vs-Approximate comparison | Continuous-integration regression harness — `tests/test_reference_harness.cpp` (lives in `tests/`) |
| `FormulaDocGenerator` — markdown export of formula metadata | API documentation generation (Doxygen owns the C++ surface) |
| `SafeMath` — single-source-of-truth guards (`safeDiv`, `safeSqrt`, `safeLog`, `safePow`) shared between the evaluator and both codegens, plus the GLSL prelude string | Shader-side math intrinsics outside Formula Pipeline use (those compile as-is) |

## 3. Architecture

```
                                ┌───────────────────────────────────────────┐
                                │             FormulaLibrary                │
                                │   (named registry of FormulaDefinition)   │
                                └──┬──────────────┬─────────────────┬───────┘
                                   │              │                 │
                       registers   │              │ findByName/Cat. │ apply()
                                   │              ▼                 │
            ┌────────────────┐  ┌──┴───────────┐  consumer code  ┌──┴────────────┐
            │PhysicsTemplates│  │FormulaDef.   │                 │FormulaPreset  │
            │(28 factories)  │  │ inputs/output│                 │+ PresetLibrary│
            └────────────────┘  │ coefficients │                 │(9 built-ins)  │
                                │ expressions/ │                 └───────────────┘
                                │   tier (map) │
                                └──┬───────────┘
                                   │ ExprNode (immutable AST)
                       ┌───────────┼─────────────┬──────────────┬─────────────┐
                       ▼           ▼             ▼              ▼             ▼
              ┌────────────┐ ┌──────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
              │Expression  │ │CodegenCpp│ │CodegenGlsl │ │LutGenerator│ │ NodeGraph   │
              │Evaluator   │ │(.h emit) │ │(prelude +  │ │(VLUT write)│ │ (DAG ⇄ AST) │
              │(tool-time) │ │          │ │ shader fn) │ │            │ │             │
              └─────┬──────┘ └──────────┘ └────────────┘ └─────┬──────┘ └─────────────┘
                    │                                          │
                    │ used by                                   │ written file
                    ▼                                           ▼
              ┌────────────┐ ┌─────────────────┐         ┌────────────┐
              │CurveFitter │ │SensitivityAnal. │         │ LutLoader  │
              │(LM, R²/    │ │(∂f/∂c, ranking) │         │(1D/2D/3D   │
              │ RMSE, wgt) │ │                 │         │ linear)    │
              └────────────┘ └─────────────────┘         └────────────┘
                                                                ▲
                                            ┌───────────────────┴────────────┐
                                            │  FormulaQualityManager         │
                                            │  (global + per-category tier,  │
                                            │   read by renderer + systems)  │
                                            └────────────────────────────────┘
                                                       ▲
                                                       │ owned by
                                                       │
                                                ┌──────┴────────┐
                                                │  Engine       │  (engine/core/engine.h:140)
                                                └───────────────┘
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `ExprNode` | struct (immutable tree) | 5-variant AST node + factory functions + JSON round-trip + identifier/op allowlist. `engine/formula/expression.h:36` |
| `ExprNodeType` | enum | `LITERAL` / `VARIABLE` / `BINARY_OP` / `UNARY_OP` / `CONDITIONAL`. `engine/formula/expression.h:22` |
| `FormulaInput` | struct | Typed input variable with units + default. `engine/formula/formula.h:41` |
| `FormulaDefinition` | struct | Named formula with per-tier expressions, coefficients, provenance, R². `engine/formula/formula.h:64` |
| `QualityTier` | enum | `FULL` / `APPROXIMATE` / `LUT`. `engine/formula/formula.h:24` |
| `FormulaLibrary` | class | Name → definition registry; JSON load/save; category index; `registerBuiltinTemplates()`. `engine/formula/formula_library.h:22` |
| `ExpressionEvaluator` | class | Tree-walking scalar evaluator + `validate()` helper. `engine/formula/expression_eval.h:32` |
| `CodegenCpp` | class (static) | AST → C++ inline function header. `engine/formula/codegen_cpp.h:25` |
| `CodegenGlsl` | class (static) | AST → GLSL function snippet + `safeMathPrelude()`. `engine/formula/codegen_glsl.h:25` |
| `SafeMath` | namespace (header-only) | `safeDiv` / `safeSqrt` / `safeLog` / `safePow` + GLSL prelude string. `engine/formula/safe_math.h:27` |
| `LutAxisDef`, `LutGenerateResult`, `LutGenerator` | struct + class (static) | Sample expression over axes; write VLUT binary. `engine/formula/lut_generator.h:35,44,61` |
| `LutLoader` | class | Load VLUT; 1D/2D/3D linear sampling; Fowler-Noll-Vo 1a (FNV-1a) axis-name hash lookup. `engine/formula/lut_loader.h:38` |
| `FormulaQualityManager` | class | Global + per-category tier intent; JSON persistence. `engine/formula/quality_manager.h:33` |
| `FormulaPreset` / `FormulaPresetLibrary` | struct + class | Coefficient-override bundles (9 built-ins). `engine/formula/formula_preset.h:37,54` |
| `FitConfig`, `FitResult`, `CurveFitter` | structs + class (static) | LM curve fitter (uniform + weighted). `engine/formula/curve_fitter.h:33,45,65` |
| `SensitivityAnalyzer` | class | Central-difference ∂f/∂c with normalised ranking. `engine/formula/sensitivity_analysis.h:47` |
| `NodeGraph`, `Node`, `Port`, `Connection` | classes + structs | Visual-editor DAG; AST round-trip; cycle check. `engine/formula/node_graph.h:112,70,59,96` |
| `FormulaBenchmark` | class | Per-tier chrono timing + Full-vs-Approximate accuracy comparison. `engine/formula/formula_benchmark.h:47` |
| `FormulaDocGenerator` | class | Markdown export of library metadata. `engine/formula/formula_doc_generator.h:23` |
| `PhysicsTemplates` | class (static) | 28 factory functions for built-in formulas. `engine/formula/physics_templates.h:21` |

## 4. Public API

The subsystem is large enough to be a facade — 14 public headers — so per the template's pattern 2 the API is grouped per-header below.

```cpp
// engine/formula/expression.h — full surface 114 lines.
struct ExprNode {
    static std::unique_ptr<ExprNode> literal(float val);
    static std::unique_ptr<ExprNode> variable(const std::string& name);
    static std::unique_ptr<ExprNode> binaryOp(const std::string& op,
                                               std::unique_ptr<ExprNode> l,
                                               std::unique_ptr<ExprNode> r);
    static std::unique_ptr<ExprNode> unaryOp(const std::string& fn,
                                              std::unique_ptr<ExprNode> arg);
    static std::unique_ptr<ExprNode> conditional(std::unique_ptr<ExprNode>,
                                                  std::unique_ptr<ExprNode>,
                                                  std::unique_ptr<ExprNode>);
    std::unique_ptr<ExprNode> clone() const;
    bool usesVariable(const std::string&) const;
    void collectVariables(std::vector<std::string>& out) const;
    nlohmann::json toJson() const;
    static std::unique_ptr<ExprNode> fromJson(const nlohmann::json&);  // throws on hostile input
    static bool isValidVariableName(const std::string&);
    static bool isAllowedBinaryOp(const std::string&);
    static bool isAllowedUnaryOp(const std::string&);
};
```

```cpp
// engine/formula/formula.h — see header for full surface.
struct FormulaInput  { std::string name; FormulaValueType type; std::string unit; float defaultValue; };
struct FormulaOutput { FormulaValueType type; std::string unit; };

struct FormulaDefinition {
    std::string name, category, description, source;
    std::vector<FormulaInput> inputs;
    FormulaOutput output;
    std::map<QualityTier, std::unique_ptr<ExprNode>> expressions;
    std::map<std::string, float> coefficients;
    float accuracy = 1.0f;

    FormulaDefinition clone() const;
    bool hasTier(QualityTier) const;
    const ExprNode* getExpression(QualityTier = QualityTier::FULL) const;  // falls back to FULL
    nlohmann::json toJson() const;
    static FormulaDefinition fromJson(const nlohmann::json&);
};
```

```cpp
// engine/formula/formula_library.h — see header:22 for full surface.
class FormulaLibrary {
public:
    void   registerFormula(FormulaDefinition);
    bool   removeFormula(const std::string&);
    const  FormulaDefinition* findByName(const std::string&) const;
    std::vector<const FormulaDefinition*> findByCategory(const std::string&) const;
    std::vector<const FormulaDefinition*> getAll() const;
    size_t loadFromJson(const nlohmann::json&);
    size_t loadFromFile(const std::string& path);
    bool   saveToFile(const std::string& path) const;
    void   registerBuiltinTemplates();
};
```

```cpp
// engine/formula/expression_eval.h — see header:32 for full surface.
class ExpressionEvaluator {
public:
    using VariableMap = std::unordered_map<std::string, float>;
    float evaluate(const ExprNode&, const VariableMap&) const;            // throws on undeclared var
    float evaluate(const ExprNode&, const VariableMap&,
                   const std::unordered_map<std::string, float>& coeffs) const;
    static bool validate(const ExprNode&,
                         const std::vector<FormulaInput>&,
                         const std::vector<std::string>& coeffNames,
                         std::string& errorOut);
};
```

```cpp
// engine/formula/codegen_cpp.h / codegen_glsl.h — symmetric facades.
class CodegenCpp {
public:
    static std::string generateFunction(const FormulaDefinition&, QualityTier = FULL);
    static std::string generateHeader  (const std::vector<const FormulaDefinition*>&, QualityTier = FULL);
    static std::string emitExpression  (const ExprNode&, const std::map<std::string,float>& coeffs);
};
class CodegenGlsl {
public:
    static std::string generateFunction(const FormulaDefinition&, QualityTier = FULL);
    static std::string generateFile    (const std::vector<const FormulaDefinition*>&, QualityTier = FULL);
    static std::string emitExpression  (const ExprNode&, const std::map<std::string,float>& coeffs);
    static std::string safeMathPrelude();  // public for ad-hoc shader splicing — AUDIT.md §H12
};
```

```cpp
// engine/formula/lut_generator.h / lut_loader.h — VLUT format.
struct LutAxisDef        { std::string variableName; float minValue, maxValue; uint32_t resolution; };
struct LutGenerateResult { bool success; std::vector<float> data; std::vector<LutAxisDef> axes;
                           float minValue, maxValue; std::string errorMessage; };

class LutGenerator {
public:
    static LutGenerateResult generate(const FormulaDefinition&,
                                      const std::vector<LutAxisDef>& axes,
                                      QualityTier = FULL,
                                      const std::unordered_map<std::string,float>& extraVars = {});
    static bool writeToFile(const LutGenerateResult&, const std::string& path);
    static uint32_t fnv1aHash(const std::string&);
};
class LutLoader {
public:
    bool  loadFromFile(const std::string& path);
    bool  loadFromResult(const LutGenerateResult&);
    float sample1D(float x) const;
    float sample2D(float x, float y) const;
    float sample3D(float x, float y, float z) const;
    int   dimensions() const;
    bool  isLoaded() const;
};
```

```cpp
// engine/formula/quality_manager.h — see header:33 for full surface.
class FormulaQualityManager {
public:
    void        setGlobalTier(QualityTier);
    QualityTier getGlobalTier() const;
    void        setCategoryTier(const std::string& category, QualityTier);
    QualityTier getCategoryTier(const std::string& category) const;
    bool        hasCategoryOverride(const std::string& category) const;
    void        clearCategoryOverride(const std::string& category);
    QualityTier getEffectiveTier(const std::string& category) const;
    nlohmann::json toJson() const;
    void           fromJson(const nlohmann::json&);
};
```

```cpp
// engine/formula/formula_preset.h — see header:54 for full surface.
struct FormulaOverride { std::string formulaName; std::map<std::string,float> coefficients;
                         std::string description; };
struct FormulaPreset   { std::string name, displayName, category, description, author;
                         std::vector<FormulaOverride> overrides;
                         nlohmann::json toJson() const;
                         static FormulaPreset fromJson(const nlohmann::json&); };

class FormulaPresetLibrary {
public:
    void   registerPreset(FormulaPreset);
    const  FormulaPreset* findByName(const std::string&) const;
    static size_t applyPreset(const FormulaPreset&, FormulaLibrary&);  // returns # updated
    void   registerBuiltinPresets();  // 9 styles
    // load / save / category / count …
};
```

```cpp
// engine/formula/curve_fitter.h — Levenberg-Marquardt.
struct DataPoint { ExpressionEvaluator::VariableMap variables; float observed; };
struct FitConfig { int maxIterations = 200; float convergenceThreshold = 1e-8f;
                   float gradientThreshold = 1e-10f; float initialLambda = 1e-3f;
                   float lambdaUpFactor = 10.0f; float lambdaDownFactor = 10.0f;
                   float finiteDiffStep = 1e-5f; };
struct FitResult { bool converged; std::map<std::string,float> coefficients;
                   float rSquared, rmse, maxError, finalError;
                   int iterations; std::string statusMessage; };

class CurveFitter {
public:
    static FitResult fit(const FormulaDefinition&,
                         const std::vector<DataPoint>&,
                         const std::map<std::string,float>& initialCoeffs,
                         QualityTier = FULL, const FitConfig& = {});
    static FitResult fitWeighted(const FormulaDefinition&,
                                 const std::vector<DataPoint>&,
                                 const std::vector<float>& weights,  // empty → uniform
                                 const std::map<std::string,float>& initialCoeffs,
                                 QualityTier = FULL, const FitConfig& = {});
};
```

```cpp
// engine/formula/sensitivity_analysis.h — see header:47.
struct CoefficientSensitivity { std::string name; float baseValue, derivative,
                                normalizedSensitivity, minEffect, maxEffect; };
struct SensitivityReport      { std::string formulaName;
                                std::vector<CoefficientSensitivity> coefficients;
                                float baseOutput; void sortByImpact(); };
class  SensitivityAnalyzer    { /* analyze(...) → SensitivityReport */ };
```

```cpp
// engine/formula/node_graph.h — see header:112.
class NodeGraph {
public:
    NodeId       addNode(Node);
    bool         removeNode(NodeId);
    ConnectionId connect(NodeId src, PortId, NodeId dst, PortId);  // 0 = invalid (cycle/type/dup)
    bool         disconnect(ConnectionId);
    bool         wouldCreateCycle(NodeId src, NodeId dst) const;
    bool         validate(std::string& errorOut) const;
    std::unique_ptr<ExprNode> toExpressionTree(NodeId outputNodeId) const;
    static NodeGraph fromExpressionTree(const ExprNode&);
    nlohmann::json toJson() const;
    static NodeGraph fromJson(const nlohmann::json&);
    // factory helpers: createMathNode / FunctionNode / LiteralNode / VariableNode /
    //                  OutputNode / ConditionalNode
};
```

```cpp
// engine/formula/safe_math.h — see header:27.
namespace Vestige::SafeMath {
    inline float safeDiv (float l, float r);  // r==0 → 0
    inline float safeSqrt(float x);            // sqrt(|x|)
    inline float safeLog (float x);            // x≤0 → 0
    inline float safePow (float b, float e);   // negative-base + non-integer exp → 0
    inline const char* glslPrelude();           // emitted by CodegenGlsl
}
```

```cpp
// engine/formula/physics_templates.h, formula_benchmark.h, formula_doc_generator.h —
// tool-side surfaces, full headers fit on one screen each (76 / 85 / 45 lines).
```

**Non-obvious contract details:**

- `ExprNode::fromJson` and the binary/unary factories **throw** `std::runtime_error` on any operator outside the allowlist or any identifier failing `[A-Za-z_][A-Za-z0-9_]*` ≤ 128 chars. This is by design — codegen-injection hardening per AUDIT.md §H11 / FIXPLAN E3. Throwing is acceptable here because both paths run at tool/load time, never per frame.
- `ExpressionEvaluator::evaluate` is the tool-time path only. **Runtime code calls into generated C++ headers** (or sampled VLUTs / generated shaders); never the evaluator. This is what makes the LM fitter and runtime use the *same* numerical semantics — `safe_math.h` is the single source of truth, and the GLSL codegen prepends the matching prelude verbatim.
- `FormulaDefinition::getExpression(tier)` falls back to `FULL` when the requested tier is absent. This lets a category opt out of `APPROXIMATE` / `LUT` simply by not authoring those expressions.
- `FormulaLibrary` owns its definitions by value; pointer returns from `findByName` / `findByCategory` are valid until the next `registerFormula` / `removeFormula` / `clear` / `loadFrom*`. Callers must not cache pointers across reload.
- `LutLoader::sample*` clamps inputs to the axis range — out-of-range queries return the boundary value rather than NaN. Negative-resolution inputs are rejected at load (`isLoaded()` stays false).
- VLUT format: little-endian magic `0x54554C56` ("VLUT") + version 1 + flags + axis-count + per-axis (FNV-1a name hash, min, max, sample-count) + data (row-major, `float32`). FNV-1a hash means file inspection requires the original variable names; the hash is opaque on disk.
- `CurveFitter::fitWeighted` treats `weights.size() != data.size()` (including empty) as a fall-through to uniform weighting — this is intentional backwards-compat with the unweighted overload, not a silent error. Negative weights clamp to 0.
- `NodeGraph::connect` returns `0` (invalid) on cycle, type mismatch, duplicate, or self-loop — there is no separate error channel. Callers check `result != 0`.
- `FormulaQualityManager::getEffectiveTier(category)` is the only call sites should make — `getCategoryTier` returns the override even when none was set (it returns the global tier in that case, which is confusing; prefer `getEffectiveTier`). Flagged as Open Q in §15.
- `SafeMath` semantics deliberately project degenerate math to 0 rather than NaN — the LM fitter's residual computation requires finite values to stay in finite-value space (AUDIT.md §H12 / FIXPLAN E4 / AUDIT M11).

**Stability:** the public facade above is semver-frozen for `v0.x`. The VLUT binary format is versioned (`VLUT_VERSION = 1`) — bumps will keep readers backwards-compatible per CODING_STANDARDS §32 conventions.

## 5. Data Flow

**Tool-time path (Workbench, build-time codegen):**

1. User authors / imports a `FormulaDefinition` (template, CSV import, PySR symbolic regression).
2. `FormulaLibrary::registerFormula(def)` indexes by name + category.
3. `CurveFitter::fit(def, samples, initialCoeffs)` → `ExpressionEvaluator::evaluate` for each residual + central-difference Jacobian → LM step → updated coefficients.
4. On convergence (`rSquared ≥ threshold`), the editor writes the fitted coefficients back into `def.coefficients`.
5. Workbench export → `CodegenCpp::generateHeader` + `CodegenGlsl::generateFile` → committed to the source tree as generated artefacts.
6. Optionally: `LutGenerator::generate(def, axes)` → `LutGenerator::writeToFile("foo.vlut")` for the LUT tier.

**Runtime path (per-frame, every frame):**

1. The renderer / system reads `engine.getFormulaQuality().getEffectiveTier("water")` (or whatever category).
2. Based on tier, the system either:
   - calls the generated C++ inline function (`FULL` / `APPROXIMATE` C++ tier),
   - samples a `LutLoader` instance (`LUT` tier),
   - or runs the generated GLSL function inside a shader.
3. Coefficient overrides from `FormulaPreset::applyPreset` were already baked into the `FormulaLibrary` at scene-load (or settings-apply) time; runtime never re-applies presets per frame.

**`FormulaPreset` apply path (settings-apply / scene-load):**

1. `FormulaPresetLibrary::applyPreset(preset, library)` walks `preset.overrides`.
2. For each `FormulaOverride`, look up the named formula in the library; copy each `coefficients[name] = value` into `def.coefficients`.
3. Any subsequent `getExpression()` returns the same AST; only the inlined coefficient values change. Re-codegen is **not** automatic — runtime users either rebuild or sample LUTs that already incorporate the new coefficients.

**Exception path:** `ExprNode::fromJson` and the factories throw `std::runtime_error` for hostile JSON (codegen-injection hardening). `ExpressionEvaluator::evaluate` throws `std::runtime_error` when an undeclared variable appears at evaluate time. `LutLoader::loadFromFile` returns `false` on bad magic / version / size mismatch (no throw). `LutGenerator::generate` returns a result with `success=false` + `errorMessage` rather than throwing. `CurveFitter::fit` returns a `FitResult` with `converged=false` rather than throwing.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7 — this subsystem has both CPU and GPU code paths.

| Workload | Placement | Reason |
|----------|-----------|--------|
| AST construction, JSON round-trip, identifier validation | CPU (load-time / tool-time) | Branching, sparse, decision-heavy; never per-frame. CODING_STANDARDS §17 default. |
| `ExpressionEvaluator::evaluate` — tree-walking scalar evaluator | CPU (tool-time only) | Recursive virtual-style dispatch on `ExprNodeType`. Acceptable at tool-time inside the LM fitter inner loop; **explicitly forbidden at runtime** — runtime uses generated code. |
| `CurveFitter` LM iterations + Jacobian | CPU (tool-time only) | `O(samples × coeffs)` finite-difference Jacobian; not a per-frame workload. |
| `CodegenCpp` output | CPU (build-time) → **CPU (runtime)** | Generated `.h` is `#include`d into runtime code; the inlined call compiles to scalar ALU ops and is the canonical CPU runtime path. |
| `CodegenGlsl` output | CPU (build-time) → **GPU (runtime, fragment / vertex / compute shader)** | Generated `.glsl` is concatenated into shader source; runs per-pixel / per-vertex / per-fragment on the Graphics Processing Unit (GPU). Per-pixel + per-vertex is exactly the CODING_STANDARDS §17 GPU heuristic. |
| `LutGenerator::generate` — sample formula across axes | CPU (tool-time / build-time) | One-shot, may take seconds for 256³ tables; never on the critical path. |
| `LutLoader::sample*` — runtime LUT lookup | **CPU (runtime)** today, GPU eligible | Currently sampled into `std::vector<float>`. Renderer is free to upload the same data to a `GL_TEXTURE_3D` and let hardware trilinear handle it; that's the §15 Open Q. The CPU path stays as the spec / parity reference. |
| `SafeMath` guards | Both | C++ helpers run on CPU; the GLSL prelude (`Vestige::SafeMath::glslPrelude`) emits the same four guards into the shader so evaluator / C++ codegen / GLSL codegen are bit-equivalent on degenerate input. |

**Dual-implementation parity.** The three evaluation paths (tree-walking evaluator, generated C++, generated GLSL) must agree on degenerate inputs to keep the LM fitter's R² meaningful at runtime. The single source of truth is `engine/formula/safe_math.h` — the C++ helpers live in that namespace, the GLSL prelude is emitted verbatim by `CodegenGlsl::safeMathPrelude()`. The pin is `tests/test_formula_compiler.cpp` (codegen vs. evaluator round-trip) — bit-equivalence is enforced for the four guarded operations. AUDIT.md §H12 / FIXPLAN E4 / AUDIT M11 record the regression that motivated this single-source design.

## 7. Threading model

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (`Engine::run`) | `FormulaQualityManager::*`, `FormulaLibrary::registerFormula`/`load*`/`save*`, `FormulaPresetLibrary::*`, runtime users of generated headers + `LutLoader::sample*` | None |
| **Tool thread** (Workbench) | `CurveFitter::*`, `SensitivityAnalyzer::*`, `ExpressionEvaluator::*`, `LutGenerator::*`, `NodeGraph::*`, `CodegenCpp::*`, `CodegenGlsl::*`, `FormulaBenchmark::*`, `FormulaDocGenerator::*` | None |
| **Worker / job thread** | Read-only: `FormulaLibrary::findByName` / `findByCategory` / `getAll`, `FormulaQualityManager::getEffectiveTier`, `LutLoader::sample*`, `ExpressionEvaluator::evaluate` (with caller-owned `VariableMap`) — **provided no other thread is mutating the same instance** | None (subsystem holds none) |

**Thread-safety contract.** `ExpressionEvaluator` is **stateless** (the class has no member variables; both `evaluate` overloads are `const` and walk a caller-owned tree against caller-owned variable maps). It is therefore safe to call concurrently from multiple threads on the same evaluator instance, **as long as the `ExprNode` and `VariableMap` arguments are not being mutated by another thread**. `CurveFitter` is similarly stateless (all-static methods).

`FormulaLibrary`, `FormulaPresetLibrary`, `FormulaQualityManager`, `NodeGraph`, `LutLoader` carry per-instance state via `std::map` / `std::vector`. **They are not internally synchronised.** Per CODING_STANDARDS §13 the contract is "main-thread mutates, workers read between mutation phases" — the same pattern as `engine/scene` and `engine/resource`. Concurrent mutation is undefined.

`PhysicsTemplates`, `CodegenCpp`, `CodegenGlsl`, `LutGenerator`, `FormulaDocGenerator`, `FormulaBenchmark`, `SensitivityAnalyzer` are stateless / `static`-only and may be called from any thread.

## 8. Performance budget

60 frames per second (FPS) hard requirement → 16.6 ms per frame.

Not yet measured — will be filled by the Phase 11 audit; tracked as Open Q6 in §15. The runtime budget breaks into three ranges depending on which tier the renderer picked:

| Path | Budget (target) | Measured (RX 6600, 1080p) |
|------|-----------------|----------------------------|
| Generated C++ inline call (`FULL` / `APPROXIMATE` tier) | < 50 ns / call (fully inlined) | TBD — measure via `FormulaBenchmark` against `tools/formula_workbench/benchmark.cpp` before Phase 11 |
| `LutLoader::sample2D` (CPU path, cache-warm) | < 100 ns / call | TBD — measure before Phase 11 |
| Generated GLSL function inside fragment shader | < ALU budget of equivalent hand-written shader (target ≤ 1.05×) | TBD — measure with RenderDoc shader profiler before Phase 11 |
| `ExpressionEvaluator::evaluate` (tool-time only) | < 5 µs / call typical | TBD — already used inside LM Jacobian; budget exists to bound LM iteration time |
| `CurveFitter::fit` (200 iterations, 100 samples, 5 coeffs) | < 100 ms total | TBD — Workbench user-perception threshold |
| `LutGenerator::generate` (256² 2D LUT) | < 500 ms total | TBD |
| `FormulaPresetLibrary::applyPreset` | < 1 ms | TBD — settings-apply path |
| `FormulaLibrary::loadFromFile` (~30 formulas) | < 20 ms | TBD — load-time path |

**Profiler markers.** No `glPushDebugGroup` markers originate in `engine/formula` itself — the runtime call sites are inside the renderer / systems (water, foliage, particle, post) and those subsystems own their own markers. The Workbench (`tools/formula_workbench/`) has its own ImGui timing display via `FormulaBenchmark`.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (`std::unique_ptr`-owned `ExprNode` trees inside `FormulaDefinition::expressions`; `std::map` / `std::vector` storage in `FormulaLibrary` / `FormulaPresetLibrary` / `FormulaQualityManager`). No arena; no per-frame transient allocation. |
| AST size | Each `ExprNode` ≈ 80 bytes (`type` + `value` + two `std::string` + `std::vector<unique_ptr<ExprNode>>`); a typical fitted formula is 5-50 nodes → low single-digit kilobytes. |
| `FormulaLibrary` working set | ~30-80 formulas × 3 tiers × ~30 nodes × ~80 bytes ≈ 200-600 kilobytes including coefficient maps. |
| LUT working set | Variable: 256³ float32 = 64 mebibytes (worst case 3D); typical 2D 256² = 256 kilobytes; 1D 1024 = 4 kilobytes. `LutLoader` keeps the data buffer alive for the lifetime of the loader. |
| `NodeGraph` working set | Per visual-editor instance: tens to hundreds of nodes × ~120 bytes + connections; tens of kilobytes typical. |
| Ownership | `FormulaLibrary` owns `FormulaDefinition` by value (its member `std::map<std::string, FormulaDefinition>`). `FormulaDefinition` owns its `ExprNode` trees via `unique_ptr`. `LutLoader` owns its data buffer. `Engine` owns `FormulaQualityManager` (`engine/core/engine.h:140`). `FormulaLibrary` and `FormulaPresetLibrary` are typically owned by the Workbench tool, *not* by `Engine` — runtime code consumes pre-baked codegen output, not live library state. |
| Lifetimes | Library + presets: workbench-lifetime (tool-time) or scene-load duration (when consumed by runtime apply paths). Quality manager: engine-lifetime. Generated C++/GLSL artefacts: source-tree-lifetime (committed). VLUTs: scene-load duration (loaded on demand by consumers). |

No `new`/`delete` in feature code (CODING_STANDARDS §12); all ownership flows through `std::unique_ptr` / value containers.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions on the steady-state runtime path. Tool-time / load-time paths may throw on hostile input because the cost is paid once.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Malformed JSON / unknown node type / unrecognised operator | `ExprNode::fromJson` throws `std::runtime_error` (the load-time codegen-injection hardening, AUDIT.md §H11 / FIXPLAN E3) | Library load: `FormulaLibrary::loadFromJson` catches per-formula and reports a count; bad formulas are skipped |
| Identifier > 128 chars or non-`[A-Za-z_][A-Za-z0-9_]*` | Same as above — `isValidVariableName` returns false, factory / `fromJson` throws | Same as above |
| `ExpressionEvaluator::evaluate` undeclared variable | throws `std::runtime_error("unknown variable: …")` (tool-time only) | Caller validates against `validate(node, inputs, coeffNames, errorOut)` before invoking |
| Type error (`vec3` evaluation requested) | Component-wise evaluation by caller — the evaluator is **scalar-only by design**; vec3 formulas are evaluated three times | This is documented in `expression_eval.h:9` |
| LUT bounds (input outside axis range) | `LutLoader::sample*` clamps to boundary (no error) | Caller's responsibility to keep inputs in range; clamping is the documented contract |
| LUT load — bad magic / version / data-size mismatch | `LutLoader::loadFromFile` returns `false`; `isLoaded()` stays `false` | Caller substitutes default behaviour or aborts |
| LUT generate — empty axes / unknown variable / evaluation failure | `LutGenerateResult::success = false` + `errorMessage` populated | Caller checks `success` before `writeToFile` |
| `CurveFitter::fit` non-convergence (max iterations / singular Jacobian) | `FitResult::converged = false` + `statusMessage` describes the cause | Workbench shows status; user can adjust `FitConfig` or initial coefficients |
| `NodeGraph::connect` cycle / type mismatch / duplicate / self-loop | Returns `ConnectionId = 0` | Caller checks `result != 0` |
| `NodeGraph::toExpressionTree` malformed graph (missing connection, output node not reachable) | Returns `nullptr` | Caller validates with `NodeGraph::validate(errorOut)` first |
| Codegen — disallowed operator from a hand-built `ExprNode` | Both `CodegenCpp::emitExpression` and `CodegenGlsl::emitExpression` emit a comment-prefixed `/* invalid op */` rather than the raw string (defence in depth on top of factory-time validation) | Tests assert no invalid ops escape; if they do, the build catches the resulting C++/GLSL syntax error |
| `safePow(negative, fractional)` etc. | Returns 0.0 (degenerate-math projection) | This is the single-source-of-truth contract — see `safe_math.h` and AUDIT M11 |
| Out of memory | `std::bad_alloc` propagates | App aborts (CODING_STANDARDS §11) |

`Result<T, E>` / `std::expected` is not yet adopted in `engine/formula` — the module predates the codebase-wide migration. The `LutGenerateResult` / `FitResult` structs are the local equivalents; migration is on the broader engine debt list, not formula-specific.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `FormulaLibrary` registration / lookup / category index / JSON round-trip | `tests/test_formula_library.cpp` | Public API contract |
| Codegen (C++ + GLSL) — emission, identifier allowlist, coefficient inlining, evaluator parity | `tests/test_formula_compiler.cpp` | Round-trip + parity |
| `CurveFitter` LM convergence on synthetic data (linear, polynomial, exponential, weighted) | `tests/test_curve_fitter.cpp` | Algorithm correctness + R²/RMSE |
| `NodeGraph` ↔ `ExprNode` round-trip, cycle detection, validation, JSON | `tests/test_node_graph.cpp` | DAG invariants |
| `FormulaPreset` apply, JSON round-trip, built-in presets registration | `tests/test_formula_tools.cpp` | Public API |
| `FormulaNodeEditorPanel` (headless `sampleFormulaCurve`) — monotonicity / linearity / sweep auto-pick / clamp / error paths | `tests/test_formula_node_editor_panel.cpp` | Tool-side wiring |
| Reference-formula regression harness (per-formula spec JSON, auto-discovered) | `tests/test_reference_harness.cpp` (lives outside `engine/formula`, registers formulas via this subsystem) | Numerical drift |

Per the project rule (every feature + bug fix gets a test), no untested public functions in the shipped surface.

**Adding a test for `engine/formula`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `FormulaLibrary` / `ExpressionEvaluator` / `CurveFitter` directly without an `Engine` instance — every primitive in this subsystem **except** the GLSL codegen output (which only exercises end-to-end inside a real shader compile) is unit-testable headlessly. Workbench-resident pieces (PySR import, ImPlot residual plots) are tested by the Workbench's own test target. Deterministic seeding for LM fits: pin `FitConfig{.initialLambda = 0.001f}` and a fixed initial-coefficient map; the algorithm is deterministic given fixed inputs.

**Coverage gap:** the GLSL codegen output is validated by `test_formula_compiler.cpp` for *string content* (allowlist + structure), but actual GPU compilation only happens during the renderer's shader-load path. A regression that compiles a syntactically-valid shader producing wrong values would slip past `test_formula_compiler` — the parity safety net is the runtime integration tests (`test_water_*`, `test_lighting_*`) plus the visual-test runner.

## 12. Accessibility

`engine/formula` itself produces no user-facing pixels or sound. **However**, it is the route every numerical visual constant flows through — every coefficient that affects how the world looks ultimately lives in this subsystem. Accessibility consequences:

- `FormulaPreset` "Calm Interior" exists specifically to satisfy users who need a low-stimulation visual environment (reduced bloom, gentler caustics, lower wind amplitude). The preset's `description` field documents the intent so the editor's preset picker can surface this to users.
- `FormulaQualityManager` is the lever that lets a partially-sighted user dial down high-frequency visual noise (animated FBM water, foliage shimmer) without disabling whole subsystems — switching `water` to `LUT` tier removes the temporal noise that animated FBM contributes.
- `safe_math.h` projecting degenerate math to 0 is itself an accessibility property: it prevents NaN-driven black-pixel flashes that would otherwise plague users sensitive to luminance discontinuities (the photosensitive-safety surface in `engine/core/settings.h` defends against deliberate flashes; this defends against accidental ones).
- The `FormulaPresetLibrary` UI (in `engine/editor/`) must back colour-coded category badges with text labels — same constraint as every other editor panel per project memory.

Constraint summary for downstream UIs that consume `engine/formula`:

- Preset names and descriptions must be readable plain text (already enforced — both fields are `std::string`).
- Quality-tier dropdowns must label every tier ("Full", "Approximate", "LUT") not just colour-code them.
- Coefficient editors must accept text input as well as sliders (Workbench already does this).

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `<nlohmann/json.hpp>` (and `json_fwd.hpp`) | external | JSON round-trip for AST, `FormulaDefinition`, `FormulaLibrary`, presets, quality manager |
| `<cmath>`, `<chrono>`, `<cstdint>`, `<map>`, `<unordered_map>`, `<memory>`, `<string>`, `<vector>` | std | Math intrinsics, benchmarking timing, unsigned-integer types, ordered + hashed containers, RAII ownership, identifier storage, sample / coefficient buffers |
| `<fstream>` | std | VLUT binary I/O, JSON file read/save |
| `<random>` | std | Used internally by `CurveFitter` for tie-break / restart strategies |
| **None** of `engine/core`, `engine/renderer`, `engine/scene`, `engine/physics`, … | engine | This subsystem is intentionally an **upstream leaf** — no engine subsystem `#include`s into `engine/formula`. The only callers in the engine tree are `engine/core/engine.h` (owns `FormulaQualityManager` by value) and `engine/scripting/pure_nodes.cpp` (uses `safe_math.h`). |

**Direction:** every other engine subsystem may `#include` `engine/formula/*` (and a handful do — water, foliage, scripting, post-process via the generated C++ headers; the renderer via the GLSL prelude). `engine/formula` includes nothing from any other engine subsystem. This one-way dependency is what lets the Workbench tool link against `engine/formula` without dragging in the entire engine.

## 14. References

Cited research / authoritative external sources (recent ones first):

- Khronos Group. *glslang — reference GLSL / ESSL front-end + AST*. The repo's GLSL parser + AST is the closest open-source analogue to this subsystem's expression evaluator + GLSL codegen pair. <https://github.com/KhronosGroup/glslang>
- ALGLIB. *Levenberg–Marquardt algorithm — C++/C#/Java library* (2025 release) — current best-practice implementation reference for the LM step + damping schedule. <https://www.alglib.net/optimization/levenbergmarquardt.php>
- *3D Lookup Table* (Wikipedia, 2025 revision) — canonical description of the trilinear-interpolation lookup geometry used by `LutLoader::sample3D`. <https://en.wikipedia.org/wiki/3D_lookup_table>
- *Levenberg–Marquardt algorithm* (Wikipedia, 2025 revision) — reference for the convergence criteria and lambda-update heuristics encoded in `FitConfig`. <https://en.wikipedia.org/wiki/Levenberg%E2%80%93Marquardt_algorithm>
- Reference papers (still load-bearing for the original FP design):
  - Wang et al. SIGGRAPH 2014 — *Automatic Shader Simplification* (per-tier expression generation).
  - He et al. SIGGRAPH 2015 — *Automatic Shader LOD Generation*.
  - GPU Gems 2 Ch. 24 (NVIDIA) — *Using Lookup Tables to Accelerate Color Transformations* — the LUT-tier rationale. <https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color>
  - GPU Gems 3 Ch. 6 — *GPU wind animations for trees* — GLSL-codegen target shape.
- Levenberg, K. (1944) and Marquardt, D. (1963) — primary LM algorithm sources.
- Babich, R. *Levenberg-Marquardt in plain C* — minimal-deps reference implementation that informed the `CurveFitter` solver (no Eigen / ALGLIB dependency). <https://gist.github.com/rbabich/3539146>

Internal cross-references:

- `CLAUDE.md` Rule 6 — "Use the Formula Workbench for numerical design" — this subsystem is the runtime that rule names.
- `CLAUDE.md` Rule 7 — CPU/GPU placement; this subsystem is the dual-impl example (CPU evaluator spec + C++/GLSL runtime + LUT alternative).
- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §32 (asset paths — VLUT files).
- `ARCHITECTURE.md` §7 (Formula Pipeline subsystem map).
- `ROADMAP.md` "Formula Pipeline (Cross-Cutting Infrastructure) — COMPLETE".
- `docs/research/formula_pipeline_design.md` — original FP-1…FP-6 design record.
- `docs/research/formula_workbench_self_learning_design.md` — symbolic-regression / self-learning roadmap that consumes this subsystem's API surface.
- `AUDIT.md` §H11 (codegen-injection hardening), §H12 (safe-math parity), `FIXPLAN.md` E3 / E4, AUDIT M11 (`safePow` regression).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | `FormulaQualityManager::getCategoryTier` returns the global tier when no override exists, which is confusing relative to `getEffectiveTier`. Rename or deprecate? | milnet01 | Phase 11 entry |
| 2 | GPU-side LUT path: should `LutLoader` own a `GL_TEXTURE_3D` handle and let renderer sample via hardware trilinear, or stay CPU-only and have the renderer manage the texture upload? | milnet01 | Phase 11 entry |
| 3 | VLUT format v2 — add per-axis interpolation mode (nearest / linear / cubic) and optional `int16` quantisation for memory savings? Currently float32 only. | milnet01 | Phase 11 entry |
| 4 | Vector formulas (`vec3`, `vec4`) are evaluated component-wise by callers. Should `ExpressionEvaluator` grow native vector support (with vector-typed `ExprNodeType`s) or stay scalar-only forever? | milnet01 | post-MIT release (Phase 12) |
| 5 | `Result<T, E>` / `std::expected` migration — `LutGenerateResult` / `FitResult` are local equivalents; replace with the engine-wide canonical type once it lands. | milnet01 | post-MIT release (Phase 12) |
| 6 | Performance budgets in §8 are placeholders. Need a one-shot Tracy capture + `FormulaBenchmark` run to fill in measured numbers. | milnet01 | Phase 11 audit (concrete: end of Phase 10.9) |
| 7 | Symbolic-regression import path (`tools/formula_workbench/pysr_parser.h`) — does it belong inside `engine/formula` so the engine can ingest fitted formulas without the Workbench? | milnet01 | triage (no scheduled phase) |

Each row also lives as a `// TODO(2026-04-28 milnet01)` per CODING_STANDARDS §20 in the relevant header / source as the questions get attacked.

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/formula` shipped through Phase 9 (FP-1…FP-6) plus Phase 9E node graph, Phase 10.5 sensitivity analysis, Phase 10.7 weighted LM, Phase 10.9 `safePow` parity (AUDIT M11). Formalised post-Phase 10.9 audit. |
