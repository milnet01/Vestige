// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file main.cpp
/// @brief Vestige application entry point.
///
/// Single binary hosts both the editor UI and the runtime walkthrough.
/// CLI flags control which mode the window opens in and whether a custom
/// scene replaces the built-in demo. Invoked by default with no flags,
/// the editor opens on the demo scene (this is what the `vestige-editor`
/// wrapper / `.desktop` entry launches).
#include "cli_args.h"
#include "core/engine.h"
#include "core/logger.h"
#include "platform/folder_dialog.h"
#include "utils/asset_locator.h"

#include <exception>
#include <filesystem>
#include <string>

// Engine::initialize constructs Window, whose constructor THROWS on the three
// commonest startup failures (no GLFW, no window, no GL 4.5 via glad --
// window.cpp:34/63/74). Without this catch those escape main: the process
// aborts with SIGABRT instead of returning the exit code cli_args.h documents,
// and because unwinding is not guaranteed for an exception leaving main,
// ~Engine never runs -- so the log file opened during init is never flushed.
static int runEngine(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    try
    {
        return runEngine(argc, argv);
    }
    catch (const std::exception& e)
    {
        Vestige::Logger::fatal(std::string("Engine initialization failed — ") + e.what());
        Vestige::Logger::closeLogFile();
        return 1;
    }
}

static int runEngine(int argc, char* argv[])
{
    Vestige::EngineConfig config;
    config.window.title = "Vestige Engine v0.5.0";
    config.window.width = 1280;
    config.window.height = 720;
    config.window.isVsyncEnabled = true;
    config.assetPath = "";  // empty ⇒ auto-locate after arg parsing (--assets sets it)

    int exitCode = 0;
    if (!parseArgs(argc, argv, config, exitCode))
    {
        return exitCode;
    }

    // Resolve the asset root in layers:
    //   A. auto-locate — explicit --assets / $VESTIGE_ASSETS, else the assets
    //      shipped next to the binary, else the working directory (this is what
    //      makes the AppImage / tarball / zip run from any directory);
    //   B. a previously-remembered path; then a native folder-picker, whose valid
    //      choice is persisted so the user is not asked again.
    {
        const std::string autoResolved = Vestige::resolveAssetPath(config.assetPath);
        const std::string remembered =
            Vestige::readRememberedAssetPath().value_or(std::string{});
        const auto isValid = [](const std::string& p) {
            return Vestige::isAssetDir(std::filesystem::path(p));
        };
        const Vestige::AssetRootChoice choice = Vestige::chooseAssetRoot(
            autoResolved, remembered, isValid, Vestige::pickAssetFolder);
        if (choice.path.empty())
        {
            Vestige::Logger::fatal(
                "Could not locate the 'assets' directory next to the executable or "
                "in the current directory, and no folder was selected. Pass "
                "--assets <path> or set VESTIGE_ASSETS.");
            return 1;
        }
        if (choice.persist)
        {
            Vestige::writeRememberedAssetPath(choice.path);
        }
        config.assetPath = choice.path;
    }
    Vestige::Logger::info("Asset root: " + config.assetPath);

    Vestige::Engine engine;

    if (!engine.initialize(config))
    {
        Vestige::Logger::fatal("Engine initialization failed — exiting");
        return 1;
    }

    // Offline acoustic bake (--bake-acoustics): initialize() has loaded the scene
    // and built its static physics bodies; bake the sidecar and exit without ever
    // entering the main loop.
    if (config.bakeAcousticsAndExit)
    {
        const bool ok = engine.bakeActiveSceneAcoustics();
        engine.shutdown();
        return ok ? 0 : 1;
    }

    engine.run();
    engine.shutdown();

    return 0;
}
