#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
#include <simforge/renderer/render_frame_data.hpp>

namespace simforge::renderer
{
// Defined GPU vertex ABI, not a persistent/raw simulation serialization format.
struct Vertex { std::array<float,3> position, color; };
static_assert(std::is_standard_layout_v<Vertex> && std::is_trivially_copyable_v<Vertex>);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(Vertex) == 24 && offsetof(Vertex,position) == 0 && offsetof(Vertex,color) == 12);
static_assert(sizeof(std::uint32_t) == 4);
struct MeshGeometry { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };
[[nodiscard]] MeshGeometry make_cube(); // Unit cube centered at origin, outward CCW faces.
[[nodiscard]] MeshGeometry make_floor(); // Unit XZ plane, normal +Y.
[[nodiscard]] MeshGeometry make_grid(std::uint32_t half_cells = 8, float spacing = 1.0f, float width = 0.014f);
[[nodiscard]] MeshGeometry make_builtin_mesh(MeshId id);
}
