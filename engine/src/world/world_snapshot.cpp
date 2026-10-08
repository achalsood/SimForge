#include <simforge/world/world.hpp>

#include <cmath>

namespace simforge::world
{
namespace
{
bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool finite(const Transform& value)
{
    return finite(value.position) && finite(value.rotation) && finite(value.scale);
}
bool finite(const Velocity& value) { return finite(value.linear); }

template<class T>
void validate_components(const WorldSnapshot& snapshot, const std::vector<ComponentSnapshot<T>>& components)
{
    if (components.size() > snapshot.live_count)
        throw std::invalid_argument("Component count exceeds living entity count");
    std::vector<bool> seen(snapshot.slots.size(), false);
    for (const auto& record : components)
    {
        const auto id = record.entity;
        if (id.index >= snapshot.slots.size() || !snapshot.slots[id.index].alive ||
            id.generation != snapshot.slots[id.index].generation || seen[id.index] || !finite(record.value))
            throw std::invalid_argument("Invalid, duplicate or nonfinite component record");
        seen[id.index] = true;
    }
}
}

void validate_snapshot(const WorldSnapshot& snapshot)
{
    if (snapshot.version != 1 || snapshot.generation_limit == 0)
        throw std::invalid_argument("Invalid or unsupported world snapshot version/generation limit");
    if (snapshot.slots.size() > snapshot_max_slots)
        throw std::length_error("World snapshot exceeds slot limit");
    std::vector<bool> free(snapshot.slots.size(), false);
    auto next = snapshot.free_head;
    while (next != snapshot_no_slot)
    {
        if (next >= snapshot.slots.size() || free[next])
            throw std::invalid_argument("Invalid or cyclic snapshot free list");
        const auto& slot = snapshot.slots[next];
        if (slot.alive || slot.retired)
            throw std::invalid_argument("Living/retired slot in snapshot free list");
        free[next] = true;
        next = slot.next_free;
    }
    std::size_t living = 0;
    for (std::size_t i = 0; i < snapshot.slots.size(); ++i)
    {
        const auto& slot = snapshot.slots[i];
        if (slot.generation == 0 || slot.generation > snapshot.generation_limit)
            throw std::invalid_argument("Snapshot generation out of range");
        if (slot.alive)
        {
            if (slot.retired || slot.next_free != snapshot_no_slot)
                throw std::invalid_argument("Invalid living snapshot slot");
            ++living;
        }
        else if (slot.retired)
        {
            if (slot.generation != snapshot.generation_limit || slot.next_free != snapshot_no_slot)
                throw std::invalid_argument("Invalid retired snapshot slot");
        }
        else if (!free[i] || slot.generation < 2)
            throw std::invalid_argument("Missing or impossible free snapshot slot");
    }
    if (living != snapshot.live_count)
        throw std::invalid_argument("Inconsistent snapshot living entity count");
    validate_components(snapshot, snapshot.transforms);
    validate_components(snapshot, snapshot.velocities);
}

WorldSnapshot World::capture_snapshot() const
{
    if (slots_.size() > snapshot_max_slots) throw std::length_error("World snapshot exceeds slot limit");
    WorldSnapshot snapshot;
    snapshot.generation_limit = generation_limit_;
    snapshot.free_head = free_head_;
    snapshot.live_count = static_cast<std::uint32_t>(live_count_);
    snapshot.slots = slots_;
    const auto capture = []<class T>(const detail::SparseSet<T>& source, auto& records)
    {
        records.reserve(source.size());
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            const auto& entity = source.entity_at(i);
            records.push_back({{entity.index, entity.generation}, source.component_at(i)});
        }
    };
    capture(storage<Transform>(), snapshot.transforms);
    capture(storage<Velocity>(), snapshot.velocities);
    validate_snapshot(snapshot);
    return snapshot;
}

void World::restore_snapshot(const WorldSnapshot& snapshot)
{
    require_structural_access();
    validate_snapshot(snapshot);
    World replacement{snapshot.generation_limit};
    replacement.slots_ = snapshot.slots;
    replacement.live_count_ = snapshot.live_count;
    replacement.free_head_ = snapshot.free_head;
    const auto rebuild = [&]<class T>(const std::vector<ComponentSnapshot<T>>& records)
    {
        auto& target = replacement.storage<T>();
        // Sparse indices may be far above the dense component count.
        target.reserve(snapshot.slots.size(), records.size());
        for (const auto& record : records)
            target.emplace(record.entity, record.value);
    };
    rebuild(snapshot.transforms);
    rebuild(snapshot.velocities);
    // All potentially throwing work completed. Vector/shared_ptr swaps are noexcept.
    static_assert(std::is_nothrow_swappable_v<decltype(components_)>);
    slots_.swap(replacement.slots_);
    components_.swap(replacement.components_);
    owner_.swap(replacement.owner_);
    generation_limit_ = replacement.generation_limit_;
    free_head_ = replacement.free_head_;
    live_count_ = replacement.live_count_;
}

} // namespace simforge::world
