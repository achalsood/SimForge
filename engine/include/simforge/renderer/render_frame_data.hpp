#pragma once
#include <simforge/renderer/camera.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>

namespace simforge::renderer
{
// Stable built-in identifiers, not Vulkan handles or indices into a mutable World.
enum class MeshId : std::uint32_t { Cube = 1, Floor = 2, Grid = 3, Dock = 4, Rack = 5, Pallet = 6, Forklift = 7, Worker = 8 };
inline constexpr std::size_t builtin_mesh_count = 8;
struct SceneBounds { glm::vec3 minimum, maximum; };
struct RenderViewport { std::uint32_t x, y, width, height; };
[[nodiscard]] Camera frame_bounds(const SceneBounds& bounds, float aspect_ratio, float margin = 1.12f);
struct RenderObject
{
    MeshId mesh{MeshId::Cube};
    RenderTransform transform;
    glm::vec3 color{1.0f}; // Linear RGB in [0,1]; opaque.
    bool neutral_material{false}; // Retain baked vertex luminance, replace its hue with color.
};
struct RenderFrameData
{
    Camera camera;
    // Owns values; copying a frame is independent. Renderer retains no references
    // after recording the call, including while GPU commands are still in flight.
    std::vector<RenderObject> objects;
    // Optional automatic framing at actual drawable aspect, including resize.
    std::optional<SceneBounds> framing_bounds;
    // Drawable pixels, relative to the surface; absent means the full surface.
    std::optional<RenderViewport> viewport;
};
[[nodiscard]] Camera frame_camera(const RenderFrameData& frame, float aspect_ratio);
void validate_frame(const RenderFrameData& frame);
[[nodiscard]] RenderFrameData make_demo_scene();
}
