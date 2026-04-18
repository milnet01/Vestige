/// @file workbench.cpp
/// @brief FormulaWorkbench implementation.
#include "workbench.h"
#include "benchmark.h"
#include "fit_history.h"
#include "formula/codegen_cpp.h"
#include "formula/codegen_glsl.h"
#include "formula/expression_eval.h"

#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <unistd.h>

namespace Vestige
{

Workbench::Workbench()
{
    m_library.registerBuiltinTemplates();
    m_presetLibrary.registerBuiltinPresets();
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void Workbench::render()
{
    // Set up dockspace over entire viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("WorkbenchDockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("WorkbenchDock");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    renderMenuBar();
    ImGui::End();

    // Individual panels
    renderTemplateBrowser();
    renderDataEditor();
    renderFittingControls();
    renderVisualizer();
    renderValidation();
    renderPresetBrowser();
    renderSuggestionsPanel();
    renderPySRPanel();

    // Status bar
    if (m_statusTimer > 0.0f)
    {
        m_statusTimer -= ImGui::GetIO().DeltaTime;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 10.0f,
                   viewport->WorkPos.y + viewport->WorkSize.y - 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - 20.0f, 0.0f));
        ImGui::Begin("##StatusBar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(m_statusMessage.c_str());
        ImGui::End();
    }

}

// ---------------------------------------------------------------------------
// Menu bar  (Improvement #2: file dialog, #8: batch fit)
// ---------------------------------------------------------------------------

void Workbench::renderMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Import CSV..."))
                openFileDialog();
            if (ImGui::MenuItem("Export Formula..."))
                exportFormula(m_exportPath);
            ImGui::Separator();
            if (ImGui::MenuItem("Batch Fit Category"))
                batchFitCategory();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                // Signal window close — handled by main loop checking glfwWindowShouldClose
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undoStack.empty()))
                undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redoStack.empty()))
                redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About"))
            {
                m_statusMessage = std::string("Vestige FormulaWorkbench v")
                    + WORKBENCH_VERSION + " — FP-4 Formula Pipeline";
                m_statusTimer = 3.0f;
            }
            ImGui::EndMenu();
        }
        // Right-aligned version indicator
        {
            const char* versionText = WORKBENCH_VERSION;
            float textWidth = ImGui::CalcTextSize(versionText).x;
            float availWidth = ImGui::GetContentRegionAvail().x;
            if (availWidth > textWidth)
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 16.0f);
                ImGui::TextDisabled("v%s", versionText);
            }
        }
        ImGui::EndMenuBar();
    }
}

// ---------------------------------------------------------------------------
// Template browser
// ---------------------------------------------------------------------------

void Workbench::renderTemplateBrowser()
{
    ImGui::Begin("Template Browser");

    // Category filter
    auto categories = m_library.getCategories();
    if (ImGui::BeginCombo("Category", m_categoryFilter.c_str()))
    {
        if (ImGui::Selectable("All", m_categoryFilter == "All"))
            m_categoryFilter = "All";
        for (const auto& cat : categories)
        {
            if (ImGui::Selectable(cat.c_str(), m_categoryFilter == cat))
                m_categoryFilter = cat;
        }
        ImGui::EndCombo();
    }

    // Search
    ImGui::InputText("Search", m_searchBuf, sizeof(m_searchBuf));
    std::string search(m_searchBuf);

    ImGui::Separator();

    // Formula list
    auto allFormulas = (m_categoryFilter == "All")
        ? m_library.getAll()
        : m_library.findByCategory(m_categoryFilter);

    for (const auto* formula : allFormulas)
    {
        // Filter by search
        if (!search.empty())
        {
            std::string lower = formula->name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::string searchLower = search;
            std::transform(searchLower.begin(), searchLower.end(),
                           searchLower.begin(), ::tolower);
            if (lower.find(searchLower) == std::string::npos)
                continue;
        }

        bool selected = (m_selectedFormulaName == formula->name);
        if (ImGui::Selectable(formula->name.c_str(), selected))
            selectFormula(formula->name);
    }

    // Details of selected formula
    if (m_selectedFormula)
    {
        ImGui::Separator();
        ImGui::TextWrapped("Category: %s", m_selectedFormula->category.c_str());
        ImGui::TextWrapped("Description: %s", m_selectedFormula->description.c_str());
        ImGui::Text("Inputs: %zu", m_selectedFormula->inputs.size());
        for (const auto& input : m_selectedFormula->inputs)
        {
            ImGui::BulletText("%s (%s) [%s] default=%.3f",
                              input.name.c_str(),
                              formulaValueTypeToString(input.type),
                              input.unit.c_str(),
                              static_cast<double>(input.defaultValue));
        }
        ImGui::Text("Coefficients: %zu", m_selectedFormula->coefficients.size());
        for (const auto& [name, val] : m_selectedFormula->coefficients)
        {
            ImGui::BulletText("%s = %.6f", name.c_str(), static_cast<double>(val));
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Data editor  (Improvement #6: multi-variable synthetic controls)
// ---------------------------------------------------------------------------

void Workbench::renderDataEditor()
{
    ImGui::Begin("Data Editor");

    if (!m_selectedFormula)
    {
        ImGui::TextWrapped("Select a formula template from the Template Browser to begin.");
        ImGui::End();
        return;
    }

    // CSV import
    static char csvPath[256] = "";
    ImGui::InputText("CSV Path", csvPath, sizeof(csvPath));
    ImGui::SameLine();
    if (ImGui::Button("Import"))
        importCsv(csvPath);
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
        openFileDialog();

    ImGui::SameLine();
    if (ImGui::Button("Generate Synthetic"))
        generateSyntheticData();

    // Synthetic data controls (Improvement #6)
    if (ImGui::CollapsingHeader("Synthetic Data Options"))
    {
        ImGui::InputInt("Sample Count", &m_syntheticCount);
        m_syntheticCount = std::max(2, std::min(m_syntheticCount, 10000));
        ImGui::InputFloat("Noise Level", &m_syntheticNoise, 0.001f, 0.01f, "%.4f");
        m_syntheticNoise = std::max(0.0f, std::min(m_syntheticNoise, 1.0f));
        ImGui::Checkbox("Multi-Variable Sweep", &m_multiVarSynthetic);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "When enabled, sweeps ALL scalar variables simultaneously,\n"
                "generating a grid of data points instead of sweeping only\n"
                "the first variable.");
        }
    }

    ImGui::Text("Data points: %zu", m_dataPoints.size());

    // Determine columns from formula inputs
    const auto& inputs = m_selectedFormula->inputs;
    std::vector<std::string> scalarInputs;
    for (const auto& inp : inputs)
    {
        if (inp.type == FormulaValueType::FLOAT)
            scalarInputs.push_back(inp.name);
    }

    // Add/remove row buttons
    if (ImGui::Button("+ Add Row"))
    {
        DataPoint dp;
        for (const auto& inp : inputs)
            dp.variables[inp.name] = inp.defaultValue;
        dp.observed = 0.0f;
        m_dataPoints.push_back(dp);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All") && !m_dataPoints.empty())
    {
        m_dataPoints.clear();
        m_hasFitResult = false;
        m_hasValidation = false;
    }

    // Data table
    size_t numCols = scalarInputs.size() + 1;  // inputs + observed
    if (numCols > 1 && ImGui::BeginTable("DataTable",
            static_cast<int>(numCols + 1),
            ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY,
            ImVec2(0, 300)))
    {
        // Header row
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        for (const auto& name : scalarInputs)
            ImGui::TableSetupColumn(name.c_str());
        ImGui::TableSetupColumn("Observed");
        ImGui::TableHeadersRow();

        // Data rows
        int toRemove = -1;
        for (size_t i = 0; i < m_dataPoints.size(); ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // Row number with remove button
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                toRemove = static_cast<int>(i);
            ImGui::SameLine();
            ImGui::Text("%zu", i + 1);

            // Variable columns
            for (const auto& name : scalarInputs)
            {
                ImGui::TableNextColumn();
                float val = m_dataPoints[i].variables[name];
                ImGui::SetNextItemWidth(-1);
                std::string label = "##" + name + std::to_string(i);
                if (ImGui::InputFloat(label.c_str(), &val, 0.0f, 0.0f, "%.4f"))
                    m_dataPoints[i].variables[name] = val;
            }

            // Observed column
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            std::string obsLabel = "##obs" + std::to_string(i);
            ImGui::InputFloat(obsLabel.c_str(), &m_dataPoints[i].observed,
                              0.0f, 0.0f, "%.4f");

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (toRemove >= 0)
            m_dataPoints.erase(m_dataPoints.begin() + toRemove);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Fitting controls  (Improvements #4, #5, #9, #10)
// ---------------------------------------------------------------------------

void Workbench::renderFittingControls()
{
    ImGui::Begin("Fitting Controls");

    if (!m_selectedFormula)
    {
        ImGui::TextWrapped("Select a formula first.");
        ImGui::End();
        return;
    }

    // Undo/Redo buttons (Improvement #9)
    {
        bool canUndo = !m_undoStack.empty();
        bool canRedo = !m_redoStack.empty();
        if (!canUndo)
            ImGui::BeginDisabled();
        if (ImGui::Button("Undo"))
            undo();
        if (!canUndo)
            ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canRedo)
            ImGui::BeginDisabled();
        if (ImGui::Button("Redo"))
            redo();
        if (!canRedo)
            ImGui::EndDisabled();

        if (!m_undoStack.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", m_undoStack.back().description.c_str());
        }
    }

    ImGui::Separator();

    // Editable coefficient initial values with bounds (Improvement #4)
    ImGui::Text("Initial Coefficients:");

    // §3.2 — "seeded from history" badge. When selectFormula()
    // pulled initial values out of .fit_history.json instead of the
    // library defaults, surface that here: the user needs to know
    // why their coefficients look different from a fresh template.
    // Silent seeding would make convergence behaviour feel
    // non-deterministic.
    if (m_seededFromHistory)
    {
        ImGui::SameLine();
        ImVec4 seedBadgeColor(0.35f, 0.65f, 1.0f, 1.0f);  // soft blue
        ImGui::PushStyleColor(ImGuiCol_Text, seedBadgeColor);
        if (!m_seededFromTimestamp.empty())
        {
            ImGui::Text("(seeded from fit @ %s)",
                        m_seededFromTimestamp.c_str());
        }
        else
        {
            ImGui::Text("(seeded from prior fit)");
        }
        ImGui::PopStyleColor();
    }
    for (auto& [name, val] : m_coefficients)
    {
        ImGui::PushID(name.c_str());

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.6f"))
        {
            // Coefficient edited manually -- no undo push here;
            // undo is pushed on fit/apply actions, not per-keystroke.
        }
        ImGui::SameLine();
        ImGui::Text("%s", name.c_str());

        // Coefficient bound controls (Improvement #4)
        CoeffBound& bound = m_coeffBounds[name];
        ImGui::SameLine();
        ImGui::Checkbox("Bound", &bound.enabled);
        if (bound.enabled)
        {
            ImGui::Indent(20.0f);
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("Min", &bound.lower, 0.0f, 0.0f, "%.4f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("Max", &bound.upper, 0.0f, 0.0f, "%.4f");
            if (bound.lower > bound.upper)
                std::swap(bound.lower, bound.upper);
            ImGui::Unindent(20.0f);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    // Config
    if (ImGui::CollapsingHeader("Algorithm Config"))
    {
        ImGui::InputInt("Max Iterations", &m_fitConfig.maxIterations);
        ImGui::InputFloat("Convergence Threshold", &m_fitConfig.convergenceThreshold,
                          0.0f, 0.0f, "%.1e");
        ImGui::InputFloat("Initial Lambda", &m_fitConfig.initialLambda,
                          0.0f, 0.0f, "%.1e");
    }

    ImGui::Separator();

    // Fit button
    bool canFit = !m_dataPoints.empty() && !m_coefficients.empty();
    if (!canFit)
        ImGui::BeginDisabled();

    if (ImGui::Button("Fit Coefficients", ImVec2(200, 40)))
        runFit();

    if (!canFit)
        ImGui::EndDisabled();

    // Results
    if (m_hasFitResult)
    {
        ImGui::Separator();
        ImGui::Text("Status: %s", m_fitResult.statusMessage.c_str());

        if (m_fitResult.converged)
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "CONVERGED");
        else
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "NOT CONVERGED");

        ImGui::Text("Iterations: %d / %d", m_fitResult.iterations,
                     m_fitConfig.maxIterations);

        // Progress bar (Improvement #5)
        float progress = (m_fitConfig.maxIterations > 0)
            ? static_cast<float>(m_fitResult.iterations) /
              static_cast<float>(m_fitConfig.maxIterations)
            : 1.0f;
        progress = std::min(progress, 1.0f);
        char progressLabel[64];
        std::snprintf(progressLabel, sizeof(progressLabel), "%d / %d iterations",
                      m_fitResult.iterations, m_fitConfig.maxIterations);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), progressLabel);

        ImGui::Text("R-squared:  %.6f", static_cast<double>(m_fitResult.rSquared));
        ImGui::Text("RMSE:       %.6f", static_cast<double>(m_fitResult.rmse));
        ImGui::Text("Max Error:  %.6f", static_cast<double>(m_fitResult.maxError));

        // Convergence history chart (Improvement #5)
        if (!m_convergenceHistory.empty())
        {
            ImGui::Separator();
            ImGui::Text("Convergence History:");

            // Prepare arrays for ImPlot
            std::vector<float> histIter;
            std::vector<float> histResid;
            histIter.reserve(m_convergenceHistory.size());
            histResid.reserve(m_convergenceHistory.size());
            for (const auto& cp : m_convergenceHistory)
            {
                histIter.push_back(static_cast<float>(cp.iteration));
                histResid.push_back(cp.residual);
            }

            if (ImPlot::BeginPlot("##Convergence", ImVec2(-1, 150)))
            {
                ImPlot::SetupAxes("Max Iterations", "R-squared");
                ImPlot::PlotLine("R2", histIter.data(), histResid.data(),
                                 static_cast<int>(histIter.size()));
                ImPlot::EndPlot();
            }
        }

        ImGui::Separator();
        ImGui::Text("Fitted Coefficients:");
        for (const auto& [name, val] : m_fitResult.coefficients)
        {
            ImGui::BulletText("%s = %.6f", name.c_str(), static_cast<double>(val));
        }

        // Apply fitted values as new initial values
        if (ImGui::Button("Apply to Initial Values"))
        {
            pushUndo("Apply fitted values");
            m_coefficients = m_fitResult.coefficients;
            m_statusMessage = "Fitted values applied as initial values.";
            m_statusTimer = 2.0f;
        }
    }

    // Stability warnings (Improvement #10)
    if (!m_stabilityWarnings.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Stability Warnings:");
        for (const auto& warning : m_stabilityWarnings)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "  - %s",
                               warning.c_str());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Visualizer (ImPlot)  (Improvement #3: quality tier comparison)
// ---------------------------------------------------------------------------

void Workbench::renderVisualizer()
{
    ImGui::Begin("Curve Visualizer");

    if (!m_selectedFormula || m_dataPoints.empty())
    {
        ImGui::TextWrapped(
            "Select a formula and add data points to see the visualization.");
        ImGui::End();
        return;
    }

    // Choose which variable to plot on X axis
    const auto& inputs = m_selectedFormula->inputs;
    std::vector<std::string> scalarInputs;
    for (const auto& inp : inputs)
    {
        if (inp.type == FormulaValueType::FLOAT)
            scalarInputs.push_back(inp.name);
    }

    if (!scalarInputs.empty())
    {
        if (m_plotVariable.empty())
            m_plotVariable = scalarInputs[0];

        if (ImGui::BeginCombo("X Axis Variable", m_plotVariable.c_str()))
        {
            for (const auto& name : scalarInputs)
            {
                if (ImGui::Selectable(name.c_str(), m_plotVariable == name))
                {
                    m_plotVariable = name;
                    rebuildVisualizationCache();
                }
            }
            ImGui::EndCombo();
        }
    }

    // Quality comparison checkbox (Improvement #3)
    if (ImGui::Checkbox("Show Quality Comparison", &m_showQualityComparison))
        rebuildVisualizationCache();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Overlay the APPROXIMATE tier curve alongside the FULL tier curve\n"
            "to compare quality tiers visually.");
    }

    // Build data arrays for plotting
    if (m_dataX.empty() || m_dataX.size() != m_dataPoints.size())
        rebuildVisualizationCache();

    // Main plot
    if (ImPlot::BeginPlot("Curve Fit", ImVec2(-1, 300)))
    {
        ImPlot::SetupAxes(m_plotVariable.c_str(), "Value");

        // Scatter plot of data points
        if (!m_dataX.empty())
        {
            ImPlotSpec scatterSpec;
            scatterSpec.Marker = ImPlotMarker_Circle;
            scatterSpec.MarkerSize = 5;
            ImPlot::PlotScatter("Data", m_dataX.data(), m_dataY.data(),
                                static_cast<int>(m_dataX.size()), scatterSpec);
        }

        // Fitted curve (only if we have a fit result)
        if (m_hasFitResult && !m_curveX.empty())
        {
            ImPlot::PlotLine("Fitted (Full)", m_curveX.data(), m_curveY.data(),
                             static_cast<int>(m_curveX.size()));

            // Approximate tier overlay (Improvement #3)
            // ImPlot auto-assigns a distinct color to each named series
            if (m_showQualityComparison && !m_approxCurveY.empty() &&
                m_approxCurveY.size() == m_curveX.size())
            {
                ImPlot::PlotLine("Fitted (Approx)", m_curveX.data(),
                                 m_approxCurveY.data(),
                                 static_cast<int>(m_curveX.size()));
            }
        }

        ImPlot::EndPlot();
    }

    // Residual plot
    if (m_showResidualPlot && m_hasFitResult && !m_residuals.empty())
    {
        if (ImPlot::BeginPlot("Residuals", ImVec2(-1, 200)))
        {
            ImPlot::SetupAxes(m_plotVariable.c_str(), "Residual");

            // Zero line
            float xRange[2] = {m_dataX.front(), m_dataX.back()};
            float yZero[2] = {0.0f, 0.0f};
            ImPlot::PlotLine("##zero", xRange, yZero, 2);

            ImPlotSpec residSpec;
            residSpec.Marker = ImPlotMarker_Diamond;
            residSpec.MarkerSize = 4;
            ImPlot::PlotScatter("Residuals", m_dataX.data(), m_residuals.data(),
                                static_cast<int>(m_dataX.size()), residSpec);

            ImPlot::EndPlot();
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Validation panel  (Improvement #11: export to C++/GLSL)
// ---------------------------------------------------------------------------

void Workbench::renderValidation()
{
    ImGui::Begin("Validation");

    if (!m_selectedFormula)
    {
        ImGui::TextWrapped("Select a formula first.");
        ImGui::End();
        return;
    }

    ImGui::SliderFloat("Train/Test Split", &m_trainRatio, 0.5f, 0.95f, "%.0f%% train",
                       ImGuiSliderFlags_AlwaysClamp);

    bool canValidate = m_dataPoints.size() >= 4 && !m_coefficients.empty();
    if (!canValidate)
        ImGui::BeginDisabled();

    if (ImGui::Button("Run Validation"))
        runValidation();

    if (!canValidate)
        ImGui::EndDisabled();

    if (m_hasValidation)
    {
        ImGui::Separator();

        ImGui::Columns(2, "ValidationColumns");
        ImGui::Text("Training Set");
        ImGui::Text("R-squared: %.4f", static_cast<double>(m_trainResult.rSquared));
        ImGui::Text("RMSE: %.4f", static_cast<double>(m_trainResult.rmse));
        ImGui::Text("Max Error: %.4f", static_cast<double>(m_trainResult.maxError));
        ImGui::Text("Converged: %s", m_trainResult.converged ? "Yes" : "No");

        ImGui::NextColumn();
        ImGui::Text("Test Set");
        ImGui::Text("R-squared: %.4f", static_cast<double>(m_testResult.rSquared));
        ImGui::Text("RMSE: %.4f", static_cast<double>(m_testResult.rmse));
        ImGui::Text("Max Error: %.4f", static_cast<double>(m_testResult.maxError));

        ImGui::Columns(1);

        // Overfitting warning
        float rDiff = m_trainResult.rSquared - m_testResult.rSquared;
        if (rDiff > 0.1f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                               "Warning: possible overfitting (train R2 >> test R2)");
        }
    }

    // Statistical diagnostics
    if (m_hasFitResult)
    {
        ImGui::Separator();
        ImGui::Text("Adjusted R²: %.4f", static_cast<double>(m_adjustedRSquared));
        ImGui::Text("AIC: %.1f", static_cast<double>(m_aic));
        ImGui::Text("BIC: %.1f", static_cast<double>(m_bic));
        ImGui::Text("Parameters: %d", m_paramCount);
    }

    ImGui::Checkbox("Show Residual Plot", &m_showResidualPlot);

    ImGui::Separator();

    // Export section
    ImGui::Text("Export");
    ImGui::InputText("Output Path", m_exportPath, sizeof(m_exportPath));
    if (ImGui::Button("Export to JSON"))
        exportFormula(m_exportPath);

    // W4 — pin-this-fit toggle. Silently-remembered seeding made
    // outlier fits poison the history well; this lets the user
    // keep the export without the history side-effect.
    ImGui::SameLine();
    ImGui::Checkbox("Remember for future seeding##remember_fit",
                    &m_rememberFitForSeeding);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "On: the fit is appended to .fit_history.json with "
            "user_action=\"exported\" and seeds future Levenberg-\n"
            "Marquardt starts for this formula (§3.2).\n"
            "Off: the fit is still logged but with "
            "user_action=\"discarded\", so it won't be picked up as\n"
            "a seed next session. Useful for one-off experimental fits.");
    }

    // C++/GLSL export buttons (Improvement #11)
    if (m_selectedFormula)
    {
        ImGui::Separator();
        ImGui::Text("Copy Formula as Code:");

        if (ImGui::Button("Copy as C++"))
        {
            // Build a temporary formula definition with current coefficients
            // (fitted if available, otherwise initial values)
            std::map<std::string, float> coeffsToUse = m_hasFitResult
                ? m_fitResult.coefficients
                : m_coefficients;

            const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
            if (expr)
            {
                // Build inline expression with coefficients baked in
                std::string cppExpr = CodegenCpp::emitExpression(*expr, coeffsToUse);

                // Build a complete function snippet
                std::ostringstream out;
                out << "// " << m_selectedFormula->description << "\n";
                out << "// Coefficients:";
                for (const auto& [cname, cval] : coeffsToUse)
                    out << " " << cname << "=" << cval;
                out << "\n";

                std::string funcName = CodegenCpp::toCppFunctionName(m_selectedFormula->name);
                std::string retType = CodegenCpp::toCppType(m_selectedFormula->output.type);
                out << "inline " << retType << " " << funcName << "(";
                bool first = true;
                for (const auto& input : m_selectedFormula->inputs)
                {
                    if (!first) out << ", ";
                    out << CodegenCpp::toCppParamType(input.type) << " " << input.name;
                    first = false;
                }
                out << ")\n{\n    return " << cppExpr << ";\n}\n";

                ImGui::SetClipboardText(out.str().c_str());
                m_statusMessage = "C++ code copied to clipboard.";
                m_statusTimer = 2.0f;
            }
            else
            {
                m_statusMessage = "No expression available for C++ export.";
                m_statusTimer = 2.0f;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Copy as GLSL"))
        {
            std::map<std::string, float> coeffsToUse = m_hasFitResult
                ? m_fitResult.coefficients
                : m_coefficients;

            const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
            if (expr)
            {
                std::string glslExpr = CodegenGlsl::emitExpression(*expr, coeffsToUse);

                std::ostringstream out;
                out << "// " << m_selectedFormula->description << "\n";
                out << "// Coefficients:";
                for (const auto& [cname, cval] : coeffsToUse)
                    out << " " << cname << "=" << cval;
                out << "\n";

                std::string funcName = CodegenGlsl::toGlslFunctionName(m_selectedFormula->name);
                std::string retType = CodegenGlsl::toGlslType(m_selectedFormula->output.type);
                out << retType << " " << funcName << "(";
                bool first = true;
                for (const auto& input : m_selectedFormula->inputs)
                {
                    if (!first) out << ", ";
                    out << CodegenGlsl::toGlslType(input.type) << " " << input.name;
                    first = false;
                }
                out << ")\n{\n    return " << glslExpr << ";\n}\n";

                ImGui::SetClipboardText(out.str().c_str());
                m_statusMessage = "GLSL code copied to clipboard.";
                m_statusTimer = 2.0f;
            }
            else
            {
                m_statusMessage = "No expression available for GLSL export.";
                m_statusTimer = 2.0f;
            }
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Preset browser
// ---------------------------------------------------------------------------

void Workbench::renderPresetBrowser()
{
    ImGui::Begin("Preset Browser");

    ImGui::InputText("Search##preset", m_presetSearchBuf, sizeof(m_presetSearchBuf));
    std::string search(m_presetSearchBuf);

    ImGui::Separator();

    // Category tabs
    auto categories = m_presetLibrary.getCategories();
    static std::string selectedCategory = "All";

    if (ImGui::BeginTabBar("PresetCategories"))
    {
        if (ImGui::BeginTabItem("All"))
        {
            selectedCategory = "All";
            ImGui::EndTabItem();
        }
        for (const auto& cat : categories)
        {
            if (ImGui::BeginTabItem(cat.c_str()))
            {
                selectedCategory = cat;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    // Preset list
    auto presets = (selectedCategory == "All")
        ? m_presetLibrary.getAll()
        : m_presetLibrary.findByCategory(selectedCategory);

    for (const auto* preset : presets)
    {
        // Filter by search
        if (!search.empty())
        {
            std::string lower = preset->displayName;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::string searchLower = search;
            std::transform(searchLower.begin(), searchLower.end(),
                           searchLower.begin(), ::tolower);
            if (lower.find(searchLower) == std::string::npos)
                continue;
        }

        bool selected = (m_selectedPresetName == preset->name);
        if (ImGui::Selectable(preset->displayName.c_str(), selected))
            m_selectedPresetName = preset->name;
    }

    // Details of selected preset
    const FormulaPreset* selectedPreset = m_presetLibrary.findByName(m_selectedPresetName);
    if (selectedPreset)
    {
        ImGui::Separator();
        ImGui::TextWrapped("Category: %s", selectedPreset->category.c_str());
        ImGui::TextWrapped("%s", selectedPreset->description.c_str());

        ImGui::Text("Overrides (%zu formulas):", selectedPreset->overrides.size());
        for (const auto& ov : selectedPreset->overrides)
        {
            if (ImGui::TreeNode(ov.formulaName.c_str()))
            {
                if (!ov.description.empty())
                    ImGui::TextWrapped("%s", ov.description.c_str());
                for (const auto& [name, val] : ov.coefficients)
                {
                    ImGui::BulletText("%s = %.6f", name.c_str(),
                                     static_cast<double>(val));
                }
                ImGui::TreePop();
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Apply Preset to Library"))
        {
            pushUndo("Apply preset: " + selectedPreset->displayName);

            size_t applied = FormulaPresetLibrary::applyPreset(
                *selectedPreset, m_library);
            m_statusMessage = "Applied preset '" + selectedPreset->displayName +
                              "' (" + std::to_string(applied) + " formulas updated)";
            m_statusTimer = 3.0f;

            // Re-select current formula to refresh coefficients
            if (m_selectedFormula)
                selectFormula(m_selectedFormulaName);
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void Workbench::selectFormula(const std::string& name)
{
    m_selectedFormulaName = name;
    m_selectedFormula = m_library.findByName(name);

    if (m_selectedFormula)
    {
        // Copy coefficients as initial fit values
        m_coefficients = m_selectedFormula->coefficients;
        m_seededFromHistory = false;
        m_seededFromTimestamp.clear();

        // §3.2 — seed LM starting point from the most recent
        // exported fit for this formula, if one exists. LM is
        // sensitive to initial guesses; a starting point near the
        // previous converged minimum typically converges in far
        // fewer iterations than the library's static default. The
        // coefficient names must match between history and current
        // library definition — mismatches (library evolved since
        // the fit was recorded) are silently skipped, so stale
        // history degrades gracefully.
        {
            FitHistory history(".fit_history.json");
            if (history.load())
            {
                const auto prior = history.lastExportedCoeffsFor(name);
                if (!prior.empty())
                {
                    bool any_applied = false;
                    for (auto& [coeff_name, val] : m_coefficients)
                    {
                        auto it = prior.find(coeff_name);
                        if (it != prior.end())
                        {
                            val = it->second;
                            any_applied = true;
                        }
                    }
                    if (any_applied)
                    {
                        m_seededFromHistory = true;
                        // Pull the timestamp from the matching
                        // entry so the UI badge can name it.
                        const auto entries = history.forFormula(name);
                        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                        {
                            if (it->user_action == "exported")
                            {
                                m_seededFromTimestamp = it->timestamp;
                                break;
                            }
                        }
                    }
                }
            }
        }

        m_hasFitResult = false;
        m_hasValidation = false;
        m_dataPoints.clear();
        m_curveX.clear();
        m_curveY.clear();
        m_approxCurveY.clear();
        m_dataX.clear();
        m_dataY.clear();
        m_residuals.clear();
        m_convergenceHistory.clear();
        m_stabilityWarnings.clear();
        m_coeffBounds.clear();

        // Default plot variable
        for (const auto& inp : m_selectedFormula->inputs)
        {
            if (inp.type == FormulaValueType::FLOAT)
            {
                m_plotVariable = inp.name;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// runFit  (Improvements #4, #5, #10)
// ---------------------------------------------------------------------------

void Workbench::runFit()
{
    if (!m_selectedFormula || m_dataPoints.empty() || m_coefficients.empty())
        return;

    pushUndo("Before curve fit");

    // Convergence history: run fitter with exponentially increasing max iters
    // to observe how R-squared improves (Improvement #5)
    m_convergenceHistory.clear();

    std::vector<int> iterSteps;
    for (int step = 1; step <= m_fitConfig.maxIterations; step *= 2)
        iterSteps.push_back(step);
    // Always include the actual configured max
    if (iterSteps.empty() || iterSteps.back() != m_fitConfig.maxIterations)
        iterSteps.push_back(m_fitConfig.maxIterations);

    for (int maxIter : iterSteps)
    {
        FitConfig stepConfig = m_fitConfig;
        stepConfig.maxIterations = maxIter;

        FitResult stepResult = CurveFitter::fit(
            *m_selectedFormula, m_dataPoints, m_coefficients,
            QualityTier::FULL, stepConfig);

        ConvergencePoint cp;
        cp.iteration = maxIter;
        cp.residual = stepResult.rSquared;
        m_convergenceHistory.push_back(cp);
    }

    // Final fit with full configured iterations
    m_fitResult = CurveFitter::fit(*m_selectedFormula, m_dataPoints,
                                   m_coefficients, QualityTier::FULL,
                                   m_fitConfig);

    // Clamp coefficients to bounds if enabled (Improvement #4)
    for (auto& [name, val] : m_fitResult.coefficients)
    {
        auto boundIt = m_coeffBounds.find(name);
        if (boundIt != m_coeffBounds.end() && boundIt->second.enabled)
        {
            val = std::max(boundIt->second.lower, std::min(val, boundIt->second.upper));
        }
    }

    m_hasFitResult = true;

    m_statusMessage = m_fitResult.converged
        ? "Fit converged! R2=" + std::to_string(m_fitResult.rSquared)
        : "Fit did not converge: " + m_fitResult.statusMessage;
    m_statusTimer = 3.0f;

    // Check numeric stability (Improvement #10)
    checkStability();

    // Compute adjusted R², AIC, BIC
    {
        int n = static_cast<int>(m_dataPoints.size());
        m_paramCount = static_cast<int>(m_fitResult.coefficients.size());
        if (n > m_paramCount + 1)
        {
            m_adjustedRSquared = 1.0f - (1.0f - m_fitResult.rSquared)
                                 * static_cast<float>(n - 1) / static_cast<float>(n - m_paramCount - 1);

            // AIC = n * ln(SSE/n) + 2k
            double sse = static_cast<double>(m_fitResult.rmse) * m_fitResult.rmse * n;
            if (sse > 0.0)
            {
                m_aic = static_cast<float>(n * std::log(sse / n) + 2.0 * m_paramCount);
                m_bic = static_cast<float>(n * std::log(sse / n) + m_paramCount * std::log(static_cast<double>(n)));
            }
        }
    }

    rebuildVisualizationCache();
}

void Workbench::runValidation()
{
    if (!m_selectedFormula || m_dataPoints.size() < 4)
        return;

    // Shuffle and split
    std::vector<DataPoint> shuffled = m_dataPoints;
    std::mt19937 rng(42);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    size_t trainSize = static_cast<size_t>(
        static_cast<float>(shuffled.size()) * m_trainRatio);
    trainSize = std::max<size_t>(trainSize, 1);
    trainSize = std::min(trainSize, shuffled.size() - 1);

    std::vector<DataPoint> trainData(shuffled.begin(),
                                     shuffled.begin() + static_cast<long>(trainSize));
    std::vector<DataPoint> testData(shuffled.begin() + static_cast<long>(trainSize),
                                    shuffled.end());

    // Fit on training data
    m_trainResult = CurveFitter::fit(*m_selectedFormula, trainData,
                                     m_coefficients, QualityTier::FULL,
                                     m_fitConfig);

    // Evaluate on test data using fitted coefficients
    ExpressionEvaluator eval;
    const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
    if (!expr)
        return;

    std::unordered_map<std::string, float> coeffMap(
        m_trainResult.coefficients.begin(), m_trainResult.coefficients.end());

    double sumSqResid = 0.0;
    float maxErr = 0.0f;
    double sumObs = 0.0;
    for (const auto& dp : testData)
        sumObs += static_cast<double>(dp.observed);
    double meanObs = sumObs / static_cast<double>(testData.size());

    double ssTot = 0.0;
    for (const auto& dp : testData)
    {
        float predicted = eval.evaluate(*expr, dp.variables, coeffMap);
        double resid = static_cast<double>(predicted) - static_cast<double>(dp.observed);
        sumSqResid += resid * resid;
        maxErr = std::max(maxErr, std::abs(predicted - dp.observed));

        double diff = static_cast<double>(dp.observed) - meanObs;
        ssTot += diff * diff;
    }

    m_testResult.rmse = static_cast<float>(std::sqrt(sumSqResid / static_cast<double>(testData.size())));
    m_testResult.maxError = maxErr;
    m_testResult.rSquared = (ssTot > 1e-15) ? static_cast<float>(1.0 - sumSqResid / ssTot) : 1.0f;
    m_testResult.coefficients = m_trainResult.coefficients;

    m_hasValidation = true;
    m_statusMessage = "Validation complete. Train R2=" +
                      std::to_string(m_trainResult.rSquared) + " Test R2=" +
                      std::to_string(m_testResult.rSquared);
    m_statusTimer = 3.0f;
}

void Workbench::importCsv(const std::string& path)
{
    if (!m_selectedFormula)
    {
        m_statusMessage = "Select a formula before importing data.";
        m_statusTimer = 3.0f;
        return;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_statusMessage = "Cannot open CSV file: " + path;
        m_statusTimer = 3.0f;
        return;
    }

    // AUDIT.md §M11 / FIXPLAN: RFC 4180-aware line splitter. The prior
    // getline(ss, cell, ',') broke on any Excel-exported CSV with quoted
    // fields (e.g. `"1,234.56",5.0` would split into three cells). Now
    // `"a, b"` is a single cell and `""` inside quotes is an escaped quote.
    // Embedded newlines inside quoted fields are NOT supported — flagged
    // as out-of-scope here; the import will still produce a short row and
    // the row-length mismatch guard below will drop it.
    auto splitCsvLine = [](const std::string& line) -> std::vector<std::string>
    {
        std::vector<std::string> out;
        std::string cell;
        bool inQuotes = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];
            if (inQuotes)
            {
                if (c == '"')
                {
                    // RFC 4180: `""` inside a quoted field is a literal ".
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        cell.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    cell.push_back(c);
                }
            }
            else
            {
                if (c == '"' && cell.empty())
                {
                    inQuotes = true;
                }
                else if (c == ',')
                {
                    out.push_back(std::move(cell));
                    cell.clear();
                }
                else
                {
                    cell.push_back(c);
                }
            }
        }
        out.push_back(std::move(cell));
        return out;
    };

    auto trim = [](std::string& s)
    {
        auto firstNonWs = s.find_first_not_of(" \t\r\n");
        if (firstNonWs == std::string::npos) { s.clear(); return; }
        s.erase(0, firstNonWs);
        auto lastNonWs = s.find_last_not_of(" \t\r\n");
        if (lastNonWs != std::string::npos)
            s.erase(lastNonWs + 1);
    };

    // Read header line
    std::string headerLine;
    if (!std::getline(file, headerLine))
    {
        m_statusMessage = "CSV file is empty.";
        m_statusTimer = 3.0f;
        return;
    }

    // Parse headers (RFC 4180-aware)
    std::vector<std::string> headers = splitCsvLine(headerLine);
    for (auto& h : headers) trim(h);

    if (headers.size() < 2)
    {
        m_statusMessage = "CSV must have at least 2 columns.";
        m_statusTimer = 3.0f;
        return;
    }

    // Last column is observed value; rest are variables
    size_t imported = 0;
    int badCells = 0;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::vector<std::string> cells = splitCsvLine(line);
        std::vector<float> values;
        values.reserve(cells.size());
        for (auto& cell : cells)
        {
            trim(cell);
            try
            {
                values.push_back(std::stof(cell));
            }
            catch (const std::invalid_argument&)
            {
                values.push_back(0.0f);
                ++badCells;
            }
            catch (const std::out_of_range&)
            {
                values.push_back(0.0f);
                ++badCells;
            }
        }

        if (values.size() < headers.size())
            continue;

        DataPoint dp;
        for (size_t i = 0; i + 1 < headers.size(); ++i)
            dp.variables[headers[i]] = values[i];
        dp.observed = values[headers.size() - 1];
        m_dataPoints.push_back(dp);
        ++imported;
    }

    m_statusMessage = "Imported " + std::to_string(imported) + " data points from CSV.";
    if (badCells > 0)
        m_statusMessage = "Warning: " + std::to_string(badCells) + " cells could not be parsed (set to 0). " + m_statusMessage;
    m_statusTimer = 3.0f;

    rebuildVisualizationCache();
}

// ---------------------------------------------------------------------------
// openFileDialog  (Improvement #2)
// ---------------------------------------------------------------------------

void Workbench::openFileDialog()
{
    // Use kdialog on KDE, fall back to zenity
    FILE* pipe = popen(
        "kdialog --getopenfilename ~ 'CSV Files (*.csv)' 2>/dev/null || "
        "zenity --file-selection --title='Select CSV File' "
        "--file-filter='CSV files|*.csv' 2>/dev/null",
        "r");
    if (pipe)
    {
        // AUDIT.md §L2 / FIXPLAN: read the full pipe output rather than a
        // single 512-byte chunk. Linux path limit (PATH_MAX) is 4096 bytes,
        // and kdialog/zenity may return a whole line plus trailing
        // newline. Read everything; fgets loops until EOF.
        std::string path;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), pipe))
        {
            path.append(buf);
        }
        auto lastNonWs = path.find_last_not_of(" \t\r\n");
        if (lastNonWs != std::string::npos)
            path.erase(lastNonWs + 1);
        else
            path.clear();
        if (!path.empty())
            importCsv(path);
        pclose(pipe);
    }
    else
    {
        m_statusMessage = "Could not open file dialog. Use the CSV path field instead.";
        m_statusTimer = 3.0f;
    }
}

// ---------------------------------------------------------------------------
// generateSyntheticData  (Improvement #6: multi-variable sweep)
// ---------------------------------------------------------------------------

void Workbench::generateSyntheticData()
{
    if (!m_selectedFormula)
        return;

    const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
    if (!expr)
        return;

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffMap(
        m_coefficients.begin(), m_coefficients.end());

    // Collect scalar inputs
    std::vector<const FormulaInput*> scalarInputs;
    for (const auto& inp : m_selectedFormula->inputs)
    {
        if (inp.type == FormulaValueType::FLOAT)
            scalarInputs.push_back(&inp);
    }

    if (scalarInputs.empty())
    {
        m_statusMessage = "No scalar input to sweep.";
        m_statusTimer = 3.0f;
        return;
    }

    m_dataPoints.clear();
    std::mt19937 rng(123);
    std::normal_distribution<float> noise(0.0f, m_syntheticNoise);

    int count = std::max(2, m_syntheticCount);
    m_dataPoints.reserve(static_cast<size_t>(count));

    if (m_multiVarSynthetic && scalarInputs.size() > 1)
    {
        // Multi-variable grid sweep (Improvement #6)
        // Compute samples per dimension so total is approximately m_syntheticCount
        int numVars = static_cast<int>(scalarInputs.size());
        int samplesPerDim = std::max(2, static_cast<int>(
            std::round(std::pow(static_cast<double>(count), 1.0 / numVars))));

        // Compute ranges for each variable
        struct VarRange
        {
            std::string name;
            float rangeMin;
            float rangeMax;
        };
        std::vector<VarRange> ranges;
        for (const auto* inp : scalarInputs)
        {
            float sweepMin = inp->defaultValue * 0.1f;
            float sweepMax = inp->defaultValue * 3.0f;
            if (sweepMax <= sweepMin)
            {
                sweepMin = 0.0f;
                sweepMax = 10.0f;
            }
            ranges.push_back({inp->name, sweepMin, sweepMax});
        }

        // Generate grid using multi-dimensional index iteration
        int totalPoints = 1;
        for (int d = 0; d < numVars; ++d)
            totalPoints *= samplesPerDim;

        // Cap at a reasonable maximum to avoid massive allocations
        totalPoints = std::min(totalPoints, 100000);

        for (int idx = 0; idx < totalPoints; ++idx)
        {
            DataPoint dp;
            // Set all variables to defaults
            for (const auto& inp : m_selectedFormula->inputs)
                dp.variables[inp.name] = inp.defaultValue;

            // Decode linear index into per-dimension indices
            int remaining = idx;
            for (int d = 0; d < numVars; ++d)
            {
                int dimIdx = remaining % samplesPerDim;
                remaining /= samplesPerDim;

                float t = static_cast<float>(dimIdx) /
                          static_cast<float>(samplesPerDim - 1);
                float val = ranges[d].rangeMin + t * (ranges[d].rangeMax - ranges[d].rangeMin);
                dp.variables[ranges[d].name] = val;
            }

            float predicted = eval.evaluate(*expr, dp.variables, coeffMap);
            dp.observed = predicted * (1.0f + noise(rng));
            m_dataPoints.push_back(dp);
        }

        m_statusMessage = "Generated " + std::to_string(totalPoints) +
                          " multi-variable synthetic data points (" +
                          std::to_string(samplesPerDim) + " per dimension).";
    }
    else
    {
        // Single-variable sweep (original behavior)
        const FormulaInput* sweepInput = scalarInputs[0];
        float sweepMin = sweepInput->defaultValue * 0.1f;
        float sweepMax = sweepInput->defaultValue * 3.0f;
        if (sweepMax <= sweepMin)
        {
            sweepMin = 0.0f;
            sweepMax = 10.0f;
        }

        for (int i = 0; i < count; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(count - 1);
            float x = sweepMin + t * (sweepMax - sweepMin);

            DataPoint dp;
            // Set all variables to defaults first
            for (const auto& inp : m_selectedFormula->inputs)
                dp.variables[inp.name] = inp.defaultValue;
            dp.variables[sweepInput->name] = x;

            float val = eval.evaluate(*expr, dp.variables, coeffMap);
            dp.observed = val * (1.0f + noise(rng));
            m_dataPoints.push_back(dp);
        }

        m_statusMessage = "Generated " + std::to_string(count) + " synthetic data points.";
    }

    m_statusTimer = 3.0f;
    rebuildVisualizationCache();
}

void Workbench::exportFormula(const std::string& path)
{
    if (!m_selectedFormula)
    {
        m_statusMessage = "No formula selected.";
        m_statusTimer = 3.0f;
        return;
    }

    // Clone the formula with fitted coefficients
    FormulaDefinition exported = m_selectedFormula->clone();
    if (m_hasFitResult)
    {
        exported.coefficients.clear();
        for (const auto& [name, val] : m_fitResult.coefficients)
            exported.coefficients[name] = val;
        exported.accuracy = m_fitResult.rSquared;
        exported.source = "Fitted by FormulaWorkbench";
    }

    // Save as JSON
    nlohmann::json j = nlohmann::json::array();
    j.push_back(exported.toJson());

    std::ofstream file(path);
    if (!file.is_open())
    {
        m_statusMessage = "Cannot write to: " + path;
        m_statusTimer = 3.0f;
        return;
    }

    file << j.dump(2);
    m_statusMessage = "Exported formula to: " + path;
    m_statusTimer = 3.0f;

    // Phase 1 §3.1 of the self-learning loop — persist this fit to
    // .fit_history.json so future sessions can seed LM from it
    // (§3.2) and so the planned --self-benchmark rank-by-AIC flow
    // (§3.3) has data to work with. Only exported fits are recorded
    // — ephemeral in-session tweaking isn't worth polluting the
    // history with. Failures are logged to the status bar but don't
    // block the export.
    if (m_hasFitResult && !m_dataPoints.empty())
    {
        FitHistory history(".fit_history.json");
        history.load();  // OK if absent

        FitHistoryEntry entry;
        // ISO-8601 UTC timestamp, seconds resolution — matches the
        // audit tool's .audit_stats.json convention.
        {
            const auto now  = std::chrono::system_clock::now();
            const auto secs = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            gmtime_r(&secs, &tm);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
            entry.timestamp = buf;
        }
        entry.formula_name = m_selectedFormula->name;
        entry.data_hash    = FitHistory::hashDataset(m_dataPoints);
        entry.data_meta    = FitHistory::computeMeta(m_dataPoints);
        entry.coefficients = m_fitResult.coefficients;
        entry.r_squared    = m_fitResult.rSquared;
        entry.rmse         = m_fitResult.rmse;
        entry.aic          = m_aic;
        entry.bic          = m_bic;
        entry.iterations   = m_fitResult.iterations;
        entry.converged    = m_fitResult.converged;
        // W4 — only "exported" entries feed §3.2 seeding. A user
        // who unchecked "Remember for future seeding" gets the fit
        // logged but not used as a seed — the history remains
        // complete, just the selection query (lastExportedCoeffsFor)
        // skips it.
        entry.user_action  = m_rememberFitForSeeding ? "exported" : "discarded";

        history.record(entry);
        history.save();
    }
}

// ---------------------------------------------------------------------------
// batchFitCategory  (Improvement #8)
// ---------------------------------------------------------------------------

void Workbench::batchFitCategory()
{
    auto formulas = (m_categoryFilter == "All")
        ? m_library.getAll()
        : m_library.findByCategory(m_categoryFilter);

    if (formulas.empty())
    {
        m_statusMessage = "No formulas in category '" + m_categoryFilter + "'.";
        m_statusTimer = 3.0f;
        return;
    }

    struct BatchResult
    {
        std::string name;
        float rSquared;
        float aic;
        bool converged;
    };

    int totalFormulas = 0;
    int convergedCount = 0;
    float totalR2 = 0.0f;
    std::vector<std::string> failedNames;
    std::vector<BatchResult> batchResults;

    for (const auto* formula : formulas)
    {
        if (!formula || formula->coefficients.empty())
            continue;

        // Generate synthetic data for this formula
        const ExprNode* expr = formula->getExpression(QualityTier::FULL);
        if (!expr)
            continue;

        // Find a scalar variable to sweep
        const FormulaInput* sweepInput = nullptr;
        for (const auto& inp : formula->inputs)
        {
            if (inp.type == FormulaValueType::FLOAT)
            {
                sweepInput = &inp;
                break;
            }
        }
        if (!sweepInput)
            continue;

        float sweepMin = sweepInput->defaultValue * 0.1f;
        float sweepMax = sweepInput->defaultValue * 3.0f;
        if (sweepMax <= sweepMin)
        {
            sweepMin = 0.0f;
            sweepMax = 10.0f;
        }

        // Generate synthetic data
        ExpressionEvaluator eval;
        std::unordered_map<std::string, float> coeffMap(
            formula->coefficients.begin(), formula->coefficients.end());

        std::mt19937 rng(42);
        std::normal_distribution<float> noise(0.0f, 0.02f);

        constexpr int SYNTH_COUNT = 20;
        std::vector<DataPoint> synthData;
        for (int i = 0; i < SYNTH_COUNT; ++i)
        {
            float t = static_cast<float>(i) / 19.0f;
            float x = sweepMin + t * (sweepMax - sweepMin);

            DataPoint dp;
            for (const auto& inp : formula->inputs)
                dp.variables[inp.name] = inp.defaultValue;
            dp.variables[sweepInput->name] = x;

            float val = eval.evaluate(*expr, dp.variables, coeffMap);
            dp.observed = val * (1.0f + noise(rng));
            synthData.push_back(dp);
        }

        // Perturb initial coefficients slightly so fitting has work to do
        std::map<std::string, float> initialCoeffs = formula->coefficients;
        std::uniform_real_distribution<float> perturbDist(0.8f, 1.2f);
        for (auto& [cname, cval] : initialCoeffs)
            cval *= perturbDist(rng);

        // Run fit
        FitResult result = CurveFitter::fit(*formula, synthData, initialCoeffs,
                                            QualityTier::FULL, m_fitConfig);

        // Compute AIC for this formula
        float formulaAic = 0.0f;
        if (result.converged)
        {
            int n = SYNTH_COUNT;
            int k = static_cast<int>(result.coefficients.size());
            double sse = static_cast<double>(result.rmse) * result.rmse * n;
            if (sse > 0.0 && n > k + 1)
            {
                formulaAic = static_cast<float>(n * std::log(sse / n) + 2.0 * k);
            }
        }

        BatchResult br;
        br.name = formula->name;
        br.rSquared = result.rSquared;
        br.aic = formulaAic;
        br.converged = result.converged;
        batchResults.push_back(br);

        ++totalFormulas;
        if (result.converged)
        {
            ++convergedCount;
            totalR2 += result.rSquared;
        }
        else
        {
            failedNames.push_back(formula->name);
        }
    }

    // Sort converged results by AIC (ascending = better fit)
    std::sort(batchResults.begin(), batchResults.end(),
              [](const BatchResult& a, const BatchResult& b)
              {
                  // Converged results first, then sort by AIC ascending
                  if (a.converged != b.converged)
                      return a.converged;
                  return a.aic < b.aic;
              });

    // Build result message
    std::ostringstream msg;
    msg << "Batch fit: " << convergedCount << "/" << totalFormulas << " converged";
    if (convergedCount > 0)
    {
        msg << ", avg R2=" << std::fixed << std::setprecision(4)
            << (totalR2 / static_cast<float>(convergedCount));
    }
    if (!failedNames.empty())
    {
        msg << ". Failed: ";
        for (size_t i = 0; i < failedNames.size() && i < 5; ++i)
        {
            if (i > 0) msg << ", ";
            msg << failedNames[i];
        }
        if (failedNames.size() > 5)
            msg << " (+" << (failedNames.size() - 5) << " more)";
    }

    // Append AIC ranking for converged formulas
    if (!batchResults.empty())
    {
        msg << "\nAIC Ranking: ";
        int rank = 0;
        for (const auto& br : batchResults)
        {
            if (!br.converged)
                continue;
            if (rank > 0)
                msg << ", ";
            msg << br.name << " (AIC=" << std::fixed << std::setprecision(1) << br.aic << ")";
            ++rank;
            if (rank >= 5)
            {
                if (convergedCount > 5)
                    msg << " (+" << (convergedCount - 5) << " more)";
                break;
            }
        }
    }

    m_statusMessage = msg.str();
    m_statusTimer = 5.0f;
}

// ---------------------------------------------------------------------------
// Undo/Redo  (Improvement #9)
// ---------------------------------------------------------------------------

void Workbench::pushUndo(const std::string& desc)
{
    CoeffSnapshot snap;
    snap.values = m_coefficients;
    snap.description = desc;

    m_undoStack.push_back(snap);
    while (m_undoStack.size() > MAX_UNDO)
        m_undoStack.pop_front();

    // Clear redo stack on new action
    m_redoStack.clear();
}

void Workbench::undo()
{
    if (m_undoStack.empty())
        return;

    // Save current state to redo stack
    CoeffSnapshot current;
    current.values = m_coefficients;
    current.description = "redo";
    m_redoStack.push_back(current);

    // Restore from undo stack
    const CoeffSnapshot& snap = m_undoStack.back();
    m_coefficients = snap.values;
    m_undoStack.pop_back();

    m_statusMessage = "Undo applied.";
    m_statusTimer = 1.5f;
}

void Workbench::redo()
{
    if (m_redoStack.empty())
        return;

    // Save current state to undo stack
    CoeffSnapshot current;
    current.values = m_coefficients;
    current.description = "undo";
    m_undoStack.push_back(current);

    // Restore from redo stack
    const CoeffSnapshot& snap = m_redoStack.back();
    m_coefficients = snap.values;
    m_redoStack.pop_back();

    m_statusMessage = "Redo applied.";
    m_statusTimer = 1.5f;
}

// ---------------------------------------------------------------------------
// checkStability  (Improvement #10)
// ---------------------------------------------------------------------------

void Workbench::checkStability()
{
    m_stabilityWarnings.clear();

    if (!m_selectedFormula || !m_hasFitResult)
        return;

    const auto& coeffs = m_fitResult.coefficients;

    // Walk through the expression tree looking for risky patterns.
    // We approximate by checking coefficient values against known risky thresholds.
    for (const auto& [name, val] : coeffs)
    {
        float absVal = std::abs(val);

        // Check for very large values that could overflow in exp()
        if (absVal > 80.0f)
        {
            m_stabilityWarnings.push_back(
                "Coefficient '" + name + "' = " + std::to_string(val) +
                " is very large. May cause overflow in exp() operations.");
        }

        // Check for near-zero values that could appear in denominators
        if (absVal > 0.0f && absVal < 1e-6f)
        {
            m_stabilityWarnings.push_back(
                "Coefficient '" + name + "' = " + std::to_string(val) +
                " is near zero. May cause instability if used in a denominator.");
        }

        // Check for NaN or Inf
        if (std::isnan(val))
        {
            m_stabilityWarnings.push_back(
                "Coefficient '" + name + "' is NaN! Fitting may have diverged.");
        }
        if (std::isinf(val))
        {
            m_stabilityWarnings.push_back(
                "Coefficient '" + name + "' is Inf! Fitting diverged.");
        }
    }

    // Walk the expression tree for structural risks
    const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
    if (!expr)
        return;

    // Use a simple recursive lambda to detect risky patterns
    struct RiskChecker
    {
        const std::map<std::string, float>& coefficients;
        std::vector<std::string>& warnings;

        void check(const ExprNode& node)
        {
            if (node.type == ExprNodeType::BINARY_OP)
            {
                // Division where denominator is a coefficient near zero
                if (node.op == "/" && node.children.size() >= 2)
                {
                    const ExprNode& denom = *node.children[1];
                    if (denom.type == ExprNodeType::VARIABLE)
                    {
                        auto it = coefficients.find(denom.name);
                        if (it != coefficients.end() && std::abs(it->second) < 1e-4f)
                        {
                            warnings.push_back(
                                "Division by coefficient '" + denom.name +
                                "' which is near zero (" +
                                std::to_string(it->second) + ").");
                        }
                    }
                }

                // pow with large exponents
                if (node.op == "pow" && node.children.size() >= 2)
                {
                    const ExprNode& exponent = *node.children[1];
                    if (exponent.type == ExprNodeType::VARIABLE)
                    {
                        auto it = coefficients.find(exponent.name);
                        if (it != coefficients.end() && std::abs(it->second) > 20.0f)
                        {
                            warnings.push_back(
                                "Power with large exponent coefficient '" +
                                exponent.name + "' = " +
                                std::to_string(it->second) +
                                ". May cause extreme values.");
                        }
                    }
                }
            }

            if (node.type == ExprNodeType::UNARY_OP)
            {
                // exp of potentially large values
                if (node.op == "exp" && !node.children.empty())
                {
                    const ExprNode& arg = *node.children[0];
                    if (arg.type == ExprNodeType::VARIABLE)
                    {
                        auto it = coefficients.find(arg.name);
                        if (it != coefficients.end() && std::abs(it->second) > 20.0f)
                        {
                            warnings.push_back(
                                "exp() of large coefficient '" + arg.name +
                                "' = " + std::to_string(it->second) +
                                ". Risk of overflow.");
                        }
                    }
                }

                // sqrt of negative values
                if (node.op == "sqrt" && !node.children.empty())
                {
                    const ExprNode& arg = *node.children[0];
                    if (arg.type == ExprNodeType::VARIABLE)
                    {
                        auto it = coefficients.find(arg.name);
                        if (it != coefficients.end() && it->second < 0.0f)
                        {
                            warnings.push_back(
                                "sqrt() of negative coefficient '" + arg.name +
                                "' = " + std::to_string(it->second) + ".");
                        }
                    }
                }
            }

            // Recurse into children
            for (const auto& child : node.children)
            {
                if (child)
                    check(*child);
            }
        }
    };

    RiskChecker checker{coeffs, m_stabilityWarnings};
    checker.check(*expr);
}

// ---------------------------------------------------------------------------
// Visualization cache  (Improvement #3: quality tier comparison)
// ---------------------------------------------------------------------------

void Workbench::rebuildVisualizationCache()
{
    m_dataX.clear();
    m_dataY.clear();
    m_curveX.clear();
    m_curveY.clear();
    m_approxCurveY.clear();
    m_residuals.clear();

    if (!m_selectedFormula || m_dataPoints.empty() || m_plotVariable.empty())
        return;

    // Extract data point X/Y for scatter
    for (const auto& dp : m_dataPoints)
    {
        auto it = dp.variables.find(m_plotVariable);
        if (it != dp.variables.end())
        {
            m_dataX.push_back(it->second);
            m_dataY.push_back(dp.observed);
        }
    }

    if (!m_hasFitResult)
        return;

    // Build fitted curve
    const ExprNode* expr = m_selectedFormula->getExpression(QualityTier::FULL);
    if (!expr)
        return;

    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffMap(
        m_fitResult.coefficients.begin(), m_fitResult.coefficients.end());

    // Find X range from data
    if (m_dataX.empty())
        return;
    float xMin = *std::min_element(m_dataX.begin(), m_dataX.end());
    float xMax = *std::max_element(m_dataX.begin(), m_dataX.end());
    float padding = (xMax - xMin) * 0.05f;
    xMin -= padding;
    xMax += padding;

    // Sample 100 points for the curve
    constexpr int CURVE_SAMPLES = 100;
    m_curveX.reserve(CURVE_SAMPLES);
    m_curveY.reserve(CURVE_SAMPLES);

    // Build variable map once, update sweep variable per sample
    ExpressionEvaluator::VariableMap vars;
    for (const auto& inp : m_selectedFormula->inputs)
        vars[inp.name] = inp.defaultValue;

    for (int i = 0; i < CURVE_SAMPLES; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(CURVE_SAMPLES - 1);
        float x = xMin + t * (xMax - xMin);
        vars[m_plotVariable] = x;

        float y = eval.evaluate(*expr, vars, coeffMap);
        m_curveX.push_back(x);
        m_curveY.push_back(y);
    }

    // Quality tier comparison (Improvement #3)
    if (m_showQualityComparison)
    {
        const ExprNode* approxExpr =
            m_selectedFormula->getExpression(QualityTier::APPROXIMATE);
        if (approxExpr)
        {
            m_approxCurveY.reserve(CURVE_SAMPLES);
            for (int i = 0; i < CURVE_SAMPLES; ++i)
            {
                ExpressionEvaluator::VariableMap vars;
                for (const auto& inp : m_selectedFormula->inputs)
                    vars[inp.name] = inp.defaultValue;
                vars[m_plotVariable] = m_curveX[i];

                float y = eval.evaluate(*approxExpr, vars, coeffMap);
                m_approxCurveY.push_back(y);
            }
        }
    }

    // Compute residuals at data points. Filter identically to the m_dataX
    // extraction above (line 1860-1868): only include points that have the
    // current plot variable. Prior to this fix the residual loop iterated
    // ALL data points, so m_residuals could drift out of sync with m_dataX
    // whenever any point lacked m_plotVariable — displaying residuals
    // against the wrong X values. (AUDIT.md §H10 / FIXPLAN E2.)
    for (const auto& dp : m_dataPoints)
    {
        auto it = dp.variables.find(m_plotVariable);
        if (it == dp.variables.end())
            continue;
        float predicted = eval.evaluate(*expr, dp.variables, coeffMap);
        m_residuals.push_back(dp.observed - predicted);
    }

    // Invariant: after filtering identically, counts must agree. Guards
    // against a future edit reintroducing the drift.
    assert(m_residuals.size() == m_dataX.size() &&
           "residual count must match filtered m_dataX count");
}

// ---------------------------------------------------------------------------
// §3.6 GUI — LLM-ranked formula shortlist panel.
//
// Two pieces: `runLlmSuggestions()` is the headless worker (writes the
// dataset to a temp CSV, dumps the library as JSON, forks the Python
// driver, reads its stdout into m_suggestionsOutput). `renderSuggestionsPanel()`
// is the ImGui face (Run button + status line + scrollable markdown).
// The panel blocks the UI while the driver runs — acceptable because
// Haiku responses are a second or two. If latency becomes an issue, move
// this to a worker thread.
// ---------------------------------------------------------------------------

void Workbench::runLlmSuggestions()
{
    m_suggestionsOutput.clear();
    m_suggestionsError.clear();

    // Pre-flight: need a dataset to rank against. No silent fallback
    // to synthetic data here — the whole point of the panel is to
    // reason about the user's real data, and a suggestion based on
    // synthetic points would be misleading.
    if (m_dataPoints.empty())
    {
        m_suggestionsError = "No data points to rank against. "
                             "Load a CSV or generate synthetic data first.";
        return;
    }

    // Locate the Python driver before writing anything. If it's
    // missing, surface the install path in the panel so the user
    // can diagnose without leaving the Workbench.
    const std::string script = findDriverScriptPath("llm_rank.py");
    if (script.empty())
    {
        m_suggestionsError =
            "llm_rank.py not found. Expected at "
            "tools/formula_workbench/scripts/llm_rank.py relative to the "
            "executable or source tree.";
        return;
    }

    // Materialise the current dataset as a temp CSV. The driver
    // takes a path, so we need a real file — but it lives in /tmp
    // and gets written / overwritten on every run; no persistence
    // concerns. Header: every variable name + "observed" as the
    // last column (matches the driver's loader expectations).
    namespace fs = std::filesystem;
    const auto csvPath = fs::temp_directory_path()
                       / ("workbench_suggest_"
                          + std::to_string(::getpid()) + ".csv");
    {
        std::ofstream out(csvPath);
        if (!out)
        {
            m_suggestionsError = "Cannot write temp CSV: " + csvPath.string();
            return;
        }
        // Union of variable names across all data points — handles the
        // (rare) case where different points have different variable
        // sets. Stable alphabetical order so the CSV is deterministic.
        std::set<std::string> names;
        for (const auto& dp : m_dataPoints)
            for (const auto& [k, _] : dp.variables) names.insert(k);
        std::vector<std::string> ordered(names.begin(), names.end());

        bool first = true;
        for (const auto& n : ordered)
        {
            if (!first) out << ",";
            out << n;
            first = false;
        }
        out << (ordered.empty() ? "" : ",") << "observed\n";

        for (const auto& dp : m_dataPoints)
        {
            first = true;
            for (const auto& n : ordered)
            {
                if (!first) out << ",";
                auto it = dp.variables.find(n);
                out << (it != dp.variables.end() ? it->second : 0.0f);
                first = false;
            }
            out << (ordered.empty() ? "" : ",") << dp.observed << "\n";
        }
    }

    const std::string libJson = libraryToJsonString(m_library);
    const std::vector<std::string> driverArgs{csvPath.string()};

    // W1 (1.10.0): driver call runs on a worker thread via
    // ``AsyncDriverJob``. The result is drained in
    // ``renderSuggestionsPanel`` each frame once the job transitions
    // to ``Done``. If a previous run is still in flight, ``start``
    // returns false and we surface that to the user — the UI button
    // is already disabled in this case, so this is belt-and-braces.
    if (!m_suggestionsJob.start(script, driverArgs, libJson))
    {
        m_suggestionsError = "A previous suggestion run is still in flight.";
        return;
    }

    // Leave the temp CSV behind on disk — developers debugging a
    // bad ranking sometimes want to re-run the driver manually
    // against the exact same data. It's a few KB in /tmp; the OS
    // cleans it up on next boot.
}

void Workbench::renderSuggestionsPanel()
{
    ImGui::Begin("Suggestions (LLM)");

    // W1 (1.10.0): drain the async job once per frame. If the worker
    // finished since last frame, pull the CapturedDriverOutput and
    // populate the panel's output / error strings. Must happen before
    // the button / status block so the UI reflects the latest state
    // in the same frame the result lands.
    if (m_suggestionsJob.poll() == AsyncDriverJob::State::Done)
    {
        const auto result = m_suggestionsJob.takeResult();
        if (!result.error.empty())
        {
            m_suggestionsError = "Driver spawn failed: " + result.error;
        }
        else if (result.exit_code != 0)
        {
            m_suggestionsError =
                "Driver exited with code "
                + std::to_string(result.exit_code)
                + " — missing ANTHROPIC_API_KEY or `anthropic` SDK? "
                  "Check the terminal for a detailed message.";
        }
        else
        {
            m_suggestionsOutput = result.stdout_text;
        }
    }

    ImGui::TextWrapped(
        "Rank which library formulas are most physically plausible for "
        "the current dataset. Needs ANTHROPIC_API_KEY in the environment "
        "and the `anthropic` Python SDK. The LLM stays advisory — the "
        "fitter still does the actual fit.");
    ImGui::Separator();

    const bool isRunning = m_suggestionsJob.isRunning();
    const bool canRun    = !isRunning && !m_dataPoints.empty();

    if (!canRun)
        ImGui::BeginDisabled();
    if (ImGui::Button("Suggest Formulas", ImVec2(200, 32)))
    {
        runLlmSuggestions();
    }
    if (!canRun)
        ImGui::EndDisabled();

    if (isRunning)
    {
        ImGui::SameLine();
        ImGui::Text("running %.1fs…", m_suggestionsJob.elapsedSeconds());
    }
    else if (m_dataPoints.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(load data first)");
    }

    if (!m_suggestionsError.empty())
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", m_suggestionsError.c_str());
        ImGui::PopStyleColor();
    }

    if (!m_suggestionsOutput.empty())
    {
        ImGui::Separator();
        // Read-only multiline. ImGui doesn't render markdown, so the
        // user sees the table source verbatim — which is fine for a
        // shortlist of ≤10 rows and preserves the LLM's commentary.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##suggestions",
                                  m_suggestionsOutput.data(),
                                  m_suggestionsOutput.size() + 1,
                                  avail, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// §3.5 GUI — "Discover via PySR" panel (W2, 1.11.0)
// ---------------------------------------------------------------------------
// Shells out to scripts/pysr_driver.py via AsyncDriverJob. Unlike the
// LLM Suggestions panel, a PySR run can take minutes, so this panel
// exercises the W2 additions to AsyncDriverJob: drainStdoutChunk for
// live progress and cancel() for user-initiated termination.
//
// Output contract (pysr_driver.py): markdown header + table, then a
// ```json fenced block containing {equations: [...]}. We display the
// raw stream while running, then parse the JSON tail into
// m_pysrEquations on Done and render as a sortable ImGui table.
// ---------------------------------------------------------------------------

namespace
{

// Extract the {"equations": [...]}-shaped JSON from the last ```json
// fence in the driver's stdout. Returns nullopt when the fence isn't
// present (run cancelled before completion, or driver errored).
std::optional<std::vector<Workbench::PySREquation>> parsePySRJsonTail(
    const std::string& stdoutText)
{
    const std::string openFence  = "```json";
    const std::string closeFence = "```";
    const auto openPos = stdoutText.rfind(openFence);
    if (openPos == std::string::npos)
        return std::nullopt;

    const auto contentStart = openPos + openFence.size();
    const auto closePos = stdoutText.find(closeFence, contentStart);
    if (closePos == std::string::npos)
        return std::nullopt;

    const std::string payload = stdoutText.substr(
        contentStart, closePos - contentStart);

    try
    {
        const auto root = nlohmann::json::parse(payload);
        if (!root.contains("equations") || !root["equations"].is_array())
            return std::nullopt;

        std::vector<Workbench::PySREquation> out;
        for (const auto& eq : root["equations"])
        {
            Workbench::PySREquation row;
            row.complexity = eq.value("complexity", 0);
            row.loss       = eq.value("loss", 0.0f);
            row.score      = eq.value("score", 0.0f);
            row.equation   = eq.value("equation", std::string{});
            out.push_back(std::move(row));
        }
        return out;
    }
    catch (const nlohmann::json::exception&)
    {
        // Driver emitted something that looked like JSON but wasn't
        // parseable (truncated stream, NaN/inf literals, etc.).
        // Falling through keeps the raw stream visible so the user
        // can still diagnose — the table just stays empty.
        return std::nullopt;
    }
}

} // namespace

void Workbench::runPySR()
{
    m_pysrStreamingOutput.clear();
    m_pysrError.clear();
    m_pysrEquations.clear();

    if (m_dataPoints.empty())
    {
        m_pysrError = "No data points to discover against. "
                      "Load a CSV or generate synthetic data first.";
        return;
    }

    const std::string script = findDriverScriptPath("pysr_driver.py");
    if (script.empty())
    {
        m_pysrError =
            "pysr_driver.py not found. Expected at "
            "tools/formula_workbench/scripts/pysr_driver.py relative to the "
            "executable or source tree.";
        return;
    }

    // Materialise the dataset to a temp CSV — pysr_driver.py takes
    // a path. Same shape as the Suggestions panel: variable columns
    // in stable alphabetical order, then "observed" as the last
    // column. Stable ordering matters so a fit on the same dataset
    // reproduces byte-identically.
    namespace fs = std::filesystem;
    const auto csvPath = fs::temp_directory_path()
                       / ("workbench_pysr_"
                          + std::to_string(::getpid()) + ".csv");
    {
        std::ofstream out(csvPath);
        if (!out)
        {
            m_pysrError = "Cannot write temp CSV: " + csvPath.string();
            return;
        }
        std::set<std::string> names;
        for (const auto& dp : m_dataPoints)
            for (const auto& [k, _] : dp.variables) names.insert(k);
        std::vector<std::string> ordered(names.begin(), names.end());

        bool first = true;
        for (const auto& n : ordered)
        {
            if (!first) out << ",";
            out << n;
            first = false;
        }
        out << (ordered.empty() ? "" : ",") << "observed\n";

        for (const auto& dp : m_dataPoints)
        {
            first = true;
            for (const auto& n : ordered)
            {
                if (!first) out << ",";
                auto it = dp.variables.find(n);
                out << (it != dp.variables.end() ? it->second : 0.0f);
                first = false;
            }
            out << (ordered.empty() ? "" : ",") << dp.observed << "\n";
        }
    }

    const std::vector<std::string> driverArgs{
        csvPath.string(),
        "--niterations",    std::to_string(m_pysrNiterations),
        "--max-complexity", std::to_string(m_pysrMaxComplexity),
    };

    if (!m_pysrJob.start(script, driverArgs, {}))
    {
        m_pysrError = "A previous PySR run is still in flight.";
        return;
    }
}

void Workbench::renderPySRPanel()
{
    ImGui::Begin("Discover via PySR");

    // Drain streaming bytes every frame so the panel reflects
    // the child's progress in real time. drainStdoutChunk is a
    // mutex-protected swap, so the cost is bounded per-frame.
    const std::string chunk = m_pysrJob.drainStdoutChunk();
    if (!chunk.empty())
        m_pysrStreamingOutput += chunk;

    // Transition to Done once the worker reports it. Also parse the
    // JSON tail into m_pysrEquations at this point — we only try
    // once per run, so repeatedly re-parsing on subsequent frames
    // is wasted work.
    if (m_pysrJob.poll() == AsyncDriverJob::State::Done)
    {
        const auto result = m_pysrJob.takeResult();
        // Make sure any final chunk emitted between the last drain
        // and Done lands in the panel. drainStdoutChunk + result's
        // stdout_text don't overlap — the result holds the COMPLETE
        // stream, the chunk channel held only what hadn't been
        // drained yet. Use whichever is longer so partial-drain and
        // full-capture paths both converge on the same display.
        if (result.stdout_text.size() > m_pysrStreamingOutput.size())
            m_pysrStreamingOutput = result.stdout_text;

        if (!result.error.empty())
        {
            m_pysrError = "Driver: " + result.error;
        }
        else if (result.exit_code == 2)
        {
            m_pysrError =
                "PySR not installed. Install with `pip install pysr` "
                "(also pulls Julia ~300 MB on first install).";
        }
        else if (result.exit_code != 0)
        {
            m_pysrError =
                "Driver exited with code "
                + std::to_string(result.exit_code)
                + " — check the terminal for details.";
        }

        if (auto parsed = parsePySRJsonTail(m_pysrStreamingOutput))
            m_pysrEquations = std::move(*parsed);
    }

    ImGui::TextWrapped(
        "Symbolic regression via PySR. Searches for an analytic "
        "formula that fits the current dataset. Runs can take tens "
        "of seconds to minutes depending on niterations and "
        "max-complexity.");
    ImGui::Separator();

    // Tuning knobs. Disabled mid-run to avoid the "what args did
    // this run actually use?" ambiguity — the sliders reflect the
    // NEXT run, so editing them while one is in flight would be
    // misleading.
    const bool isRunning = m_pysrJob.isRunning();
    if (isRunning)
        ImGui::BeginDisabled();
    ImGui::SliderInt("niterations",    &m_pysrNiterations,    5, 200);
    ImGui::SliderInt("max-complexity", &m_pysrMaxComplexity,  5, 40);
    if (isRunning)
        ImGui::EndDisabled();

    ImGui::Separator();

    const bool canRun = !isRunning && !m_dataPoints.empty();
    if (!canRun)
        ImGui::BeginDisabled();
    if (ImGui::Button("Discover via PySR", ImVec2(200, 32)))
    {
        runPySR();
    }
    if (!canRun)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (!isRunning)
        ImGui::BeginDisabled();
    if (ImGui::Button("Cancel", ImVec2(100, 32)))
    {
        m_pysrJob.cancel();
    }
    if (!isRunning)
        ImGui::EndDisabled();

    if (isRunning)
    {
        ImGui::SameLine();
        ImGui::Text("running %.1fs…", m_pysrJob.elapsedSeconds());
    }
    else if (m_dataPoints.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(load data first)");
    }

    if (!m_pysrError.empty())
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
        ImGui::TextWrapped("%s", m_pysrError.c_str());
        ImGui::PopStyleColor();
    }

    // Leaderboard — populated post-Done from the JSON tail. Shown
    // alongside the raw stream, not instead of it, so the user can
    // still see PySR's human-readable header.
    if (!m_pysrEquations.empty())
    {
        ImGui::Separator();
        ImGui::Text("Discovered equations (%zu):",
                    m_pysrEquations.size());

        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Sortable
            | ImGuiTableFlags_SortMulti
            | ImGuiTableFlags_ScrollY;
        const ImVec2 tableSize(0.0f, 180.0f);
        if (ImGui::BeginTable("##pysr_leaderboard", 4,
                              tableFlags, tableSize))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Complexity",
                                    ImGuiTableColumnFlags_DefaultSort
                                    | ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Loss",
                                    ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Score",
                                    ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Equation",
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
            {
                if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0)
                {
                    const auto& s = sortSpecs->Specs[0];
                    std::sort(m_pysrEquations.begin(), m_pysrEquations.end(),
                        [&](const PySREquation& a, const PySREquation& b) {
                            const bool asc =
                                (s.SortDirection == ImGuiSortDirection_Ascending);
                            switch (s.ColumnIndex)
                            {
                                case 0: return asc ? a.complexity < b.complexity
                                                   : a.complexity > b.complexity;
                                case 1: return asc ? a.loss < b.loss
                                                   : a.loss > b.loss;
                                case 2: return asc ? a.score < b.score
                                                   : a.score > b.score;
                                default: return asc ? a.equation < b.equation
                                                    : a.equation > b.equation;
                            }
                        });
                    sortSpecs->SpecsDirty = false;
                }
            }

            for (const auto& eq : m_pysrEquations)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", eq.complexity);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.4g", eq.loss);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.4g", eq.score);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(eq.equation.c_str());
            }
            ImGui::EndTable();
        }

        // W2c placeholder — explicit note that library-import is
        // still a follow-up. Avoids the "why doesn't this button do
        // anything" confusion that a disabled Import button would
        // create.
        ImGui::TextDisabled(
            "(Import-as-library requires a PySR expression parser — "
            "tracked as W2c in the self-learning roadmap.)");
    }

    if (!m_pysrStreamingOutput.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("Raw PySR output:");
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##pysr_raw",
                                  m_pysrStreamingOutput.data(),
                                  m_pysrStreamingOutput.size() + 1,
                                  avail, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}

} // namespace Vestige
