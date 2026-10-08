#pragma once
#include <simforge/renderer/camera.hpp>
#include <cstdint>
#include <vector>

namespace simforge::renderer
{
// Stable built-in identifiers, not Vulkan handles or indices into a mutable World.
enum class MeshId : std::uint32_t { Cube = 1, Floor = 2, Grid = 3 };
struct RenderObject
{
    MeshId mesh{MeshId::Cube};
    RenderTransform transform;
    glm::vec3 color{1.0f}; // Linear RGB in [0,1]; opaque.
};
struct RenderFrameData
{
    Camera camera;
    // Owns values; copying a frame is independent. Renderer retains no references
    // after recording the call, including while GPU commands are still in flight.
    std::vector<RenderObject> objects;
};
void validate_frame(const RenderFrameData& frame);
[[nodiscard]] RenderFrameData make_demo_scene();
}
