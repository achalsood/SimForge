#pragma once

#include <simforge/world/entity.hpp>

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace simforge::world::detail
{

// Owned by World; callers never access storage without entity validation.
template<class T>
class SparseSet
{
    static_assert(std::is_nothrow_move_constructible_v<T> &&
                  std::is_nothrow_move_assignable_v<T> &&
                  std::is_nothrow_destructible_v<T>);
    static constexpr auto missing = std::numeric_limits<std::size_t>::max();

public:
    void reserve(std::size_t count)
    {
        sparse_.reserve(count);
        entities_.reserve(count);
        components_.reserve(count);
    }

    [[nodiscard]] bool has(Entity entity) const noexcept
    {
        return entity.index < sparse_.size() && sparse_[entity.index] != missing &&
               entities_[sparse_[entity.index]] == entity;
    }

    template<class... Args>
    T& emplace(Entity entity, Args&&... args)
    {
        if (has(entity))
        {
            throw std::logic_error("Entity already has this component");
        }
        if (entity.index >= sparse_.size())
        {
            sparse_.resize(static_cast<std::size_t>(entity.index) + 1, missing);
        }
        // Grow geometrically. Reserve the parallel entity array before constructing
        // a component so constructor/allocation failure cannot desynchronize it.
        if (entities_.size() == entities_.capacity())
        {
            const auto capacity = entities_.empty() ? 1 : entities_.size() * 2;
            entities_.reserve(capacity);
        }
        components_.emplace_back(std::forward<Args>(args)...);
        entities_.push_back(entity);
        sparse_[entity.index] = components_.size() - 1;
        return components_.back();
    }

    [[nodiscard]] T& get(Entity entity)
    {
        if (!has(entity)) throw std::out_of_range("Entity has no such component");
        return components_[sparse_[entity.index]];
    }

    [[nodiscard]] const T& get(Entity entity) const
    {
        if (!has(entity)) throw std::out_of_range("Entity has no such component");
        return components_[sparse_[entity.index]];
    }

    bool remove(Entity entity) noexcept
    {
        if (!has(entity)) return false;
        const auto dense = sparse_[entity.index];
        const auto last = components_.size() - 1;
        if (dense != last)
        {
            components_[dense] = std::move(components_[last]);
            entities_[dense] = entities_[last];
            sparse_[entities_[dense].index] = dense;
        }
        components_.pop_back();
        entities_.pop_back();
        sparse_[entity.index] = missing;
        return true;
    }

    void clear() noexcept
    {
        for (Entity entity : entities_) sparse_[entity.index] = missing;
        entities_.clear();
        components_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return components_.size(); }
    [[nodiscard]] Entity entity_at(std::size_t index) const noexcept { return entities_[index]; }
    [[nodiscard]] T& component_at(std::size_t index) noexcept { return components_[index]; }
    [[nodiscard]] const T& component_at(std::size_t index) const noexcept { return components_[index]; }

private:
    std::vector<std::size_t> sparse_;
    std::vector<Entity> entities_;
    std::vector<T> components_;
};

} // namespace simforge::world::detail
