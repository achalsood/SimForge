#include <simforge/renderer/geometry.hpp>
#include <stdexcept>

namespace simforge::renderer
{
namespace
{
// Assemble closed, positively scaled cube parts into one reusable indexed mesh.
// Baked face shades retain the existing inexpensive ambient lighting treatment.
void box(MeshGeometry& result, glm::vec3 center, glm::vec3 size, glm::vec3 color)
{
    const auto cube = make_cube();
    const auto base = static_cast<std::uint32_t>(result.vertices.size());
    for (auto vertex : cube.vertices)
    {
        for (int axis = 0; axis < 3; ++axis)
        { vertex.position[axis] = center[axis]+vertex.position[axis]*size[axis]; vertex.color[axis] *= color[axis]; }
        result.vertices.push_back(vertex);
    }
    for (auto index : cube.indices) result.indices.push_back(base+index);
}
}
MeshGeometry make_builtin_mesh(MeshId id)
{
    if (id == MeshId::Cube) return make_cube();
    if (id == MeshId::Floor) return make_floor();
    if (id == MeshId::Grid) return make_grid();
    MeshGeometry mesh;
    const glm::vec3 metal{0.18f,0.24f,0.30f}, yellow{0.85f,0.60f,0.10f}, dark{0.035f,0.045f,0.055f};
    switch (id)
    {
    case MeshId::Dock:
        box(mesh,{0,0.15f,0},{3.6f,0.3f,3.6f},{0.22f,0.28f,0.32f});
        box(mesh,{0,1.65f,-1.65f},{3.2f,2.7f,0.15f},{0.095f,0.14f,0.19f});
        for (float x : {-1.7f,1.7f}) box(mesh,{x,1.85f,-1.65f},{0.18f,3.1f,0.28f},metal);
        box(mesh,{0,3.35f,-1.65f},{3.6f,0.18f,0.28f},metal);
        for (int i = 0; i < 7; ++i) box(mesh,{0,0.65f+i*0.35f,-1.55f},{3.1f,0.025f,0.04f},{0.28f,0.34f,0.39f});
        for (float x : {-1.45f,1.45f}) box(mesh,{x,0.7f,1.45f},{0.14f,1.4f,0.14f},yellow);
        box(mesh,{0,0.315f,1.5f},{2.7f,0.025f,0.25f},yellow);
        break;
    case MeshId::Rack:
        for (float x : {-1.65f,1.65f}) for (float z : {-0.65f,0.65f})
            box(mesh,{x,1.8f,z},{0.12f,3.6f,0.12f},{0.16f,0.26f,0.34f});
        for (float y : {0.25f,1.65f,3.05f})
        {
            box(mesh,{0,y,0},{3.4f,0.12f,1.5f},{0.45f,0.49f,0.50f});
            box(mesh,{0,y,-0.78f},{3.4f,0.18f,0.10f},{0.70f,0.32f,0.10f});
            for (float x : {-0.85f,0.85f}) box(mesh,{x,y+0.43f,0},{1.35f,0.72f,1.1f},{0.33f,0.39f,0.41f});
        }
        break;
    case MeshId::Pallet:
        for (float x : {-0.5f,0.0f,0.5f}) box(mesh,{x,0.10f,0},{0.14f,0.20f,1.2f},{0.37f,0.23f,0.10f});
        for (int i = 0; i < 5; ++i) box(mesh,{0,0.23f,-0.5f+i*0.25f},{1.3f,0.09f,0.18f},{0.53f,0.36f,0.17f});
        box(mesh,{0,0.80f,0},{1.15f,1.0f,1.1f},{0.29f,0.43f,0.47f});
        for (float x : {-0.35f,0.35f}) box(mesh,{x,0.82f,0},{0.065f,1.04f,1.13f},{0.60f,0.65f,0.64f});
        break;
    case MeshId::Forklift:
        box(mesh,{0,0.60f,0.2f},{1.3f,0.85f,1.65f},{0.83f,0.40f,0.055f});
        box(mesh,{0,0.65f,0.85f},{1.25f,0.8f,0.45f},yellow);
        box(mesh,{0,1.05f,0.25f},{0.60f,0.28f,0.6f},dark);
        box(mesh,{0,1.4f,0.5f},{0.60f,0.5f,0.18f},dark);
        for (float x : {-0.62f,0.62f}) for (float z : {-0.48f,0.72f})
            box(mesh,{x,0.32f,z},{0.25f,0.6f,0.6f},dark);
        for (float x : {-0.48f,0.48f})
        {
            box(mesh,{x,1.45f,-0.68f},{0.15f,2.9f,0.18f},metal);
            box(mesh,{x,0.25f,-1.22f},{0.15f,0.12f,1.2f},metal);
            box(mesh,{x,1.65f,0.8f},{0.09f,1.3f,0.09f},metal);
        }
        box(mesh,{0,2.3f,0.3f},{1.3f,0.12f,1.4f},metal);
        box(mesh,{0,1.15f,-0.65f},{1.15f,0.16f,0.25f},metal);
        break;
    case MeshId::Worker:
        for (float x : {-0.15f,0.15f})
        { box(mesh,{x,0.40f,0},{0.18f,0.8f,0.23f},metal); box(mesh,{x,0.07f,-0.05f},{0.22f,0.14f,0.34f},dark); }
        box(mesh,{0,1.05f,0},{0.52f,0.60f,0.32f},{0.73f,0.61f,0.13f});
        for (float x : {-0.33f,0.33f}) box(mesh,{x,1.0f,0},{0.14f,0.55f,0.18f},{0.24f,0.35f,0.39f});
        box(mesh,{0,1.52f,0},{0.32f,0.32f,0.32f},{0.62f,0.43f,0.29f});
        box(mesh,{0,1.73f,0},{0.40f,0.13f,0.40f},yellow);
        for (float x : {-0.15f,0.15f}) box(mesh,{x,1.05f,-0.165f},{0.045f,0.56f,0.02f},{0.79f,0.83f,0.75f});
        break;
    default: throw std::invalid_argument("Unknown built-in mesh identifier");
    }
    return mesh;
}
}
