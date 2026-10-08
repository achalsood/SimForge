#pragma once

#include <simforge/world/components.hpp>

#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace simforge::world
{

inline constexpr std::uint32_t snapshot_no_slot = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t snapshot_max_slots = 1'000'000;

struct EntityId
{
    std::uint32_t index{snapshot_no_slot};
    std::uint32_t generation{0};
    bool operator==(const EntityId&) const = default;
};

struct SlotSnapshot
{
    std::uint32_t generation{1};
    std::uint32_t next_free{snapshot_no_slot};
    bool alive{false};
    bool retired{false};
};

template<class T>
struct ComponentSnapshot
{
    EntityId entity;
    T value;
};

struct WorldSnapshot
{
    std::uint32_t version{1};
    std::uint32_t generation_limit{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t free_head{snapshot_no_slot};
    std::uint32_t live_count{0};
    std::vector<SlotSnapshot> slots;
    std::vector<ComponentSnapshot<Transform>> transforms;
    std::vector<ComponentSnapshot<Velocity>> velocities;
};

// Throws invalid_argument/length_error before any destination state is changed.
void validate_snapshot(const WorldSnapshot& snapshot);

} // namespace simforge::world
