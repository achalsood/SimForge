#include <simforge/world/world.hpp>

namespace simforge::world
{

World::World(std::uint32_t generation_limit) : generation_limit_{generation_limit}
{
    if (generation_limit == 0) throw std::invalid_argument("Generation limit must be positive");
}

Entity World::create_entity()
{
    require_structural_access();
    std::uint32_t index;
    if (free_head_ != no_slot)
    {
        index = free_head_;
        free_head_ = slots_[index].next_free;
    }
    else
    {
        if (slots_.size() >= no_slot) throw std::overflow_error("Entity index space exhausted");
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    auto& slot = slots_[index];
    slot.alive = true;
    slot.next_free = no_slot;
    ++live_count_;
    return Entity{index, slot.generation, owner_};
}

bool World::valid(const Entity& entity) const noexcept
{
    return entity.owner_ == owner_ && entity.generation != 0 && entity.index < slots_.size() &&
           slots_[entity.index].alive && slots_[entity.index].generation == entity.generation;
}

Entity World::handle_at(std::uint32_t index) const
{
    if (index >= slots_.size() || !slots_[index].alive)
        throw std::out_of_range("No living entity at this index");
    return Entity{index, slots_[index].generation, owner_};
}

void World::require_valid(const Entity& entity) const
{
    if (!valid(entity)) throw std::invalid_argument("Invalid or stale entity handle");
}

void World::require_structural_access() const
{
    if (iteration_depth_ != 0) throw std::logic_error("Structural mutation during World iteration");
}

void World::release_slot(std::uint32_t index) noexcept
{
    auto& slot = slots_[index];
    slot.alive = false;
    // Never wrap: the last generation permanently retires its slot.
    if (slot.generation < generation_limit_)
    {
        ++slot.generation;
        slot.next_free = free_head_;
        free_head_ = index;
    }
    else slot.retired = true;
    --live_count_;
}

void World::destroy_entity(const Entity& entity)
{
    require_structural_access();
    require_valid(entity);
    const EntityId id{entity.index, entity.generation};
    std::apply([&](auto&... storage) { (storage.remove(id), ...); }, components_);
    release_slot(entity.index);
}

void World::clear()
{
    require_structural_access();
    std::apply([](auto&... storage) { (storage.clear(), ...); }, components_);
    // Keep generation history and retired slots: clearing vectors would revive
    // old handles when subsequent entities reused their indices/generations.
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].alive) release_slot(static_cast<std::uint32_t>(i));
}

void World::reserve(std::size_t entities)
{
    require_structural_access();
    if (entities > no_slot) throw std::length_error("Entity reserve exceeds index space");
    slots_.reserve(entities);
    std::apply([&](auto&... storage) { (storage.reserve(entities), ...); }, components_);
}

} // namespace simforge::world
