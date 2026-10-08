#include <simforge/core/engine.hpp>

#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    simforge::Engine engine;

    engine.initialize();

    std::cout << "SimForge Engine initialized\n";

    while (engine.running())
    {
        const double delta_time = engine.begin_frame();

        std::cout
            << "Frame " << engine.frame_count()
            << " | dt: " << delta_time
            << " s\n";

        std::this_thread::sleep_for(
            std::chrono::milliseconds(16)
        );

        engine.end_frame();

        if (engine.frame_count() >= 5)
        {
            engine.request_exit();
        }
    }

    std::cout
        << "Engine uptime: "
        << engine.uptime_seconds()
        << " s\n";

    engine.shutdown();

    return 0;
}