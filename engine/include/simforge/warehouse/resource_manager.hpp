#pragma once

#include <simforge/warehouse/event_scheduler.hpp>
#include <string_view>

namespace simforge::warehouse
{
struct ResourceCounts
{
    std::uint32_t loading_bays, workers, forklifts;
    bool operator==(const ResourceCounts&) const = default;
};
enum class ResourceType : std::uint8_t { LoadingBay = 0, Worker = 1, Forklift = 2 };
[[nodiscard]] std::uint32_t capacity_for(ResourceCounts counts, ResourceType resource);
void set_capacity(ResourceCounts& counts, ResourceType resource, std::uint32_t capacity);
[[nodiscard]] std::string_view resource_name(ResourceType resource);
[[nodiscard]] ResourceType parse_resource_type(std::string_view name);

// One of each resource is reserved atomically for a shipment, without identities
// for individual bays/workers/forklifts. A per-shipment ledger prevents double release.
class ResourceManager
{
public:
    ResourceManager(ResourceCounts capacity, std::uint32_t shipment_limit);
    [[nodiscard]] bool try_acquire(ShipmentId shipment);
    void release(ShipmentId shipment);
    [[nodiscard]] ResourceCounts capacity() const noexcept { return capacity_; }
    [[nodiscard]] ResourceCounts usage() const noexcept { return {active_, active_, active_}; }
    [[nodiscard]] std::uint32_t active_count() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t concurrency() const noexcept { return concurrency_; }
    [[nodiscard]] bool reserved(ShipmentId shipment) const noexcept;
    [[nodiscard]] std::size_t approximate_storage_bytes() const noexcept { return (held_.capacity() + 7) / 8; }
private:
    friend class WarehouseSimulation;
    ResourceCounts capacity_;
    std::uint32_t concurrency_, active_{0};
    std::vector<bool> held_;
};
} // namespace simforge::warehouse
