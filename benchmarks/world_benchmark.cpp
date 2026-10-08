#include <simforge/world/world.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/simulation/simulation_clock.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef NDEBUG
namespace
{
using timer = std::chrono::steady_clock;
constexpr int measured_runs = 7;
constexpr int update_ticks = 120;
struct Sample
{
    double creation_ms;
    double insertion_ms;
    double update_ms;
    double destruction_ms;
    double checksum;
};
double milliseconds(timer::time_point start, timer::time_point end)
{
    return std::chrono::duration<double, std::milli>{end - start}.count();
}
Sample run(std::size_t count)
{
    using namespace simforge::world;
    World world;
    std::vector<Entity> entities;
    entities.reserve(count); // Harness memory is outside timed entity creation.
    simforge::simulation::SimulationClock clock;
    const auto creation_start = timer::now();
    for (std::size_t i = 0; i < count; ++i) entities.push_back(world.create_entity());
    const auto creation_end = timer::now();
    for (const Entity& entity : entities)
    {
        world.emplace<Transform>(entity);
        world.emplace<Velocity>(entity, Vec3{1, 2, 3});
    }
    const auto insertion_end = timer::now();
    for (int i = 0; i < update_ticks; ++i)
        simforge::simulation::MovementSystem::update(world, clock.fixed_step_seconds());
    const auto update_end = timer::now();
    clock.advance_ticks(update_ticks);

    // Consume and validate every entity, outside the measured update phase.
    double checksum = 0;
    std::size_t visited = 0;
    world.each<Transform>([&](Entity, const Transform& transform)
    {
        ++visited;
        if (!std::isfinite(transform.position.x) || !std::isfinite(transform.position.y) ||
            !std::isfinite(transform.position.z) || std::abs(transform.position.x - 2) > 1e-9 ||
            std::abs(transform.position.y - 4) > 1e-9 ||
            std::abs(transform.position.z - 6) > 1e-9)
            throw std::runtime_error("Benchmark movement validation failed");
        checksum += transform.position.x + transform.position.y + transform.position.z;
    });
    if (world.entity_count() != count || visited != count ||
        world.component_count<Velocity>() != count || clock.elapsed_seconds() != 2)
        throw std::runtime_error("Benchmark setup validation failed");
    const auto destruction_start = timer::now();
    for (const Entity& entity : entities) world.destroy_entity(entity);
    const auto destruction_end = timer::now();
    if (world.entity_count() != 0 || world.component_count<Transform>() != 0 || world.component_count<Velocity>() != 0)
        throw std::runtime_error("Benchmark cleanup validation failed");
    for (const Entity& entity : entities)
        if (world.valid(entity)) throw std::runtime_error("Benchmark stale handle validation failed");
    return {milliseconds(creation_start, creation_end), milliseconds(creation_end, insertion_end),
            milliseconds(insertion_end, update_end), milliseconds(destruction_start, destruction_end), checksum};
}
template<class Member>
double median(const std::array<Sample, measured_runs>& samples, Member member)
{
    std::array<double, measured_runs> values;
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = samples[i].*member;
    std::ranges::sort(values);
    return values[values.size() / 2];
}
} // namespace
#endif

int main()
{
#ifndef NDEBUG
    std::cerr << "Benchmarks require a Release build\n";
    return 1;
#else
    try
    {
        std::cout << "SimForge ECS benchmark: one warmup + " << measured_runs
                  << " measured runs per size; " << update_ticks << " updates at 60 Hz\n";
#ifdef _MSC_FULL_VER
        std::cout << "MSVC _MSC_FULL_VER=" << _MSC_FULL_VER << '\n';
#endif
        std::cout << "No World pre-reservation; growth allocation included. Units: milliseconds.\n"
                  << "entities,create_ms,insert_both_ms,update_120_ticks_ms,update_per_tick_ms,destroy_ms,checksum\n";
        for (std::size_t count : {1'000, 10'000, 100'000})
        {
            (void)run(count); // Untimed-for-report warmup, with validation.
            std::array<Sample, measured_runs> samples;
            double checksum = 0;
            for (auto& sample : samples) { sample = run(count); checksum += sample.checksum; }
            const double update = median(samples, &Sample::update_ms);
            std::cout << std::fixed << std::setprecision(6) << count << ','
                      << median(samples, &Sample::creation_ms) << ','
                      << median(samples, &Sample::insertion_ms) << ',' << update << ',' << update / update_ticks << ','
                      << median(samples, &Sample::destruction_ms) << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
}
