#include <simforge/warehouse/warehouse_simulation.hpp>

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace simforge::warehouse
{
namespace
{
scenario::ScenarioDefinition validated_definition(const WarehouseSnapshot& snapshot)
{
    validate_snapshot(snapshot);
    return scenario::parse_scenario(snapshot.scenario_identity);
}
}
WarehouseSnapshot WarehouseSimulation::capture_snapshot() const
{
    WarehouseSnapshot result;
    result.scenario_identity = scenario_identity_;
    result.tick_rate = clock_.ticks_per_second();
    result.horizon_ticks = horizon_ticks_; result.processing_ticks = processing_ticks_;
    result.current_tick = clock_.tick_count(); result.settled = settled_;
    result.next_shipment = static_cast<ShipmentId>(shipments_.size());
    result.shipments = shipments_;
    result.waiting_queue.assign(waiting_queue_.begin() + queue_head_, waiting_queue_.end());
    for (const auto& shipment : shipments_)
        if (resources_.reserved(shipment.id)) result.reservations.push_back(shipment.id);
    result.capacity = resources_.capacity(); result.usage = resources_.usage();
    result.events = scheduler_.heap_;
    std::sort(result.events.begin(), result.events.end(), [](const auto& a, const auto& b)
    { return EventScheduler::Later{}(b, a); });
    result.next_event_sequence = scheduler_.next_sequence_;
    result.peak_pending_events = scheduler_.peak_pending_;
    result.capacity_changes = capacity_changes_;
    result.totals = {started_, completed_, total_wait_ticks_, max_wait_ticks_, completed_processing_ticks_,
        occupied_resource_ticks_, processed_events_, available_bay_ticks_, available_worker_ticks_, available_forklift_ticks_};
    return result;
}
WarehouseSimulation::WarehouseSimulation(const WarehouseSnapshot& snapshot)
    : WarehouseSimulation{validated_definition(snapshot)}
{
    resources_.capacity_ = snapshot.capacity;
    resources_.concurrency_ = std::min({snapshot.capacity.loading_bays, snapshot.capacity.workers, snapshot.capacity.forklifts});
    resources_.active_ = snapshot.usage.forklifts;
    for (auto id : snapshot.reservations) resources_.held_[id] = true;
    scheduler_.pending_limit_ = static_cast<std::size_t>(std::min(resources_.concurrency_, arrival_limit_)) + 1;
    scheduler_.heap_ = snapshot.events;
    scheduler_.heap_.reserve(scheduler_.pending_limit_);
    std::make_heap(scheduler_.heap_.begin(), scheduler_.heap_.end(), EventScheduler::Later{});
    std::fill(scheduler_.scheduled_.begin(), scheduler_.scheduled_.end(), std::uint8_t{0});
    const auto mark = [&](ShipmentId id, EventType type)
    { scheduler_.scheduled_[id] |= static_cast<std::uint8_t>(1U << static_cast<unsigned>(type)); };
    for (const auto& record : snapshot.shipments)
    {
        mark(record.id, EventType::ShipmentArrival); // Also retain consumed scheduling markers.
        if (record.start_tick) mark(record.id, EventType::ShipmentCompletion);
    }
    for (const auto& event : snapshot.events) mark(event.shipment, event.type);
    scheduler_.current_tick_ = snapshot.current_tick;
    scheduler_.next_sequence_ = snapshot.next_event_sequence;
    scheduler_.peak_pending_ = static_cast<std::size_t>(snapshot.peak_pending_events);
    shipments_ = snapshot.shipments; waiting_queue_ = snapshot.waiting_queue;
    shipments_.reserve(arrival_limit_); waiting_queue_.reserve(arrival_limit_);
    clock_.advance_ticks(snapshot.current_tick); settled_ = snapshot.settled;
    capacity_changes_ = snapshot.capacity_changes;
    const auto& totals = snapshot.totals;
    started_ = totals.started; completed_ = totals.completed;
    total_wait_ticks_ = totals.total_wait_ticks; max_wait_ticks_ = totals.max_wait_ticks;
    completed_processing_ticks_ = totals.completed_processing_ticks;
    occupied_resource_ticks_ = totals.occupied_resource_ticks; processed_events_ = totals.processed_events;
    available_bay_ticks_ = totals.available_bay_ticks; available_worker_ticks_ = totals.available_worker_ticks;
    available_forklift_ticks_ = totals.available_forklift_ticks;
}
WarehouseSimulation WarehouseSimulation::create_from(const WarehouseSnapshot& snapshot)
{
    return WarehouseSimulation{snapshot}; // Guaranteed copy elision; branches own their storage.
}
void WarehouseSimulation::swap_state(WarehouseSimulation& other) noexcept
{
    static_assert(std::is_nothrow_swappable_v<EventScheduler> && std::is_nothrow_swappable_v<ResourceManager> &&
                  std::is_nothrow_swappable_v<simulation::SimulationClock>);
    using std::swap;
    swap(config_, other.config_); swap(scenario_identity_, other.scenario_identity_); swap(clock_, other.clock_);
    swap(horizon_ticks_, other.horizon_ticks_); swap(arrival_interval_ticks_, other.arrival_interval_ticks_);
    swap(processing_ticks_, other.processing_ticks_); swap(arrival_limit_, other.arrival_limit_);
    swap(resources_, other.resources_); swap(scheduler_, other.scheduler_);
    swap(shipments_, other.shipments_); swap(waiting_queue_, other.waiting_queue_); swap(queue_head_, other.queue_head_);
    swap(started_, other.started_); swap(completed_, other.completed_); swap(total_wait_ticks_, other.total_wait_ticks_);
    swap(max_wait_ticks_, other.max_wait_ticks_); swap(completed_processing_ticks_, other.completed_processing_ticks_);
    swap(occupied_resource_ticks_, other.occupied_resource_ticks_); swap(processed_events_, other.processed_events_);
    swap(available_bay_ticks_, other.available_bay_ticks_); swap(available_worker_ticks_, other.available_worker_ticks_);
    swap(available_forklift_ticks_, other.available_forklift_ticks_); swap(capacity_changes_, other.capacity_changes_);
    swap(settled_, other.settled_);
}
void WarehouseSimulation::restore_snapshot(const WarehouseSnapshot& snapshot)
{
    if (snapshot.scenario_identity != scenario_identity_)
        throw std::invalid_argument("Warehouse snapshot scenario identity mismatch");
    WarehouseSimulation replacement{snapshot};
    swap_state(replacement);
}
void WarehouseSimulation::set_forklift_capacity(std::uint32_t forklifts)
{
    if (!settled_) throw std::invalid_argument("Capacity changes require a settled boundary; advance_to(0) first");
    const auto capacity = resources_.capacity();
    if (static_cast<std::uint64_t>(capacity.loading_bays) + capacity.workers + forklifts > 100000 ||
        forklifts < resources_.active_count())
        throw std::invalid_argument("Forklift capacity must cover active reservations and respect resource limits");
    if (capacity_changes_.size() == max_capacity_changes)
        throw std::length_error("Warehouse capacity change limit exceeded");
    const auto concurrency = std::min({capacity.loading_bays, capacity.workers, forklifts});
    const auto limit = static_cast<std::size_t>(std::min(concurrency, arrival_limit_)) + 1;
    // All allocations precede logical mutation. Domain limits prove subsequent
    // counters/ticks safe and start_waiting's pre-reserved pushes nonthrowing.
    scheduler_.heap_.reserve(limit);
    capacity_changes_.push_back({clock_.tick_count(), started_, forklifts});
    scheduler_.pending_limit_ = limit;
    resources_.capacity_.forklifts = forklifts; resources_.concurrency_ = concurrency;
    start_waiting();
}
} // namespace simforge::warehouse
