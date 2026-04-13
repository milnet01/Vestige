/// @file main.cpp
/// @brief Vestige application entry point.
#include "core/engine.h"
#include "core/logger.h"

#include <cstring>

int main(int argc, char* argv[])
{
    Vestige::EngineConfig config;
    config.window.title = "Vestige Engine v0.5.0";
    config.window.width = 1280;
    config.window.height = 720;
    config.window.isVsyncEnabled = true;
    config.assetPath = "assets";

    // Parse command-line arguments
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--visual-test") == 0)
        {
            config.visualTestMode = true;
            Vestige::Logger::info("Visual test mode enabled via CLI");
        }
        else if (std::strncmp(argv[i], "--isolate-feature=", 18) == 0)
        {
            // Diagnostic toggle for bisecting visual regressions. The
            // engine logs and applies the disable in initialize().
            config.isolateFeature = argv[i] + 18;
            Vestige::Logger::info("Isolate feature requested: " +
                                  config.isolateFeature);
        }
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
