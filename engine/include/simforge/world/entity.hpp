#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

namespace simforge::world
{

class World;

// Ownership tokens remain alive while external handles exist, preventing address
// reuse from reviving a handle. Tokens are never part of logical snapshot state.
struct Entity
{
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{0};

    Entity() = default;
    // An unowned ID is not a valid public handle. Use World::handle_at to reacquire.
    Entity(std::uint32_t index_value, std::uint32_t generation_value)
        : index{index_value}, generation{generation_value} {}

    bool operator==(const Entity&) const = default;

private:
    friend class World;
    Entity(std::uint32_t index_value, std::uint32_t generation_value,
           std::shared_ptr<const std::byte> owner)
        : index{index_value}, generation{generation_value}, owner_{std::move(owner)} {}
    std::shared_ptr<const std::byte> owner_;
};

} // namespace simforge::world
