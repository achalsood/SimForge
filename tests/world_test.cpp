#include <simforge/world/world.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/simulation/simulation_clock.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

using namespace simforge::world;
using simforge::simulation::MovementSystem;
using simforge::simulation::SimulationClock;
using namespace std::chrono_literals;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Check failed at line " + std::to_string(location.line()));
}
template<class Exception, class Function>
void rejects(Function&& function, std::source_location location = std::source_location::current())
{
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception at line " + std::to_string(location.line()));
}
void near(double actual, double expected) { check(std::abs(actual - expected) < 1e-9); }

void lifecycle()
{
    World world;
    check(world.entity_count() == 0 && !world.valid(Entity{}));
    const auto first = world.create_entity();
    const auto other = world.create_entity();
    check(first != other && world.valid(first) && world.entity_count() == 2);
    world.emplace<Transform>(first);
    world.emplace<Velocity>(first);
    world.destroy_entity(first);
    check(!world.valid(first) && !world.has<Transform>(first));
    check(world.component_count<Transform>() == 0 && world.component_count<Velocity>() == 0);
    rejects<std::invalid_argument>([&] { world.destroy_entity(first); });
    rejects<std::invalid_argument>([&] { (void)world.get<Transform>(first); });
    rejects<std::invalid_argument>([&] { world.emplace<Velocity>(first); });
    rejects<std::invalid_argument>([&] { world.remove<Velocity>(first); });
    for (Entity invalid : {Entity{}, Entity{first.index, 0}, Entity{999, 1}, Entity{other.index, 99}})
    {
        check(!world.valid(invalid) && !world.has<Velocity>(invalid));
        rejects<std::invalid_argument>([&] { world.destroy_entity(invalid); });
    }
    const auto reused = world.create_entity();
    check(reused.index == first.index && reused.generation == first.generation + 1);
    check(world.valid(reused) && !world.valid(first) && !world.has<Transform>(reused));
    world.emplace<Transform>(reused);
    world.clear();
    check(!world.valid(other) && !world.valid(reused) && world.entity_count() == 0);
    check(world.component_count<Transform>() == 0);
    world.clear();
    for (int i = 0; i < 4; ++i) (void)world.create_entity();
    check(!world.valid(first) && !world.valid(reused) && !world.valid(other));
    rejects<std::length_error>([&] { world.reserve(static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1); });
    rejects<std::invalid_argument>([] { World invalid{0}; });

    // Exercise the actual retirement branch with a bounded generation budget,
    // rather than requiring billions of create/destroy cycles.
    World bounded{2};
    const auto generation_one = bounded.create_entity();
    bounded.destroy_entity(generation_one);
    const auto generation_two = bounded.create_entity();
    check(generation_two.index == generation_one.index && generation_two.generation == 2);
    bounded.destroy_entity(generation_two);
    const auto fresh = bounded.create_entity();
    check(fresh.index != generation_two.index && !bounded.valid(generation_two));
    bounded.clear();
    const auto last = bounded.create_entity();
    check(last.generation == 2);
    bounded.clear();
    check(bounded.create_entity().index != last.index);
    check(!bounded.valid(generation_one) && !bounded.valid(generation_two));
    World one_generation{1};
    const auto retired = one_generation.create_entity();
    one_generation.clear();
    check(one_generation.create_entity().index != retired.index);
}

void components()
{
    World world;
    world.reserve(8);
    const auto a = world.create_entity();
    const auto b = world.create_entity();
    const auto c = world.create_entity();
    auto& first = world.emplace<Transform>(a, Vec3{1, 2, 3});
    check(first.position == Vec3{1, 2, 3} && first.scale == Vec3{1, 1, 1});
    const auto* address = &first;
    world.emplace<Transform>(b, Vec3{2, 0, 0});
    world.emplace<Transform>(c, Vec3{3, 0, 0});
    check(&world.get<Transform>(a) == address); // Reserved capacity avoids growth.
    first.position.y = 4;
    check(world.get<Transform>(a).position.y == 4);
    const World& constant = world;
    check(constant.get<Transform>(a).position.x == 1);
    rejects<std::logic_error>([&] { world.emplace<Transform>(a, Vec3{99, 0, 0}); });
    check(world.get<Transform>(a).position.x == 1);
    rejects<std::out_of_range>([&] { (void)world.get<Velocity>(a); });
    rejects<std::out_of_range>([&] { (void)constant.get<Velocity>(a); });
    check(!world.remove<Velocity>(a));
    check(world.remove<Transform>(b) && !world.has<Transform>(b));
    // Swap-and-pop moved c into b's dense slot; reacquire references after removal.
    std::vector<Entity> order;
    world.each<Transform>([&](Entity entity, Transform&) { order.push_back(entity); });
    check(order == std::vector<Entity>{a, c});
    check(world.get<Transform>(c).position.x == 3);
    world.emplace<Transform>(b, Vec3{5, 0, 0});
    order.clear();
    constant.each<Transform>([&](Entity entity, const Transform&) { order.push_back(entity); });
    check(order == std::vector<Entity>{a, c, b});
    world.destroy_entity(c);
    check(world.component_count<Transform>() == 2 && world.get<Transform>(b).position.x == 5);
    check(world.remove<Transform>(a) && world.remove<Transform>(b));
    check(world.component_count<Transform>() == 0);
    int visits = 0;
    world.each<Transform>([&](Entity, Transform&) { ++visits; });
    check(visits == 0);
}

void iteration_lifetime()
{
    World world;
    const auto entity = world.create_entity();
    world.emplace<Transform>(entity);
    world.each<Transform>([&](Entity current, Transform& transform)
    {
        check(current == entity);
        rejects<std::logic_error>([&] { (void)world.create_entity(); });
        rejects<std::logic_error>([&] { world.destroy_entity(entity); });
        rejects<std::logic_error>([&] { world.clear(); });
        rejects<std::logic_error>([&] { world.reserve(100); });
        rejects<std::logic_error>([&] { world.emplace<Velocity>(entity); });
        rejects<std::logic_error>([&] { world.remove<Transform>(entity); });
        transform.position.x = 7;
        world.each<Transform>([&](Entity, Transform&) { check(world.get<Transform>(entity).position.x == 7); });
        check(&world.get<Transform>(entity) == &transform);
    });
    const World& constant = world;
    constant.each<Transform>([&](Entity, const Transform&)
    {
        rejects<std::logic_error>([&] { world.clear(); });
    });
    rejects<std::runtime_error>([&]
    {
        world.each<Transform>([](Entity, Transform&) { throw std::runtime_error("callback failure"); });
    });
    // Exception unwinding must release all iteration guards.
    world.emplace<Velocity>(entity);
    world.destroy_entity(entity);
    check(world.entity_count() == 0);
}

void movement()
{
    World world;
    const auto moving = world.create_entity();
    world.emplace<Transform>(moving, Vec3{1, 2, 3}, Vec3{0.1, 0.2, 0.3}, Vec3{2, 2, 2});
    world.emplace<Velocity>(moving, Vec3{2, -4, 6});
    const auto stationary = world.create_entity();
    world.emplace<Transform>(stationary, Vec3{9, 8, 7});
    world.emplace<Velocity>(stationary);
    const auto transform_only = world.create_entity();
    world.emplace<Transform>(transform_only, Vec3{5, 5, 5});
    const auto velocity_only = world.create_entity();
    world.emplace<Velocity>(velocity_only, Vec3{100, 100, 100});
    const auto dead = world.create_entity();
    world.emplace<Transform>(dead);
    world.emplace<Velocity>(dead);
    world.destroy_entity(dead);
    MovementSystem::update(world, 0.5);
    check(world.get<Transform>(moving).position == Vec3{2, 0, 6});
    check(world.get<Transform>(moving).rotation == Vec3{0.1, 0.2, 0.3});
    check(world.get<Transform>(moving).scale == Vec3{2, 2, 2});
    check(world.get<Transform>(stationary).position == Vec3{9, 8, 7});
    check(world.get<Transform>(transform_only).position == Vec3{5, 5, 5});
    check(!world.has<Transform>(velocity_only) && !world.valid(dead));
    MovementSystem::update(world, 0);
    check(world.get<Transform>(moving).position.x == 2);
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects<std::invalid_argument>([&] { MovementSystem::update(world, invalid); });
    for (int i = 0; i < 60; ++i) MovementSystem::update(world, 1.0 / 60);
    near(world.get<Transform>(moving).position.x, 4);
    near(world.get<Transform>(moving).position.y, -4);
    near(world.get<Transform>(moving).position.z, 12);
}

void clock_integration()
{
    World a, b;
    const auto populate = [](World& world)
    {
        for (int i = 0; i < 64; ++i)
        {
            const auto entity = world.create_entity();
            world.emplace<Transform>(entity, Vec3{static_cast<double>(i), 0, 0});
            world.emplace<Velocity>(entity, Vec3{1, 2, 3});
        }
    };
    populate(a); populate(b);
    SimulationClock first, second;
    const auto advance = [](World& world, SimulationClock& clock, std::chrono::nanoseconds input)
    {
        const auto ticks = clock.advance(input);
        for (std::uint64_t i = 0; i < ticks; ++i) MovementSystem::update(world, clock.fixed_step_seconds());
    };
    first.pause(); second.pause();
    advance(a, first, 1s); advance(b, second, 1s);
    check(a.get<Transform>(Entity{0, 1}).position.x == 0 && first.tick_count() == 0);
    first.single_step(); second.single_step();
    MovementSystem::update(a, first.fixed_step_seconds());
    MovementSystem::update(b, second.fixed_step_seconds());
    near(a.get<Transform>(Entity{0, 1}).position.x, 1.0 / 60);
    first.resume(); second.resume();
    first.set_time_scale(0.5); second.set_time_scale(0.5);
    advance(a, first, 1s); advance(b, second, 1s);
    check(first.tick_count() == 31);
    first.set_time_scale(2); second.set_time_scale(2);
    advance(a, first, 1s); advance(b, second, 1s);
    check(first.tick_count() == 151);
    for (int i = 0; i < 100; ++i)
    {
        advance(a, first, std::chrono::nanoseconds{i * 7919});
        advance(b, second, std::chrono::nanoseconds{i * 7919});
    }
    check(first.tick_count() == second.tick_count());
    std::vector<Entity> order_a, order_b;
    a.each<Velocity, Transform>([&](Entity entity, const Velocity&, const Transform& transform)
    {
        order_a.push_back(entity);
        check(transform.position == b.get<Transform>(entity).position);
    });
    b.each<Velocity, Transform>([&](Entity entity, const Velocity&, const Transform&) { order_b.push_back(entity); });
    check(order_a == order_b);
    near(a.get<Transform>(Entity{0, 1}).position.x, first.elapsed_seconds());
}

void large_world()
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 100'000; ++i)
    {
        const auto entity = world.create_entity();
        entities.push_back(entity);
        world.emplace<Transform>(entity, Vec3{static_cast<double>(i), 0, 0});
        world.emplace<Velocity>(entity, Vec3{1, 0, 0});
    }
    for (std::size_t i = 0; i < entities.size(); i += 2) world.destroy_entity(entities[i]);
    MovementSystem::update(world, 1);
    check(world.entity_count() == 50'000 && world.component_count<Velocity>() == 50'000);
    for (std::size_t i = 1; i < entities.size(); i += 2)
        check(world.get<Transform>(entities[i]).position.x == static_cast<double>(i) + 1);
    for (std::size_t i = 0; i < entities.size(); i += 2)
    {
        check(!world.valid(entities[i]));
        const auto new_entity = world.create_entity();
        check(new_entity.generation == 2);
        world.emplace<Transform>(new_entity);
        world.emplace<Velocity>(new_entity);
    }
    check(world.entity_count() == 100'000);
    world.clear();
    check(world.component_count<Transform>() == 0 && world.entity_count() == 0);
}
} // namespace

int main()
{
    try
    {
        lifecycle(); components(); iteration_lifetime(); movement(); clock_integration(); large_world();
        std::cout << "World lifecycle, storage, movement, clock integration and 100K stress checks passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
