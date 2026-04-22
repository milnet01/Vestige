// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file first_run_wizard.cpp
/// @brief Phase 10.5 slice 14.2 — first-run onboarding wizard.
#include "editor/panels/first_run_wizard.h"

#include "editor/entity_factory.h"
#include "scene/camera_component.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <system_error>

namespace fs = std::filesystem;

namespace Vestige
{

namespace
{

/// @brief Map a TemplateDialog template index by enum type.
///        The wizard's featured / more filter is defined over the
///        existing 8 templates — we just select specific ones by
///        their `GameTemplateType` so Phase 9F's template data
///        stays the single source of truth.
std::vector<GameTemplateConfig> pickByTypes(
    const std::vector<GameTemplateType>& types)
{
    std::vector<GameTemplateConfig> all = TemplateDialog::getTemplates();
    std::vector<GameTemplateConfig> out;
    out.reserve(types.size());
    for (GameTemplateType want : types)
    {
        auto it = std::find_if(all.begin(), all.end(),
            [want](const GameTemplateConfig& c) { return c.type == want; });
        if (it != all.end())
        {
            out.push_back(*it);
        }
    }
    return out;
}

} // namespace

// ============================================================================
// Template filter (Q1 — feature 4, rest under expander)
// ============================================================================

std::vector<GameTemplateConfig> featuredTemplates()
{
    // Order matches design §10 Q1 proposal.
    return pickByTypes({
        GameTemplateType::FIRST_PERSON_3D,
        GameTemplateType::THIRD_PERSON_3D,
        GameTemplateType::TWO_POINT_FIVE_D,
        GameTemplateType::ISOMETRIC,
    });
}

std::vector<GameTemplateConfig> moreTemplates()
{
    return pickByTypes({
        GameTemplateType::TOP_DOWN,
        GameTemplateType::POINT_AND_CLICK,
        GameTemplateType::SIDE_SCROLLER_2D,
        GameTemplateType::SHMUP_2D,
    });
}

std::vector<GameTemplateConfig> allWizardTemplates()
{
    auto featured = featuredTemplates();
    auto more     = moreTemplates();
    featured.insert(featured.end(), more.begin(), more.end());
    return featured;
}

void applyEmptyScene(Scene& scene, ResourceManager& resources)
{
    scene.clearEntities();

    // One ground plane, scaled up for a reasonable play area (matches
    // the default TemplateDialog::applyTemplate ground sizing).
    if (auto* ground = EntityFactory::createPlane(
            scene, resources, glm::vec3(0.0f)))
    {
        ground->setName("Ground");
        ground->transform.scale = glm::vec3(10.0f, 1.0f, 10.0f);
    }

    // One directional light.
    if (auto* light = EntityFactory::createDirectionalLight(
            scene, glm::vec3(5.0f, 10.0f, 5.0f)))
    {
        light->setName("Sun");
    }

    // One camera at eye height looking down -Z.
    if (auto* cameraEntity = EntityFactory::createEmptyEntity(
            scene, glm::vec3(0.0f, 1.7f, 5.0f)))
    {
        cameraEntity->setName("Camera");
        auto* cam = cameraEntity->addComponent<CameraComponent>();
        cam->projectionType = ProjectionType::PERSPECTIVE;
        cam->fov            = 60.0f;
    }
}

std::vector<GameTemplateConfig> filterByAvailability(
    const std::vector<GameTemplateConfig>& templates,
    const std::filesystem::path& assetRoot)
{
    // Empty root disables filtering. Useful for tests that don't
    // want to stand up fake asset trees and for early engine-init
    // calls before the asset path has been resolved.
    if (assetRoot.empty())
    {
        return templates;
    }

    std::vector<GameTemplateConfig> out;
    out.reserve(templates.size());
    for (const auto& cfg : templates)
    {
        if (cfg.requiredAssets.empty())
        {
            out.push_back(cfg);
            continue;
        }

        bool allPresent = true;
        for (const auto& rel : cfg.requiredAssets)
        {
            std::error_code ec;
            if (!fs::exists(assetRoot / rel, ec) || ec)
            {
                allPresent = false;
                break;
            }
        }
        if (allPresent)
        {
            out.push_back(cfg);
        }
    }
    return out;
}

// ============================================================================
// Pure state machine
// ============================================================================

namespace
{

/// @brief Skip-count threshold at which "Skip for now" auto-flips
///        `hasCompletedFirstRun` (Q7 resolution — remind twice,
///        then silently complete on the third launch).
constexpr int kSkipAutoCompleteThreshold = 2;

void markComplete(OnboardingSettings& ob, const std::string& nowIso)
{
    ob.hasCompletedFirstRun = true;
    // Only stamp the timestamp when it's empty — a future
    // re-completion via Help menu shouldn't overwrite the historical
    // record of when onboarding actually finished.
    if (ob.completedAt.empty())
    {
        ob.completedAt = nowIso;
    }
}

} // namespace

FirstRunTransition applyFirstRunIntent(
    FirstRunWizardStep currentStep,
    const OnboardingSettings& current,
    FirstRunIntent intent,
    const std::string& nowIso)
{
    FirstRunTransition t;
    t.step       = currentStep;
    t.onboarding = current;

    switch (intent)
    {
        // ----- From Welcome ---------------------------------------
        case FirstRunIntent::PickTemplate:
            t.step = FirstRunWizardStep::TemplatePicker;
            break;

        case FirstRunIntent::StartEmpty:
            t.step    = FirstRunWizardStep::Done;
            t.sceneOp = FirstRunWizardSceneOp::ApplyEmpty;
            t.closed  = true;
            markComplete(t.onboarding, nowIso);
            break;

        case FirstRunIntent::ShowDemo:
            t.step    = FirstRunWizardStep::Done;
            t.sceneOp = FirstRunWizardSceneOp::ApplyDemo;
            t.closed  = true;
            markComplete(t.onboarding, nowIso);
            break;

        case FirstRunIntent::SkipForNow:
        case FirstRunIntent::CloseAtWelcome:
            // Q7: increment skipCount; auto-complete on the second skip.
            t.onboarding.skipCount += 1;
            t.closed = true;
            if (t.onboarding.skipCount >= kSkipAutoCompleteThreshold)
            {
                t.step = FirstRunWizardStep::Done;
                markComplete(t.onboarding, nowIso);
            }
            // Below the threshold we close without marking complete —
            // the wizard re-opens on next launch.
            break;

        // ----- From TemplatePicker --------------------------------
        case FirstRunIntent::Back:
        case FirstRunIntent::CloseAtPicker:
            t.step = FirstRunWizardStep::Welcome;
            // Deliberately does NOT bump skipCount — Back is "I changed
            // my mind about picking", not "I'm skipping onboarding".
            break;

        case FirstRunIntent::FinishWithTemplate:
            t.step    = FirstRunWizardStep::Done;
            t.sceneOp = FirstRunWizardSceneOp::ApplyTemplate;
            t.closed  = true;
            markComplete(t.onboarding, nowIso);
            break;
    }

    return t;
}

// ============================================================================
// ImGui-binding panel
// ============================================================================

namespace
{

std::string isoUtcNow()
{
    using namespace std::chrono;
    const auto now     = system_clock::now();
    const auto time_t_ = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    ::gmtime_s(&tm, &time_t_);
#else
    ::gmtime_r(&time_t_, &tm);
#endif
    char buf[32];
    // ISO-8601 UTC: 2026-04-22T14:30:00Z
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

const char* kWindowTitle = "Welcome to Vestige";

} // namespace

void FirstRunWizard::initialize(OnboardingSettings* onboarding,
                                 std::filesystem::path assetRoot)
{
    m_onboarding    = onboarding;
    m_assetRoot     = std::move(assetRoot);
    m_step          = FirstRunWizardStep::Welcome;
    m_selectedIndex = 0;
    m_showMore      = false;

    // Auto-open when onboarding hasn't completed. Does not re-open
    // within a session after a terminal transition.
    m_open = (m_onboarding != nullptr) && !m_onboarding->hasCompletedFirstRun;
}

void FirstRunWizard::openFromHelpMenu()
{
    m_open = true;
    m_step = FirstRunWizardStep::Welcome;
}

FirstRunWizardSceneOp FirstRunWizard::fire(FirstRunIntent intent)
{
    if (!m_onboarding)
    {
        return FirstRunWizardSceneOp::None;
    }

    FirstRunTransition t = applyFirstRunIntent(
        m_step, *m_onboarding, intent, isoUtcNow());

    m_step        = t.step;
    *m_onboarding = t.onboarding;
    if (t.closed)
    {
        m_open = false;
    }
    return t.sceneOp;
}

FirstRunWizardSceneOp FirstRunWizard::draw()
{
    if (!m_open || !m_onboarding)
    {
        return FirstRunWizardSceneOp::None;
    }

    FirstRunWizardSceneOp op = FirstRunWizardSceneOp::None;

    ImGui::SetNextWindowSize(ImVec2(620.0f, 500.0f), ImGuiCond_FirstUseEver);
    bool stayOpen = true;
    if (ImGui::Begin(kWindowTitle, &stayOpen,
                     ImGuiWindowFlags_NoCollapse))
    {
        if (m_step == FirstRunWizardStep::Welcome)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                               "Welcome to Vestige.");
            ImGui::Separator();
            ImGui::TextWrapped(
                "Vestige is a 3D engine for exploration, games, and "
                "architectural walkthroughs. Pick a starting scene to "
                "begin, or start empty and build from scratch.");
            ImGui::Spacing();

            if (ImGui::Button("Pick a template...",
                              ImVec2(-FLT_MIN, 0.0f)))
            {
                fire(FirstRunIntent::PickTemplate);
            }
            ImGui::Spacing();

            if (ImGui::Button("Start empty", ImVec2(-FLT_MIN, 0.0f)))
            {
                op = fire(FirstRunIntent::StartEmpty);
            }
            ImGui::TextDisabled(
                "One camera, one directional light, one ground plane.");
            ImGui::Spacing();

            if (ImGui::Button("Show me the demo", ImVec2(-FLT_MIN, 0.0f)))
            {
                op = fire(FirstRunIntent::ShowDemo);
            }
            ImGui::TextDisabled(
                "The engine's built-in CC0 showroom (four textured blocks "
                "on a ground plane).");
            ImGui::Spacing();

            ImGui::Separator();

            if (ImGui::Button("Skip for now", ImVec2(200.0f, 0.0f)))
            {
                fire(FirstRunIntent::SkipForNow);
            }
            if (m_onboarding->skipCount > 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "(This wizard will stop auto-opening after the next skip.)");
            }

            ImGui::Spacing();
            ImGui::TextDisabled(
                "You can reopen this wizard any time from Help → Show Welcome.");
        }
        else if (m_step == FirstRunWizardStep::TemplatePicker)
        {
            // Filter both buckets by availability against the stored
            // asset root so private-repo-only templates stay hidden
            // in public clones (Q4 / slice 14.3).
            const std::vector<GameTemplateConfig> featured =
                filterByAvailability(featuredTemplates(), m_assetRoot);
            const std::vector<GameTemplateConfig> more =
                filterByAvailability(moreTemplates(),     m_assetRoot);

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                               "Pick a game-type template.");
            ImGui::Separator();

            const auto showRow = [&](const GameTemplateConfig& cfg, int index)
            {
                const bool selected = (m_selectedIndex == index);
                if (ImGui::Selectable(cfg.displayName.c_str(), selected,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(0.0f, 0.0f)))
                {
                    m_selectedIndex = index;
                }
                if (selected)
                {
                    ImGui::TextWrapped("%s", cfg.description.c_str());
                    ImGui::Spacing();
                }
            };

            ImGui::BeginChild("##wizard_featured",
                              ImVec2(0.0f, -70.0f), true);
            ImGui::TextDisabled("Featured");
            int idx = 0;
            for (const auto& cfg : featured)
            {
                showRow(cfg, idx++);
            }

            ImGui::Spacing();
            if (ImGui::Checkbox("More templates", &m_showMore))
            {
                // toggled
            }
            if (m_showMore)
            {
                ImGui::TextDisabled("Additional archetypes");
                for (const auto& cfg : more)
                {
                    showRow(cfg, idx++);
                }
            }
            ImGui::EndChild();

            ImGui::Separator();

            if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
            {
                op = fire(FirstRunIntent::FinishWithTemplate);
            }
            ImGui::SameLine();
            if (ImGui::Button("Back", ImVec2(120.0f, 0.0f)))
            {
                fire(FirstRunIntent::Back);
            }
        }
    }
    ImGui::End();

    // Honour the window's X button — route per current step.
    if (!stayOpen && m_open)
    {
        if (m_step == FirstRunWizardStep::Welcome)
        {
            fire(FirstRunIntent::CloseAtWelcome);
        }
        else if (m_step == FirstRunWizardStep::TemplatePicker)
        {
            fire(FirstRunIntent::CloseAtPicker);
            // Close-at-picker routes back to Welcome — keep the
            // window open so the user sees they've returned to the
            // welcome step, rather than dismissing entirely.
            m_open = true;
        }
    }

    return op;
}

} // namespace Vestige
