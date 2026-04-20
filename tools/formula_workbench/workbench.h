/// @file workbench.h
/// @brief FormulaWorkbench — interactive tool for formula discovery and fitting.
///
/// Standalone ImGui application providing:
/// - Template browser (browse/search built-in physics formulas)
/// - Data editor (manual entry + CSV import via file dialog)
/// - Coefficient fitter (Levenberg-Marquardt with bounds)
/// - Curve visualizer (ImPlot) with quality tier comparison
/// - Convergence history visualization
/// - Validation panel (R², RMSE, train/test split, overfitting detection)
/// - Preset browser (apply/create environment/style presets)
/// - Batch fitting across formula categories
/// - Numeric stability warnings
/// - Export to FormulaLibrary JSON and C++/GLSL snippets
#pragma once

#include "async_driver.h"
#include "formula_node_editor_panel.h"
#include "markdown_render.h"
#include "formula/curve_fitter.h"
#include "formula/formula_library.h"
#include "formula/formula_preset.h"

#include <deque>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Version string for the FormulaWorkbench.
inline constexpr const char* WORKBENCH_VERSION = "1.16.0";

/// @brief Interactive formula workbench application.
class Workbench
{
public:
    Workbench();

    /// @brief Renders all workbench panels. Call each frame.
    void render();

    /// @brief One-shot setup that requires a live ImGui context (e.g.
    /// creating the imgui-node-editor context inside the Node Editor
    /// panel). Call exactly once, after `ImGui::CreateContext()`.
    void initializeGui();

    /// @brief Tear down GUI resources that must die before
    /// `ImGui::DestroyContext()`.
    void shutdownGui();

    /// @brief One row of the PySR symbolic-regression leaderboard.
    ///        Public so the free-function JSON parser in
    ///        ``workbench.cpp`` can construct instances without
    ///        having to be a friend of the class.
    struct PySREquation
    {
        int         complexity = 0;
        float       loss       = 0.0f;
        float       score      = 0.0f;
        std::string equation;
    };

private:
    // -- Panel renderers ------------------------------------------------------
    void renderMenuBar();
    void renderTemplateBrowser();
    void renderDataEditor();
    void renderFittingControls();
    void renderVisualizer();
    void renderValidation();
    void renderPresetBrowser();
    void renderSuggestionsPanel();   ///< §3.6 GUI — LLM-ranked formula shortlist.
    void renderPySRPanel();          ///< §3.5 GUI — PySR symbolic regression discovery.
    void renderNodeEditor();         ///< Phase 9E — visual formula composition panel.

    // -- Actions --------------------------------------------------------------
    void selectFormula(const std::string& name);
    void runFit();
    void runValidation();
    void importCsv(const std::string& path);
    void generateSyntheticData();
    void exportFormula(const std::string& path);
    void openFileDialog();          ///< Native file dialog for CSV import
    void batchFitCategory();        ///< Fit all formulas in the current category

    // -- Formula library & presets --------------------------------------------
    FormulaLibrary m_library;
    FormulaPresetLibrary m_presetLibrary;

    // -- Selection state ------------------------------------------------------
    std::string m_selectedFormulaName;
    const FormulaDefinition* m_selectedFormula = nullptr;
    std::string m_categoryFilter = "All";
    char m_searchBuf[128] = "";

    // -- Data -----------------------------------------------------------------
    std::vector<DataPoint> m_dataPoints;
    static constexpr int MAX_VARIABLES = 8;

    // -- Coefficients (editable initial values) -------------------------------
    std::map<std::string, float> m_coefficients;

    /// @brief §3.2 seed-from-history tracking.
    ///
    /// When selectFormula() or reseedFromHistoryForCurrentData()
    /// finds a prior exported fit in .fit_history.json for the
    /// selected formula, it seeds m_coefficients from that fit
    /// instead of the library's default initial guess. This flag
    /// records that the seeding happened so the UI can surface a
    /// small "seeded from history" badge — the user should always
    /// know when the tool is using remembered values vs. library
    /// defaults, because it changes the initial starting point of
    /// Levenberg-Marquardt and can dramatically change convergence
    /// behaviour.
    ///
    /// W6 (1.15.0): when data has been loaded,
    /// m_seededSimilarity holds the ``FitHistory::similarity`` score
    /// of the match in [0, 1]; the badge displays it. At
    /// selectFormula time (no data yet) the similarity is set to
    /// 0.0 and the badge omits it.
    bool        m_seededFromHistory = false;
    std::string m_seededFromTimestamp;   ///< ISO-8601, empty when not seeded.
    float       m_seededSimilarity = 0.0f;

    /// @brief W6 — re-seed m_coefficients from .fit_history.json
    ///        using the currently-loaded dataset's meta-features.
    ///
    /// Invoked from ``importCsv`` and ``generateSyntheticData`` once
    /// m_dataPoints has been populated. Picks the exported fit whose
    /// ``data_meta`` is most similar to the current dataset; falls
    /// back to library defaults when no match clears
    /// ``FitHistory::DEFAULT_SEED_SIMILARITY_THRESHOLD``. No-op when
    /// no formula is selected.
    void reseedFromHistoryForCurrentData();

    // -- Coefficient bounds (improvement #4) ----------------------------------
    struct CoeffBound
    {
        float lower = -1e6f;
        float upper =  1e6f;
        bool  enabled = false;
    };
    std::map<std::string, CoeffBound> m_coeffBounds;

    // -- Fit state ------------------------------------------------------------
    FitConfig m_fitConfig;
    FitResult m_fitResult;
    bool m_hasFitResult = false;

    // -- Convergence history (improvement #5) ---------------------------------
    struct ConvergencePoint
    {
        int iteration;
        float residual;
    };
    std::vector<ConvergencePoint> m_convergenceHistory;

    // -- Validation -----------------------------------------------------------
    float m_trainRatio = 0.8f;
    FitResult m_trainResult;
    FitResult m_testResult;
    bool m_hasValidation = false;

    // -- Visualization cache --------------------------------------------------
    std::vector<float> m_curveX;
    std::vector<float> m_curveY;
    std::vector<float> m_dataX;
    std::vector<float> m_dataY;
    std::vector<float> m_residuals;

    // -- Statistical diagnostics -----------------------------------------------
    float m_adjustedRSquared = 0.0f;
    float m_aic = 0.0f;
    float m_bic = 0.0f;
    int m_paramCount = 0;
    bool m_showResidualPlot = true;

    std::string m_plotVariable;
    void rebuildVisualizationCache();

    // -- Quality tier comparison (improvement #3) -----------------------------
    std::vector<float> m_approxCurveY;
    bool m_showQualityComparison = false;

    // -- Multi-variable synthetic data (improvement #6) -----------------------
    int m_syntheticCount = 20;
    float m_syntheticNoise = 0.02f;
    bool m_multiVarSynthetic = false;   ///< Sweep all variables, not just the first

    // -- Undo/redo for coefficients (improvement #9) --------------------------
    struct CoeffSnapshot
    {
        std::map<std::string, float> values;
        std::string description;
    };
    std::deque<CoeffSnapshot> m_undoStack;
    std::deque<CoeffSnapshot> m_redoStack;
    static constexpr size_t MAX_UNDO = 50;
    void pushUndo(const std::string& desc);
    void undo();
    void redo();

    // -- Stability warnings (improvement #10) ---------------------------------
    std::vector<std::string> m_stabilityWarnings;
    void checkStability();

    // -- Preset state ---------------------------------------------------------
    std::string m_selectedPresetName;
    char m_presetSearchBuf[128] = "";

    // -- Export ----------------------------------------------------------------
    char m_exportPath[256] = "formula_export.json";
    std::string m_statusMessage;
    float m_statusTimer = 0.0f;

    /// @brief §3.2 / W4 — pin-this-fit toggle.
    ///
    /// When ``true`` (default), the fit recorded on the next export
    /// gets ``user_action: "exported"`` in ``.fit_history.json`` and
    /// becomes a seed source for future sessions. When ``false``
    /// (user opted out for this fit), the entry is still logged but
    /// with ``user_action: "discarded"`` — present in the history
    /// but ignored by ``lastExportedCoeffsFor``. Lets a user export
    /// an outlier or experimental fit without poisoning the seed
    /// well for the formula.
    bool m_rememberFitForSeeding = true;

    // -- §3.6 GUI — LLM-ranked formula shortlist -------------------------------
    //
    // On-demand panel that pipes the dataset + library metadata to
    // scripts/llm_rank.py and displays the ranked markdown shortlist
    // the LLM returns. W1 (1.10.0): run the driver on a worker
    // thread via ``AsyncDriverJob`` and poll from the render loop so
    // the UI stays responsive even when the LLM takes several
    // seconds.
    std::string     m_suggestionsOutput;  ///< Full markdown from the driver.
    std::string     m_suggestionsError;   ///< Short human-readable failure.
    AsyncDriverJob  m_suggestionsJob;     ///< Worker-thread wrapper around runDriverCaptured.

    /// @brief W3 (1.13.0) — rendered-markdown state.
    ///
    /// The panel defaults to rendered-markdown view; the checkbox
    /// toggles back to the raw multiline buffer for copy-paste. The
    /// block list is re-parsed lazily when the source buffer changes;
    /// we use the string size as a cheap "content fingerprint" so we
    /// don't re-parse every frame.
    bool                                 m_suggestionsShowRaw = false;
    std::vector<markdown::Block>         m_suggestionsBlocks;
    void runLlmSuggestions();

    // -- §3.5 GUI — PySR symbolic regression panel (W2, 1.11.0) ----------------
    //
    // Shells out to scripts/pysr_driver.py via the same AsyncDriverJob
    // substrate as the Suggestions panel, but with two additions:
    // (a) incremental streaming — PySR runs are long (30 s – minutes)
    // and the panel must show progress; (b) Cancel button backed by
    // ``AsyncDriverJob::cancel()`` (SIGTERM → SIGKILL grace).
    //
    // Once the run finishes, the JSON tail emitted by the driver is
    // parsed into m_pysrEquations and rendered as a sortable
    // leaderboard. "Import as library formula" is NOT wired yet — the
    // PySR expression-string parser is tracked as W2c.
    std::string               m_pysrStreamingOutput;  ///< Raw stdout accumulated so far.
    std::string               m_pysrError;
    std::vector<PySREquation> m_pysrEquations;        ///< Populated on Done from the JSON tail.
    AsyncDriverJob            m_pysrJob;
    int                       m_pysrNiterations = 40;
    int                       m_pysrMaxComplexity = 20;
    void runPySR();

    /// @brief W2c — parse a PySR equation string and register it as a
    ///        new ``FormulaDefinition`` in ``m_library`` under the
    ///        "imported" category. Variables referenced in the
    ///        expression become the formula's inputs. On parse failure
    ///        writes a human-readable message to ``m_pysrError`` and
    ///        returns false.
    bool importPySREquationAsLibrary(const PySREquation& eq);

    // -- Phase 9E — visual formula composition ---------------------------------
    //
    // Closes the "Formula Node Editor" roadmap items: drag-and-drop from
    // the PhysicsTemplates catalog, visual composition canvas over the
    // NodeGraph data model, and an ImPlot curve preview on the output
    // node. Initialized lazily in ``initializeGui`` so the node-editor
    // context is created with a live ImGui context, and torn down in
    // ``shutdownGui`` so ``ed::DestroyEditor`` runs before
    // ``ImGui::DestroyContext``.
    FormulaNodeEditorPanel m_nodeEditorPanel;
};

} // namespace Vestige
