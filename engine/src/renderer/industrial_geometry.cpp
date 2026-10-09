#include <simforge/renderer/geometry.hpp>
#include <stdexcept>
#include <cmath>
#include <glm/geometric.hpp>

namespace simforge::renderer
{
namespace
{
// Assemble closed, positively scaled cube parts into one reusable indexed mesh.
// Vertex normals are retained for positively scaled, axis-aligned parts.
void box(MeshGeometry& result, glm::vec3 center, glm::vec3 size, glm::vec3 color, bool safety_accent = false)
{
    const auto cube = make_cube();
    const auto base = static_cast<std::uint32_t>(result.vertices.size());
    for (auto vertex : cube.vertices)
    {
        for (int axis = 0; axis < 3; ++axis)
        { vertex.position[axis] = center[axis]+vertex.position[axis]*size[axis]; vertex.color[axis] *= color[axis]; }
        if(safety_accent) vertex.state_weight=0; // Helmet/vest/safety paint keep their material hue.
        result.vertices.push_back(vertex);
    }
    for (auto index : cube.indices) result.indices.push_back(base+index);
}
void wheel(MeshGeometry& mesh,glm::vec3 center,float radius,float width)
{
    // Eight-sided tire, axis X. Hard face normals keep the silhouette crisp.
    const auto triangle=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 outward)
    {
        if(glm::dot(glm::cross(b-a,c-a),outward)<0) std::swap(b,c);
        const auto n=glm::normalize(glm::cross(b-a,c-a));
        const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
        for(auto p:{a,b,c}) mesh.vertices.push_back({{p.x,p.y,p.z},{0.025f,0.033f,0.043f},{n.x,n.y,n.z}});
        for(auto i:{0u,1u,2u}) mesh.indices.push_back(base+i);
    };
    for(int i=0;i<8;++i)
    {
        const float a=i*6.283185307f/8,b=(i+1)*6.283185307f/8;
        const glm::vec3 first{0,std::cos(a)*radius,std::sin(a)*radius},next{0,std::cos(b)*radius,std::sin(b)*radius};
        const glm::vec3 left=center-glm::vec3{width/2,0,0},right=center+glm::vec3{width/2,0,0};
        triangle(left+first,right+first,right+next,first+next); triangle(left+first,right+next,left+next,first+next);
        triangle(left,left+next,left+first,{-1,0,0}); triangle(right,right+first,right+next,{1,0,0});
    }
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
        for (float x : {-1.45f,1.45f}) box(mesh,{x,0.7f,1.45f},{0.14f,1.4f,0.14f},yellow,true);
        box(mesh,{0,0.315f,1.5f},{2.7f,0.025f,0.25f},yellow,true);
        for(float x:{-1.5f,1.5f}) box(mesh,{x,0.18f,1.71f},{0.25f,0.25f,0.15f},dark);
        for(int i=0;i<9;++i) box(mesh,{-1.2f+i*0.3f,0.335f,1.5f},{0.12f,0.02f,0.25f},dark);
        box(mesh,{0,3.18f,-1.48f},{0.65f,0.12f,0.06f},{0.04f,0.54f,0.86f});
        break;
    case MeshId::Rack:
        for (float x : {-1.65f,1.65f}) for (float z : {-0.65f,0.65f})
            box(mesh,{x,1.8f,z},{0.12f,3.6f,0.12f},{0.16f,0.26f,0.34f});
        for (float y : {0.25f,1.65f,3.05f})
        {
            box(mesh,{0,y,0},{3.4f,0.12f,1.5f},{0.45f,0.49f,0.50f});
            box(mesh,{0,y,-0.78f},{3.4f,0.18f,0.10f},{0.70f,0.32f,0.10f});
            box(mesh,{0,y,0.78f},{3.4f,0.18f,0.10f},{0.68f,0.26f,0.055f});
            for (float x : {-0.85f,0.85f})
            {
                box(mesh,{x,y+0.15f,0},{1.40f,0.14f,1.14f},{0.30f,0.18f,0.07f});
                for(float offset:{-0.35f,0.35f})
                    box(mesh,{x+offset,y+0.43f,0},{0.64f,0.60f+(x>0?0.1f:0),1.0f},{0.46f+(y/30),0.34f+(x>0?0.07f:0),0.20f});
                box(mesh,{x,y+0.43f,0},{0.06f,0.64f,1.04f},{0.65f,0.66f,0.60f});
            }
        }
        break;
    case MeshId::Pallet:
        for (float x : {-0.5f,0.0f,0.5f}) box(mesh,{x,0.10f,0},{0.14f,0.20f,1.2f},{0.37f,0.23f,0.10f});
        for (int i = 0; i < 5; ++i) box(mesh,{0,0.23f,-0.5f+i*0.25f},{1.3f,0.09f,0.18f},{0.53f,0.36f,0.17f});
        for(float x:{-0.29f,0.29f}) for(float z:{-0.28f,0.28f})
            box(mesh,{x,0.80f,z},{0.55f,1.0f,0.53f},{0.42f,0.49f,0.53f});
        box(mesh,{0,1.29f,0},{1.16f,0.04f,1.10f},{0.53f,0.59f,0.61f});
        for (float x : {-0.35f,0.35f}) box(mesh,{x,0.82f,0},{0.065f,1.04f,1.13f},{0.60f,0.65f,0.64f});
        break;
    case MeshId::Forklift:
        box(mesh,{0,0.60f,0.2f},{1.3f,0.85f,1.65f},{0.83f,0.40f,0.055f},true);
        box(mesh,{0,0.65f,0.85f},{1.25f,0.8f,0.45f},yellow,true);
        box(mesh,{0,1.05f,0.25f},{0.60f,0.28f,0.6f},dark);
        box(mesh,{0,1.4f,0.5f},{0.60f,0.5f,0.18f},dark);
        for (float x : {-0.62f,0.62f}) for (float z : {-0.48f,0.72f})
            wheel(mesh,{x,0.32f,z},0.30f,0.25f);
        for (float x : {-0.48f,0.48f})
        {
            box(mesh,{x,1.45f,-0.68f},{0.15f,2.9f,0.18f},metal);
            box(mesh,{x,0.25f,-1.22f},{0.15f,0.12f,1.2f},metal);
            box(mesh,{x,1.65f,0.8f},{0.09f,1.3f,0.09f},metal);
        }
        box(mesh,{0,2.3f,0.3f},{1.3f,0.12f,1.4f},metal);
        box(mesh,{0,1.15f,-0.65f},{1.15f,0.16f,0.25f},metal);
        for(float y:{0.85f,1.85f,2.72f}) box(mesh,{0,y,-0.67f},{0.96f,0.075f,0.08f},metal);
        box(mesh,{0,1.7f,-0.68f},{0.07f,2.1f,0.08f},{0.46f,0.53f,0.59f});
        box(mesh,{0,1.20f,-0.1f},{0.45f,0.06f,0.28f},dark);
        box(mesh,{0,0.87f,0.94f},{0.84f,0.18f,0.12f},{0.90f,0.75f,0.28f});
        for(float x:{-0.38f,0.38f}) box(mesh,{x,2.38f,-0.2f},{0.20f,0.06f,0.12f},{0.84f,0.93f,1.0f});
        break;
    case MeshId::Worker:
        for (float x : {-0.15f,0.15f})
        { box(mesh,{x,0.40f,0},{0.18f,0.8f,0.23f},metal); box(mesh,{x,0.07f,-0.05f},{0.22f,0.14f,0.34f},dark); }
        box(mesh,{0,1.05f,0},{0.52f,0.60f,0.32f},{0.73f,0.61f,0.13f},true);
        for (float x : {-0.33f,0.33f}) box(mesh,{x,1.0f,0},{0.14f,0.55f,0.18f},{0.24f,0.35f,0.39f});
        box(mesh,{0,1.52f,0},{0.32f,0.32f,0.32f},{0.62f,0.43f,0.29f});
        box(mesh,{0,1.73f,0},{0.35f,0.13f,0.35f},yellow,true);
        box(mesh,{0,1.67f,-0.025f},{0.40f,0.04f,0.39f},yellow,true);
        box(mesh,{0,1.00f,-0.18f},{0.52f,0.05f,0.03f},{0.88f,0.91f,0.80f});
        for(float x:{-0.33f,0.33f}) box(mesh,{x,0.70f,-0.015f},{0.12f,0.14f,0.16f},{0.58f,0.38f,0.24f});
        for (float x : {-0.15f,0.15f}) box(mesh,{x,1.05f,-0.165f},{0.045f,0.56f,0.02f},{0.79f,0.83f,0.75f});
        break;
    default: throw std::invalid_argument("Unknown built-in mesh identifier");
    }
    return mesh;
}
}
