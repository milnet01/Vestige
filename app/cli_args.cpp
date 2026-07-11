// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cli_args.cpp
/// @brief Implementation of the Vestige CLI parser (see cli_args.h).

#include "cli_args.h"

#include "core/logger.h"

#include <cstring>
#include <iostream>
#include <string>

namespace Vestige
{

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
        << "  --isolate-feature=NAME  Disable one feature for visual-test\n"
        << "                          bisection. NAME ∈ {motion-overlay,\n"
        << "                          bloom, ssao, ibl}.\n"
        << "  --profile-log[=PATH]    Enable the profiler and append ~1 Hz\n"
        << "                          per-pass CPU/GPU timings to PATH as CSV\n"
        << "                          (default: vestige_profile_<ts>.csv in the\n"
        << "                          current directory). For bottleneck\n"
        << "                          analysis of the benchmark scene.\n"
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
    // Bare `--profile-log` (no `=PATH`) uses this sentinel; ProfileLog resolves
    // it to a timestamped default at open time (see EngineConfig::profileLogPath).
    static constexpr const char* kProfileLogDefaultSentinel = "\x01" "default";

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
            // and so desktop launchers can be unambiguous.
            config.startInPlayMode = false;
        }
        else if (std::strcmp(arg, "--play") == 0)
        {
            config.startInPlayMode = true;
        }
        else if (std::strcmp(arg, "--scene") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "--scene requires a path argument\n";
                exitCode = 2;
                return false;
            }
            config.startupScene = argv[++i];
        }
        else if (std::strcmp(arg, "--assets") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "--assets requires a path argument\n";
                exitCode = 2;
                return false;
            }
            config.assetPath = argv[++i];
        }
        else if (std::strcmp(arg, "--bake-acoustics") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "--bake-acoustics requires a scene path argument\n";
                exitCode = 2;
                return false;
            }
            config.startupScene         = argv[++i];
            config.bakeAcousticsAndExit = true;
            Logger::info("Acoustic bake requested for scene: " +
                         config.startupScene);
        }
        else if (std::strcmp(arg, "--visual-test") == 0)
        {
            config.visualTestMode = true;
            Logger::info("Visual test mode enabled via CLI");
        }
        else if (std::strncmp(arg, "--isolate-feature=", 18) == 0)
        {
            config.isolateFeature = arg + 18;
            Logger::info("Isolate feature requested: " + config.isolateFeature);
        }
        else if (std::strcmp(arg, "--profile-log") == 0)
        {
            config.profileLogPath = kProfileLogDefaultSentinel;
            Logger::info("Profiler CSV logging enabled (default path)");
        }
        else if (std::strncmp(arg, "--profile-log=", 14) == 0)
        {
            config.profileLogPath = arg + 14;
            if (config.profileLogPath.empty())
            {
                std::cerr << "--profile-log= requires a non-empty path\n";
                exitCode = 2;
                return false;
            }
            Logger::info("Profiler CSV logging enabled: " + config.profileLogPath);
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
