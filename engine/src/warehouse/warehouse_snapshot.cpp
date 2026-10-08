#include <simforge/warehouse/warehouse_snapshot.hpp>
#include <simforge/scenario/scenario.hpp>

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace simforge::warehouse
{
namespace
{
void require(bool condition, const char* field)
{
    if (!condition) throw std::invalid_argument(std::string{"Invalid warehouse snapshot: "} + field);
}
bool earlier(const ShipmentEvent& a, const ShipmentEvent& b)
{
    return std::tie(a.tick, a.type, a.sequence) < std::tie(b.tick, b.type, b.sequence);
}
std::string status_name(ShipmentStatus status)
{
    switch (status)
    {
    case ShipmentStatus::Waiting: return "waiting";
    case ShipmentStatus::InProgress: return "in_progress";
    case ShipmentStatus::Completed: return "completed";
    }
    return "invalid(" + std::to_string(static_cast<unsigned>(status)) + ")";
}
struct Transition
{
    std::uint64_t tick, order;
    enum class Type { Completion, Change, Start } type;
    std::uint32_t forklifts{0};
};
}
void validate_snapshot(const WarehouseSnapshot& state)
{
    require(state.version == 1, "version");
    // Check externally supplied container bounds before parsing or allocating.
    require(state.scenario_identity.size() <= scenario::max_scenario_bytes, "scenario_identity.size");
    const auto limit = scenario::max_warehouse_shipments;
    require(state.shipments.size() <= limit && state.waiting_queue.size() <= limit &&
        state.reservations.size() <= limit && state.events.size() <= limit + 1ULL &&
        state.capacity_changes.size() <= max_capacity_changes, "container bounds");
    const auto definition = scenario::parse_scenario(state.scenario_identity);
    require(scenario::serialize_scenario(definition) == state.scenario_identity, "canonical scenario identity");
    const auto* config = std::get_if<scenario::WarehouseConfig>(&definition.domain);
    require(config != nullptr, "warehouse domain");
    const auto rate = definition.simulation.tick_rate;
    const auto horizon = static_cast<std::uint64_t>(rate) * definition.simulation.duration_seconds;
    const auto interval = static_cast<std::uint64_t>(rate) * config->shipment_interval_seconds;
    const auto processing = static_cast<std::uint64_t>(rate) * config->processing_duration_seconds;
    const auto arrivals = 1 + (definition.simulation.duration_seconds - 1) / config->shipment_interval_seconds;
    require(state.tick_rate == rate && state.horizon_ticks == horizon && state.processing_ticks == processing,
        "scenario timing compatibility");
    require(state.current_tick <= horizon, "current_tick");
    const auto tick = state.current_tick;
    const auto arrived = state.shipments.size();
    require(arrived == (state.settled ? std::min<std::uint64_t>(arrivals, 1 + tick / interval) : 0), "arrivals at boundary");
    require(state.next_shipment == arrived, "next_shipment");
    require(state.settled || (tick == 0 && state.capacity_changes.empty()), "unsettled boundary");

    WarehouseTotals totals;
    std::vector<ShipmentId> waiting, reservations;
    std::vector<Transition> transitions;
    transitions.reserve(2 * arrived + state.capacity_changes.size());
    std::uint64_t previous_start = 0;
    bool waiting_seen = false;
    for (std::size_t i = 0; i < arrived; ++i)
    {
        const auto& shipment = state.shipments[i];
        require(shipment.id == i, "shipments.id (unique contiguous arrival order)");
        require(shipment.arrival_tick == i * interval && shipment.arrival_tick <= tick && shipment.arrival_tick < horizon,
            "shipments.arrival_tick");
        require(shipment.status == ShipmentStatus::Waiting || shipment.status == ShipmentStatus::InProgress ||
            shipment.status == ShipmentStatus::Completed, "shipments.status");
        if (shipment.status == ShipmentStatus::Waiting)
        {
            require(!shipment.start_tick && !shipment.completion_tick, "waiting lifecycle");
            waiting_seen = true; waiting.push_back(shipment.id);
            continue;
        }
        require(!waiting_seen && shipment.start_tick.has_value(), "FIFO start prefix");
        const auto start = *shipment.start_tick;
        require(start >= shipment.arrival_tick && start <= tick && start < horizon && start >= previous_start,
            "shipments.start_tick");
        previous_start = start;
        ++totals.started;
        const auto wait = start - shipment.arrival_tick;
        totals.total_wait_ticks += wait; totals.max_wait_ticks = std::max(totals.max_wait_ticks, wait);
        // start < validated H and processing <=604800000; addition cannot overflow.
        const auto completion = start + processing;
        if (shipment.status == ShipmentStatus::Completed)
        {
            require(shipment.completion_tick == completion && completion <= tick, "completed lifecycle");
            ++totals.completed; totals.completed_processing_ticks += processing;
        }
        else
        {
            require(!shipment.completion_tick && completion > tick, "active lifecycle");
            reservations.push_back(shipment.id);
        }
        totals.occupied_resource_ticks += std::min(completion, tick) - start;
        transitions.push_back({start, 2 * i + 1, Transition::Type::Start});
        if (completion <= tick) transitions.push_back({completion, 0, Transition::Type::Completion});
    }
    require(state.waiting_queue == waiting, "waiting_queue (exact FIFO membership)");
    require(state.reservations == reservations, "reservations (exact active membership)");
    const auto active = static_cast<std::uint32_t>(reservations.size());
    require(state.usage == ResourceCounts{active, active, active}, "resource usage");

    std::uint64_t previous_tick = 0, previous_started = 0;
    auto forklift_capacity = config->forklifts;
    for (const auto& change : state.capacity_changes)
    {
        require(change.tick >= previous_tick && change.tick <= tick && change.started_before >= previous_started &&
            change.started_before <= totals.started, "capacity_changes ordering");
        require(static_cast<std::uint64_t>(config->loading_bays) + config->workers + change.forklifts <= 100000,
            "capacity_changes resource bounds");
        if (change.started_before != 0)
            require(*state.shipments[static_cast<std::size_t>(change.started_before - 1)].start_tick <= change.tick,
                "capacity_changes started_before");
        if (change.started_before < totals.started)
            require(*state.shipments[static_cast<std::size_t>(change.started_before)].start_tick >= change.tick,
                "capacity_changes future starts");
        totals.available_forklift_ticks += (change.tick - previous_tick) * forklift_capacity;
        forklift_capacity = change.forklifts; previous_tick = change.tick; previous_started = change.started_before;
        transitions.push_back({change.tick, 2 * change.started_before, Transition::Type::Change, change.forklifts});
    }
    totals.available_forklift_ticks += (tick - previous_tick) * forklift_capacity;
    totals.available_bay_ticks = tick * config->loading_bays;
    totals.available_worker_ticks = tick * config->workers;
    require(state.capacity == ResourceCounts{config->loading_bays, config->workers, forklift_capacity}, "current capacity");

    // Validate historical reservation intervals, including capacity cuts and
    // equal-tick commands, without replaying arrivals or completed service work.
    std::stable_sort(transitions.begin(), transitions.end(), [](const auto& a, const auto& b)
    {
        const auto a_phase = a.type == Transition::Type::Completion ? 0 : 1;
        const auto b_phase = b.type == Transition::Type::Completion ? 0 : 1;
        return std::tie(a.tick, a_phase, a.order) < std::tie(b.tick, b_phase, b.order);
    });
    std::uint32_t historical_active = 0, max_active = 0;
    forklift_capacity = config->forklifts;
    for (const auto& transition : transitions)
    {
        if (transition.type == Transition::Type::Completion)
        {
            require(historical_active != 0, "historical release"); --historical_active;
        }
        else if (transition.type == Transition::Type::Start) ++historical_active;
        else forklift_capacity = transition.forklifts;
        require(historical_active <= std::min({config->loading_bays, config->workers, forklift_capacity}),
            "historical resource over-allocation");
        max_active = std::max(max_active, historical_active);
    }
    require(historical_active == active, "historical reservations");
    if (state.settled && tick < horizon && !waiting.empty())
        require(active == std::min({state.capacity.loading_bays, state.capacity.workers, state.capacity.forklifts}),
            "unstarted eligible waiting work at settled boundary");

    const bool has_arrival = arrived < arrivals;
    require(state.events.size() == reservations.size() + static_cast<std::size_t>(has_arrival), "pending event membership");
    require(state.next_event_sequence == arrived + totals.started + static_cast<std::uint64_t>(has_arrival),
        "next_event_sequence");
    std::vector<bool> sequences(static_cast<std::size_t>(state.next_event_sequence), false);
    std::vector<bool> completions(arrived, false);
    bool arrival_seen = false;
    for (std::size_t i = 0; i < state.events.size(); ++i)
    {
        const auto& event = state.events[i];
        require(i == 0 || earlier(state.events[i - 1], event), "canonical event ordering");
        require(event.tick >= tick && (!state.settled || event.tick > tick), "past or unsettled event");
        require(event.sequence < state.next_event_sequence && !sequences[static_cast<std::size_t>(event.sequence)],
            "event sequence uniqueness/range");
        sequences[static_cast<std::size_t>(event.sequence)] = true;
        if (event.type == EventType::ShipmentArrival)
        {
            require(has_arrival && !arrival_seen && event.shipment == arrived && event.tick == arrived * interval &&
                event.tick < horizon, "pending arrival schedule");
            arrival_seen = true;
        }
        else
        {
            require(event.type == EventType::ShipmentCompletion && event.shipment < arrived, "event type/reference");
            const auto& shipment = state.shipments[event.shipment];
            require(shipment.status == ShipmentStatus::InProgress && !completions[event.shipment] &&
                event.tick == *shipment.start_tick + processing, "pending completion");
            completions[event.shipment] = true;
        }
    }
    require(arrival_seen == has_arrival, "missing arrival");
    require(state.peak_pending_events >= state.events.size() && state.peak_pending_events >= 1 &&
        state.peak_pending_events <= std::min<std::uint64_t>(state.next_event_sequence, max_active + 1ULL), "peak_pending_events");
    totals.processed_events = arrived + totals.completed;
    require(state.totals == totals, "metric accumulators");
    if (!state.settled)
        require(state.events == std::vector<ShipmentEvent>{{0, EventType::ShipmentArrival, 0, 0}}, "initial event");
}

std::optional<std::string> first_difference(const WarehouseSnapshot& expected, const WarehouseSnapshot& actual)
{
    const auto mismatch = [&](const std::string& field)
    { return std::optional<std::string>{"Warehouse divergence at tick " + std::to_string(actual.current_tick) + ": " + field}; };
#define CHECK(field) if (expected.field != actual.field) return mismatch(#field)
    CHECK(version); CHECK(scenario_identity); CHECK(tick_rate); CHECK(horizon_ticks); CHECK(processing_ticks);
    CHECK(current_tick); CHECK(settled); CHECK(next_shipment);
    CHECK(capacity.loading_bays); CHECK(capacity.workers); CHECK(capacity.forklifts);
    CHECK(usage.loading_bays); CHECK(usage.workers); CHECK(usage.forklifts);
    CHECK(next_event_sequence); CHECK(peak_pending_events);
    CHECK(totals.started); CHECK(totals.completed); CHECK(totals.total_wait_ticks); CHECK(totals.max_wait_ticks);
    CHECK(totals.completed_processing_ticks); CHECK(totals.occupied_resource_ticks); CHECK(totals.processed_events);
    CHECK(totals.available_bay_ticks); CHECK(totals.available_worker_ticks); CHECK(totals.available_forklift_ticks);
    CHECK(shipments.size());
    for (std::size_t i = 0; i < expected.shipments.size(); ++i)
    {
        const auto& a = expected.shipments[i]; const auto& b = actual.shipments[i];
        if (a == b) continue;
        const auto path = "shipments[" + std::to_string(i) + "].";
        if (a.id != b.id) return mismatch(path + "id");
        if (a.status != b.status) return mismatch(path + "status (expected " + status_name(a.status) +
            ", actual " + status_name(b.status) + ")");
        if (a.arrival_tick != b.arrival_tick) return mismatch(path + "arrival_tick");
        if (a.start_tick != b.start_tick) return mismatch(path + "start_tick");
        if (a.completion_tick != b.completion_tick) return mismatch(path + "completion_tick");
    }
    CHECK(waiting_queue.size()); CHECK(reservations.size()); CHECK(events.size()); CHECK(capacity_changes.size());
    for (std::size_t i = 0; i < expected.waiting_queue.size(); ++i)
        if (expected.waiting_queue[i] != actual.waiting_queue[i]) return mismatch("waiting_queue[" + std::to_string(i) + "]");
    for (std::size_t i = 0; i < expected.reservations.size(); ++i)
        if (expected.reservations[i] != actual.reservations[i]) return mismatch("reservations[" + std::to_string(i) + "]");
    for (std::size_t i = 0; i < expected.events.size(); ++i)
    {
        const auto& a = expected.events[i]; const auto& b = actual.events[i];
        if (a == b) continue;
        const auto path = "events[" + std::to_string(i) + "] (shipment " + std::to_string(b.shipment) + ").";
        if (a.tick != b.tick) return mismatch(path + "tick");
        if (a.type != b.type) return mismatch(path + "type");
        if (a.sequence != b.sequence) return mismatch(path + "sequence");
        return mismatch(path + "shipment");
    }
    for (std::size_t i = 0; i < expected.capacity_changes.size(); ++i)
    {
        const auto& a = expected.capacity_changes[i]; const auto& b = actual.capacity_changes[i];
        if (a == b) continue;
        const auto path = "capacity_changes[" + std::to_string(i) + "].";
        if (a.tick != b.tick) return mismatch(path + "tick");
        if (a.started_before != b.started_before) return mismatch(path + "started_before");
        return mismatch(path + "forklifts");
    }
#undef CHECK
    return std::nullopt;
}
} // namespace simforge::warehouse
