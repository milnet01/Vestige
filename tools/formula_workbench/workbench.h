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

#include "formula/curve_fitter.h"
#include "formula/formula_library.h"
#include "formula/formula_preset.h"

#include <deque>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Version string for the FormulaWorkbench.
inline constexpr const char* WORKBENCH_VERSION = "1.7.0";

/// @brief Interactive formula workbench application.
class Workbench
{
public:
    Workbench();

    /// @brief Renders all workbench panels. Call each frame.
    void render();

private:
    // -- Panel renderers ------------------------------------------------------
    void renderMenuBar();
    void renderTemplateBrowser();
    void renderDataEditor();
    void renderFittingControls();
    void renderVisualizer();
    void renderValidation();
    void renderPresetBrowser();

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
    /// When selectFormula() finds a prior exported fit in
    /// .fit_history.json for the selected formula, it seeds
    /// m_coefficients from that fit instead of the library's default
    /// initial guess. This flag records that the seeding happened so
    /// the UI can surface a small "seeded from history" badge — the
    /// user should always know when the tool is using remembered
    /// values vs. library defaults, because it changes the initial
    /// starting point of Levenberg-Marquardt and can dramatically
    /// change convergence behaviour.
    bool        m_seededFromHistory = false;
    std::string m_seededFromTimestamp;   ///< ISO-8601, empty when not seeded.

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

};

} // namespace Vestige
