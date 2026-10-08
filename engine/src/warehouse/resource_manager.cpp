#include <simforge/warehouse/resource_manager.hpp>
#include <simforge/scenario/scenario.hpp>

#include <algorithm>
#include <stdexcept>

namespace simforge::warehouse
{
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
