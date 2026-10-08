#include <simforge/core/engine.hpp>
#include <simforge/platform/window.hpp>

#include <iostream>

int main()
{
    simforge::Engine engine;
    simforge::platform::Window window;

    engine.initialize();

    window.create(
        "SimForge Sandbox",
        1280,
        720
    );

    std::cout << "SimForge Engine initialized\n";

    while (engine.running() && window.is_open())
    {
        const double delta_time = engine.begin_frame();

        window.poll_events();

        if (!window.is_open())
        {
            engine.request_exit();
        }

        engine.end_frame();

        if (engine.frame_count() % 300 == 0)
        {
            std::cout
                << "Frame " << engine.frame_count()
                << " | dt: " << delta_time
                << " s\n";
        }
    }

    window.destroy();
    engine.shutdown();

    return 0;
}