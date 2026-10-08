#pragma once
#include <simforge/visualization/warehouse_layout.hpp>
#include <simforge/warehouse/warehouse_observation.hpp>
#include <chrono>
#include <string>

namespace simforge::visualization
{
inline constexpr std::size_t max_visible_waiting = 128;
inline constexpr auto shipment_settle_duration = std::chrono::milliseconds{250};
struct CapacityVisual
{
    WarehouseVisualKind kind;
    std::uint32_t id;
    bool busy;
    renderer::RenderObject render;
};
struct ShipmentVisual
{
    warehouse::ShipmentId id;
    warehouse::ShipmentStatus status;
    renderer::RenderObject target;
    std::chrono::nanoseconds age{shipment_settle_duration};
};
struct LiveVisibility
{
    std::uint64_t visible_waiting{0}, hidden_waiting{0}, visible_processing{0}, hidden_processing{0};
};
// Owns a static prefix and bounded dynamic values. Restore callers pass reset=true,
// even if the restored tick/IDs happen to match the last observation.
class LiveWarehouseScene
{
public:
    explicit LiveWarehouseScene(const scenario::ScenarioDefinition& definition);
    void set_state(const warehouse::WarehouseObservation& state, bool reset = false);
    void animate(std::chrono::nanoseconds elapsed);
    [[nodiscard]] const renderer::RenderFrameData& frame() const noexcept { return frame_; }
    [[nodiscard]] const WarehouseLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] std::span<const ShipmentVisual> shipments() const noexcept { return shipments_; }
    [[nodiscard]] std::span<const CapacityVisual> resources() const noexcept { return resources_; }
    [[nodiscard]] LiveVisibility visibility() const noexcept { return visibility_; }
    [[nodiscard]] std::size_t waiting_limit() const noexcept { return queue_slots_.size(); }
    [[nodiscard]] std::size_t processing_limit() const noexcept { return bay_slots_.size(); }
    [[nodiscard]] std::uint64_t layout_generations() const noexcept { return layout_generations_; }
private:
    void rebuild(warehouse::ResourceCounts capacity);
    scenario::ScenarioDefinition definition_;
    warehouse::ResourceCounts capacity_{};
    WarehouseLayout layout_;
    renderer::RenderFrameData frame_;
    std::size_t static_count_{0};
    std::vector<glm::vec3> queue_slots_, bay_slots_;
    std::vector<CapacityVisual> resources_;
    std::vector<ShipmentVisual> shipments_;
    LiveVisibility visibility_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t layout_generations_{0};
};
[[nodiscard]] std::string format_live_metrics(const warehouse::WarehouseObservation& state, LiveVisibility visibility);
}
