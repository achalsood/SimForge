#include <simforge/renderer/render_frame_data.hpp>
#include <simforge/renderer/geometry.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <string>

using namespace simforge::renderer;
namespace
{
void check(bool value, std::source_location where = std::source_location::current())
{ if (!value) throw std::runtime_error("Scene check failed at line "+std::to_string(where.line())); }
void near(float a, float b, float tolerance = 2e-5f) { check(std::isfinite(a) && std::abs(a-b) <= tolerance); }
template<class Function> void rejects(Function&& function)
{
    bool rejected = false; try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}
void view()
{
    Camera camera; camera.position = {0,0,5}; camera.target = {0,0,0};
    const auto matrix = camera.view();
    const auto eye = matrix*glm::vec4{camera.position,1}; near(eye.x,0); near(eye.y,0); near(eye.z,0); near(eye.w,1);
    const auto target = matrix*glm::vec4{camera.target,1}; near(target.x,0); near(target.y,0); near(target.z,-5);
    const auto above = matrix*glm::vec4{0,1,0,1}; near(above.y,1);
    const glm::vec4 point{1,2,0,1};
    const auto combined = camera.view_projection()*point, separate = camera.projection()*(matrix*point);
    for (int i = 0; i < 4; ++i) near(combined[i],separate[i]);
}
void projection()
{
    Camera camera; camera.aspect_ratio = 1;
    const auto square = camera.projection();
    const auto near_clip = square*glm::vec4{0,0,-camera.near_plane,1};
    const auto far_clip = square*glm::vec4{0,0,-camera.far_plane,1};
    near(near_clip.z/near_clip.w,0); near(far_clip.z/far_clip.w,1);
    const auto elevated = square*glm::vec4{0,1,-5,1}; check(elevated.y < 0 && elevated.w > 0);
    camera.aspect_ratio = 2; const auto wide = camera.projection();
    near(wide[0][0],square[0][0]/2); near(wide[1][1],square[1][1]);
    const float expected = 1/std::tan(camera.vertical_fov/2); near(square[0][0],expected); near(square[1][1],-expected);
    Camera front; front.position = {0,0,5}; front.target = {0,0,0};
    const auto mesh = make_cube(); const auto vp = front.view_projection();
    std::array<glm::vec2,3> projected;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& p = mesh.vertices[mesh.indices[i]].position;
        const auto clip = vp*glm::vec4{p[0],p[1],p[2],1}; projected[i] = glm::vec2{clip}/clip.w;
    }
    const auto a = projected[1]-projected[0], b = projected[2]-projected[0];
    check(a.x*b.y-a.y*b.x < 0); // Outward CCW face in Y-down framebuffer coordinates.
}
void camera_validation()
{
    const auto invalid = [](auto change) { Camera camera; change(camera); rejects([&] { (void)camera.view_projection(); }); };
    const float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
    invalid([](auto& c) { c.vertical_fov = 0; }); invalid([](auto& c) { c.vertical_fov = -1; });
    invalid([](auto& c) { c.vertical_fov = glm::pi<float>(); }); invalid([&](auto& c) { c.vertical_fov = nan; });
    invalid([](auto& c) { c.aspect_ratio = 0; }); invalid([](auto& c) { c.aspect_ratio = -1; });
    invalid([&](auto& c) { c.aspect_ratio = infinity; }); invalid([](auto& c) { c.near_plane = 0; });
    invalid([](auto& c) { c.near_plane = -1; }); invalid([](auto& c) { c.far_plane = c.near_plane; });
    invalid([](auto& c) { c.far_plane = c.near_plane/2; }); invalid([&](auto& c) { c.far_plane = infinity; });
    invalid([&](auto& c) { c.position.x = nan; }); invalid([&](auto& c) { c.target.y = infinity; });
    invalid([](auto& c) { c.target = c.position; }); invalid([](auto& c) { c.up = {0,0,0}; });
    invalid([](auto& c) { c.up = c.target-c.position; });
    invalid([](auto& c) { c.vertical_fov = std::numeric_limits<float>::denorm_min(); });
}
void transforms()
{
    RenderTransform transform{{1,2,3},{glm::half_pi<float>(),glm::half_pi<float>(),0},{2,3,4}};
    const auto result = transform.matrix()*glm::vec4{0,1,0,1};
    near(result.x,4); near(result.y,2); near(result.z,3); near(result.w,1);
    const auto origin = transform.matrix()*glm::vec4{0,0,0,1}; near(origin.x,1); near(origin.y,2); near(origin.z,3);
    RenderTransform identity;
    const auto same = identity.matrix()*glm::vec4{1,2,3,1}; near(same.x,1); near(same.y,2); near(same.z,3);
    identity.scale.x = 0; rejects([&] { (void)identity.matrix(); });
    identity.scale = {-1,1,1}; rejects([&] { (void)identity.matrix(); });
    identity.scale = {1,1,1}; identity.rotation.z = std::numeric_limits<float>::quiet_NaN(); rejects([&] { (void)identity.matrix(); });
    identity.rotation = {0,0,0}; identity.position.y = std::numeric_limits<float>::infinity(); rejects([&] { (void)identity.matrix(); });
}
void ownership()
{
    static_assert(static_cast<std::uint32_t>(MeshId::Cube) == 1);
    static_assert(static_cast<std::uint32_t>(MeshId::Floor) == 2);
    static_assert(static_cast<std::uint32_t>(MeshId::Grid) == 3);
    RenderFrameData snapshot;
    { auto original = make_demo_scene(); snapshot = original; original.objects.clear(); original.camera.position = {0,0,0}; }
    check(snapshot.objects.size() == 6); validate_frame(snapshot);
    auto independent = snapshot; independent.objects[0].transform.position.x = 100;
    check(snapshot.objects[0].transform.position.x == 0);
    RenderFrameData empty; validate_frame(empty); check(empty.objects.empty());
}
void scene_validation()
{
    const auto invalid = [](auto change) { auto frame = make_demo_scene(); change(frame); rejects([&] { validate_frame(frame); }); };
    invalid([](auto& f) { f.objects[0].mesh = static_cast<MeshId>(0); });
    invalid([](auto& f) { f.objects[0].mesh = static_cast<MeshId>(4); });
    invalid([](auto& f) { f.objects[1].color.r = -0.1f; }); invalid([](auto& f) { f.objects[1].color.b = 1.1f; });
    invalid([](auto& f) { f.objects[1].color.g = std::numeric_limits<float>::quiet_NaN(); });
    invalid([](auto& f) { f.objects[1].transform.scale.y = 0; });
    invalid([](auto& f) { f.objects[1].transform.position.z = std::numeric_limits<float>::infinity(); });
    invalid([](auto& f) { f.camera.aspect_ratio = 0; });
    invalid([](auto& f) { f.objects[1].transform.position.x = std::numeric_limits<float>::max(); });
}
glm::vec3 position(const Vertex& vertex) { return {vertex.position[0],vertex.position[1],vertex.position[2]}; }
void inspect(const MeshGeometry& mesh, bool cube)
{
    check(!mesh.vertices.empty() && mesh.indices.size()%3 == 0);
    for (const auto& vertex : mesh.vertices)
    {
        for (auto p : vertex.position) check(std::isfinite(p));
        for (auto c : vertex.color) check(std::isfinite(c) && c >= 0 && c <= 1);
    }
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        for (std::size_t offset = 0; offset < 3; ++offset) check(mesh.indices[i+offset] < mesh.vertices.size());
        const auto a = position(mesh.vertices[mesh.indices[i]]), b = position(mesh.vertices[mesh.indices[i+1]]), c = position(mesh.vertices[mesh.indices[i+2]]);
        const auto normal = glm::cross(b-a,c-a); check(glm::length(normal) > 0);
        if (cube) check(glm::dot(normal,(a+b+c)/3.0f) > 0); else check(normal.y > 0);
    }
}
void cube()
{
    const auto mesh = make_cube(); check(mesh.vertices.size() == 24 && mesh.indices.size() == 36); inspect(mesh,true);
    for (const auto& vertex : mesh.vertices) for (auto value : vertex.position) near(std::abs(value),0.5f);
    const auto floor = make_floor(); check(floor.vertices.size() == 4 && floor.indices.size() == 6); inspect(floor,false);
    for (const auto& vertex : floor.vertices) { near(vertex.position[1],0); near(std::abs(vertex.position[0]),0.5f); near(std::abs(vertex.position[2]),0.5f); }
}
void grid()
{
    const auto mesh = make_grid(); check(mesh.vertices.size() == 136 && mesh.indices.size() == 204); inspect(mesh,false);
    for (const auto& vertex : mesh.vertices)
    { near(vertex.position[1],0.008f); check(std::abs(vertex.position[0]) <= 8.008f && std::abs(vertex.position[2]) <= 8.008f); }
    const auto small = make_grid(2,2,0.05f); check(small.vertices.size() == 40 && small.indices.size() == 60); inspect(small,false);
    const auto large = make_grid(256); check(large.vertices.size() == 4104 && large.indices.size() == 6156); inspect(large,false);
}
void geometry_validation()
{
    rejects([] { (void)make_grid(0); }); rejects([] { (void)make_grid(257); });
    rejects([] { (void)make_grid(8,0); }); rejects([] { (void)make_grid(8,-1); });
    rejects([] { (void)make_grid(8,1,0); }); rejects([] { (void)make_grid(8,1,1); });
    rejects([] { (void)make_grid(8,std::numeric_limits<float>::infinity()); });
    rejects([] { (void)make_grid(8,1,std::numeric_limits<float>::quiet_NaN()); });
    rejects([] { (void)make_grid(8,std::numeric_limits<float>::max()); });
    rejects([] { (void)make_grid(8,1e20f,0.01f); });
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected scene case");
        const std::string_view name{argv[1]};
        if (name == "view") view(); else if (name == "projection") projection();
        else if (name == "camera_validation") camera_validation(); else if (name == "transforms") transforms();
        else if (name == "ownership") ownership(); else if (name == "validation") scene_validation();
        else if (name == "cube") cube(); else if (name == "grid") grid(); else if (name == "geometry_validation") geometry_validation();
        else throw std::invalid_argument("Unknown scene case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
