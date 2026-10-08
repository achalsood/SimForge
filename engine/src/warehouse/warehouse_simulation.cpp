#include <simforge/warehouse/warehouse_simulation.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace simforge::warehouse
{
namespace
{
std::uint64_t checked_add(std::uint64_t a, std::uint64_t b)
{
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        throw std::overflow_error("Warehouse event tick overflow");
    return a + b;
}
std::size_t pending_limit(const scenario::WarehouseConfig& config, std::uint32_t arrivals)
{
    return static_cast<std::size_t>(std::min({config.loading_bays, config.workers, config.forklifts, arrivals})) + 1;
}
}
WarehouseSimulation::Parameters WarehouseSimulation::parameters(const scenario::ScenarioDefinition& definition)
{
    scenario::validate(definition);
    const auto* config = std::get_if<scenario::WarehouseConfig>(&definition.domain);
    if (!config) throw std::invalid_argument("WarehouseSimulation requires a warehouse scenario");
    return {*config, definition.simulation.tick_rate,
            1 + (definition.simulation.duration_seconds - 1) / config->shipment_interval_seconds,
            static_cast<std::uint64_t>(definition.simulation.duration_seconds) * definition.simulation.tick_rate};
}
WarehouseSimulation::WarehouseSimulation(const scenario::ScenarioDefinition& definition)
    : WarehouseSimulation{parameters(definition)} {}
WarehouseSimulation::WarehouseSimulation(Parameters params)
    : config_{params.config}, clock_{params.rate}, horizon_ticks_{params.horizon},
      arrival_interval_ticks_{static_cast<std::uint64_t>(config_.shipment_interval_seconds) * params.rate},
      processing_ticks_{static_cast<std::uint64_t>(config_.processing_duration_seconds) * params.rate},
      arrival_limit_{params.arrivals},
      resources_{{config_.loading_bays, config_.workers, config_.forklifts}, arrival_limit_},
      scheduler_{arrival_limit_, pending_limit(config_, arrival_limit_)}
{
    shipments_.reserve(arrival_limit_);
    waiting_queue_.reserve(arrival_limit_);
    (void)scheduler_.schedule(0, EventType::ShipmentArrival, 0);
}
void WarehouseSimulation::move_clock_to(std::uint64_t tick)
{
    const auto delta = tick - clock_.tick_count();
    // Counts * horizon are bounded by validated scenario limits (< 1e12 ticks).
    occupied_resource_ticks_ += static_cast<std::uint64_t>(resources_.active_count()) * delta;
    clock_.advance_ticks(delta);
}
void WarehouseSimulation::start_waiting()
{
    const auto tick = clock_.tick_count();
    if (tick == horizon_ticks_) return;
    while (queue_head_ < waiting_queue_.size())
    {
        const auto id = waiting_queue_[queue_head_];
        auto& record = shipments_[id];
        if (record.status != ShipmentStatus::Waiting) throw std::logic_error("Invalid warehouse waiting queue state");
        if (!resources_.try_acquire(id)) break;
        const auto completion_tick = checked_add(tick, processing_ticks_);
        (void)scheduler_.schedule(completion_tick, EventType::ShipmentCompletion, id);
        record.status = ShipmentStatus::InProgress;
        record.start_tick = tick;
        const auto wait = tick - record.arrival_tick;
        total_wait_ticks_ += wait;
        max_wait_ticks_ = std::max(max_wait_ticks_, wait);
        ++queue_head_;
        ++started_;
    }
}
void WarehouseSimulation::process(const ShipmentEvent& event)
{
    if (event.type == EventType::ShipmentArrival)
    {
        if (event.shipment != shipments_.size() || event.tick >= horizon_ticks_)
            throw std::logic_error("Invalid warehouse arrival event");
        shipments_.push_back({event.shipment, ShipmentStatus::Waiting, event.tick, {}, {}});
        waiting_queue_.push_back(event.shipment);
        const auto next = checked_add(event.tick, arrival_interval_ticks_);
        if (next < horizon_ticks_)
            (void)scheduler_.schedule(next, EventType::ShipmentArrival, event.shipment + 1);
        start_waiting();
    }
    else
    {
        if (event.shipment >= shipments_.size()) throw std::logic_error("Completion refers to unarrived shipment");
        auto& record = shipments_[event.shipment];
        if (record.status != ShipmentStatus::InProgress || !record.start_tick ||
            event.tick != checked_add(*record.start_tick, processing_ticks_))
            throw std::logic_error("Invalid or duplicate warehouse completion event");
        resources_.release(event.shipment);
        record.status = ShipmentStatus::Completed;
        record.completion_tick = event.tick;
        completed_processing_ticks_ += event.tick - *record.start_tick;
        ++completed_;
        start_waiting();
    }
    ++processed_events_;
}
void WarehouseSimulation::advance_to(std::uint64_t tick)
{
    if (tick < clock_.tick_count() || tick > horizon_ticks_)
        throw std::invalid_argument("Warehouse target tick must be current..horizon");
    while (const auto event = scheduler_.pop_due(tick))
    {
        move_clock_to(event->tick);
        process(*event);
    }
    move_clock_to(tick);
}
WarehouseMetrics WarehouseSimulation::metrics() const
{
    WarehouseMetrics result;
    result.elapsed_ticks = clock_.tick_count();
    result.arrived = shipments_.size();
    result.started = started_; result.completed = completed_;
    result.waiting = waiting_queue_.size() - queue_head_;
    result.in_progress = resources_.active_count();
    result.total_wait_ticks = total_wait_ticks_; result.max_wait_ticks = max_wait_ticks_;
    result.completed_processing_ticks = completed_processing_ticks_;
    result.occupied_resource_ticks = occupied_resource_ticks_;
    const double rate = clock_.ticks_per_second();
    result.completed_processing_seconds = static_cast<double>(completed_processing_ticks_) / rate;
    if (result.arrived != result.waiting + result.in_progress + result.completed ||
        result.started != result.in_progress + result.completed)
        throw std::logic_error("Warehouse metric conservation violated");
    if (started_ != 0)
    {
        result.average_wait_seconds = static_cast<double>(total_wait_ticks_) / static_cast<double>(started_) / rate;
        result.maximum_wait_seconds = static_cast<double>(max_wait_ticks_) / rate;
    }
    if (completed_ != 0)
        result.average_processing_seconds = result.completed_processing_seconds / static_cast<double>(completed_);
    if (result.elapsed_ticks != 0)
        result.throughput_per_hour = static_cast<double>(completed_) * 3600.0 * rate / static_cast<double>(result.elapsed_ticks);
    const auto utilization = [&](std::uint32_t capacity) -> std::optional<double>
    {
        if (capacity == 0 || result.elapsed_ticks == 0) return {};
        return static_cast<double>(occupied_resource_ticks_) / (static_cast<double>(capacity) * static_cast<double>(result.elapsed_ticks));
    };
    result.loading_bay_utilization = utilization(config_.loading_bays);
    result.worker_utilization = utilization(config_.workers);
    result.forklift_utilization = utilization(config_.forklifts);
    return result;
}
std::size_t WarehouseSimulation::approximate_storage_bytes() const noexcept
{
    return shipments_.capacity() * sizeof(ShipmentRecord) + waiting_queue_.capacity() * sizeof(ShipmentId) +
           resources_.approximate_storage_bytes() + scheduler_.approximate_storage_bytes();
}
} // namespace simforge::warehouse
