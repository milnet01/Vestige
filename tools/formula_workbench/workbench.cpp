/// @file workbench.cpp
/// @brief FormulaWorkbench implementation.
#include "workbench.h"
#include "formula/expression_eval.h"
#include "formula/lut_generator.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

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

    m_firstFrame = false;
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void Workbench::renderMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Import CSV..."))
            {
                // For now, use the text input in data editor
                m_statusMessage = "Use the CSV path field in the Data Editor panel.";
                m_statusTimer = 3.0f;
            }
            if (ImGui::MenuItem("Export Formula..."))
                exportFormula(m_exportPath);
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                // Signal window close — handled by main loop checking glfwWindowShouldClose
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About"))
            {
                m_statusMessage = "Vestige FormulaWorkbench — FP-4 Formula Pipeline";
                m_statusTimer = 3.0f;
            }
            ImGui::EndMenu();
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
// Data editor
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
    if (ImGui::Button("Generate Synthetic"))
        generateSyntheticData();

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
// Fitting controls
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

    // Editable coefficient initial values
    ImGui::Text("Initial Coefficients:");
    for (auto& [name, val] : m_coefficients)
    {
        ImGui::SetNextItemWidth(120);
        std::string label = name;
        ImGui::InputFloat(label.c_str(), &val, 0.0f, 0.0f, "%.6f");
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

        ImGui::Text("Iterations: %d", m_fitResult.iterations);
        ImGui::Text("R-squared:  %.6f", static_cast<double>(m_fitResult.rSquared));
        ImGui::Text("RMSE:       %.6f", static_cast<double>(m_fitResult.rmse));
        ImGui::Text("Max Error:  %.6f", static_cast<double>(m_fitResult.maxError));

        ImGui::Separator();
        ImGui::Text("Fitted Coefficients:");
        for (const auto& [name, val] : m_fitResult.coefficients)
        {
            ImGui::BulletText("%s = %.6f", name.c_str(), static_cast<double>(val));
        }

        // Apply fitted values as new initial values
        if (ImGui::Button("Apply to Initial Values"))
        {
            m_coefficients = m_fitResult.coefficients;
            m_statusMessage = "Fitted values applied as initial values.";
            m_statusTimer = 2.0f;
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Visualizer (ImPlot)
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
            ImPlot::PlotLine("Fitted", m_curveX.data(), m_curveY.data(),
                             static_cast<int>(m_curveX.size()));
        }

        ImPlot::EndPlot();
    }

    // Residual plot
    if (m_hasFitResult && !m_residuals.empty())
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
// Validation panel
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

    ImGui::Separator();

    // Export section
    ImGui::Text("Export");
    ImGui::InputText("Output Path", m_exportPath, sizeof(m_exportPath));
    if (ImGui::Button("Export to JSON"))
        exportFormula(m_exportPath);

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
        m_hasFitResult = false;
        m_hasValidation = false;
        m_dataPoints.clear();
        m_curveX.clear();
        m_curveY.clear();
        m_dataX.clear();
        m_dataY.clear();
        m_residuals.clear();

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

void Workbench::runFit()
{
    if (!m_selectedFormula || m_dataPoints.empty() || m_coefficients.empty())
        return;

    m_fitResult = CurveFitter::fit(*m_selectedFormula, m_dataPoints,
                                   m_coefficients, QualityTier::FULL,
                                   m_fitConfig);
    m_hasFitResult = true;

    m_statusMessage = m_fitResult.converged
        ? "Fit converged! R²=" + std::to_string(m_fitResult.rSquared)
        : "Fit did not converge: " + m_fitResult.statusMessage;
    m_statusTimer = 3.0f;

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

    float sumSqResid = 0.0f;
    float maxErr = 0.0f;
    float sumObs = 0.0f;
    for (const auto& dp : testData)
        sumObs += dp.observed;
    float meanObs = sumObs / static_cast<float>(testData.size());

    float ssTot = 0.0f;
    for (const auto& dp : testData)
    {
        float predicted = eval.evaluate(*expr, dp.variables, coeffMap);
        float resid = predicted - dp.observed;
        sumSqResid += resid * resid;
        maxErr = std::max(maxErr, std::abs(resid));

        float diff = dp.observed - meanObs;
        ssTot += diff * diff;
    }

    m_testResult.rmse = std::sqrt(sumSqResid / static_cast<float>(testData.size()));
    m_testResult.maxError = maxErr;
    m_testResult.rSquared = (ssTot > 1e-15f) ? (1.0f - sumSqResid / ssTot) : 1.0f;
    m_testResult.coefficients = m_trainResult.coefficients;

    m_hasValidation = true;
    m_statusMessage = "Validation complete. Train R²=" +
                      std::to_string(m_trainResult.rSquared) + " Test R²=" +
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

    // Read header line
    std::string headerLine;
    if (!std::getline(file, headerLine))
    {
        m_statusMessage = "CSV file is empty.";
        m_statusTimer = 3.0f;
        return;
    }

    // Parse headers
    std::vector<std::string> headers;
    {
        std::istringstream ss(headerLine);
        std::string col;
        while (std::getline(ss, col, ','))
        {
            // Trim whitespace
            col.erase(0, col.find_first_not_of(" \t\r\n"));
            col.erase(col.find_last_not_of(" \t\r\n") + 1);
            headers.push_back(col);
        }
    }

    if (headers.size() < 2)
    {
        m_statusMessage = "CSV must have at least 2 columns.";
        m_statusTimer = 3.0f;
        return;
    }

    // Last column is observed value; rest are variables
    size_t imported = 0;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::istringstream ss(line);
        std::string cell;
        std::vector<float> values;
        while (std::getline(ss, cell, ','))
        {
            cell.erase(0, cell.find_first_not_of(" \t\r\n"));
            cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
            try
            {
                values.push_back(std::stof(cell));
            }
            catch (...)
            {
                values.push_back(0.0f);
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
    m_statusTimer = 3.0f;

    rebuildVisualizationCache();
}

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

    // Find the first scalar input to sweep
    std::string sweepVar;
    float sweepMin = 0.0f;
    float sweepMax = 10.0f;
    for (const auto& inp : m_selectedFormula->inputs)
    {
        if (inp.type == FormulaValueType::FLOAT)
        {
            sweepVar = inp.name;
            sweepMin = inp.defaultValue * 0.1f;
            sweepMax = inp.defaultValue * 3.0f;
            if (sweepMax <= sweepMin)
            {
                sweepMin = 0.0f;
                sweepMax = 10.0f;
            }
            break;
        }
    }

    if (sweepVar.empty())
    {
        m_statusMessage = "No scalar input to sweep.";
        m_statusTimer = 3.0f;
        return;
    }

    // Generate 20 data points with slight noise
    m_dataPoints.clear();
    std::mt19937 rng(123);
    std::normal_distribution<float> noise(0.0f, 0.02f);

    for (int i = 0; i < 20; ++i)
    {
        float t = static_cast<float>(i) / 19.0f;
        float x = sweepMin + t * (sweepMax - sweepMin);

        DataPoint dp;
        // Set all variables to defaults first
        for (const auto& inp : m_selectedFormula->inputs)
            dp.variables[inp.name] = inp.defaultValue;
        dp.variables[sweepVar] = x;

        float val = eval.evaluate(*expr, dp.variables, coeffMap);
        dp.observed = val * (1.0f + noise(rng));  // Add ~2% noise
        m_dataPoints.push_back(dp);
    }

    m_statusMessage = "Generated 20 synthetic data points.";
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
}

// ---------------------------------------------------------------------------
// Visualization cache
// ---------------------------------------------------------------------------

void Workbench::rebuildVisualizationCache()
{
    m_dataX.clear();
    m_dataY.clear();
    m_curveX.clear();
    m_curveY.clear();
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
    float xMin = *std::min_element(m_dataX.begin(), m_dataX.end());
    float xMax = *std::max_element(m_dataX.begin(), m_dataX.end());
    float padding = (xMax - xMin) * 0.05f;
    xMin -= padding;
    xMax += padding;

    // Sample 100 points for the curve
    constexpr int CURVE_SAMPLES = 100;
    for (int i = 0; i < CURVE_SAMPLES; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(CURVE_SAMPLES - 1);
        float x = xMin + t * (xMax - xMin);

        // Build variable map with defaults + sweep variable
        ExpressionEvaluator::VariableMap vars;
        for (const auto& inp : m_selectedFormula->inputs)
            vars[inp.name] = inp.defaultValue;
        vars[m_plotVariable] = x;

        float y = eval.evaluate(*expr, vars, coeffMap);
        m_curveX.push_back(x);
        m_curveY.push_back(y);
    }

    // Compute residuals at data points
    for (const auto& dp : m_dataPoints)
    {
        float predicted = eval.evaluate(*expr, dp.variables, coeffMap);
        m_residuals.push_back(predicted - dp.observed);
    }
}

} // namespace Vestige
