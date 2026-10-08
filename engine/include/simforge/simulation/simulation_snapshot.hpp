#pragma once

#include <simforge/simulation/simulation_clock.hpp>
#include <simforge/world/world_snapshot.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace simforge::world { class World; }

namespace simforge::simulation
{

struct SimulationSnapshot
{
    std::uint32_t version{1};
    world::WorldSnapshot world;
    ClockSnapshot clock;
};

inline constexpr std::size_t snapshot_max_bytes = 64 * 1024 * 1024;

void validate_snapshot(const SimulationSnapshot& snapshot);
[[nodiscard]] SimulationSnapshot capture_snapshot(const world::World& world, const SimulationClock& clock);
// Atomic with respect to failures: validates/builds before changing World or clock.
void restore_snapshot(world::World& world, SimulationClock& clock, const SimulationSnapshot& snapshot);

// Explicit fixed ticks bypass pause/scale, like SimulationClock::advance_ticks.
void replay_fixed_ticks(world::World& world, SimulationClock& clock, std::uint64_t ticks);

// Portable schema v1: explicit little-endian integers and IEEE-754 binary64.
[[nodiscard]] std::vector<std::byte> serialize_snapshot(const SimulationSnapshot& snapshot);
[[nodiscard]] SimulationSnapshot deserialize_snapshot(std::span<const std::byte> bytes);

// Exact finite numeric equality (signed zeros equal); includes observable dense
// order/allocation history. Ownership tokens/capacity are deliberately ignored.
[[nodiscard]] std::optional<std::string> first_difference(
    const SimulationSnapshot& expected, const SimulationSnapshot& actual);

} // namespace simforge::simulation
