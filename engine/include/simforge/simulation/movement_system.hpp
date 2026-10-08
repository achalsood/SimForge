#pragma once

namespace simforge::world { class World; }

namespace simforge::simulation
{

// Executes exactly one update. No clock, engine or platform ownership.
class MovementSystem
{
public:
    static void update(world::World& world, double fixed_delta_seconds);
};

} // namespace simforge::simulation
