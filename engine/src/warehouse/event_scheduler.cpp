#include <simforge/warehouse/event_scheduler.hpp>
#include <simforge/scenario/scenario.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace simforge::warehouse
{
EventScheduler::EventScheduler(std::uint32_t shipment_limit, std::size_t pending_limit)
    : pending_limit_{pending_limit}
{
    if (shipment_limit > scenario::max_warehouse_shipments || pending_limit > 2ULL * shipment_limit + 1)
        throw std::length_error("Warehouse scheduler capacity exceeds domain limits");
    scheduled_.resize(shipment_limit, 0);
    heap_.reserve(pending_limit);
}
bool EventScheduler::Later::operator()(const ShipmentEvent& a, const ShipmentEvent& b) const noexcept
{
    return std::tie(a.tick, a.type, a.sequence) > std::tie(b.tick, b.type, b.sequence);
}
std::uint64_t EventScheduler::schedule(std::uint64_t tick, EventType type, ShipmentId shipment)
{
    if (tick < current_tick_) throw std::invalid_argument("Cannot schedule warehouse event in the past");
    if (type != EventType::ShipmentArrival && type != EventType::ShipmentCompletion)
        throw std::invalid_argument("Unsupported warehouse event type");
    if (shipment >= scheduled_.size()) throw std::out_of_range("Invalid warehouse event shipment ID");
    const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(type));
    if ((scheduled_[shipment] & mask) != 0) throw std::logic_error("Duplicate warehouse shipment event");
    if (heap_.size() == pending_limit_) throw std::length_error("Warehouse pending event capacity exceeded");
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Warehouse event sequence exhausted");
    const auto sequence = next_sequence_;
    heap_.push_back({tick, type, sequence, shipment});
    std::push_heap(heap_.begin(), heap_.end(), Later{});
    scheduled_[shipment] |= mask;
    ++next_sequence_;
    peak_pending_ = std::max(peak_pending_, heap_.size());
    return sequence;
}
std::optional<ShipmentEvent> EventScheduler::pop_due(std::uint64_t limit)
{
    if (limit < current_tick_) throw std::invalid_argument("Warehouse scheduler cannot move backwards");
    if (heap_.empty() || heap_.front().tick > limit)
    {
        current_tick_ = limit;
        return std::nullopt;
    }
    std::pop_heap(heap_.begin(), heap_.end(), Later{});
    const auto event = heap_.back();
    heap_.pop_back();
    current_tick_ = event.tick;
    return event;
}
} // namespace simforge::warehouse
