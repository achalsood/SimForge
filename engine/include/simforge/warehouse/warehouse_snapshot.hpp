#pragma once

#include <simforge/warehouse/resource_manager.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace simforge::warehouse
{
enum class ShipmentStatus : std::uint8_t { Waiting = 0, InProgress = 1, Completed = 2 };
struct ShipmentRecord
{
    ShipmentId id;
    ShipmentStatus status;
    std::uint64_t arrival_tick;
    std::optional<std::uint64_t> start_tick, completion_tick;
    std::uint64_t required_processing_ticks{0}; // v3: sampled once at arrival; v1/v2 retain implicit fixed duration.
    bool operator==(const ShipmentRecord&) const = default;
};

// Commands apply after due events. started_before disambiguates reservations
// acquired between multiple capacity changes at the same tick.
struct CapacityChange
{
    std::uint64_t tick, started_before;
    std::uint32_t capacity;
    ResourceType resource{ResourceType::Forklift}; // v1's implicit resource; explicit in v2.
    bool operator==(const CapacityChange&) const = default;
};
struct WarehouseTotals
{
    std::uint64_t started{0}, completed{0}, total_wait_ticks{0}, max_wait_ticks{0};
    std::uint64_t completed_processing_ticks{0}, occupied_resource_ticks{0}, processed_events{0};
    std::uint64_t available_bay_ticks{0}, available_worker_ticks{0}, available_forklift_ticks{0};
    bool operator==(const WarehouseTotals&) const = default;
};
struct WarehouseSnapshot
{
    std::uint32_t version{1};
    std::uint32_t workload_generator_version{0}; // Required for v3, never inferred on restore.
    std::string scenario_identity;
    std::uint32_t tick_rate{60};
    std::uint64_t horizon_ticks{0}, processing_ticks{0}, current_tick{0};
    // false is valid only for the untouched constructor state (pending arrival 0).
    bool settled{false};
    ShipmentId next_shipment{0};
    std::vector<ShipmentRecord> shipments;
    std::vector<ShipmentId> waiting_queue, reservations;
    ResourceCounts capacity{}, usage{};
    // Canonical dispatch order, independent of heap representation.
    std::vector<ShipmentEvent> events;
    std::uint64_t next_event_sequence{0}, peak_pending_events{0};
    std::vector<CapacityChange> capacity_changes;
    WarehouseTotals totals;
    bool operator==(const WarehouseSnapshot&) const = default;
};
inline constexpr std::size_t warehouse_snapshot_max_bytes = 32 * 1024 * 1024;
inline constexpr std::size_t max_capacity_changes = 100000;

void validate_snapshot(const WarehouseSnapshot& snapshot);
[[nodiscard]] std::optional<std::string> first_difference(
    const WarehouseSnapshot& expected, const WarehouseSnapshot& actual);
[[nodiscard]] std::vector<std::byte> serialize_snapshot(const WarehouseSnapshot& snapshot);
[[nodiscard]] WarehouseSnapshot deserialize_snapshot(std::span<const std::byte> bytes);
void save_snapshot(const std::filesystem::path& path, const WarehouseSnapshot& snapshot);
[[nodiscard]] WarehouseSnapshot load_snapshot(const std::filesystem::path& path);
} // namespace simforge::warehouse
