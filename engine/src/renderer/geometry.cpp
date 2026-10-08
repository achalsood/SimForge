#include <simforge/renderer/geometry.hpp>
#include <cmath>
#include <stdexcept>

namespace simforge::renderer
{
namespace
{
void quad(MeshGeometry& mesh, std::array<float,3> a, std::array<float,3> b,
    std::array<float,3> c, std::array<float,3> d, std::array<float,3> color)
{
    const auto start = static_cast<std::uint32_t>(mesh.vertices.size());
    for (auto position : {a,b,c,d}) mesh.vertices.push_back({position,color});
    for (auto index : {0u,1u,2u,2u,3u,0u}) mesh.indices.push_back(start+index);
}
}
MeshGeometry make_cube()
{
    MeshGeometry mesh; mesh.vertices.reserve(24); mesh.indices.reserve(36);
    // Face shades are baked vertex colors, not a lighting system.
    quad(mesh,{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f},{0.82f,0.82f,0.82f});
    quad(mesh,{0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.65f,0.65f,0.65f});
    quad(mesh,{0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f},{0.68f,0.68f,0.68f});
    quad(mesh,{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{-0.5f,0.5f,-0.5f},{0.75f,0.75f,0.75f});
    quad(mesh,{-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{1,1,1});
    quad(mesh,{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f},{0.55f,0.55f,0.55f});
    return mesh;
}
MeshGeometry make_floor()
{
    MeshGeometry mesh;
    quad(mesh,{-0.5f,0,-0.5f},{-0.5f,0,0.5f},{0.5f,0,0.5f},{0.5f,0,-0.5f},{1,1,1});
    return mesh;
}
MeshGeometry make_grid(std::uint32_t half_cells, float spacing, float width)
{
    if (half_cells == 0 || half_cells > 256 || !std::isfinite(spacing) || spacing <= 0 ||
        !std::isfinite(width) || width <= 0 || width >= spacing)
        throw std::invalid_argument("Grid requires 1..256 half-cells and finite 0 < width < spacing");
    const float extent = static_cast<float>(half_cells)*spacing;
    if (!std::isfinite(extent+width/2)) throw std::invalid_argument("Grid extent overflow");
    MeshGeometry mesh;
    const auto count = (2*half_cells+1)*2;
    mesh.vertices.reserve(count*4); mesh.indices.reserve(count*6);
    for (int cell = -static_cast<int>(half_cells); cell <= static_cast<int>(half_cells); ++cell)
    {
        const float center = static_cast<float>(cell)*spacing;
        const float a = center-width/2, b = center+width/2;
        if (!(a < b)) throw std::invalid_argument("Grid width is not representable at this spacing/extent");
        const std::array<float,3> color = cell % 4 == 0 ? std::array{0.22f,0.27f,0.31f} : std::array{0.085f,0.11f,0.14f};
        quad(mesh,{a,0.008f,-extent},{a,0.008f,extent},{b,0.008f,extent},{b,0.008f,-extent},color);
        quad(mesh,{-extent,0.008f,a},{-extent,0.008f,b},{extent,0.008f,b},{extent,0.008f,a},color);
    }
    return mesh;
}
}
