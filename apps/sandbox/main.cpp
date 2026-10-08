#include <simforge/core/engine.hpp>
#include <simforge/platform/window.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>
#include <simforge/simulation/input_recording.hpp>
#include <simforge/warehouse/warehouse_simulation.hpp>

#include <charconv>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <iomanip>
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
    std::optional<std::filesystem::path> scenario, record, replay;
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
        if (argument == "--scenario" || argument == "--record" || argument == "--replay")
        {
            auto& destination = argument == "--scenario" ? options.scenario : argument == "--record" ? options.record : options.replay;
            if (destination) throw std::invalid_argument("Duplicate file option");
            if (++i == argc) throw std::invalid_argument("Missing file option value");
            destination = argv[i];
        }
        else if (argument == "--headless" && !options.headless) options.headless = true;
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
    if ((options.scenario || options.record || options.replay) && !options.headless)
        throw std::invalid_argument("Scenario and recording workflows require --headless");
    if ((options.scenario || options.record || options.replay) && (entities_seen || ticks_seen))
        throw std::invalid_argument("Scenario/recording tick budgets conflict with --entities/--ticks");
    if (options.record && (options.replay || !options.scenario))
        throw std::invalid_argument("--record requires --scenario and conflicts with --replay");
    return options;
}

int scenario_workflow(const Options& options)
{
    using namespace simforge;
    std::optional<simulation::InputRecording> recording;
    if (options.replay) recording = simulation::load_recording(*options.replay);
    const auto definition = options.scenario ? scenario::load_scenario(*options.scenario)
        : scenario::parse_scenario(recording->scenario_identity);
    std::cout << "Validated scenario: " << definition.name << '\n';
    if (std::holds_alternative<scenario::WarehouseConfig>(definition.domain))
    {
        if (options.record || options.replay) throw std::invalid_argument("Warehouse command recording/replay is not implemented; use movement recording");
        using timer = std::chrono::steady_clock;
        const auto begin = timer::now();
        warehouse::WarehouseSimulation simulation{definition};
        const auto initialized = timer::now();
        simulation.run();
        const auto executed = timer::now();
        const auto metrics = simulation.metrics();
        const auto finalized = timer::now();
        const auto& config = std::get<scenario::WarehouseConfig>(definition.domain);
        std::cout << std::fixed << std::setprecision(3)
                  << "SimForge Warehouse Simulation\n"
                  << "Tick rate: " << definition.simulation.tick_rate << " Hz\n"
                  << "Duration: " << definition.simulation.duration_seconds << " seconds ("
                  << static_cast<double>(definition.simulation.duration_seconds) / 3600 << " simulated hours)\n"
                  << "Loading bays: " << config.loading_bays << " | Workers: " << config.workers << " | Forklifts: " << config.forklifts << '\n'
                  << "Arrival interval: " << config.shipment_interval_seconds << " seconds | Processing: " << config.processing_duration_seconds << " seconds\n"
                  << "Arrivals: " << metrics.arrived << " | Started: " << metrics.started << " | Completed: " << metrics.completed
                  << " | Waiting: " << metrics.waiting << " | In progress: " << metrics.in_progress << '\n';
        const auto print_metric = [](const char* label, std::optional<double> value, double factor, const char* unit)
        {
            std::cout << label << ": ";
            if (value) std::cout << *value * factor << ' ' << unit;
            else std::cout << "unavailable";
            std::cout << '\n';
        };
        print_metric("Throughput", metrics.throughput_per_hour, 1, "shipments/hour");
        print_metric("Average queue wait (started)", metrics.average_wait_seconds, 1.0 / 60, "minutes");
        print_metric("Maximum queue wait (started)", metrics.maximum_wait_seconds, 1.0 / 60, "minutes");
        print_metric("Average processing (completed)", metrics.average_processing_seconds, 1, "seconds");
        std::cout << "Total processing (completed): " << metrics.completed_processing_seconds << " seconds\n";
        print_metric("Loading bay utilization", metrics.loading_bay_utilization, 100, "%");
        print_metric("Worker utilization", metrics.worker_utilization, 100, "%");
        print_metric("Forklift utilization", metrics.forklift_utilization, 100, "%");
        std::cout << "Initialization: " << std::chrono::duration<double, std::milli>{initialized - begin}.count() << " ms\n"
                  << "Execution: " << std::chrono::duration<double, std::milli>{executed - initialized}.count() << " ms\n"
                  << "Metric finalization: " << std::chrono::duration<double, std::milli>{finalized - executed}.count() << " ms\n";
        return 0;
    }
    world::World world;
    simulation::SimulationClock clock;
    if (recording) simulation::replay_recording(definition, *recording, world, clock);
    else
    {
        simulation::restore_snapshot(world, clock, scenario::initial_state(definition));
        if (options.record)
        {
            const auto initial = world.capture_snapshot();
            if (initial.velocities.empty()) throw std::invalid_argument("Recording demonstration requires at least one Velocity component");
            const auto target = initial.velocities.front().entity;
            simulation::InputRecorder recorder{definition, world, clock};
            recorder.submit(simulation::SetEntityVelocity{target, {2, 0.5, -0.25}});
            recorder.submit(simulation::PauseSimulation{});
            recorder.submit(simulation::StepSimulation{});
            recorder.submit(simulation::ResumeSimulation{});
            recorder.advance_to(std::max<std::uint64_t>(1, scenario::total_ticks(definition) / 2));
            recorder.submit(simulation::SetEntityVelocity{target, {-1, 0, 0}});
            recorder.advance_to(scenario::total_ticks(definition));
            recording = recorder.capture_recording();
            // Verify outcomes before writing the recording.
            world::World replay_world; simulation::SimulationClock replay_clock;
            simulation::replay_recording(definition, *recording, replay_world, replay_clock);
            simulation::save_recording(*options.record, *recording);
            std::cout << "Recorded " << recording->commands.size() << " accepted commands; replay verified.\n";
        }
        else simulation::replay_fixed_ticks(world, clock, scenario::total_ticks(definition));
    }
    if (options.replay) std::cout << "Replay verified against recorded final state.\n";
    double checksum = 0;
    world.each_components<world::Transform>([&](const auto& transform) { checksum += transform.position.x; });
    std::cout << "Executable domain: movement\nEntities: " << world.entity_count() << " | Ticks: " << clock.tick_count()
              << " | simulated seconds: " << clock.elapsed_seconds() << "\nPosition checksum: " << checksum << '\n';
    return 0;
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
    if (options.scenario || options.replay) return scenario_workflow(options);
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
                  << "\nUsage: simforge_sandbox [--headless [--entities N] [--ticks N]]\n"
                  << "  --headless --scenario FILE [--record OUTPUT]\n"
                  << "  --headless --replay FILE [--scenario FILE]\n";
        return 1;
    }
}
