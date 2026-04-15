// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file performance_panel.cpp
/// @brief PerformancePanel implementation — ImGui dashboard with tabs.
#include "editor/panels/performance_panel.h"
#include "core/timer.h"
#include "core/window.h"
#include "profiler/performance_profiler.h"
#include "renderer/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Vestige
{

void PerformancePanel::draw(PerformanceProfiler& profiler, const Renderer* renderer,
                            Timer* timer, Window* window)
{
    // Auto-enable profiling when panel is open
    profiler.setEnabled(m_open);

    if (!ImGui::Begin("Performance", &m_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("PerfTabs"))
    {
        if (ImGui::BeginTabItem("Overview"))
        {
            drawOverviewTab(profiler, timer, window);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("GPU"))
        {
            drawGpuTab(profiler);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CPU"))
        {
            drawCpuTab(profiler);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory"))
        {
            drawMemoryTab(profiler);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Draw Calls"))
        {
            drawDrawCallsTab(renderer);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void PerformancePanel::drawOverviewTab(PerformanceProfiler& profiler,
                                       Timer* timer, Window* window)
{
    // FPS and frame time
    float fps = profiler.getFps();
    float frameMs = profiler.getFrameTimeMs();

    ImVec4 fpsColor = (fps >= 59.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                    : (fps >= 30.0f) ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                                     : ImVec4(1.0f, 0.3f, 0.2f, 1.0f);

    ImGui::TextColored(fpsColor, "%.0f FPS", static_cast<double>(fps));
    ImGui::SameLine(120);
    ImGui::Text("%.2f ms", static_cast<double>(frameMs));
    ImGui::SameLine(240);
    ImGui::Text("Avg: %.2f ms", static_cast<double>(profiler.getAvgFrameTimeMs()));

    ImGui::Text("Min: %.2f ms  Max: %.2f ms",
                static_cast<double>(profiler.getMinFrameTimeMs()),
                static_cast<double>(profiler.getMaxFrameTimeMs()));

    // Frame rate cap controls (above graphs so always visible)
    if (timer && window)
    {
        int cap = timer->getFrameRateCap();
        bool vsync = window->isVsyncEnabled();

        int mode = 0;  // 0=Uncapped, 1=60 FPS, 2=VSync
        if (cap == 60 && !vsync)
        {
            mode = 1;
        }
        else if (vsync)
        {
            mode = 2;
        }

        ImGui::SameLine(240);
        const char* modeLabel = (mode == 0) ? "[Uncapped]"
                              : (mode == 1) ? "[60 FPS cap]"
                                            : "[VSync]";
        ImGui::TextDisabled("%s", modeLabel);

        if (ImGui::RadioButton("Uncapped", mode == 0))
        {
            timer->setFrameRateCap(0);
            window->setVsync(false);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("60 FPS", mode == 1))
        {
            timer->setFrameRateCap(60);
            window->setVsync(false);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("VSync", mode == 2))
        {
            timer->setFrameRateCap(0);
            window->setVsync(true);
        }
    }

    ImGui::Separator();

    // CPU vs GPU breakdown
    float cpuMs = profiler.getCpuProfiler().getTotalCpuTimeMs();
    float gpuMs = profiler.getGpuTimer().getTotalGpuTimeMs();
    bool gpuBound = gpuMs > cpuMs;

    ImGui::Text("CPU: %.2f ms  GPU: %.2f ms",
                static_cast<double>(cpuMs), static_cast<double>(gpuMs));
    ImGui::SameLine();
    ImGui::TextColored(gpuBound ? ImVec4(1, 0.6f, 0.2f, 1) : ImVec4(0.2f, 0.8f, 1, 1),
                       gpuBound ? "[GPU bound]" : "[CPU bound]");

    ImGui::Separator();

    // Frame time history graph
    ImGui::Text("Frame Time History:");

    // Custom overlay callback text
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%.1f ms (%.0f FPS)",
             static_cast<double>(frameMs), static_cast<double>(fps));

    // Plot with budget line
    const float* history = profiler.getFrameTimeHistory();
    int offset = profiler.getHistoryOffset();

    // PlotLines with ring buffer
    ImGui::PlotLines("##FrameTime", history, PerformanceProfiler::HISTORY_SIZE,
                     offset, overlay, 0.0f, 33.3f, ImVec2(0, 120));

    // Budget line label
    ImGui::TextDisabled("--- 16.67ms (60 FPS) budget ---");

    ImGui::Spacing();

    // FPS history graph (derived from frame time history)
    ImGui::Text("FPS History:");

    static float fpsHistory[PerformanceProfiler::HISTORY_SIZE];
    for (int i = 0; i < PerformanceProfiler::HISTORY_SIZE; ++i)
    {
        float ms = history[i];
        fpsHistory[i] = (ms > 0.0f) ? (1000.0f / ms) : 0.0f;
    }

    char fpsOverlay[64];
    snprintf(fpsOverlay, sizeof(fpsOverlay), "%.0f FPS", static_cast<double>(fps));

    ImGui::PlotLines("##FPSHistory", fpsHistory, PerformanceProfiler::HISTORY_SIZE,
                     offset, fpsOverlay, 0.0f, 120.0f, ImVec2(0, 120));

    ImGui::TextDisabled("--- 60 FPS target ---");
}

void PerformancePanel::drawGpuTab(PerformanceProfiler& profiler)
{
    if (!profiler.getGpuTimer().hasResults())
    {
        ImGui::TextDisabled("Collecting GPU data... (needs 3 frames)");
        return;
    }

    const auto& results = profiler.getGpuTimer().getResults();
    float totalGpu = profiler.getGpuTimer().getTotalGpuTimeMs();

    ImGui::Text("Total GPU: %.2f ms", static_cast<double>(totalGpu));
    ImGui::Separator();

    // Stacked bar visualization
    float barWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    float barHeight = 24.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Color palette for passes
    static const ImU32 colors[] = {
        IM_COL32(220, 80, 80, 200),   // Red
        IM_COL32(80, 180, 220, 200),  // Blue
        IM_COL32(80, 200, 100, 200),  // Green
        IM_COL32(220, 180, 80, 200),  // Yellow
        IM_COL32(180, 100, 220, 200), // Purple
        IM_COL32(220, 140, 80, 200),  // Orange
        IM_COL32(100, 200, 200, 200), // Cyan
        IM_COL32(200, 200, 100, 200), // Lime
    };

    if (totalGpu > 0.0f)
    {
        float xOffset = 0.0f;
        for (size_t i = 0; i < results.size(); ++i)
        {
            float fraction = results[i].timeMs / totalGpu;
            float width = fraction * barWidth;

            drawList->AddRectFilled(
                ImVec2(barPos.x + xOffset, barPos.y),
                ImVec2(barPos.x + xOffset + width, barPos.y + barHeight),
                colors[i % 8]);

            xOffset += width;
        }
    }

    // Border
    drawList->AddRect(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight),
                      IM_COL32(200, 200, 200, 150));

    ImGui::Dummy(ImVec2(0, barHeight + 4));

    // Per-pass table
    if (ImGui::BeginTable("GpuPasses", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < results.size(); ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            // Color indicator
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(colors[i % 8]);
            ImGui::ColorButton(("##c" + std::to_string(i)).c_str(), color,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::Text("%s", results[i].name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", static_cast<double>(results[i].timeMs));

            ImGui::TableSetColumnIndex(2);
            float pct = (totalGpu > 0.0f) ? (results[i].timeMs / totalGpu * 100.0f) : 0.0f;
            ImGui::Text("%.0f%%", static_cast<double>(pct));
        }

        ImGui::EndTable();
    }
}

void PerformancePanel::drawCpuTab(PerformanceProfiler& profiler)
{
    float totalCpu = profiler.getCpuProfiler().getTotalCpuTimeMs();
    ImGui::Text("Total CPU: %.2f ms", static_cast<double>(totalCpu));
    ImGui::Separator();

    const auto& entries = profiler.getCpuProfiler().getLastFrame();
    if (entries.empty())
    {
        ImGui::TextDisabled("No CPU profile data. Add VESTIGE_PROFILE_SCOPE macros.");
        return;
    }

    if (ImGui::BeginTable("CpuScopes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (const auto& entry : entries)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            // Indent based on depth
            for (int d = 0; d < entry.depth; ++d)
            {
                ImGui::Indent(12.0f);
            }
            ImGui::Text("%s", entry.name);
            for (int d = 0; d < entry.depth; ++d)
            {
                ImGui::Unindent(12.0f);
            }

            ImGui::TableSetColumnIndex(1);
            float scopeMs = entry.endMs - entry.startMs;
            ImGui::Text("%.3f", static_cast<double>(scopeMs));

            ImGui::TableSetColumnIndex(2);
            float pct = (totalCpu > 0.0f) ? (scopeMs / totalCpu * 100.0f) : 0.0f;
            ImGui::Text("%.0f%%", static_cast<double>(pct));
        }

        ImGui::EndTable();
    }
}

void PerformancePanel::drawMemoryTab(PerformanceProfiler& profiler)
{
    auto& mem = profiler.getMemoryTracker();

    ImGui::Text("CPU Heap");
    size_t cpuBytes = MemoryTracker::getCpuAllocatedBytes();
    size_t cpuCount = MemoryTracker::getCpuAllocationCount();
    size_t cpuPeak = MemoryTracker::getCpuPeakBytes();

    ImGui::Text("  Allocated: %.1f MB (%zu allocs)",
                static_cast<double>(cpuBytes) / (1024.0 * 1024.0), cpuCount);
    ImGui::Text("  Peak:      %.1f MB",
                static_cast<double>(cpuPeak) / (1024.0 * 1024.0));

    ImGui::Separator();

    ImGui::Text("GPU VRAM");
    size_t gpuUsed = mem.getGpuUsedMB();
    size_t gpuTotal = mem.getGpuTotalMB();

    if (gpuTotal > 0)
    {
        float fraction = static_cast<float>(gpuUsed) / static_cast<float>(gpuTotal);
        char label[64];
        snprintf(label, sizeof(label), "%zu / %zu MB (%.0f%%)",
                 gpuUsed, gpuTotal, static_cast<double>(fraction) * 100.0);
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
    }
    else
    {
        ImGui::TextDisabled("GPU VRAM data unavailable (AMD sysfs not found)");
    }
}

void PerformancePanel::drawDrawCallsTab(const Renderer* renderer)
{
    if (!renderer)
    {
        ImGui::TextDisabled("No renderer data available.");
        return;
    }

    const auto& stats = renderer->getCullingStats();

    if (ImGui::BeginTable("DrawStats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        auto row = [](const char* label, int value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", value);
        };

        row("Total Opaque Items", stats.totalItems);
        row("After Frustum Cull", stats.culledItems);
        row("Transparent Items", stats.transparentTotal);
        row("Shadow Casters", stats.shadowCastersTotal);
        row("Draw Calls", stats.drawCalls);
        row("Instanced Batches", stats.instanceBatches);

        // Batch efficiency
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Batch Efficiency");
        ImGui::TableSetColumnIndex(1);
        if (stats.drawCalls > 0)
        {
            float efficiency = static_cast<float>(stats.instanceBatches)
                             / static_cast<float>(stats.drawCalls) * 100.0f;
            ImGui::Text("%.0f%%", static_cast<double>(efficiency));
        }
        else
        {
            ImGui::Text("N/A");
        }

        ImGui::EndTable();
    }
}

} // namespace Vestige
