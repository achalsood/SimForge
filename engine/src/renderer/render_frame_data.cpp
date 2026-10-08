#include <simforge/renderer/render_frame_data.hpp>
#include <stdexcept>

namespace simforge::renderer
{
void validate_frame(const RenderFrameData& frame)
{
    const auto view_projection = frame.camera.view_projection();
    for (const auto& object : frame.objects)
    {
        if (object.mesh != MeshId::Cube && object.mesh != MeshId::Floor && object.mesh != MeshId::Grid)
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
