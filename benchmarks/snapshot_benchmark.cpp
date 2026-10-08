#include <simforge/simulation/simulation_snapshot.hpp>
#include <simforge/world/world.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
namespace
{
using timer = std::chrono::steady_clock;
constexpr int measured_runs = 7;
struct Sample
{
    double capture_ms, restore_ms, serialize_ms, deserialize_ms;
    std::size_t bytes;
    double checksum;
};
double milliseconds(timer::time_point start, timer::time_point end)
{
    return std::chrono::duration<double, std::milli>{end - start}.count();
}
void check_equal(const simforge::simulation::SimulationSnapshot& expected,
                 const simforge::simulation::SimulationSnapshot& actual)
{
    if (auto difference = simforge::simulation::first_difference(expected, actual))
        throw std::runtime_error(*difference);
}
Sample run(std::size_t count)
{
    using namespace simforge::world;
    using namespace simforge::simulation;
    World world;
    SimulationClock clock;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<Transform>(entity);
        world.emplace<Velocity>(entity, Vec3{1, 2, 3});
    }
    replay_fixed_ticks(world, clock, 120); // Same state/workload as the ECS baseline; outside snapshot timing.
    const auto expected = capture_snapshot(world, clock);
    const auto old_handle = world.handle_at(0);
    const auto start = timer::now();
    const auto snapshot = capture_snapshot(world, clock);
    const auto captured = timer::now();
    restore_snapshot(world, clock, snapshot);
    const auto restored = timer::now();
    const auto bytes = serialize_snapshot(snapshot);
    const auto serialized = timer::now();
    const auto decoded = deserialize_snapshot(bytes);
    const auto deserialized = timer::now();
    check_equal(expected, snapshot);
    check_equal(snapshot, capture_snapshot(world, clock));
    check_equal(snapshot, decoded);
    if (world.valid(old_handle) || world.entity_count() != count)
        throw std::runtime_error("Snapshot benchmark ownership/count validation failed");
    double checksum = 0;
    world.each<Transform>([&](const Entity&, const Transform& value)
    {
        checksum += value.position.x + value.position.y + value.position.z;
    });
    return {milliseconds(start, captured), milliseconds(captured, restored),
            milliseconds(restored, serialized), milliseconds(serialized, deserialized), bytes.size(), checksum};
}
template<class Member>
double median(const std::array<Sample, measured_runs>& samples, Member member)
{
    std::array<double, measured_runs> values;
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = samples[i].*member;
    std::ranges::sort(values);
    return values[values.size() / 2];
}
}
#endif

int main()
{
#ifndef NDEBUG
    std::cerr << "Snapshot benchmarks require a Release build\n";
    return 1;
#else
    try
    {
        std::cout << "SimForge snapshot benchmark: one warmup + 7 measured runs per size; units milliseconds\n"
                  << "Setup: both components, 120 movement ticks at 60 Hz; all validations/checksum enabled in timed snapshot APIs\n"
                  << "Final exact comparisons/checksum traversal outside timing; restore includes old-state release\n"
                  << "entities,capture_ms,restore_ms,serialize_ms,deserialize_ms,encoded_bytes,checksum\n";
        for (std::size_t count : {1'000, 10'000, 100'000})
        {
            (void)run(count);
            std::array<Sample, measured_runs> samples;
            double checksum = 0;
            for (auto& sample : samples) { sample = run(count); checksum += sample.checksum; }
            std::cout << std::fixed << std::setprecision(6) << count << ','
                      << median(samples, &Sample::capture_ms) << ',' << median(samples, &Sample::restore_ms) << ','
                      << median(samples, &Sample::serialize_ms) << ',' << median(samples, &Sample::deserialize_ms) << ','
                      << samples.front().bytes << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
}
