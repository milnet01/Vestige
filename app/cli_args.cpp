// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cli_args.cpp
/// @brief Implementation of the Vestige CLI parser (see cli_args.h).

#include "cli_args.h"

#include "core/logger.h"

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace Vestige
{

namespace
{

/// The accepted --isolate-feature names, comma-separated, for messages.
std::string isolateFeatureNameList()
{
    std::string list;
    for (const char* name : EngineConfig::ISOLATE_FEATURE_NAMES)
    {
        list += list.empty() ? name : std::string(", ") + name;
    }
    return list;
}

/// Fetches the value that must follow @a argv[i]. When it is missing, prints
/// what is needed and the --help hint, sets @a exitCode to 2 and returns null.
const char* takeValue(int argc, char* argv[], int& i, const char* what, int& exitCode)
{
    if (i + 1 >= argc)
    {
        std::cerr << argv[i] << " requires " << what << "\n"
                  << "Run '" << argv[0] << " --help' for usage.\n";
        exitCode = 2;
        return nullptr;
    }
    return argv[++i];
}

/// True when @a arg starts with @a prefix.
bool hasPrefix(const char* arg, std::string_view prefix)
{
    return std::strncmp(arg, prefix.data(), prefix.size()) == 0;
}

constexpr std::string_view kIsolateFeaturePrefix = "--isolate-feature=";
constexpr std::string_view kProfileLogPrefix     = "--profile-log=";

bool isIsolateFeatureName(const std::string& name)
{
    for (const char* known : EngineConfig::ISOLATE_FEATURE_NAMES)
    {
        if (name == known)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void printUsage(const char* argv0)
{
    std::cout
        << "Vestige — 3D exploration engine\n"
        << "\n"
        << "Usage: " << argv0 << " [OPTIONS]\n"
        << "\n"
        << "With no options the editor opens on the built-in demo scene.\n"
        << "\n"
        << "Options:\n"
        << "  --editor                Open in editor mode (default).\n"
        << "  --play                  Skip the editor UI and start the\n"
        << "                          first-person walkthrough directly.\n"
        << "                          Inside the engine, Esc toggles back\n"
        << "                          to editor mode.\n"
        << "  --player                Play as a shipped game would: like\n"
        << "                          --play, but Esc opens the pause menu\n"
        << "                          (Resume / Settings / Quit) instead of\n"
        << "                          the editor.\n"
        << "  --scene PATH            Load a .scene file on startup,\n"
        << "                          replacing the built-in demo. PATH\n"
        << "                          is resolved against the current\n"
        << "                          directory first, then\n"
        << "                          <assets>/scenes/PATH.\n"
        << "  --assets PATH           Override the asset directory\n"
        << "                          (default: \"assets\").\n"
        << "  --bake-acoustics PATH   Load the scene at PATH, bake its\n"
        << "                          acoustic probe IRs into its\n"
        << "                          <scene>_acoustics/ sidecar, and exit.\n"
        << "                          Needs a display (or xvfb) like\n"
        << "                          --visual-test. For asset pipelines.\n"
        << "  --visual-test           Run the automated visual-test\n"
        << "                          harness and exit. Used by CI.\n"
        << "  --demo-flythrough       Fly the camera along a built-in path\n"
        << "                          over the meadow with no input, then\n"
        << "                          exit. For recording demo videos.\n"
        << "  --demo-capture DIR      Like --demo-flythrough, but step time\n"
        << "                          by exactly 1/30 s per frame and save\n"
        << "                          every frame to DIR as a numbered PNG,\n"
        << "                          so the video is smooth at any speed.\n"
        << "  --isolate-feature=NAME  Disable one feature for visual-test\n"
        << "                          bisection. NAME is one of:\n"
        << "                          " << isolateFeatureNameList() << ".\n"
        << "  --profile-log[=PATH]    Enable the profiler and append ~1 Hz\n"
        << "                          per-pass CPU/GPU timings to PATH as CSV\n"
        << "                          (default: vestige_profile_<ts>.csv in the\n"
        << "                          current directory). For bottleneck\n"
        << "                          analysis of the benchmark scene.\n"
        << "  --no-vsync              Disable vertical sync so the frame rate runs\n"
        << "                          uncapped. For benchmarking: with vsync on,\n"
        << "                          FPS reports the display's refresh rate, not\n"
        << "                          what the frame actually costs.\n"
        << "  --material-demo         Open the legacy material-test-cube demo\n"
        << "                          (PBR / glass / emissive / skeletal-\n"
        << "                          animation bench) instead of the default\n"
        << "                          natural meadow scene.\n"
        << "  --biblical-demo         Maintainer-only: open the built-in\n"
        << "                          Tabernacle scene instead of the\n"
        << "                          neutral demo. Requires the private\n"
        << "                          assets/textures/tabernacle/ set,\n"
        << "                          which is not in public clones.\n"
        << "  -h, --help              Print this message and exit.\n"
        << "\n"
        << "Examples:\n"
        << "  " << argv0 << "                   # open editor with demo scene\n"
        << "  " << argv0 << " --play            # first-person walkthrough\n"
        << "  " << argv0 << " --scene my.scene  # open a saved scene\n"
        << "\n"
        << "Full docs: ARCHITECTURE.md, README.md\n";
}

bool parseArgs(int argc, char* argv[], EngineConfig& config, int& exitCode)
{
    for (int i = 1; i < argc; i++)
    {
        const char* arg = argv[i];

        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0)
        {
            printUsage(argv[0]);
            exitCode = 0;
            return false;
        }
        else if (std::strcmp(arg, "--editor") == 0)
        {
            // Default — explicit form is accepted for symmetry with --play
            // and so desktop launchers can be unambiguous. It restores the
            // whole default, so it also undoes --player's game screens.
            config.startInPlayMode   = false;
            config.enableGameScreens = false;
        }
        else if (std::strcmp(arg, "--play") == 0)
        {
            config.startInPlayMode = true;
        }
        else if (std::strcmp(arg, "--player") == 0)
        {
            config.startInPlayMode   = true;
            config.enableGameScreens = true;
        }
        else if (std::strcmp(arg, "--scene") == 0)
        {
            const char* path = takeValue(argc, argv, i, "a path argument", exitCode);
            if (path == nullptr)
            {
                return false;
            }
            if (!config.startupScene.empty())
            {
                std::cerr << "--scene: a startup scene was already given\n";
                exitCode = 2;
                return false;
            }
            config.startupScene = path;
        }
        else if (std::strcmp(arg, "--assets") == 0)
        {
            const char* path = takeValue(argc, argv, i, "a path argument", exitCode);
            if (path == nullptr)
            {
                return false;
            }
            config.assetPath = path;
        }
        else if (std::strcmp(arg, "--bake-acoustics") == 0)
        {
            const char* path = takeValue(argc, argv, i, "a scene path argument", exitCode);
            if (path == nullptr)
            {
                return false;
            }
            if (!config.startupScene.empty())
            {
                std::cerr << "--bake-acoustics: a startup scene was already given\n";
                exitCode = 2;
                return false;
            }
            config.startupScene         = path;
            config.bakeAcousticsAndExit = true;
            Logger::info("Acoustic bake requested for scene: " +
                         config.startupScene);
        }
        else if (std::strcmp(arg, "--visual-test") == 0)
        {
            config.visualTestMode = true;
            Logger::info("Visual test mode enabled via CLI");
        }
        else if (std::strcmp(arg, "--demo-flythrough") == 0)
        {
            config.demoFlythroughMode = true;
            Logger::info("Demo fly-through enabled via CLI");
        }
        else if (std::strcmp(arg, "--demo-capture") == 0)
        {
            const char* dir = takeValue(argc, argv, i, "a directory argument", exitCode);
            if (dir == nullptr)
            {
                return false;
            }
            config.demoFlythroughMode = true;
            config.demoCaptureDir = dir;
            Logger::info("Demo capture enabled: " + config.demoCaptureDir);
        }
        else if (hasPrefix(arg, kIsolateFeaturePrefix))
        {
            config.isolateFeature = arg + kIsolateFeaturePrefix.size();
            if (!isIsolateFeatureName(config.isolateFeature))
            {
                std::cerr << "--isolate-feature= needs one of: "
                          << isolateFeatureNameList() << "\n";
                exitCode = 2;
                return false;
            }
            Logger::info("Isolate feature requested: " + config.isolateFeature);
        }
        else if (std::strcmp(arg, "--profile-log") == 0)
        {
            // Bare `--profile-log` (no `=PATH`): store the shared sentinel;
            // Engine::initialize() resolves it to a timestamped default path.
            config.profileLogPath = EngineConfig::PROFILE_LOG_DEFAULT_SENTINEL;
            Logger::info("Profiler CSV logging enabled (default path)");
        }
        else if (hasPrefix(arg, kProfileLogPrefix))
        {
            config.profileLogPath = arg + kProfileLogPrefix.size();
            if (config.profileLogPath.empty())
            {
                std::cerr << "--profile-log= requires a non-empty path\n";
                exitCode = 2;
                return false;
            }
            Logger::info("Profiler CSV logging enabled: " + config.profileLogPath);
        }
        else if (std::strcmp(arg, "--no-vsync") == 0)
        {
            // Benchmarking aid: vsync pins the frame to the display's refresh, so
            // frame/FPS numbers in the --profile-log CSV read as the cap rather than
            // as what the frame actually costs. Uncapped, a pass optimisation shows
            // up in FPS instead of only in the per-pass GPU timings.
            config.window.isVsyncEnabled = false;
            Logger::info("VSync disabled via CLI (uncapped frame rate)");
        }
        else if (std::strcmp(arg, "--biblical-demo") == 0)
        {
            config.biblicalDemo = true;
            Logger::info("Biblical demo (Tabernacle) scene requested via CLI");
        }
        else if (std::strcmp(arg, "--material-demo") == 0)
        {
            config.materialDemo = true;
            Logger::info("Material-test-cube demo scene requested via CLI");
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::cerr << "Run '" << argv[0] << " --help' for usage.\n";
            exitCode = 2;
            return false;
        }
    }
    return true;
}

} // namespace Vestige
