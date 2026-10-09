#pragma once
#include <glm/vec3.hpp>
#include <simforge/visualization/design_palette.hpp>
namespace simforge::visualization::palette
{
// Linear RGB multipliers for the existing baked industrial meshes.
inline constexpr auto waiting = design::waiting;
inline constexpr glm::vec3 queue_head{1.0f,0.70f,0.04f};
inline constexpr auto processing = design::processing, busy = design::processing;
inline constexpr auto idle = design::idle, selected = design::selected;
}
