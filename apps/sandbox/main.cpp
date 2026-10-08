#include <simforge/core/engine.hpp>
#include <simforge/platform/window.hpp>

#include <iostream>
#include <chrono>
#include <string_view>

int main(int argc, char* argv[])
{
    simforge::Engine engine;
    if (argc == 2 && std::string_view{argv[1]} == "--headless")
    {
        engine.initialize();
        engine.advance_simulation_ticks(600);
        std::cout << "Ticks: " << engine.simulation_clock().tick_count()
                  << " | simulated seconds: " << engine.simulation_clock().elapsed_seconds() << '\n';
        return 0;
    }
    if (argc != 1)
    {
        std::cerr << "Usage: simforge_sandbox [--headless]\n";
        return 1;
    }
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
        const auto ticks = engine.advance_simulation(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>{delta_time}));
        // Future simulation systems execute one update per returned tick,
        // always using simulation_clock().fixed_step_seconds().
        (void)ticks;

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
            std::cout << "Simulation ticks: " << engine.simulation_clock().tick_count() << '\n';
        }
    }

    window.destroy();
    engine.shutdown();

    return 0;
}
