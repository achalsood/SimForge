#include <simforge/simulation/simulation_snapshot.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string_view>

using namespace simforge::world;
using namespace simforge::simulation;
using namespace std::chrono_literals;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Check failed at line " + std::to_string(location.line()));
}
template<class Exception = std::invalid_argument, class Function>
void rejects(Function&& function, std::source_location location = std::source_location::current())
{
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected rejection at line " + std::to_string(location.line()));
}
void equal(const SimulationSnapshot& a, const SimulationSnapshot& b)
{
    if (auto difference = first_difference(a, b)) throw std::runtime_error(*difference);
}
void equal(const WorldSnapshot& a, const WorldSnapshot& b) { equal({1, a, {}}, {1, b, {}}); }

void capture_and_restore()
{
    World world{3};
    const auto empty = world.capture_snapshot();
    check(empty.slots.empty() && empty.live_count == 0);
    world.restore_snapshot(empty);
    const auto a = world.create_entity();
    world.emplace<Transform>(a, Vec3{1, 2, 3}, Vec3{0.1, 0.2, 0.3}, Vec3{2, 3, 4});
    world.emplace<Velocity>(a, Vec3{-1, 2, 3});
    const auto single = world.capture_snapshot();
    check(world.valid(a));
    equal(single, world.capture_snapshot());
    const auto b = world.create_entity();
    world.emplace<Velocity>(b, Vec3{7, 8, 9});
    const auto c = world.create_entity();
    world.emplace<Transform>(c);
    const auto dead = world.create_entity();
    world.destroy_entity(dead);
    auto reused = world.create_entity();
    check(reused.index == dead.index && reused.generation == 2);
    world.destroy_entity(reused);
    reused = world.create_entity();
    world.destroy_entity(reused); // Retired at generation 3.
    const auto free1 = world.create_entity();
    const auto free2 = world.create_entity();
    world.destroy_entity(free1); world.destroy_entity(free2);
    const auto baseline = world.capture_snapshot();
    check(baseline.slots[dead.index].retired && baseline.free_head == free2.index);
    auto independent = baseline;
    independent.transforms[0].value.position.x = 99;
    check(world.get<Transform>(a).position.x == 1 && baseline.transforms[0].value.position.x == 1);
    world.get<Transform>(a).position.x = 50;
    check(baseline.transforms[0].value.position.x == 1);
    world.destroy_entity(a);
    check(!world.valid(a));
    world.restore_snapshot(baseline);
    check(!world.valid(a) && !world.valid(b) && !world.valid(c) && !world.valid(reused));
    const auto restored_a = world.handle_at(a.index);
    check(restored_a.generation == a.generation && world.get<Transform>(restored_a).position.x == 1);
    rejects<std::out_of_range>([&] { (void)world.handle_at(dead.index); });
    equal(baseline, world.capture_snapshot());
    const auto next = world.create_entity();
    check(next.index == free2.index && next.generation == 2);
    const auto next2 = world.create_entity();
    check(next2.index == free1.index && next2.generation == 2);
    const auto appended = world.create_entity();
    check(appended.index == baseline.slots.size());
    world.restore_snapshot(baseline);
    check(!world.valid(restored_a) && !world.valid(next));
    const auto before_clear = world.handle_at(a.index);
    world.clear();
    check(!world.valid(before_clear));
    world.restore_snapshot(baseline);
    equal(baseline, world.capture_snapshot());
    World branch;
    branch.restore_snapshot(baseline);
    equal(baseline, branch.capture_snapshot());
    check(!branch.valid(world.handle_at(a.index)));
    rejects([&] { (void)branch.get<Transform>(world.handle_at(a.index)); });
    branch.get<Transform>(branch.handle_at(a.index)).position.x = 123;
    check(world.get<Transform>(world.handle_at(a.index)).position.x == 1);
    check(baseline.transforms[0].value.position.x == 1);
    // Reconstructing a World at the same address cannot revive retained handles.
    std::optional<World> reused_address;
    reused_address.emplace();
    const auto old = reused_address->create_entity();
    reused_address.reset(); reused_address.emplace();
    const auto fresh = reused_address->create_entity();
    check(old.index == fresh.index && old.generation == fresh.generation && old != fresh);
    check(!reused_address->valid(old));
    world.restore_snapshot(empty);
    check(world.entity_count() == 0 && !world.valid(fresh));
    World maximum_generation;
    auto maximum = maximum_generation.capture_snapshot();
    maximum.slots.push_back({std::numeric_limits<std::uint32_t>::max(), snapshot_no_slot, true, false});
    maximum.live_count = 1;
    maximum_generation.restore_snapshot(maximum);
    const auto last_generation = maximum_generation.handle_at(0);
    maximum_generation.destroy_entity(last_generation);
    check(maximum_generation.capture_snapshot().slots[0].retired);
    check(maximum_generation.create_entity().index == 1 && !maximum_generation.valid(last_generation));
}

void invalid_restore()
{
    World world{3}; SimulationClock clock;
    const auto entity = world.create_entity();
    world.emplace<Transform>(entity);
    world.emplace<Velocity>(entity);
    (void)world.create_entity(); // Allows duplicate records to reach ownership validation.
    const auto free = world.create_entity(); world.destroy_entity(free);
    const auto original = capture_snapshot(world, clock);
    auto* reference = &world.get<Transform>(entity);
    const auto reject_world = [&](auto mutation)
    {
        auto invalid = original;
        mutation(invalid.world);
        rejects([&] { restore_snapshot(world, clock, invalid); });
        equal(original, capture_snapshot(world, clock));
        check(world.valid(entity) && &world.get<Transform>(entity) == reference);
    };
    reject_world([](auto& s) { s.version = 2; });
    reject_world([](auto& s) { s.generation_limit = 0; });
    reject_world([](auto& s) { ++s.live_count; });
    reject_world([](auto& s) { s.free_head = 999; });
    reject_world([&](auto& s) { s.slots[free.index].next_free = free.index; });
    reject_world([](auto& s) { s.free_head = snapshot_no_slot; });
    reject_world([](auto& s) { s.slots[0].generation = 0; });
    reject_world([](auto& s) { s.slots[0].generation = 4; });
    reject_world([](auto& s) { s.slots[0].retired = true; });
    reject_world([](auto& s) { s.slots[0].next_free = 1; });
    reject_world([&](auto& s) { s.slots[free.index].retired = true; });
    reject_world([](auto& s) { s.transforms.push_back(s.transforms[0]); });
    reject_world([](auto& s) { s.transforms[0].entity.index = 99; });
    reject_world([&](auto& s) { s.transforms[0].entity.index = free.index; });
    reject_world([](auto& s) { s.transforms[0].entity.generation = 2; });
    reject_world([](auto& s) { s.velocities.push_back(s.velocities[0]); });
    for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        reject_world([&](auto& s) { s.transforms[0].value.rotation.z = value; });
        reject_world([&](auto& s) { s.velocities[0].value.linear.x = value; });
    }
    auto invalid = original;
    invalid.clock.remainder = 1'000'000'000;
    rejects([&] { restore_snapshot(world, clock, invalid); });
    equal(original, capture_snapshot(world, clock)); check(world.valid(entity));
    invalid = original; invalid.version = 2;
    rejects([&] { restore_snapshot(world, clock, invalid); });
    world.each<Transform>([&](const Entity&, Transform&)
    {
        rejects<std::logic_error>([&] { restore_snapshot(world, clock, original); });
    });
    world.each_components<Transform>([&](Transform& transform)
    {
        check(&transform == reference);
        rejects<std::logic_error>([&] { world.restore_snapshot(original.world); });
    });
    const World& constant = world;
    constant.each_components<Velocity>([&](const Velocity& velocity)
    {
        check(velocity.linear == Vec3{});
        rejects<std::logic_error>([&] { world.clear(); });
    });
    rejects<std::runtime_error>([&]
    {
        world.each_components<Transform>([](Transform&) { throw std::runtime_error("iteration failure"); });
    });
    world.reserve(10); // Guard released after an exception; live handle preserved.
    reference = &world.get<Transform>(entity);
    equal(original, capture_snapshot(world, clock));
    world.get<Transform>(entity).position.x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)world.capture_snapshot(); });
    world.get<Transform>(entity).position.x = 0;
    equal(original, capture_snapshot(world, clock));
    auto too_large = original.world;
    too_large.slots.resize(snapshot_max_slots + 1);
    rejects<std::length_error>([&] { world.restore_snapshot(too_large); });
    equal(original, capture_snapshot(world, clock));
}

void clock_restore()
{
    SimulationClock original{120};
    original.set_time_scale(0.25);
    check(original.advance(1ms) == 0);
    original.advance_ticks(17); original.pause();
    const auto saved = original.capture_snapshot();
    check(saved.remainder != 0 && saved.ticks == 17 && saved.paused && saved.time_scale == 0.25);
    SimulationClock restored;
    restored.restore_snapshot(saved);
    check(restored.ticks_per_second() == 120 && restored.elapsed_seconds() == original.elapsed_seconds());
    check(restored.advance(1s) == 0);
    original.single_step(); restored.single_step();
    original.resume(); restored.resume();
    for (int i = 0; i < 1000; ++i)
        check(original.advance(std::chrono::nanoseconds{i * 9179}) == restored.advance(std::chrono::nanoseconds{i * 9179}));
    const auto final = original.capture_snapshot();
    check(final.ticks == restored.tick_count() && final.remainder == restored.capture_snapshot().remainder);
    for (int invalid_case = 0; invalid_case < 7; ++invalid_case)
    {
        auto invalid = final;
        switch (invalid_case)
        {
        case 0: invalid.version = 99; break;
        case 1: invalid.ticks_per_second = 0; break;
        case 2: invalid.ticks_per_second = 1'000'000'001; break;
        case 3: invalid.remainder = 1'000'000'000; break;
        case 4: invalid.time_scale = -1; break;
        case 5: invalid.time_scale = std::numeric_limits<double>::quiet_NaN(); break;
        case 6: invalid.time_scale = std::numeric_limits<double>::infinity(); break;
        }
        rejects([&] { restored.restore_snapshot(invalid); });
        check(restored.tick_count() == final.ticks && restored.capture_snapshot().remainder == final.remainder);
    }
    auto limit = saved;
    limit.ticks = std::numeric_limits<std::uint64_t>::max();
    limit.remainder = 999'999'999;
    restored.restore_snapshot(limit);
    check(restored.tick_count() == limit.ticks);
}

void put(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8)) & 255);
}
void repair_checksum(std::vector<std::byte>& bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i)
    {
        hash ^= std::to_integer<std::uint8_t>(bytes[i]); hash *= 1099511628211ULL;
    }
    put(bytes, bytes.size() - 8, hash, 8);
}
void serialization()
{
    World empty; SimulationClock clock;
    const auto empty_bytes = serialize_snapshot(capture_snapshot(empty, clock));
    check(empty_bytes.size() == 81);
    equal(capture_snapshot(empty, clock), deserialize_snapshot(empty_bytes));
    check(serialize_snapshot(deserialize_snapshot(empty_bytes)) == empty_bytes);
    World world{2};
    for (int i = 0; i < 3; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<Transform>(entity, Vec3{static_cast<double>(i), -0.0, 0.125});
        world.emplace<Velocity>(entity, Vec3{1, 2, -3});
    }
    const auto baseline = capture_snapshot(world, clock);
    const auto bytes = serialize_snapshot(baseline);
    equal(baseline, deserialize_snapshot(bytes));
    check(serialize_snapshot(deserialize_snapshot(bytes)) == bytes);
    world.destroy_entity(world.handle_at(1));
    const auto reused = world.create_entity(); world.emplace<Transform>(reused);
    world.destroy_entity(reused); // Retired slot.
    const auto reused_snapshot = capture_snapshot(world, clock);
    equal(reused_snapshot, deserialize_snapshot(serialize_snapshot(reused_snapshot)));
    const auto bad_wire = [&](std::size_t offset, std::uint64_t value, std::size_t size)
    {
        auto invalid = bytes; put(invalid, offset, value, size); repair_checksum(invalid);
        rejects([&] { (void)deserialize_snapshot(invalid); });
    };
    bad_wire(0, 0, 1); // Magic.
    bad_wire(8, 2, 4); bad_wire(12, 2, 4); // Versions.
    bad_wire(16, 0, 4); bad_wire(24, 99, 4); // Allocation metadata.
    bad_wire(32, 0, 4); bad_wire(40, 2, 1); // Generation and boolean.
    const std::size_t transform_start = 32 + 3 * 10 + 4;
    bad_wire(transform_start, 99, 4); // Invalid component owner.
    bad_wire(transform_start + 80, 0, 4); // Duplicate entity ownership.
    bad_wire(transform_start + 8, 0x7ff8000000000000ULL, 8); // NaN.
    const auto clock_start = bytes.size() - 8 - 33;
    bad_wire(clock_start, 2, 4);
    bad_wire(clock_start + 24, 2, 1);
    auto excessive_count = bytes; put(excessive_count, 28, snapshot_max_slots + 1, 4); repair_checksum(excessive_count);
    rejects<std::length_error>([&] { (void)deserialize_snapshot(excessive_count); });
    auto truncated_count = bytes; put(truncated_count, 28, snapshot_max_slots, 4); repair_checksum(truncated_count);
    rejects([&] { (void)deserialize_snapshot(truncated_count); });
    for (std::size_t length = 0; length < bytes.size(); ++length)
        rejects([&] { (void)deserialize_snapshot(std::span{bytes}.first(length)); });
    auto corrupt = bytes; corrupt[transform_start + 8] ^= std::byte{1};
    rejects([&] { (void)deserialize_snapshot(corrupt); });
    auto trailing = bytes; trailing.insert(trailing.end() - 8, std::byte{0}); repair_checksum(trailing);
    rejects([&] { (void)deserialize_snapshot(trailing); });
    std::vector<std::byte> excessive_input(snapshot_max_bytes + 1);
    rejects<std::length_error>([&] { (void)deserialize_snapshot(excessive_input); });
    auto plus_zero = baseline;
    plus_zero.world.transforms[0].value.position.y = 0.0;
    equal(baseline, plus_zero); // Exact numeric equality differs from byte equality.
    check(serialize_snapshot(baseline) != serialize_snapshot(plus_zero));
    // Preserve all 64 bits of counters, without a lossy numeric/text conversion.
    auto max_counter = baseline;
    max_counter.clock.ticks = std::numeric_limits<std::uint64_t>::max();
    equal(max_counter, deserialize_snapshot(serialize_snapshot(max_counter)));
}

void replay_and_branching()
{
    World world{3}; SimulationClock clock;
    for (int i = 0; i < 40; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<Transform>(entity, Vec3{static_cast<double>(i), 0, -2});
        world.emplace<Velocity>(entity, Vec3{0.125, 2, -3});
    }
    world.destroy_entity(world.handle_at(5));
    world.remove<Velocity>(world.handle_at(7));
    world.emplace<Velocity>(world.handle_at(7), Vec3{0.125, 2, -3}); // Observable order change.
    (void)clock.advance(1ms);
    clock.set_time_scale(0.5);
    const auto initial = capture_snapshot(world, clock);
    const auto stale = world.handle_at(0);
    replay_fixed_ticks(world, clock, 600);
    const auto final = capture_snapshot(world, clock);
    check(clock.tick_count() == 600 && clock.elapsed_seconds() == 10);
    check(std::abs(world.get<Transform>(world.handle_at(0)).position.x - 1.25) < 1e-12);
    restore_snapshot(world, clock, initial);
    check(!world.valid(stale));
    replay_fixed_ticks(world, clock, 600);
    equal(final, capture_snapshot(world, clock));
    restore_snapshot(world, clock, deserialize_snapshot(serialize_snapshot(initial)));
    replay_fixed_ticks(world, clock, 600);
    equal(final, capture_snapshot(world, clock));
    World branch_a, branch_b; SimulationClock a, b;
    restore_snapshot(branch_a, a, initial); restore_snapshot(branch_b, b, initial);
    const auto handle_a = branch_a.handle_at(0);
    check(!branch_b.valid(handle_a));
    rejects([&] { branch_b.emplace<Velocity>(handle_a); });
    rejects([&] { branch_b.destroy_entity(handle_a); });
    branch_a.get<Velocity>(handle_a).linear.x = 1;
    check(branch_b.get<Velocity>(branch_b.handle_at(0)).linear.x == 0.125);
    check(initial.world.velocities[0].value.linear.x == 0.125);
    replay_fixed_ticks(branch_a, a, 600); replay_fixed_ticks(branch_b, b, 600);
    equal(final, capture_snapshot(branch_b, b));
    const auto divergence = first_difference(final, capture_snapshot(branch_a, a));
    check(divergence && divergence->find("position.x") != std::string::npos && divergence->find("entity=0") != std::string::npos);
    restore_snapshot(branch_a, a, initial); replay_fixed_ticks(branch_a, a, 600);
    equal(final, capture_snapshot(branch_a, a));
    auto clock_divergence = final; ++clock_divergence.clock.ticks;
    check(first_difference(final, clock_divergence)->find("clock.ticks") != std::string::npos);
    auto order_divergence = final;
    std::swap(order_divergence.world.velocities[0], order_divergence.world.velocities[1]);
    check(first_difference(final, order_divergence)->find("Velocity") != std::string::npos);
    auto overflow = clock.capture_snapshot(); overflow.ticks = std::numeric_limits<std::uint64_t>::max();
    clock.restore_snapshot(overflow);
    const auto before = capture_snapshot(world, clock);
    rejects<std::overflow_error>([&] { replay_fixed_ticks(world, clock, 1); });
    equal(before, capture_snapshot(world, clock));
}

// Deliberately simple reference model: vectors, linear scans and a LIFO free list.
struct ModelSlot
{
    std::uint32_t generation{1}; bool alive{false}; bool retired{false};
    std::optional<Transform> transform; std::optional<Velocity> velocity;
};
struct Model
{
    std::vector<ModelSlot> slots;
    std::vector<std::uint32_t> free;
    std::vector<std::uint32_t> transform_order, velocity_order;
    static void erase(std::vector<std::uint32_t>& order, std::uint32_t index)
    {
        const auto found = std::find(order.begin(), order.end(), index);
        if (found != order.end()) { *found = order.back(); order.pop_back(); }
    }
    std::uint32_t create()
    {
        std::uint32_t index;
        if (free.empty()) { index = static_cast<std::uint32_t>(slots.size()); slots.emplace_back(); }
        else { index = free.back(); free.pop_back(); }
        slots[index].alive = true;
        return index;
    }
    void destroy(std::uint32_t index)
    {
        auto& slot = slots[index]; slot.alive = false;
        slot.transform.reset(); slot.velocity.reset();
        erase(transform_order, index); erase(velocity_order, index);
        if (slot.generation == 3) slot.retired = true;
        else { ++slot.generation; free.push_back(index); }
    }
    WorldSnapshot snapshot() const
    {
        WorldSnapshot result; result.generation_limit = 3;
        for (const auto& slot : slots)
        {
            result.slots.push_back({slot.generation, snapshot_no_slot, slot.alive, slot.retired});
            if (slot.alive) ++result.live_count;
        }
        if (!free.empty()) result.free_head = free.back();
        for (std::size_t i = 1; i < free.size(); ++i) result.slots[free[i]].next_free = free[i - 1];
        for (auto index : transform_order) result.transforms.push_back({{index, slots[index].generation}, *slots[index].transform});
        for (auto index : velocity_order) result.velocities.push_back({{index, slots[index].generation}, *slots[index].velocity});
        return result;
    }
};
void randomized()
{
    constexpr std::uint32_t seed = 0x5F062026;
    std::mt19937 random{seed};
    std::size_t operation = 0;
    try
    {
        World world{3}; Model model;
        auto saved = world.capture_snapshot(); Model saved_model;
        for (; operation < 3000; ++operation)
        {
            std::vector<std::uint32_t> live;
            for (std::size_t i = 0; i < model.slots.size(); ++i)
                if (model.slots[i].alive) live.push_back(static_cast<std::uint32_t>(i));
            const auto action = random() % 9;
            if (action == 0 || live.empty())
            {
                const auto actual = world.create_entity(); const auto index = model.create();
                check(actual.index == index && actual.generation == model.slots[index].generation);
            }
            else
            {
                const auto index = live[random() % live.size()];
                const auto entity = world.handle_at(index);
                auto& slot = model.slots[index];
                switch (action)
                {
                case 1: world.destroy_entity(entity); model.destroy(index); check(!world.valid(entity)); break;
                case 2:
                    if (!slot.transform)
                    {
                        const Transform value{Vec3{static_cast<double>(random() % 100), 0, -0.25}};
                        world.emplace<Transform>(entity, value); slot.transform = value; model.transform_order.push_back(index);
                    }
                    break;
                case 3:
                    if (!slot.velocity)
                    {
                        const Velocity value{Vec3{0.5, 1, static_cast<double>(random() % 100)}};
                        world.emplace<Velocity>(entity, value); slot.velocity = value; model.velocity_order.push_back(index);
                    }
                    break;
                case 4: check(world.remove<Transform>(entity) == slot.transform.has_value()); slot.transform.reset(); Model::erase(model.transform_order, index); break;
                case 5: check(world.remove<Velocity>(entity) == slot.velocity.has_value()); slot.velocity.reset(); Model::erase(model.velocity_order, index); break;
                case 6:
                    if (slot.transform) { slot.transform->position.x += 0.25; world.get<Transform>(entity).position.x += 0.25; }
                    break;
                case 7: saved = world.capture_snapshot(); saved_model = model; break;
                case 8:
                    world.restore_snapshot(saved); model = saved_model;
                    check(!world.valid(entity));
                    break;
                }
            }
            equal(model.snapshot(), world.capture_snapshot());
        }
    }
    catch (...)
    {
        std::cerr << "Randomized ECS seed=" << seed << " operation=" << operation << '\n'; throw;
    }
}
void stress()
{
    World world; SimulationClock clock;
    world.reserve(100'000);
    for (int i = 0; i < 100'000; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<Transform>(entity, Vec3{static_cast<double>(i), 0, 0});
        world.emplace<Velocity>(entity, Vec3{1, -2, 0.5});
    }
    const auto initial = capture_snapshot(world, clock);
    const auto bytes = serialize_snapshot(initial);
    World restored; SimulationClock restored_clock;
    restore_snapshot(restored, restored_clock, deserialize_snapshot(bytes));
    equal(initial, capture_snapshot(restored, restored_clock));
    replay_fixed_ticks(world, clock, 2); replay_fixed_ticks(restored, restored_clock, 2);
    equal(capture_snapshot(world, clock), capture_snapshot(restored, restored_clock));
}
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected test case name");
        const std::string_view name{argv[1]};
        if (name == "capture") capture_and_restore();
        else if (name == "validation") invalid_restore();
        else if (name == "clock") clock_restore();
        else if (name == "serialization") serialization();
        else if (name == "replay") replay_and_branching();
        else if (name == "randomized") randomized();
        else if (name == "stress") stress();
        else throw std::invalid_argument("Unknown test case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
