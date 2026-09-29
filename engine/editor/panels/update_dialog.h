// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_dialog.h
/// @brief The editor's self-update UI (3D_E-0729, spec §4.7-§4.8): the
///        one-time "check automatically?" question, the offer with what
///        changed, download progress, and install.
#pragma once

#include "core/settings.h"
#include "update/notes_markup.h"
#include "update/update_installer.h"
#include "update/update_service.h"
#include "update/update_version.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace Vestige
{

class UpdateDialog
{
public:
    /// @brief What the dialog needs from the rest of the editor.
    struct Hooks
    {
        /// Marshal work back to the main thread (JobSystem::runOnMainThread).
        std::function<void(std::function<void()>)> runOnMainThread;
        /// The preferences as saved now (SettingsEditor::applied().updates).
        /// Read before every change, so the Settings window's edits are kept.
        std::function<UpdateSettings()> loadPreferences;
        /// Persist the preferences (SettingsEditor::commitUpdatePreferences).
        std::function<void(const UpdateSettings&)> savePreferences;
        /// Run an action once unsaved scene changes are dealt with.
        std::function<void(std::function<void()>)> afterUnsavedCheck;
        /// Quit the editor (after the Windows swap helper has started).
        std::function<void()> requestExit;
        /// Open a web page in the user's browser.
        std::function<void(const std::string&)> openUrl;
    };

    UpdateDialog() = default;
    ~UpdateDialog();
    UpdateDialog(const UpdateDialog&) = delete;
    UpdateDialog& operator=(const UpdateDialog&) = delete;

    void initialize(Hooks hooks, Update::Version installed, Update::InstallKind kind);

    /// @brief Start a check. @a manual (Help menu) reports every outcome and
    ///        offers a skipped version; an automatic check is silent unless
    ///        there is something to offer.
    void startCheck(bool manual);

    /// @brief Show the one-time "check automatically?" question.
    void askAboutAutomaticChecks();

    /// @brief True while a check or download is running or a window is open.
    bool isBusy() const;

    /// @brief Draw whichever window is showing. Call once per frame.
    void draw();

private:
    enum class Stage
    {
        Idle,
        Asking,       ///< The one-time question.
        Checking,     ///< Background check running.
        Result,       ///< Manual check: "up to date" / "could not check".
        Offer,        ///< A newer release, with notes.
        Confirming,   ///< Waiting on the Unsaved Changes modal before updating.
        Downloading,  ///< Download + verify + apply running.
        Failed,       ///< Download or install failed.
    };

    void onCheckDone(const Update::CheckResult& result);
    void beginUpdate();
    void onInstallDone(const std::string& error);
    void drawAsk();
    void drawResult();
    void drawOffer();
    void drawProgress();
    void drawFailed();
    void joinWorker();
    UpdateSettings currentPreferences() const;
    void changePreferences(const std::function<void(UpdateSettings&)>& change);

    Hooks m_hooks;
    Update::Version m_installed;
    Update::InstallKind m_kind = Update::InstallKind::None;

    Stage m_stage = Stage::Idle;
    bool m_manual = false;
    bool m_openPopup = false;
    std::string m_message;
    Update::CheckResult m_offer;
    std::vector<Update::NotesLine> m_notes;
    std::unique_ptr<Update::IInstaller> m_installer;

    std::thread m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<float>> m_progress = std::make_shared<std::atomic<float>>(0.0f);
    /// Cleared on destruction so a late worker callback does nothing.
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace Vestige
