#include <simforge/warehouse/warehouse_observation.hpp>
#include <algorithm>
#include <stdexcept>

namespace simforge::warehouse
{
void WarehouseSimulation::advance_elapsed(std::chrono::nanoseconds elapsed)
{
    if (elapsed.count() < 0) throw std::invalid_argument("Elapsed time cannot be negative");
    if (clock_.tick_count() == horizon_ticks_) return;
    // This temporary schedules a target, never exposes a second warehouse time.
    auto scheduled = clock_;
    (void)scheduled.advance(elapsed); // Validate scheduling before mutating runtime.
    const auto target = std::min(scheduled.tick_count(),horizon_ticks_);
    advance_to(target); // The authoritative clock visits every due event first.
    auto state = scheduled.capture_snapshot(); state.ticks = target;
    if (target == horizon_ticks_) state.remainder = 0;
    clock_.restore_snapshot(state);
}
WarehouseObservation WarehouseSimulation::capture_observation(std::size_t waiting_limit, std::size_t processing_limit) const
{
    if (waiting_limit > scenario::max_warehouse_shipments || processing_limit > scenario::max_warehouse_shipments)
        throw std::invalid_argument("Observation limits exceed the supported shipment workload");
    WarehouseObservation result;
    result.tick = clock_.tick_count(); result.tick_rate = clock_.ticks_per_second(); result.horizon = horizon_ticks_;
    result.at_horizon = result.tick == result.horizon;
    result.capacity = resources_.capacity(); result.usage = resources_.usage(); result.metrics = metrics();
    const auto waiting_count = std::min(waiting_limit,waiting_queue_.size()-queue_head_);
    result.waiting.reserve(waiting_count);
    for (std::size_t i = 0; i < waiting_count; ++i) result.waiting.push_back(shipments_[waiting_queue_[queue_head_+i]]);
    // FIFO starts with equal processing durations imply completed and started
    // prefixes. This is independent of the heap and avoids scanning history.
    const auto active_count = static_cast<std::size_t>(started_-completed_);
    const auto processing_count = std::min(processing_limit,active_count);
    result.processing.reserve(processing_count);
    if(config_.workload.processing==scenario::ProcessingMode::Uniform) {
        auto id=active_shipments_.begin();
        for(std::size_t i=0;i<processing_count;++i,++id) result.processing.push_back(shipments_.at(*id));
    } else for (std::size_t i = 0; i < processing_count; ++i)
    {
        const auto& record = shipments_[static_cast<std::size_t>(completed_)+i];
        if (record.status != ShipmentStatus::InProgress) throw std::logic_error("Warehouse active prefix invariant violated");
        result.processing.push_back(record);
    }
    result.hidden_waiting = result.metrics.waiting-waiting_count;
    result.hidden_processing = result.metrics.in_progress-processing_count;
    return result;
}
}
