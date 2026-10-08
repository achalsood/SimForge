#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>

#include <cmath>
#include <stdexcept>

namespace simforge::simulation
{

void MovementSystem::update(world::World& world, double fixed_delta_seconds)
{
    if (!std::isfinite(fixed_delta_seconds) || fixed_delta_seconds < 0.0)
        throw std::invalid_argument("Movement timestep must be finite and nonnegative");
    if (fixed_delta_seconds == 0.0) return;
    world.each_components<world::Velocity, world::Transform>(
        [fixed_delta_seconds](const world::Velocity& velocity, world::Transform& transform)
        {
            transform.position.x += velocity.linear.x * fixed_delta_seconds;
            transform.position.y += velocity.linear.y * fixed_delta_seconds;
            transform.position.z += velocity.linear.z * fixed_delta_seconds;
        });
}

} // namespace simforge::simulation
