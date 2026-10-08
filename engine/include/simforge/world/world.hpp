#pragma once

#include <simforge/world/components.hpp>
#include <simforge/world/entity.hpp>
#include <simforge/world/sparse_set.hpp>
#include <simforge/world/world_snapshot.hpp>

#include <functional>
#include <tuple>

namespace simforge::world
{

class World
{
public:
    // A smaller ceiling can enforce a handle reuse budget; exhausted slots retire.
    explicit World(std::uint32_t generation_limit = std::numeric_limits<std::uint32_t>::max());
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    [[nodiscard]] Entity create_entity();
    void destroy_entity(const Entity& entity);
    [[nodiscard]] bool valid(const Entity& entity) const noexcept;
    [[nodiscard]] Entity handle_at(std::uint32_t index) const;
    [[nodiscard]] WorldSnapshot capture_snapshot() const;
    void restore_snapshot(const WorldSnapshot& snapshot);
    void clear();
    void reserve(std::size_t entities);
    [[nodiscard]] std::size_t entity_count() const noexcept { return live_count_; }

    template<class T, class... Args>
    T& emplace(const Entity& entity, Args&&... args)
    {
        require_structural_access();
        require_valid(entity);
        return storage<T>().emplace({entity.index, entity.generation}, std::forward<Args>(args)...);
    }

    template<class T>
    bool remove(const Entity& entity)
    {
        require_structural_access();
        require_valid(entity);
        return storage<T>().remove({entity.index, entity.generation});
    }

    template<class T>
    [[nodiscard]] bool has(const Entity& entity) const noexcept
    {
        return valid(entity) && storage<T>().has({entity.index, entity.generation});
    }

    template<class T>
    [[nodiscard]] T& get(const Entity& entity)
    {
        require_valid(entity);
        return storage<T>().get({entity.index, entity.generation});
    }

    template<class T>
    [[nodiscard]] const T& get(const Entity& entity) const
    {
        require_valid(entity);
        return storage<T>().get({entity.index, entity.generation});
    }

    template<class T>
    [[nodiscard]] std::size_t component_count() const noexcept { return storage<T>().size(); }

    // Traverses the first type's dense order and filters the remaining types.
    // Values may be edited; structural mutation is forbidden, including via aliases.
    template<class First, class... Rest, class Function>
    void each(Function&& function)
    {
        IterationGuard guard{*this};
        auto& first = storage<First>();
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            const EntityId id = first.entity_at(i);
            if ((storage<Rest>().has(id) && ...))
                std::invoke(function, Entity{id.index, id.generation, owner_},
                            first.component_at(i), storage<Rest>().get(id)...);
        }
    }

    template<class First, class... Rest, class Function>
    void each(Function&& function) const
    {
        IterationGuard guard{*this};
        const auto& first = storage<First>();
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            const EntityId id = first.entity_at(i);
            if ((storage<Rest>().has(id) && ...))
                std::invoke(function, Entity{id.index, id.generation, owner_},
                            first.component_at(i), storage<Rest>().get(id)...);
        }
    }

    // Same dense traversal/structural guard, without constructing public handles.
    // Systems that only need component values avoid ownership-token copies.
    template<class First, class... Rest, class Function>
    void each_components(Function&& function)
    {
        IterationGuard guard{*this};
        auto& first = storage<First>();
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            const EntityId id = first.entity_at(i);
            if ((storage<Rest>().has(id) && ...))
                std::invoke(function, first.component_at(i), storage<Rest>().get(id)...);
        }
    }

    template<class First, class... Rest, class Function>
    void each_components(Function&& function) const
    {
        IterationGuard guard{*this};
        const auto& first = storage<First>();
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            const EntityId id = first.entity_at(i);
            if ((storage<Rest>().has(id) && ...))
                std::invoke(function, first.component_at(i), storage<Rest>().get(id)...);
        }
    }

private:
    static constexpr auto no_slot = std::numeric_limits<std::uint32_t>::max();
    using Slot = SlotSnapshot;

    struct IterationGuard
    {
        explicit IterationGuard(const World& world) : world_{world} { ++world_.iteration_depth_; }
        ~IterationGuard() { --world_.iteration_depth_; }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;
        const World& world_;
    };

    template<class T> auto& storage() noexcept
    {
        static_assert(std::is_same_v<T, Transform> || std::is_same_v<T, Velocity>,
                      "This milestone supports Transform and Velocity components");
        return std::get<detail::SparseSet<T>>(components_);
    }
    template<class T> const auto& storage() const noexcept
    {
        static_assert(std::is_same_v<T, Transform> || std::is_same_v<T, Velocity>);
        return std::get<detail::SparseSet<T>>(components_);
    }

    void require_valid(const Entity& entity) const;
    void require_structural_access() const;
    void release_slot(std::uint32_t index) noexcept;

    std::vector<Slot> slots_;
    std::shared_ptr<const std::byte> owner_{std::make_shared<const std::byte>()};
    std::tuple<detail::SparseSet<Transform>, detail::SparseSet<Velocity>> components_;
    std::uint32_t generation_limit_;
    std::uint32_t free_head_{no_slot};
    std::size_t live_count_{0};
    mutable std::size_t iteration_depth_{0};
};

} // namespace simforge::world
