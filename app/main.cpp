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
#include "core/engine.h"
#include "core/logger.h"

#include <cstring>
#include <iostream>
#include <string>

namespace
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
        << "  --visual-test           Run the automated visual-test\n"
        << "                          harness and exit. Used by CI.\n"
        << "  --isolate-feature=NAME  Disable one feature for visual-test\n"
        << "                          bisection. NAME ∈ {motion-overlay,\n"
        << "                          bloom, ssao, ibl}.\n"
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

/// @brief Returns true on success, false if parsing failed and main should exit.
bool parseArgs(int argc, char* argv[], Vestige::EngineConfig& config, int& exitCode)
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
        else if (std::strcmp(arg, "--visual-test") == 0)
        {
            config.visualTestMode = true;
            Vestige::Logger::info("Visual test mode enabled via CLI");
        }
        else if (std::strncmp(arg, "--isolate-feature=", 18) == 0)
        {
            config.isolateFeature = arg + 18;
            Vestige::Logger::info("Isolate feature requested: " +
                                  config.isolateFeature);
        }
        else if (std::strcmp(arg, "--biblical-demo") == 0)
        {
            config.biblicalDemo = true;
            Vestige::Logger::info("Biblical demo (Tabernacle) scene requested via CLI");
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

} // namespace

int main(int argc, char* argv[])
{
    Vestige::EngineConfig config;
    config.window.title = "Vestige Engine v0.5.0";
    config.window.width = 1280;
    config.window.height = 720;
    config.window.isVsyncEnabled = true;
    config.assetPath = "assets";

    int exitCode = 0;
    if (!parseArgs(argc, argv, config, exitCode))
    {
        return exitCode;
    }

    Vestige::Engine engine;

    if (!engine.initialize(config))
    {
        Vestige::Logger::fatal("Engine initialization failed — exiting");
        return 1;
    }

    engine.run();
    engine.shutdown();

    return 0;
}
