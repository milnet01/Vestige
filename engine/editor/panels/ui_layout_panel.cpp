// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "editor/panels/ui_layout_panel.h"
#include "ui/ui_canvas.h"
#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <imgui.h>

#include <cstdio>

namespace Vestige
{

namespace
{

const char* anchorName(Anchor a)
{
    switch (a)
    {
        case Anchor::TOP_LEFT:      return "TOP_LEFT";
        case Anchor::TOP_CENTER:    return "TOP_CENTER";
        case Anchor::TOP_RIGHT:     return "TOP_RIGHT";
        case Anchor::CENTER_LEFT:   return "CENTER_LEFT";
        case Anchor::CENTER:        return "CENTER";
        case Anchor::CENTER_RIGHT:  return "CENTER_RIGHT";
        case Anchor::BOTTOM_LEFT:   return "BOTTOM_LEFT";
        case Anchor::BOTTOM_CENTER: return "BOTTOM_CENTER";
        case Anchor::BOTTOM_RIGHT:  return "BOTTOM_RIGHT";
    }
    return "?";
}

void drawElementRow(UIElement* e, int index, int& selected)
{
    char label[64];
    std::snprintf(label, sizeof(label), "[%d] %s %s %s",
                   index,
                   e->visible ? " " : "H",
                   e->interactive ? "I" : " ",
                   anchorName(e->anchor));
    const bool isSelected = (selected == index);
    if (ImGui::Selectable(label, isSelected))
    {
        selected = index;
    }
}

void drawElementInspector(UIElement* e)
{
    ImGui::Separator();
    ImGui::Text("Element properties");

    ImGui::DragFloat2("Position (px)", &e->position.x, 1.0f, -4096.0f, 4096.0f, "%.0f");
    ImGui::DragFloat2("Size (px)",     &e->size.x,     1.0f,     0.0f, 4096.0f, "%.0f");

    const char* anchorNames[] = {
        "TOP_LEFT", "TOP_CENTER", "TOP_RIGHT",
        "CENTER_LEFT", "CENTER", "CENTER_RIGHT",
        "BOTTOM_LEFT", "BOTTOM_CENTER", "BOTTOM_RIGHT",
    };
    int anchorIdx = static_cast<int>(e->anchor);
    if (ImGui::Combo("Anchor", &anchorIdx, anchorNames, IM_ARRAYSIZE(anchorNames)))
    {
        e->anchor = static_cast<Anchor>(anchorIdx);
    }
    ImGui::Checkbox("Visible",     &e->visible);
    ImGui::SameLine();
    ImGui::Checkbox("Interactive", &e->interactive);
}

void drawThemeEditor(UITheme* theme)
{
    if (theme == nullptr) return;

    ImGui::Separator();
    ImGui::Text("Theme palette");

    // Register selector (Vellum ↔ Plumbline) — offers a quick-switch beyond
    // hand-editing fields.
    if (ImGui::Button("Reset to Vellum"))
    {
        *theme = UITheme::defaultTheme();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Plumbline"))
    {
        *theme = UITheme::plumbline();
    }

    if (ImGui::TreeNodeEx("Backgrounds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("bgBase",         &theme->bgBase.r);
        ImGui::ColorEdit4("bgRaised",       &theme->bgRaised.r);
        ImGui::ColorEdit4("panelBg",        &theme->panelBg.r);
        ImGui::ColorEdit4("panelBgHover",   &theme->panelBgHover.r);
        ImGui::ColorEdit4("panelBgPressed", &theme->panelBgPressed.r);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Strokes / rules"))
    {
        ImGui::ColorEdit4("panelStroke",       &theme->panelStroke.r);
        ImGui::ColorEdit4("panelStrokeStrong", &theme->panelStrokeStrong.r);
        ImGui::ColorEdit4("rule",              &theme->rule.r);
        ImGui::ColorEdit4("ruleStrong",        &theme->ruleStrong.r);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Text"))
    {
        ImGui::ColorEdit3("textPrimary",   &theme->textPrimary.r);
        ImGui::ColorEdit3("textSecondary", &theme->textSecondary.r);
        ImGui::ColorEdit3("textDisabled",  &theme->textDisabled.r);
        ImGui::ColorEdit3("textWarning",   &theme->textWarning.r);
        ImGui::ColorEdit3("textError",     &theme->textError.r);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Accent"))
    {
        ImGui::ColorEdit4("accent",    &theme->accent.r);
        ImGui::ColorEdit4("accentDim", &theme->accentDim.r);
        ImGui::ColorEdit3("accentInk", &theme->accentInk.r);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("HUD"))
    {
        ImGui::ColorEdit4("crosshair",        &theme->crosshair.r);
        ImGui::ColorEdit4("progressBarFill",  &theme->progressBarFill.r);
        ImGui::ColorEdit4("progressBarEmpty", &theme->progressBarEmpty.r);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Sizes"))
    {
        ImGui::DragFloat("Button height",        &theme->buttonHeight,        1.0f, 24.0f, 128.0f, "%.0f");
        ImGui::DragFloat("Button height (sm)",   &theme->buttonHeightSmall,   1.0f, 20.0f, 80.0f,  "%.0f");
        ImGui::DragFloat("Slider height",        &theme->sliderHeight,        1.0f, 20.0f, 80.0f,  "%.0f");
        ImGui::DragFloat("Checkbox size",        &theme->checkboxSize,        1.0f,  8.0f, 48.0f,  "%.0f");
        ImGui::DragFloat("Dropdown height",      &theme->dropdownHeight,      1.0f, 20.0f, 80.0f,  "%.0f");
        ImGui::DragFloat("Keybind key height",   &theme->keybindKeyHeight,    1.0f, 20.0f, 80.0f,  "%.0f");
        ImGui::DragFloat("Crosshair arm length", &theme->crosshairLength,     1.0f,  4.0f, 64.0f,  "%.0f");
        ImGui::DragFloat("Transition (sec)",     &theme->transitionDuration, 0.005f, 0.0f, 1.0f,  "%.3f");
        ImGui::TreePop();
    }
}

} // namespace

void UILayoutPanel::draw(UICanvas* canvas, UITheme* theme)
{
    if (!m_open) return;

    if (!ImGui::Begin("UI Layout", &m_open))
    {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.80f, 0.66f, 0.29f, 1.0f),
                        "VESTIGE  UI  LAYOUT");
    ImGui::TextDisabled("Live editor for in-game canvas + theme.");
    ImGui::Spacing();

    if (canvas == nullptr)
    {
        ImGui::TextDisabled("No canvas attached. Game code should call "
                             "editor.getUILayoutPanel().draw(&canvas, &theme).");
    }
    else
    {
        const size_t count = canvas->getElementCount();
        ImGui::Text("Elements: %zu", count);

        ImGui::BeginChild("##ui-layout-elements",
                          ImVec2(0, 180),
                          true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < count; ++i)
        {
            drawElementRow(canvas->getElementAt(i),
                            static_cast<int>(i),
                            m_selectedElement);
        }
        ImGui::EndChild();

        if (m_selectedElement >= 0
            && m_selectedElement < static_cast<int>(count))
        {
            drawElementInspector(canvas->getElementAt(
                static_cast<size_t>(m_selectedElement)));
        }
    }

    drawThemeEditor(theme);
    ImGui::End();
}

} // namespace Vestige
