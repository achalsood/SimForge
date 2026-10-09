#pragma once
#include <glm/vec3.hpp>
namespace simforge::visualization::palette
{
// Linear RGB multipliers for the existing baked industrial meshes.
inline constexpr glm::vec3 waiting{1.0f,0.62f,0.12f}, queue_head{1.0f,0.82f,0.22f};
inline constexpr glm::vec3 processing{0.08f,0.62f,1.0f}, busy{0.18f,0.76f,1.0f};
inline constexpr glm::vec3 idle{0.28f,0.31f,0.35f}, selected{1.0f,0.86f,0.35f};
}
