#include <simforge/warehouse/resource_manager.hpp>
#include <simforge/scenario/scenario.hpp>

#include <algorithm>
#include <stdexcept>

namespace simforge::warehouse
{
std::uint32_t capacity_for(ResourceCounts counts, ResourceType resource)
{
    switch (resource)
    {
    case ResourceType::LoadingBay: return counts.loading_bays;
    case ResourceType::Worker: return counts.workers;
    case ResourceType::Forklift: return counts.forklifts;
    }
    throw std::invalid_argument("Unknown warehouse resource type");
}
void set_capacity(ResourceCounts& counts, ResourceType resource, std::uint32_t capacity)
{
    switch (resource)
    {
    case ResourceType::LoadingBay: counts.loading_bays = capacity; return;
    case ResourceType::Worker: counts.workers = capacity; return;
    case ResourceType::Forklift: counts.forklifts = capacity; return;
    }
    throw std::invalid_argument("Unknown warehouse resource type");
}
std::string_view resource_name(ResourceType resource)
{
    switch (resource)
    {
    case ResourceType::LoadingBay: return "loading_bay";
    case ResourceType::Worker: return "worker";
    case ResourceType::Forklift: return "forklift";
    }
    throw std::invalid_argument("Unknown warehouse resource type");
}
ResourceType parse_resource_type(std::string_view name)
{
    for (auto resource : {ResourceType::LoadingBay, ResourceType::Worker, ResourceType::Forklift})
        if (resource_name(resource) == name) return resource;
    throw std::invalid_argument("Unknown warehouse resource name");
}
ResourceManager::ResourceManager(ResourceCounts capacity, std::uint32_t shipment_limit)
    : capacity_{capacity}, concurrency_{std::min({capacity.loading_bays, capacity.workers, capacity.forklifts})}
{
    if (static_cast<std::uint64_t>(capacity.loading_bays) + capacity.workers + capacity.forklifts > 100000 ||
        shipment_limit > scenario::max_warehouse_shipments)
        throw std::length_error("Warehouse resources exceed domain limits");
    held_.resize(shipment_limit, false);
}
bool ResourceManager::try_acquire(ShipmentId shipment)
{
    if (shipment >= held_.size()) throw std::out_of_range("Invalid resource reservation shipment ID");
    if (held_[shipment]) throw std::logic_error("Shipment already holds warehouse resources");
    if (active_ == concurrency_) return false;
    held_[shipment] = true;
    ++active_;
    return true;
}
void ResourceManager::release(ShipmentId shipment)
{
    if (shipment >= held_.size()) throw std::out_of_range("Invalid resource release shipment ID");
    if (!held_[shipment]) throw std::logic_error("Shipment has no warehouse reservation (double release)");
    held_[shipment] = false;
    --active_;
}
bool ResourceManager::reserved(ShipmentId shipment) const noexcept
{
    return shipment < held_.size() && held_[shipment];
}
} // namespace simforge::warehouse
