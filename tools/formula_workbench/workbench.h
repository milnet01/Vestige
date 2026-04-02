/// @file workbench.h
/// @brief FormulaWorkbench — interactive tool for formula discovery and fitting.
///
/// Standalone ImGui application providing:
/// - Template browser (browse/search built-in physics formulas)
/// - Data editor (manual entry + CSV import)
/// - Coefficient fitter (Levenberg-Marquardt)
/// - Curve visualizer (ImPlot)
/// - Validation panel (R², RMSE, train/test split)
/// - Preset browser (apply/create environment/style presets)
/// - Export to FormulaLibrary JSON
#pragma once

#include "formula/curve_fitter.h"
#include "formula/formula_library.h"
#include "formula/formula_preset.h"

#include <string>
#include <vector>

namespace Vestige
{

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

    // -- Fit state ------------------------------------------------------------
    FitConfig m_fitConfig;
    FitResult m_fitResult;
    bool m_hasFitResult = false;

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
    std::string m_plotVariable;
    void rebuildVisualizationCache();

    // -- Preset state ---------------------------------------------------------
    std::string m_selectedPresetName;
    char m_presetSearchBuf[128] = "";

    // -- Export ----------------------------------------------------------------
    char m_exportPath[256] = "formula_export.json";
    std::string m_statusMessage;
    float m_statusTimer = 0.0f;

    // -- First frame flag -----------------------------------------------------
    bool m_firstFrame = true;
};

} // namespace Vestige
