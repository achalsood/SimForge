#pragma once

namespace simforge::world
{

struct Vec3
{
    double x{0.0};
    double y{0.0};
    double z{0.0};

    bool operator==(const Vec3&) const = default;
};

struct Transform
{
    Vec3 position{};
    Vec3 rotation{}; // Euler angles in radians; no rotation integration yet.
    Vec3 scale{1.0, 1.0, 1.0};
};

struct Velocity
{
    Vec3 linear{};
};

} // namespace simforge::world
