#pragma once
#include <simforge/warehouse/warehouse_simulation.hpp>

namespace simforge::warehouse
{
// Owned read-only observation values, not a persistence snapshot. Completed
// history is represented by metrics; bounded vectors contain only active work.
struct WarehouseObservation
{
    std::uint64_t tick{0}, horizon{0};
    std::uint32_t tick_rate{60};
    bool at_horizon{false};
    ResourceCounts capacity{}, usage{};
    WarehouseMetrics metrics;
    std::vector<ShipmentRecord> processing, waiting; // Stable ID / FIFO order.
    std::uint64_t hidden_processing{0}, hidden_waiting{0};
    bool operator==(const WarehouseObservation&) const = default;
};
}
