#include <simforge/visualization/warehouse_layout.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simforge::visualization
{
namespace
{
struct Grid
{
    std::uint32_t columns, rows;
    float width, depth, dx, dz;
    Grid(std::uint32_t count, float x, float z, bool row_first = false) : dx{x}, dz{z}
    {
        columns = std::min(8u,std::max(1u,static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))))));
        if (row_first) columns = std::min(8u,std::max(1u,count));
        rows = std::max(1u,(count+columns-1)/columns);
        width = columns*dx; depth = rows*dz;
    }
    glm::vec3 position(std::uint32_t index, float left, float back) const
    { return {left+(index%columns+0.5f)*dx,0,back+(index/columns+0.5f)*dz}; }
};
}
WarehouseLayout build_warehouse_layout(const scenario::ScenarioDefinition& definition)
{
    scenario::validate(definition);
    const auto* config = std::get_if<scenario::WarehouseConfig>(&definition.domain);
    if (!config) throw std::invalid_argument("Graphical scenario requires the warehouse domain");
    if (config->loading_bays > max_visual_capacity || config->workers > max_visual_capacity || config->forklifts > max_visual_capacity)
        throw std::invalid_argument("Warehouse visualization supports 0..256 of each resource; use --headless for larger capacities");
    const auto racks = std::max(2u,config->loading_bays*2), pallets = std::max(4u,config->loading_bays*2);
    const Grid docks{config->loading_bays,4.8f,5.0f,true}, storage{racks,4.5f,3.5f}, cargo{pallets,2.2f,2.2f},
        forklifts{config->forklifts,3.0f,4.0f}, workers{config->workers,1.2f,1.6f};
    const float left_width = std::max(storage.width,forklifts.width), right_width = std::max(cargo.width,workers.width);
    const float width = std::max(docks.width,left_width+right_width+4);
    const float middle_back = docks.depth+4, middle_depth = std::max(storage.depth,cargo.depth);
    const float front_back = middle_back+middle_depth+4, front_depth = std::max(forklifts.depth,workers.depth);
    const float depth = front_back+front_depth;
    WarehouseLayout result; result.bounds = {{-2,-0.1f,-2},{width+2,4.0f,depth+2}};
    auto zone = [&](WarehouseZoneKind kind, float x, float z, float w, float d)
    { result.zones.push_back({kind,{{x,0,z},{x+w,4,z+d}}}); };
    zone(WarehouseZoneKind::Docks,0,0,width,docks.depth);
    zone(WarehouseZoneKind::Storage,0,middle_back,left_width,middle_depth);
    zone(WarehouseZoneKind::Cargo,left_width+4,middle_back,right_width,middle_depth);
    zone(WarehouseZoneKind::Forklifts,0,front_back,left_width,front_depth);
    zone(WarehouseZoneKind::Workers,left_width+4,front_back,right_width,front_depth);
    zone(WarehouseZoneKind::MainAisle,left_width,middle_back,4,depth-middle_back);
    zone(WarehouseZoneKind::CrossAisle,0,docks.depth,width,4);
    zone(WarehouseZoneKind::CrossAisle,0,middle_back+middle_depth,left_width,4);
    zone(WarehouseZoneKind::CrossAisle,left_width+4,middle_back+middle_depth,right_width,4);
    std::array<std::uint32_t,7> ids{};
    auto object = [&](WarehouseVisualKind kind, renderer::MeshId mesh, glm::vec3 p, glm::vec3 s,
                      glm::vec3 color, glm::vec3 local_min, glm::vec3 local_max)
    {
        result.objects.push_back({kind,++ids[static_cast<std::size_t>(kind)],{mesh,{p,{0,0,0},s},color},
            {p+local_min*s,p+local_max*s}});
    };
    using Kind = WarehouseVisualKind; using Mesh = renderer::MeshId;
    object(Kind::Floor,Mesh::Floor,{width/2,0,depth/2},{width+4,1,depth+4},{0.10f,0.13f,0.16f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
    for (float x = 0; x <= width; x += 4)
        object(Kind::FloorMarking,Mesh::Floor,{x,0.002f,depth/2},{0.018f,1,depth+4},
            {0.14f,0.17f,0.20f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
    for (float z = 0; z <= depth; z += 4)
        object(Kind::FloorMarking,Mesh::Floor,{width/2,0.003f,z},{width+4,1,0.018f},
            {0.14f,0.17f,0.20f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
    // Small separated floor strips are geometry, never coplanar with the floor.
    for (const auto& region : result.zones)
    {
        const auto size = region.bounds.maximum-region.bounds.minimum;
        const auto center = (region.bounds.maximum+region.bounds.minimum)*0.5f;
        const bool aisle = region.kind == WarehouseZoneKind::MainAisle || region.kind == WarehouseZoneKind::CrossAisle;
        if (!aisle)
        {
            object(Kind::FloorMarking,Mesh::Floor,{center.x,0.006f,center.z},{size.x-0.16f,1,size.z-0.16f},
                {0.12f,0.17f,0.20f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
            for (float x : {region.bounds.minimum.x+0.05f,region.bounds.maximum.x-0.05f})
                object(Kind::FloorMarking,Mesh::Floor,{x,0.012f,center.z},{0.07f,1,size.z},
                    {0.66f,0.48f,0.10f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
            for (float z : {region.bounds.minimum.z+0.05f,region.bounds.maximum.z-0.05f})
                object(Kind::FloorMarking,Mesh::Floor,{center.x,0.014f,z},{size.x,1,0.07f},
                    {0.66f,0.48f,0.10f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
        }
        else for (float z = region.bounds.minimum.z+0.6f; z < region.bounds.maximum.z; z += 2)
            object(Kind::FloorMarking,Mesh::Floor,{center.x,0.010f,z},{0.10f,1,0.75f},
                {0.55f,0.43f,0.12f},{-0.5f,0,-0.5f},{0.5f,0,0.5f});
    }
    auto populate = [&](Kind kind, Mesh mesh, std::uint32_t count, const Grid& grid, float x, float z,
                        glm::vec3 low, glm::vec3 high)
    {
        for (std::uint32_t i = 0; i < count; ++i)
            object(kind,mesh,grid.position(i,x,z),{1,1,1},{1,1,1},low,high);
    };
    populate(Kind::LoadingBay,Mesh::Dock,config->loading_bays,docks,(width-docks.width)/2,0,{-1.8f,0,-1.8f},{1.8f,3.44f,1.8f});
    populate(Kind::StorageRack,Mesh::Rack,racks,storage,0,middle_back,{-1.71f,0,-0.83f},{1.71f,3.84f,0.83f});
    populate(Kind::Pallet,Mesh::Pallet,pallets,cargo,left_width+4,middle_back,{-0.65f,0,-0.6f},{0.65f,1.34f,0.6f});
    populate(Kind::Forklift,Mesh::Forklift,config->forklifts,forklifts,(left_width-forklifts.width)/2,front_back,{-0.75f,0,-1.82f},{0.75f,2.9f,1.10f});
    populate(Kind::Worker,Mesh::Worker,config->workers,workers,left_width+4+(right_width-workers.width)/2,front_back,{-0.40f,0,-0.22f},{0.40f,1.80f,0.2f});
    return result;
}
renderer::RenderFrameData make_warehouse_frame(const WarehouseLayout& layout)
{
    renderer::RenderFrameData frame; frame.framing_bounds = layout.bounds;
    frame.camera = renderer::frame_bounds(layout.bounds,16.0f/9.0f);
    frame.objects.reserve(layout.objects.size());
    for (const auto& object : layout.objects) frame.objects.push_back(object.render);
    renderer::validate_frame(frame); return frame;
}
}
