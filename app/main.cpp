/// @file main.cpp
/// @brief Vestige application entry point.
#include "core/engine.h"
#include "core/logger.h"

int main()
{
    Vestige::EngineConfig config;
    config.window.title = "Vestige Engine v0.1.0";
    config.window.width = 1280;
    config.window.height = 720;
    config.window.isVsyncEnabled = true;
    config.assetPath = "assets";

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
