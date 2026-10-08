#pragma once
#include <simforge/scenario/scenario.hpp>
#include <simforge/renderer/render_frame_data.hpp>
#include <array>

namespace simforge::visualization
{
inline constexpr std::uint32_t max_visual_capacity = 256; // Per resource; headless domain limits unchanged.
enum class WarehouseVisualKind : std::uint32_t { Floor, LoadingBay, StorageRack, Pallet, Forklift, Worker, FloorMarking };
enum class WarehouseZoneKind { Docks, Storage, Cargo, Forklifts, Workers, MainAisle, CrossAisle };
struct WarehouseVisual
{
    WarehouseVisualKind kind;
    std::uint32_t id; // One-based, local to kind and layout; capacity representative, not an ECS handle.
    renderer::RenderObject render;
    renderer::SceneBounds bounds;
};
struct WarehouseZone { WarehouseZoneKind kind; renderer::SceneBounds bounds; };
struct WarehouseLayout
{
    renderer::SceneBounds bounds;
    std::vector<WarehouseZone> zones;
    std::vector<WarehouseVisual> objects;
};
[[nodiscard]] WarehouseLayout build_warehouse_layout(const scenario::ScenarioDefinition& definition);
[[nodiscard]] renderer::RenderFrameData make_warehouse_frame(const WarehouseLayout& layout);
}
