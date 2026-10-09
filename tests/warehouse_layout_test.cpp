#include <simforge/visualization/warehouse_layout.hpp>
#include <simforge/renderer/geometry.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace simforge;
using namespace simforge::visualization;
namespace
{
void check(bool value, std::source_location where = std::source_location::current())
{ if (!value) throw std::runtime_error("Warehouse layout check at line "+std::to_string(where.line())); }
template<class Function> void rejects(Function function)
{ bool caught = false; try { function(); } catch (const std::invalid_argument&) { caught = true; } check(caught); }
scenario::ScenarioDefinition definition(std::uint32_t bays, std::uint32_t workers, std::uint32_t forklifts)
{
    scenario::ScenarioDefinition result; result.name = "Layout test";
    result.domain = scenario::WarehouseConfig{bays,workers,forklifts,1,10}; return result;
}
std::size_t count(const WarehouseLayout& layout, WarehouseVisualKind kind)
{ return std::count_if(layout.objects.begin(),layout.objects.end(),[&](const auto& o) { return o.kind == kind; }); }
bool inside(const renderer::SceneBounds& outer, const renderer::SceneBounds& inner)
{
    for (int axis = 0; axis < 3; ++axis)
        if (inner.minimum[axis] < outer.minimum[axis]-1e-4f || inner.maximum[axis] > outer.maximum[axis]+1e-4f) return false;
    return true;
}
bool overlap(const renderer::SceneBounds& a, const renderer::SceneBounds& b)
{ return a.minimum.x < b.maximum.x && b.minimum.x < a.maximum.x && a.minimum.z < b.maximum.z && b.minimum.z < a.maximum.z; }
void equal(const WarehouseLayout& a, const WarehouseLayout& b)
{
    check(a.bounds.minimum == b.bounds.minimum && a.bounds.maximum == b.bounds.maximum);
    check(a.zones.size() == b.zones.size() && a.objects.size() == b.objects.size());
    for (std::size_t i = 0; i < a.zones.size(); ++i)
        check(a.zones[i].kind == b.zones[i].kind && a.zones[i].bounds.minimum == b.zones[i].bounds.minimum && a.zones[i].bounds.maximum == b.zones[i].bounds.maximum);
    for (std::size_t i = 0; i < a.objects.size(); ++i)
    {
        const auto& x = a.objects[i]; const auto& y = b.objects[i];
        check(x.id == y.id && x.kind == y.kind && x.render.mesh == y.render.mesh && x.render.color == y.render.color);
        check(x.render.transform.position == y.render.transform.position && x.render.transform.rotation == y.render.transform.rotation &&
            x.render.transform.scale == y.render.transform.scale && x.bounds.minimum == y.bounds.minimum && x.bounds.maximum == y.bounds.maximum);
    }
}
void audit(const WarehouseLayout& layout)
{
    std::array<std::uint32_t,7> ids{};
    for (std::size_t i = 0; i < layout.zones.size(); ++i)
    {
        check(inside(layout.bounds,layout.zones[i].bounds));
        for (std::size_t j = i+1; j < layout.zones.size(); ++j) check(!overlap(layout.zones[i].bounds,layout.zones[j].bounds));
    }
    for (std::size_t i = 0; i < layout.objects.size(); ++i)
    {
        const auto& o = layout.objects[i];
        check(o.id == ++ids[static_cast<std::size_t>(o.kind)]);
        check(renderer::finite(o.render.transform.matrix()) && inside(layout.bounds,o.bounds));
        if (o.kind == WarehouseVisualKind::Floor || o.kind == WarehouseVisualKind::FloorMarking) continue;
        const auto zone_kind = o.kind == WarehouseVisualKind::LoadingBay ? WarehouseZoneKind::Docks :
            o.kind == WarehouseVisualKind::StorageRack ? WarehouseZoneKind::Storage :
            o.kind == WarehouseVisualKind::Pallet ? WarehouseZoneKind::Cargo :
            o.kind == WarehouseVisualKind::Forklift ? WarehouseZoneKind::Forklifts : WarehouseZoneKind::Workers;
        const auto zone = std::find_if(layout.zones.begin(),layout.zones.end(),[&](const auto& z) { return z.kind == zone_kind; });
        check(zone != layout.zones.end() && inside(zone->bounds,o.bounds));
        for (const auto& z : layout.zones)
            if (z.kind == WarehouseZoneKind::MainAisle || z.kind == WarehouseZoneKind::CrossAisle) check(!overlap(o.bounds,z.bounds));
        for (std::size_t j = i+1; j < layout.objects.size(); ++j)
            if (layout.objects[j].kind != WarehouseVisualKind::Floor && layout.objects[j].kind != WarehouseVisualKind::FloorMarking)
                check(!overlap(o.bounds,layout.objects[j].bounds));
    }
    auto frame = make_warehouse_frame(layout); renderer::validate_frame(frame);
    check(frame.objects.size() == layout.objects.size());
    auto copy = frame; copy.objects.clear(); check(!frame.objects.empty());
}
void counts()
{
    for (auto resources : {std::array{0u,0u,0u},std::array{1u,1u,1u},std::array{0u,10u,2u},std::array{3u,0u,2u},
                          std::array{3u,10u,0u},std::array{64u,128u,100u},std::array{256u,256u,256u}})
    {
        const auto layout = build_warehouse_layout(definition(resources[0],resources[1],resources[2]));
        check(count(layout,WarehouseVisualKind::LoadingBay) == resources[0]);
        check(count(layout,WarehouseVisualKind::Worker) == resources[1]);
        check(count(layout,WarehouseVisualKind::Forklift) == resources[2]); audit(layout);
    }
}
void deterministic()
{
    auto input = definition(3,10,2); const auto original = build_warehouse_layout(input);
    for (int i = 0; i < 10; ++i) equal(original,build_warehouse_layout(input));
    input.name = "Other name"; input.simulation.tick_rate = 30; input.simulation.duration_seconds = 100; input.simulation.random_seed = 999;
    auto& config = std::get<scenario::WarehouseConfig>(input.domain);
    config.shipment_interval_seconds = 20; config.processing_duration_seconds = 400;
    equal(original,build_warehouse_layout(input));
    // An owned frame remains valid after destroying the layout that produced it.
    const auto frame = make_warehouse_frame(build_warehouse_layout(input)); renderer::validate_frame(frame);
}
void spatial()
{
    // Deterministic varied capacity coverage, including row/column boundaries.
    for (std::uint32_t i = 0; i < 40; ++i)
        audit(build_warehouse_layout(definition(i*7%257,i*13%257,i*17%257)));
}
void geometry()
{
    for (std::uint32_t value = 4; value <= renderer::builtin_mesh_count; ++value)
    {
        const auto id = static_cast<renderer::MeshId>(value);
        const auto mesh = renderer::make_builtin_mesh(id), repeat = renderer::make_builtin_mesh(id);
        check(!mesh.vertices.empty() && mesh.indices.size()%3 == 0);
        check(mesh.indices == repeat.indices && mesh.vertices.size() == repeat.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const auto& vertex = mesh.vertices[i]; check(vertex.position == repeat.vertices[i].position && vertex.color == repeat.vertices[i].color && vertex.normal == repeat.vertices[i].normal);
            for (auto p : vertex.position) check(std::isfinite(p));
            for (auto c : vertex.color) check(std::isfinite(c) && c >= 0 && c <= 1);
        }
        for (std::size_t t=0;t<mesh.indices.size();t+=3)
        {
            std::array<glm::vec3,3> points;
            for(std::size_t k=0;k<3;++k)
            { const auto index=mesh.indices[t+k]; check(index<mesh.vertices.size()); const auto& v=mesh.vertices[index]; points[k]={v.position[0],v.position[1],v.position[2]};
              const glm::vec3 n{v.normal[0],v.normal[1],v.normal[2]}; check(std::abs(glm::length(n)-1)<1e-5f); }
            const auto& v=mesh.vertices[mesh.indices[t]]; const glm::vec3 n{v.normal[0],v.normal[1],v.normal[2]};
            check(glm::dot(glm::cross(points[1]-points[0],points[2]-points[0]),n)>0);
        }
    }
    const auto layout = build_warehouse_layout(definition(3,10,2));
    for (const auto& object : layout.objects)
    {
        const auto mesh = renderer::make_builtin_mesh(object.render.mesh);
        for (const auto& vertex : mesh.vertices)
        {
            const auto& p = vertex.position; const glm::vec3 point{object.render.transform.matrix()*glm::vec4{p[0],p[1],p[2],1}};
            check(inside(object.bounds,{point,point}));
        }
    }
}
void camera()
{
    for (auto n : {0u,1u,3u,64u,256u})
    {
        const auto layout = build_warehouse_layout(definition(n,n,n)); const auto frame = make_warehouse_frame(layout);
        for (auto aspect : {0.25f,0.5f,1.0f,1.5f,16.0f/9.0f,4.0f})
        {
            const auto c = renderer::frame_camera(frame,aspect); check(c.aspect_ratio == aspect && c.near_plane > 0 && c.far_plane > c.near_plane);
            const auto vp = c.view_projection();
            for (int corner = 0; corner < 8; ++corner)
            {
                const auto& a = layout.bounds.minimum; const auto& b = layout.bounds.maximum;
                const auto clip = vp*glm::vec4{(corner&1)?b.x:a.x,(corner&2)?b.y:a.y,(corner&4)?b.z:a.z,1};
                check(clip.w > 0 && std::abs(clip.x/clip.w) < 1 && std::abs(clip.y/clip.w) < 1 && clip.z/clip.w > 0 && clip.z/clip.w < 1);
            }
        }
    }
}
void examples()
{
    for (const auto& pair : {std::pair{"warehouse-baseline.json",2u},std::pair{"warehouse-constrained.json",1u},std::pair{"warehouse-higher-capacity.json",3u}})
    {
        const auto input = scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples"/pair.first);
        const auto layout = build_warehouse_layout(input); audit(layout);
        check(count(layout,WarehouseVisualKind::LoadingBay) == 3 && count(layout,WarehouseVisualKind::Worker) == 10 && count(layout,WarehouseVisualKind::Forklift) == pair.second);
    }
}
void invalid()
{
    rejects([] { (void)build_warehouse_layout(definition(257,0,0)); });
    rejects([] { (void)build_warehouse_layout(definition(0,257,0)); });
    rejects([] { (void)build_warehouse_layout(definition(0,0,257)); });
    rejects([] { (void)build_warehouse_layout(definition(std::numeric_limits<std::uint32_t>::max(),0,0)); });
    rejects([] { auto d = definition(1,1,1); d.schema_version = 99; (void)build_warehouse_layout(d); });
    rejects([] { auto d = definition(1,1,1); d.domain = scenario::MovementConfig{}; (void)build_warehouse_layout(d); });
    rejects([] { (void)renderer::frame_bounds({{0,0,0},{0,1,1}},1); });
    rejects([] { (void)renderer::frame_bounds({{0,0,0},{1,1,1}},0); });
    rejects([] { (void)renderer::frame_bounds({{0,0,0},{1,std::numeric_limits<float>::infinity(),1}},1); });
    rejects([] { (void)renderer::make_builtin_mesh(static_cast<renderer::MeshId>(9)); });
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected warehouse layout case");
        const std::string_view name{argv[1]};
        if (name == "counts") counts(); else if (name == "deterministic") deterministic(); else if (name == "spatial") spatial();
        else if (name == "geometry") geometry(); else if (name == "camera") camera(); else if (name == "examples") examples();
        else if (name == "invalid") invalid(); else throw std::invalid_argument("Unknown layout case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
