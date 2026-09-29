// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_dialog.cpp
#include "editor/panels/update_dialog.h"

#include "core/logger.h"
#include "update/update_key.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

namespace
{
constexpr const char* kAskTitle = "Check for Updates?";
constexpr const char* kResultTitle = "Vestige Updates";
constexpr const char* kOfferTitle = "Update Available";
constexpr const char* kProgressTitle = "Updating Vestige";
constexpr const char* kFailedTitle = "Update Failed";

void centreNextWindow(float width)
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Appearing);
}
}  // namespace

UpdateDialog::~UpdateDialog()
{
    m_alive->store(false);
    m_cancel->store(true);
    joinWorker();
}

void UpdateDialog::joinWorker()
{
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void UpdateDialog::initialize(Hooks hooks, Update::Version installed, Update::InstallKind kind,
                              UpdateSettings preferences)
{
    m_hooks = std::move(hooks);
    m_installed = std::move(installed);
    m_kind = kind;
    m_preferences = std::move(preferences);
}

bool UpdateDialog::isBusy() const
{
    return m_stage != Stage::Idle;
}

void UpdateDialog::askAboutAutomaticChecks()
{
    if (m_stage != Stage::Idle)
    {
        return;
    }
    m_stage = Stage::Asking;
    m_openPopup = true;
}

void UpdateDialog::startCheck(bool manual)
{
    if (m_stage == Stage::Checking || m_stage == Stage::Downloading)
    {
        return;
    }
    joinWorker();
    m_manual = manual;
    m_stage = Stage::Checking;
    m_openPopup = manual;  // an automatic check shows nothing until it finds something

    const auto installed = m_installed;
    const auto kind = m_kind;
    const std::string skipped = m_preferences.skippedVersion;
    const auto alive = m_alive;
    const auto post = m_hooks.runOnMainThread;
    m_worker = std::thread([this, installed, kind, skipped, manual, alive, post]() {
        Update::CurlTransport transport("Vestige/" + Update::toString(installed));
        Update::UpdateService service(transport, installed, kind, Update::kUpdatePublicKey);
        Update::CheckResult result = service.check(skipped, manual);
        if (post)
        {
            post([this, alive, result]() {
                if (alive->load())
                {
                    onCheckDone(result);
                }
            });
        }
    });
}

void UpdateDialog::onCheckDone(const Update::CheckResult& result)
{
    using Status = Update::CheckResult::Status;
    if (result.status == Status::Available)
    {
        m_offer = result;
        m_notes = Update::parseNotes(result.notes);
        m_stage = Stage::Offer;
        m_openPopup = true;
        return;
    }
    if (!m_manual)
    {
        if (result.status == Status::Failed)
        {
            Logger::info("Update check failed: " + result.error);
        }
        m_stage = Stage::Idle;
        return;
    }
    m_message = result.status == Status::UpToDate
        ? "You have the latest version of Vestige (" + Update::toString(m_installed) + ")."
        : "Could not reach GitHub to check for updates.\n" + result.error;
    m_stage = Stage::Result;
    m_openPopup = true;
}

void UpdateDialog::beginUpdate()
{
    joinWorker();
    m_installer = Update::installerFor(m_kind, Update::currentInstallContext());
    if (!m_installer)
    {
        onInstallDone("this copy of Vestige cannot update itself in place");
        return;
    }
    m_stage = Stage::Downloading;
    m_openPopup = true;
    m_cancel->store(false);
    m_progress->store(0.0f);

    const auto installed = m_installed;
    const auto kind = m_kind;
    const auto offer = m_offer;
    const auto alive = m_alive;
    const auto cancel = m_cancel;
    const auto progress = m_progress;
    const auto post = m_hooks.runOnMainThread;
    Update::IInstaller* installer = m_installer.get();
    m_worker = std::thread([this, installed, kind, offer, alive, cancel, progress, post,
                            installer]() {
        Update::CurlTransport transport("Vestige/" + Update::toString(installed));
        Update::UpdateService service(transport, installed, kind, Update::kUpdatePublicKey);
        const Update::DownloadResult download = service.download(
            offer, [cancel, progress](std::size_t got, std::size_t total) {
                if (total > 0)
                {
                    progress->store(static_cast<float>(got) / static_cast<float>(total));
                }
                return !cancel->load();
            });
        std::string error = download.error;
        if (error.empty())
        {
            error = installer->apply(download.bytes, Update::toString(offer.release.version));
        }
        if (post)
        {
            post([this, alive, error]() {
                if (alive->load())
                {
                    onInstallDone(error);
                }
            });
        }
    });
}

void UpdateDialog::onInstallDone(const std::string& error)
{
    if (!error.empty())
    {
        m_message = error == "cancelled" ? "The update was cancelled." : error;
        m_stage = Stage::Failed;
        m_openPopup = true;
        return;
    }
    // In place: start the new version. AppImage: execv does not return on
    // success. Windows: the helper waits for us to exit.
    const std::string relaunchError = m_installer->relaunch();
    if (!relaunchError.empty())
    {
        m_message = relaunchError;
        m_stage = Stage::Failed;
        m_openPopup = true;
        return;
    }
    if (m_hooks.requestExit)
    {
        m_hooks.requestExit();
    }
}

// ---------------------------------------------------------------- drawing

void UpdateDialog::draw()
{
    switch (m_stage)
    {
        case Stage::Asking:      drawAsk(); break;
        case Stage::Result:      drawResult(); break;
        case Stage::Offer:       drawOffer(); break;
        case Stage::Checking:
            if (m_manual)
            {
                if (m_openPopup)
                {
                    ImGui::OpenPopup(kResultTitle);
                    m_openPopup = false;
                }
                centreNextWindow(360.0f);
                if (ImGui::BeginPopupModal(kResultTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Checking GitHub for a newer version...");
                    ImGui::EndPopup();
                }
            }
            break;
        case Stage::Downloading: drawProgress(); break;
        case Stage::Failed:      drawFailed(); break;
        case Stage::Idle:        break;
    }
}

void UpdateDialog::drawAsk()
{
    if (m_openPopup)
    {
        ImGui::OpenPopup(kAskTitle);
        m_openPopup = false;
    }
    centreNextWindow(440.0f);
    if (ImGui::BeginPopupModal(kAskTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Check for a newer version of Vestige automatically when the editor "
                           "starts? It contacts GitHub once per start. You can change this in "
                           "Settings, and Help > Check for Updates always works.");
        ImGui::Spacing();
        auto answer = [this](UpdateCheckMode mode) {
            m_preferences.mode = mode;
            if (m_hooks.savePreferences)
            {
                m_hooks.savePreferences(m_preferences);
            }
            m_stage = Stage::Idle;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Yes, check automatically"))
        {
            answer(UpdateCheckMode::On);
        }
        ImGui::SameLine();
        if (ImGui::Button("No"))
        {
            answer(UpdateCheckMode::Off);
        }
        ImGui::EndPopup();
    }
}

void UpdateDialog::drawResult()
{
    if (m_openPopup)
    {
        ImGui::OpenPopup(kResultTitle);
        m_openPopup = false;
    }
    centreNextWindow(420.0f);
    if (ImGui::BeginPopupModal(kResultTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK"))
        {
            m_stage = Stage::Idle;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void UpdateDialog::drawOffer()
{
    if (m_openPopup)
    {
        ImGui::OpenPopup(kOfferTitle);
        m_openPopup = false;
    }
    centreNextWindow(640.0f);
    if (!ImGui::BeginPopupModal(kOfferTitle, nullptr, ImGuiWindowFlags_None))
    {
        return;
    }
    const std::string offered = Update::toString(m_offer.release.version);
    ImGui::Text("Vestige %s is available. You have %s.", offered.c_str(),
                Update::toString(m_installed).c_str());
    if (m_offer.skipped)
    {
        ImGui::TextDisabled("You chose to skip this version earlier.");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("What changed:");

    const float buttonsHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("notes", ImVec2(0.0f, std::max(200.0f, 420.0f - buttonsHeight)),
                      ImGuiChildFlags_Borders);
    if (!m_offer.notesLoaded)
    {
        ImGui::TextWrapped("The release notes could not be loaded. The release page has them.");
    }
    for (const auto& line : m_notes)
    {
        switch (line.kind)
        {
            case Update::NotesLine::Kind::Heading:
                ImGui::Spacing();
                ImGui::TextWrapped("%s", line.text.c_str());
                ImGui::Separator();
                break;
            case Update::NotesLine::Kind::Bullet:
                ImGui::Indent(static_cast<float>(line.indent) * 16.0f);
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", line.text.c_str());
                ImGui::Unindent(static_cast<float>(line.indent) * 16.0f);
                break;
            case Update::NotesLine::Kind::Paragraph:
                ImGui::Indent(static_cast<float>(line.indent) * 16.0f);
                ImGui::TextWrapped("%s", line.text.c_str());
                ImGui::Unindent(static_cast<float>(line.indent) * 16.0f);
                break;
            case Update::NotesLine::Kind::Blank:
                ImGui::Spacing();
                break;
        }
    }
    ImGui::EndChild();

    const bool inPlace = m_offer.asset.has_value();
    if (!inPlace)
    {
        ImGui::TextWrapped("This copy of Vestige cannot update itself in place. Download the "
                           "new version from the release page.");
    }
    if (ImGui::Button("Later"))
    {
        m_stage = Stage::Idle;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Skip this version"))
    {
        m_preferences.skippedVersion = offered;
        if (m_hooks.savePreferences)
        {
            m_hooks.savePreferences(m_preferences);
        }
        m_stage = Stage::Idle;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (inPlace)
    {
        if (ImGui::Button("Update now"))
        {
            ImGui::CloseCurrentPopup();
            m_stage = Stage::Idle;
            auto go = [this]() { beginUpdate(); };
            if (m_hooks.afterUnsavedCheck)
            {
                m_hooks.afterUnsavedCheck(go);
            }
            else
            {
                go();
            }
        }
    }
    else if (ImGui::Button("Open download page"))
    {
        if (m_hooks.openUrl)
        {
            m_hooks.openUrl(m_offer.release.pageUrl.empty() ? Update::kReleasesPageUrl
                                                             : m_offer.release.pageUrl);
        }
        m_stage = Stage::Idle;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void UpdateDialog::drawProgress()
{
    if (m_openPopup)
    {
        ImGui::OpenPopup(kProgressTitle);
        m_openPopup = false;
    }
    centreNextWindow(420.0f);
    if (ImGui::BeginPopupModal(kProgressTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Downloading and checking the update...");
        ImGui::ProgressBar(m_progress->load(), ImVec2(380.0f, 0.0f));
        if (ImGui::Button("Cancel"))
        {
            m_cancel->store(true);
        }
        ImGui::EndPopup();
    }
}

void UpdateDialog::drawFailed()
{
    if (m_openPopup)
    {
        ImGui::OpenPopup(kFailedTitle);
        m_openPopup = false;
    }
    centreNextWindow(440.0f);
    if (ImGui::BeginPopupModal(kFailedTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("The update was not installed. Your current version is unchanged.");
        ImGui::TextWrapped("%s", m_message.c_str());
        if (ImGui::Button("Open download page") && m_hooks.openUrl)
        {
            m_hooks.openUrl(Update::kReleasesPageUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            m_stage = Stage::Idle;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace Vestige
