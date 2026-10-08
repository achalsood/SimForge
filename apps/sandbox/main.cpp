#include <simforge/core/engine.hpp>
#include <simforge/platform/window.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
struct Options
{
    bool headless{false};
    std::uint64_t entities{10'000};
    std::uint64_t ticks{600};
};

std::uint64_t parse_count(std::string_view value)
{
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("Counts must be unsigned decimal integers in range");
    return result;
}

Options parse_options(int argc, char* argv[])
{
    Options options;
    bool entities_seen = false;
    bool ticks_seen = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument{argv[i]};
        if (argument == "--headless" && !options.headless) options.headless = true;
        else if (argument == "--entities" && !entities_seen)
        {
            if (++i == argc) throw std::invalid_argument("Missing --entities value");
            options.entities = parse_count(argv[i]);
            entities_seen = true;
        }
        else if (argument == "--ticks" && !ticks_seen)
        {
            if (++i == argc) throw std::invalid_argument("Missing --ticks value");
            options.ticks = parse_count(argv[i]);
            ticks_seen = true;
        }
        else throw std::invalid_argument("Unknown or duplicate argument");
    }
    if ((entities_seen || ticks_seen) && !options.headless)
        throw std::invalid_argument("--entities and --ticks require --headless");
    if (options.entities > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Entity count exceeds entity index space");
    return options;
}

void populate(simforge::world::World& world, std::uint64_t count)
{
    world.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<simforge::world::Transform>(entity);
        world.emplace<simforge::world::Velocity>(entity, simforge::world::Vec3{1.0, 0.5, -0.25});
    }
}

int run(const Options& options)
{
    using timer = std::chrono::steady_clock;
    simforge::Engine engine;
    engine.initialize();
    simforge::world::World world;
    const auto setup_start = timer::now();
    populate(world, options.headless ? options.entities : 1);
    const auto setup_end = timer::now();
    if (options.headless)
    {
        const auto update_start = timer::now();
        for (std::uint64_t tick = 0; tick < options.ticks; ++tick)
        {
            simforge::simulation::MovementSystem::update(world, engine.simulation_clock().fixed_step_seconds());
            engine.advance_simulation_ticks(1);
        }
        const auto update_end = timer::now();
        double checksum = 0.0;
        world.each<simforge::world::Transform>([&](simforge::world::Entity, const auto& transform)
        {
            checksum += transform.position.x;
        });
        std::cout << "Entities: " << world.entity_count()
                  << " | Ticks: " << engine.simulation_clock().tick_count()
                  << " | simulated seconds: " << engine.simulation_clock().elapsed_seconds()
                  << "\nSetup wall seconds: " << std::chrono::duration<double>{setup_end - setup_start}.count()
                  << "\nSimulation update wall seconds: " << std::chrono::duration<double>{update_end - update_start}.count()
                  << "\nPosition checksum: " << checksum << '\n';
        return 0;
    }

    simforge::platform::Window window;
    window.create("SimForge Sandbox", 1280, 720);
    std::cout << "SimForge Engine initialized\n";
    while (engine.running() && window.is_open())
    {
        const double delta_time = engine.begin_frame();
        window.poll_events();
        if (!window.is_open()) engine.request_exit();
        const auto ticks = engine.advance_simulation(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>{delta_time}));
        for (std::uint64_t tick = 0; tick < ticks; ++tick)
            simforge::simulation::MovementSystem::update(world, engine.simulation_clock().fixed_step_seconds());
        engine.end_frame();
        if (engine.frame_count() % 300 == 0)
            std::cout << "Frame " << engine.frame_count() << " | dt: " << delta_time
                      << " s | Simulation ticks: " << engine.simulation_clock().tick_count() << '\n';
    }
    window.destroy();
    engine.shutdown();
    return 0;
}
} // namespace

int main(int argc, char* argv[])
{
    try { return run(parse_options(argc, argv)); }
    catch (const std::exception& error)
    {
        std::cerr << "SimForge sandbox: " << error.what()
                  << "\nUsage: simforge_sandbox [--headless [--entities N] [--ticks N]]\n";
        return 1;
    }
}
