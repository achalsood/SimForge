#pragma once

#include <cstdint>
#include <limits>

namespace simforge::world
{

// Handles belong to the World that created them. Generation zero is invalid.
struct Entity
{
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{0};

    bool operator==(const Entity&) const = default;
};

} // namespace simforge::world
