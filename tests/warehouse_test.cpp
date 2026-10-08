#include <simforge/warehouse/warehouse_simulation.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string_view>

using namespace simforge;
using namespace simforge::warehouse;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Warehouse check failed at line " + std::to_string(location.line()));
}
void near(double actual, double expected)
{
    check(std::isfinite(actual) && std::abs(actual - expected) < 1e-10);
}
template<class Exception = std::invalid_argument, class Function>
void rejects(Function&& function)
{
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected warehouse rejection");
}
scenario::ScenarioDefinition definition(std::uint32_t duration, std::uint32_t interval, std::uint32_t processing,
                                       ResourceCounts capacity = {1,1,1}, std::uint32_t rate = 60)
{
    scenario::ScenarioDefinition result;
    result.name = "Warehouse correctness";
    result.simulation = {rate, duration, 12345};
    result.domain = scenario::WarehouseConfig{capacity.loading_bays, capacity.workers, capacity.forklifts, interval, processing};
    return result;
}
void scheduler()
{
    EventScheduler queue{5,10};
    check(queue.schedule(5, EventType::ShipmentArrival, 0) == 0);
    check(queue.schedule(5, EventType::ShipmentArrival, 1) == 1);
    check(queue.schedule(5, EventType::ShipmentCompletion, 2) == 2);
    check(queue.schedule(5, EventType::ShipmentCompletion, 3) == 3);
    check(queue.schedule(6, EventType::ShipmentArrival, 4) == 4);
    check(!queue.pop_due(4)); check(queue.current_tick() == 4);
    rejects([&] { (void)queue.schedule(3, EventType::ShipmentCompletion, 0); });
    rejects([&] { (void)queue.schedule(5, static_cast<EventType>(99), 0); });
    rejects<std::out_of_range>([&] { (void)queue.schedule(5, EventType::ShipmentArrival, 99); });
    rejects<std::logic_error>([&] { (void)queue.schedule(5, EventType::ShipmentArrival, 0); });
    check(queue.next_sequence() == 5 && queue.pending_count() == 5);
    const std::uint64_t sequence[] = {2,3,0,1};
    for (auto expected : sequence)
    {
        const auto event = queue.pop_due(5); check(event && event->tick == 5 && event->sequence == expected);
    }
    check(!queue.pop_due(5) && queue.pending_count() == 1); // Future event retained.
    rejects<std::logic_error>([&] { (void)queue.schedule(5, EventType::ShipmentArrival, 0); }); // Already processed.
    const auto last = queue.pop_due(6); check(last && last->shipment == 4);
    check(!queue.pop_due(6) && !queue.pop_due(6)); // No duplicate execution.
    rejects([&] { (void)queue.pop_due(5); });
    EventScheduler bounded{2,1};
    (void)bounded.schedule(1, EventType::ShipmentArrival, 0);
    rejects<std::length_error>([&] { (void)bounded.schedule(2, EventType::ShipmentArrival, 1); });
    check(bounded.next_sequence() == 1);
    (void)bounded.pop_due(1);
    check(bounded.schedule(2, EventType::ShipmentArrival, 1) == 1); // Failed scheduling didn't consume ID or mark payload.
    EventScheduler empty{0,0}; check(!empty.pop_due(0));
    rejects<std::length_error>([] { EventScheduler invalid{100001,1}; });
}
void resources()
{
    ResourceManager resources{{3,2,1},4};
    check(resources.try_acquire(0));
    check(resources.usage() == ResourceCounts{1,1,1} && resources.reserved(0));
    check(!resources.try_acquire(1)); check(resources.usage() == ResourceCounts{1,1,1});
    check(!resources.reserved(1));
    rejects<std::logic_error>([&] { (void)resources.try_acquire(0); });
    rejects<std::logic_error>([&] { resources.release(1); });
    check(resources.active_count() == 1);
    resources.release(0); check(resources.active_count() == 0);
    rejects<std::logic_error>([&] { resources.release(0); });
    check(resources.try_acquire(1)); resources.release(1);
    rejects<std::out_of_range>([&] { (void)resources.try_acquire(4); });
    rejects<std::out_of_range>([&] { resources.release(4); });
    for (ResourceCounts capacity : {ResourceCounts{0,1,1}, ResourceCounts{1,0,1}, ResourceCounts{1,1,0}})
    {
        ResourceManager zero{capacity,1};
        check(!zero.try_acquire(0) && zero.usage() == ResourceCounts{0,0,0});
    }
    rejects<std::length_error>([] { ResourceManager invalid{{100000,1,1},1}; });
}
void analytical()
{
    WarehouseSimulation simulation{definition(10,2,3)};
    const auto empty = simulation.metrics();
    check(empty.arrived == 0 && !empty.throughput_per_hour && !empty.loading_bay_utilization);
    simulation.run();
    const auto m = simulation.metrics();
    check(m.arrived == 5 && m.started == 4 && m.completed == 3 && m.waiting == 1 && m.in_progress == 1);
    check(m.total_wait_ticks == 6*60 && m.max_wait_ticks == 3*60 && m.occupied_resource_ticks == 10*60);
    check(m.completed_processing_ticks == 9*60 && simulation.pending_events() == 1);
    near(*m.average_wait_seconds,1.5); near(*m.maximum_wait_seconds,3);
    near(*m.average_processing_seconds,3); near(m.completed_processing_seconds,9);
    near(*m.loading_bay_utilization,1); near(*m.worker_utilization,1); near(*m.forklift_utilization,1);
    near(*m.throughput_per_hour,1080);
    const auto records = simulation.shipments();
    const std::uint64_t starts[] = {0,3,6,9};
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        check(records[i].id == i && records[i].arrival_tick == i*2*60);
        if (i < 4) check(records[i].start_tick == starts[i]*60);
        else check(!records[i].start_tick);
        if (i < 3) check(records[i].completion_tick == (starts[i]+3)*60);
        else check(!records[i].completion_tick);
    }
    check(simulation.processed_events() == 8);
    simulation.run(); check(simulation.metrics() == m && simulation.processed_events() == 8);
    // A slow arrival stream has idle time rather than fabricated full utilization.
    WarehouseSimulation idle{definition(10,2,1,{3,4,3})}; idle.run();
    const auto light = idle.metrics();
    check(light.completed == 5 && light.waiting == 0 && light.in_progress == 0);
    near(*light.average_wait_seconds,0); near(*light.loading_bay_utilization,1.0/6); near(*light.worker_utilization,0.125);
}
void horizon()
{
    WarehouseSimulation exact{definition(6,2,2)}; exact.run();
    const auto m = exact.metrics();
    check(m.arrived == 3 && m.completed == 3 && m.started == 3 && m.in_progress == 0 && m.waiting == 0);
    check(exact.shipments().back().completion_tick == 6*60); // Completion included, arrival at 6 excluded.
    check(exact.processed_events() == 6 && exact.pending_events() == 0);
    WarehouseSimulation queued{definition(6,1,3)}; queued.run();
    check(queued.metrics().completed == 2 && queued.metrics().started == 2 && queued.metrics().waiting == 4);
    check(queued.metrics().in_progress == 0); // No new service starts at exact horizon.
    WarehouseSimulation unfinished{definition(5,20,7)}; unfinished.run();
    const auto partial = unfinished.metrics();
    check(partial.arrived == 1 && partial.started == 1 && partial.completed == 0 && partial.in_progress == 1);
    check(partial.completed_processing_ticks == 0 && !partial.average_processing_seconds);
    near(*partial.loading_bay_utilization,1); check(partial.occupied_resource_ticks == 5*60);
    check(unfinished.pending_events() == 1 && unfinished.processed_events() == 1);
    const auto before = unfinished.metrics();
    rejects([&] { unfinished.advance_to(6*60); }); rejects([&] { unfinished.advance_to(4*60); });
    check(unfinished.metrics() == before);
    WarehouseSimulation chunks{definition(10,2,3)};
    chunks.advance_to(0); check(chunks.metrics().started == 1 && chunks.clock().tick_count() == 0);
    chunks.advance_to(6*60); check(chunks.metrics().completed == 2 && chunks.metrics().started == 3);
    chunks.run(); WarehouseSimulation all{definition(10,2,3)}; all.run();
    check(chunks.metrics() == all.metrics());
    check(std::ranges::equal(chunks.shipments(), all.shipments()));
}
void capacity()
{
    for (ResourceCounts counts : {ResourceCounts{0,2,2}, ResourceCounts{2,0,2}, ResourceCounts{2,2,0}, ResourceCounts{0,0,0}})
    {
        WarehouseSimulation zero{definition(10,2,3,counts)}; zero.run(); const auto m = zero.metrics();
        check(m.arrived == 5 && m.waiting == 5 && m.started == 0 && m.completed == 0 && m.in_progress == 0);
        check(m.occupied_resource_ticks == 0 && !m.average_wait_seconds && !m.maximum_wait_seconds && !m.average_processing_seconds);
        near(*m.throughput_per_hour,0);
        check(m.loading_bay_utilization.has_value() == (counts.loading_bays != 0));
        check(m.worker_utilization.has_value() == (counts.workers != 0));
        check(m.forklift_utilization.has_value() == (counts.forklifts != 0));
    }
    // Changing each genuine bottleneck independently improves completions.
    WarehouseSimulation abundant{definition(12,1,3,{3,3,3})}; abundant.run();
    for (ResourceCounts counts : {ResourceCounts{1,3,3}, ResourceCounts{3,1,3}, ResourceCounts{3,3,1}})
    {
        WarehouseSimulation limited{definition(12,1,3,counts)}; limited.run();
        check(limited.metrics().completed == 4 && abundant.metrics().completed == 10);
        check(limited.metrics().waiting > abundant.metrics().waiting);
    }
    WarehouseSimulation no_gain{definition(12,1,3,{1,3,100})}; no_gain.run();
    WarehouseSimulation bay_limit{definition(12,1,3,{1,3,3})}; bay_limit.run();
    check(no_gain.metrics().completed == bay_limit.metrics().completed);
    check(std::ranges::equal(no_gain.shipments(), bay_limit.shipments()));
}
void validation()
{
    const std::string legacy = R"({"schema_version":1,"name":"Legacy","domain":"warehouse","simulation":{"tick_rate":60,"duration_seconds":10,"random_seed":0},"warehouse":{"loading_bays":1,"workers":1,"forklifts":1,"shipment_interval_seconds":2}})";
    const auto parsed = scenario::parse_scenario(legacy);
    check(std::get<scenario::WarehouseConfig>(parsed.domain).processing_duration_seconds == 600);
    const auto canonical = scenario::serialize_scenario(parsed);
    check(canonical.find("processing_duration_seconds") != std::string::npos);
    check(scenario::serialize_scenario(scenario::parse_scenario(canonical)) == canonical);
    const auto invalid_json = [&](const char* key, nlohmann::json value)
    {
        auto json = nlohmann::json::parse(canonical); json["warehouse"][key] = value;
        rejects([&] { (void)scenario::parse_scenario(json.dump()); });
    };
    invalid_json("processing_duration_seconds",0); invalid_json("processing_duration_seconds",-1);
    invalid_json("processing_duration_seconds",1.5); invalid_json("processing_duration_seconds",604801);
    invalid_json("processing_duration_seconds","900"); invalid_json("shipment_interval_seconds",0);
    invalid_json("processing",1);
    auto invalid = definition(0,1,1); rejects([&] { WarehouseSimulation simulation{invalid}; });
    invalid = definition(100001,1,1, {1,1,1},1); rejects([&] { WarehouseSimulation simulation{invalid}; });
    invalid = definition(10,1,std::numeric_limits<std::uint32_t>::max()); rejects([&] { WarehouseSimulation simulation{invalid}; });
    invalid = definition(604800,1,1,{1,1,1},1000); rejects([&] { WarehouseSimulation simulation{invalid}; });
    invalid = definition(10,1,1); invalid.simulation.tick_rate = 0; rejects([&] { WarehouseSimulation simulation{invalid}; });
    invalid = definition(10,1,1); invalid.domain = scenario::MovementConfig{};
    rejects([&] { WarehouseSimulation simulation{invalid}; });
}
void randomized()
{
    constexpr std::uint32_t seed = 0x5F082026;
    std::mt19937 random{seed}; scenario::ScenarioDefinition current;
    std::size_t iteration = 0;
    try
    {
        for (; iteration < 300; ++iteration)
        {
            const std::uint32_t rates[] = {1,7,60,1000};
            current = definition(1+random()%100, 1+random()%10, 1+random()%20,
                                 {random()%4, random()%4, random()%4}, rates[random()%4]);
            const auto config = std::get<scenario::WarehouseConfig>(current.domain);
            const auto rate = current.simulation.tick_rate;
            const auto horizon = static_cast<std::uint64_t>(current.simulation.duration_seconds)*rate;
            const auto interval = static_cast<std::uint64_t>(config.shipment_interval_seconds)*rate;
            const auto processing = static_cast<std::uint64_t>(config.processing_duration_seconds)*rate;
            const auto count = 1+(horizon-1)/interval;
            const auto servers = std::min({config.loading_bays,config.workers,config.forklifts});
            std::vector<std::uint64_t> ready(servers,0);
            std::vector<ShipmentRecord> expected;
            std::uint64_t starts=0, completions=0, busy=0, wait=0, maximum_wait=0;
            // Independent multi-server FIFO recurrence, not an event heap implementation.
            for (std::uint64_t i = 0; i < count; ++i)
            {
                const auto arrival = i*interval;
                ShipmentRecord record{static_cast<ShipmentId>(i),ShipmentStatus::Waiting,arrival,{}, {}};
                if (!ready.empty())
                {
                    const auto server = std::min_element(ready.begin(),ready.end());
                    const auto start = std::max(arrival,*server);
                    if (start < horizon)
                    {
                        const auto end = start+processing; *server = end;
                        record.status = ShipmentStatus::InProgress; record.start_tick = start; ++starts;
                        wait += start-arrival; maximum_wait = std::max(maximum_wait,start-arrival);
                        busy += std::min(end,horizon)-start;
                        if (end <= horizon) { record.status = ShipmentStatus::Completed; record.completion_tick = end; ++completions; }
                    }
                }
                expected.push_back(record);
            }
            WarehouseSimulation actual{current}; actual.run(); const auto metrics = actual.metrics();
            check(std::ranges::equal(actual.shipments(),expected));
            check(metrics.arrived == count && metrics.started == starts && metrics.completed == completions);
            check(metrics.waiting == count-starts && metrics.in_progress == starts-completions);
            check(metrics.total_wait_ticks == wait && metrics.max_wait_ticks == maximum_wait && metrics.occupied_resource_ticks == busy);
            check(metrics.completed_processing_ticks == completions*processing);
            check(actual.processed_events() == count+completions && actual.pending_events() == starts-completions);
            check(actual.clock().tick_count() == horizon && actual.peak_pending_events() <= servers+1);
            const auto usage = actual.resource_usage();
            check(usage.loading_bays <= config.loading_bays && usage.workers <= config.workers && usage.forklifts <= config.forklifts);
            for (auto value : {metrics.loading_bay_utilization,metrics.worker_utilization,metrics.forklift_utilization})
                if (value) check(*value >= 0 && *value <= 1);
            WarehouseSimulation repeated{current}; repeated.run();
            check(repeated.metrics() == metrics && std::ranges::equal(repeated.shipments(),actual.shipments()));
        }
    }
    catch (...)
    {
        std::cerr << "Warehouse seed=" << seed << " iteration=" << iteration << " scenario=" << scenario::serialize_scenario(current) << '\n'; throw;
    }
}
void stress()
{
    WarehouseSimulation simulation{definition(100000,1,4,{4,8,4},60)}; simulation.run();
    const auto m = simulation.metrics();
    check(m.arrived == 100000 && m.started == 100000 && m.completed == 99997 && m.in_progress == 3 && m.waiting == 0);
    check(m.occupied_resource_ticks == (4ULL*100000-6)*60 && m.total_wait_ticks == 0);
    check(simulation.processed_events() == 199997 && simulation.peak_pending_events() == 5);
    for (std::size_t i=0; i<simulation.shipments().size(); ++i)
    {
        const auto& record = simulation.shipments()[i];
        check(record.id == i && record.arrival_tick == i*60 && record.start_tick == i*60);
        if (i < 99997) check(record.completion_tick == (i+4)*60);
        else check(!record.completion_tick);
    }
    // Same seconds/timeline at multiple supported rates.
    for (std::uint32_t rate : {1,7,60,1000})
    {
        WarehouseSimulation sample{definition(10,2,3,{1,1,1},rate)}; sample.run();
        check(sample.metrics().completed == 3 && sample.metrics().total_wait_ticks == 6*rate);
        near(*sample.metrics().average_wait_seconds,1.5); near(*sample.metrics().throughput_per_hour,1080);
    }
}
}
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Warehouse test case required");
        const std::string_view name{argv[1]};
        if (name == "scheduler") scheduler(); else if (name == "resources") resources();
        else if (name == "analytical") analytical(); else if (name == "horizon") horizon();
        else if (name == "capacity") capacity(); else if (name == "validation") validation();
        else if (name == "randomized") randomized(); else if (name == "stress") stress();
        else throw std::invalid_argument("Unknown warehouse test case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
