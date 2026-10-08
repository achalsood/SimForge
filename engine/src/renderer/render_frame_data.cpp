#include <simforge/renderer/render_frame_data.hpp>
#include <stdexcept>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>

namespace simforge::renderer
{
Camera frame_bounds(const SceneBounds& bounds, float aspect_ratio)
{
    if (!finite(bounds.minimum) || !finite(bounds.maximum) ||
        bounds.minimum.x >= bounds.maximum.x || bounds.minimum.y >= bounds.maximum.y || bounds.minimum.z >= bounds.maximum.z)
        throw std::invalid_argument("Scene bounds must be finite and have positive extent");
    Camera camera; camera.aspect_ratio = aspect_ratio; camera.validate();
    const glm::dvec3 center = (glm::dvec3{bounds.minimum}+glm::dvec3{bounds.maximum})*0.5;
    const auto outward = glm::normalize(glm::dvec3{0.85,1.1,1.25});
    const auto right = glm::normalize(glm::cross(glm::dvec3{0,1,0},outward));
    const auto up = glm::cross(outward,right);
    const double tangent = std::tan(camera.vertical_fov*0.5);
    const double horizontal = tangent*aspect_ratio/1.12, vertical = tangent/1.12;
    std::array<glm::dvec3,8> projected;
    double distance = 1, radius = 0;
    for (int i = 0; i < 8; ++i)
    {
        const glm::dvec3 corner{(i&1) ? bounds.maximum.x : bounds.minimum.x,
            (i&2) ? bounds.maximum.y : bounds.minimum.y,(i&4) ? bounds.maximum.z : bounds.minimum.z};
        const auto offset = corner-center;
        projected[i] = {glm::dot(offset,right),glm::dot(offset,up),glm::dot(offset,outward)};
        distance = std::max(distance,projected[i].z+1);
        radius = std::max(radius,glm::length(offset));
    }
    // Fit the intersection of all perspective corner intervals. Translating the
    // camera in its image plane centers the asymmetric perspective footprint.
    for (const auto& a : projected) for (const auto& b : projected)
        distance = std::max(distance,(a.z+b.z)*0.5+std::max(
            (a.x-b.x)/(2*horizontal),(a.y-b.y)/(2*vertical)));
    glm::dvec2 low{-std::numeric_limits<double>::infinity()}, high{std::numeric_limits<double>::infinity()};
    for (const auto& p : projected)
    {
        low.x = std::max(low.x,p.x-(distance-p.z)*horizontal); high.x = std::min(high.x,p.x+(distance-p.z)*horizontal);
        low.y = std::max(low.y,p.y-(distance-p.z)*vertical); high.y = std::min(high.y,p.y+(distance-p.z)*vertical);
    }
    const auto shift = (low+high)*0.5;
    const auto target = center+right*shift.x+up*shift.y;
    camera.target = glm::vec3{target}; camera.position = glm::vec3{target+outward*distance};
    camera.far_plane = static_cast<float>(distance+radius+glm::length(shift)+10);
    camera.validate(); (void)camera.view_projection(); return camera;
}
Camera frame_camera(const RenderFrameData& frame, float aspect_ratio)
{
    if (frame.framing_bounds) return frame_bounds(*frame.framing_bounds,aspect_ratio);
    auto camera = frame.camera; camera.aspect_ratio = aspect_ratio; camera.validate(); return camera;
}
void validate_frame(const RenderFrameData& frame)
{
    const auto view_projection = frame_camera(frame,frame.camera.aspect_ratio).view_projection();
    for (const auto& object : frame.objects)
    {
        if (static_cast<std::uint32_t>(object.mesh) < 1 || static_cast<std::uint32_t>(object.mesh) > builtin_mesh_count)
            throw std::invalid_argument("Unknown built-in mesh identifier");
        if (!finite(object.color) || object.color.x < 0 || object.color.y < 0 || object.color.z < 0 ||
            object.color.x > 1 || object.color.y > 1 || object.color.z > 1)
            throw std::invalid_argument("Render color must contain finite linear RGB values in [0,1]");
        if (!finite(view_projection*object.transform.matrix())) throw std::invalid_argument("Object camera transform overflow");
    }
}
RenderFrameData make_demo_scene()
{
    RenderFrameData frame;
    frame.objects = {
        {MeshId::Floor,{{0,0,0},{0,0,0},{16,1,16}},{0.055f,0.068f,0.084f}},
        {MeshId::Grid,{}, {1,1,1}},
        {MeshId::Cube,{{-3,1.2f,1.5f},{0,0,0},{1.8f,2.4f,1.8f}},{0.80f,0.24f,0.045f}},
        {MeshId::Cube,{{-0.2f,0.75f,3.2f},{0,0,0},{1.5f,1.5f,1.5f}},{0.035f,0.50f,0.54f}},
        {MeshId::Cube,{{2.8f,1.6f,-1.8f},{0,0,0},{1.7f,3.2f,1.7f}},{0.84f,0.61f,0.075f}},
        {MeshId::Cube,{{-0.2f,1.05f,0.1f},{0,0,0},{2.1f,2.1f,2.1f}},{0.38f,0.13f,0.64f}}
    };
    return frame;
}
}
