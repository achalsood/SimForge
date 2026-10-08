#include <simforge/simulation/simulation_snapshot.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>

#include <limits>
#include <stdexcept>

namespace simforge::simulation
{

void validate_snapshot(const SimulationSnapshot& snapshot)
{
    if (snapshot.version != 1) throw std::invalid_argument("Unsupported simulation snapshot version");
    world::validate_snapshot(snapshot.world);
    validate_snapshot(snapshot.clock);
}

SimulationSnapshot capture_snapshot(const world::World& world, const SimulationClock& clock)
{
    return {1, world.capture_snapshot(), clock.capture_snapshot()};
}

void restore_snapshot(world::World& world, SimulationClock& clock, const SimulationSnapshot& snapshot)
{
    if (snapshot.version != 1) throw std::invalid_argument("Unsupported simulation snapshot version");
    SimulationClock replacement;
    replacement.restore_snapshot(snapshot.clock);
    world.restore_snapshot(snapshot.world);
    static_assert(std::is_nothrow_copy_assignable_v<SimulationClock>);
    clock = replacement;
}

void replay_fixed_ticks(world::World& world, SimulationClock& clock, std::uint64_t ticks)
{
    if (ticks > std::numeric_limits<std::uint64_t>::max() - clock.tick_count())
        throw std::overflow_error("Replay tick count overflow");
    for (std::uint64_t i = 0; i < ticks; ++i)
    {
        MovementSystem::update(world, clock.fixed_step_seconds());
        clock.advance_ticks(1);
    }
}

} // namespace simforge::simulation
