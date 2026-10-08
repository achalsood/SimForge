#include <simforge/simulation/simulation_snapshot.hpp>

#include <iomanip>
#include <sstream>

namespace simforge::simulation
{
namespace
{
struct Comparison
{
    explicit Comparison(std::uint64_t tick) : tick_{tick} {}
    std::optional<std::string> difference;
    std::uint64_t tick_;
    template<class T> void field(const std::string& path, const T& expected, const T& actual)
    {
        if (!difference && expected != actual)
        {
            std::ostringstream message;
            message << std::setprecision(17) << "tick " << tick_ << ", " << path
                    << ": expected " << expected << ", actual " << actual;
            difference = message.str();
        }
    }
    void vector(const std::string& path, world::Vec3 expected, world::Vec3 actual)
    {
        field(path + ".x", expected.x, actual.x);
        field(path + ".y", expected.y, actual.y);
        field(path + ".z", expected.z, actual.z);
    }
    void entity(const std::string& path, world::EntityId expected, world::EntityId actual)
    {
        field(path + ".index", expected.index, actual.index);
        field(path + ".generation", expected.generation, actual.generation);
    }
};
}

std::optional<std::string> first_difference(const SimulationSnapshot& expected, const SimulationSnapshot& actual)
{
    validate_snapshot(expected);
    validate_snapshot(actual);
    Comparison compare{expected.clock.ticks};
    const auto& a = expected.world;
    const auto& b = actual.world;
    compare.field("world.generation_limit", a.generation_limit, b.generation_limit);
    compare.field("world.free_head", a.free_head, b.free_head);
    compare.field("world.live_count", a.live_count, b.live_count);
    compare.field("world.slots.size", a.slots.size(), b.slots.size());
    compare.field("world.Transform.size", a.transforms.size(), b.transforms.size());
    compare.field("world.Velocity.size", a.velocities.size(), b.velocities.size());
    if (compare.difference) return compare.difference;
    for (std::size_t i = 0; i < a.slots.size(); ++i)
    {
        const auto path = "world.slots[" + std::to_string(i) + "]";
        compare.field(path + ".generation", a.slots[i].generation, b.slots[i].generation);
        compare.field(path + ".next_free", a.slots[i].next_free, b.slots[i].next_free);
        compare.field(path + ".alive", a.slots[i].alive, b.slots[i].alive);
        compare.field(path + ".retired", a.slots[i].retired, b.slots[i].retired);
        if (compare.difference) return compare.difference;
    }
    for (std::size_t i = 0; i < a.transforms.size(); ++i)
    {
        const auto path = "world.Transform[dense=" + std::to_string(i) + ",entity=" +
                          std::to_string(a.transforms[i].entity.index) + "]";
        compare.entity(path + ".owner", a.transforms[i].entity, b.transforms[i].entity);
        compare.vector(path + ".position", a.transforms[i].value.position, b.transforms[i].value.position);
        compare.vector(path + ".rotation", a.transforms[i].value.rotation, b.transforms[i].value.rotation);
        compare.vector(path + ".scale", a.transforms[i].value.scale, b.transforms[i].value.scale);
        if (compare.difference) return compare.difference;
    }
    for (std::size_t i = 0; i < a.velocities.size(); ++i)
    {
        const auto path = "world.Velocity[dense=" + std::to_string(i) + ",entity=" +
                          std::to_string(a.velocities[i].entity.index) + "]";
        compare.entity(path + ".owner", a.velocities[i].entity, b.velocities[i].entity);
        compare.vector(path + ".linear", a.velocities[i].value.linear, b.velocities[i].value.linear);
        if (compare.difference) return compare.difference;
    }
    const auto& x = expected.clock;
    const auto& y = actual.clock;
    compare.field("clock.ticks_per_second", x.ticks_per_second, y.ticks_per_second);
    compare.field("clock.ticks", x.ticks, y.ticks);
    compare.field("clock.remainder", x.remainder, y.remainder);
    compare.field("clock.paused", x.paused, y.paused);
    compare.field("clock.time_scale", x.time_scale, y.time_scale);
    return compare.difference;
}

} // namespace simforge::simulation
