#include <simforge/warehouse/warehouse_simulation.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>

#ifdef NDEBUG
namespace
{
using timer = std::chrono::steady_clock;
struct Sample
{
    double initialization_ms, execution_ms, metrics_ms, total_ms;
    std::size_t storage_bytes;
    std::uint64_t events, checksum;
};
double milliseconds(timer::time_point begin, timer::time_point end)
{
    return std::chrono::duration<double,std::milli>{end-begin}.count();
}
Sample run(std::uint32_t arrivals)
{
    using namespace simforge;
    scenario::ScenarioDefinition definition;
    definition.name = "Warehouse benchmark";
    definition.simulation = {60,arrivals,0};
    definition.domain = scenario::WarehouseConfig{4,8,4,1,4};
    const auto begin = timer::now();
    auto runtime = std::make_unique<warehouse::WarehouseSimulation>(definition);
    const auto initialized = timer::now();
    runtime->run();
    const auto executed = timer::now();
    const auto metrics = runtime->metrics();
    const auto finalized = timer::now();
    // One query is below clock resolution on this machine. Batch read-only
    // finalizations separately; report per-query cost including checksum consumption.
    const auto query_begin = timer::now();
    std::uint64_t metric_checksum = 0;
    for (int query = 0; query < 1000; ++query)
    {
        const auto value = runtime->metrics();
        metric_checksum += value.completed + value.occupied_resource_ticks;
    }
    const auto query_end = timer::now();
    // Independent closed-form expectations. Every record is consumed/validated
    // outside measured phases; no correctness timing assertions are made.
    if (metrics.arrived != arrivals || metrics.started != arrivals || metrics.completed != arrivals-3 ||
        metrics.waiting != 0 || metrics.in_progress != 3 || metrics.total_wait_ticks != 0 ||
        metrics.occupied_resource_ticks != (4ULL*arrivals-6)*60 || runtime->processed_events() != 2ULL*arrivals-3)
        throw std::runtime_error("Warehouse benchmark metric validation failed");
    std::uint64_t checksum = metrics.occupied_resource_ticks + metrics.completed + metric_checksum;
    for (const auto& record : runtime->shipments())
    {
        const auto tick = static_cast<std::uint64_t>(record.id)*60;
        if (record.arrival_tick != tick || record.start_tick != tick ||
            (record.id < arrivals-3 && record.completion_tick != tick+240) ||
            (record.id >= arrivals-3 && record.completion_tick))
            throw std::runtime_error("Warehouse benchmark timeline validation failed");
        checksum += record.arrival_tick + *record.start_tick;
    }
    return {milliseconds(begin,initialized), milliseconds(initialized,executed), milliseconds(query_begin,query_end) / 1000,
            milliseconds(begin,finalized), runtime->approximate_storage_bytes(), runtime->processed_events(), checksum};
}
template<class Member> double median(const std::array<Sample,7>& samples, Member field)
{
    std::array<double,7> values;
    for (std::size_t i=0; i<values.size(); ++i) values[i] = samples[i].*field;
    std::ranges::sort(values); return values[3];
}
}
#endif
int main()
{
#ifndef NDEBUG
    std::cerr << "Warehouse benchmarks require Release\n"; return 1;
#else
    try
    {
        std::cout << "Warehouse benchmark: one warmup + seven measured runs; 60 Hz, 1-second arrivals, 4-second processing, resources 4/8/4\n"
                  << "Initialization includes validation and runtime allocation; parsing/console/teardown excluded. Owned payload bytes approximate peak storage.\n"
                  << "Metrics column: per-query cost from a 1000-query batch incl. checksum consumption; batch excluded from total runtime.\n"
                  << "arrivals,initialize_ms,events_ms,metrics_ms,total_ms,owned_storage_bytes,processed_events,checksum\n";
        for (std::uint32_t count : {1000,10000,100000})
        {
            (void)run(count); std::array<Sample,7> samples; std::uint64_t checksum = 0;
            for (auto& sample : samples) { sample=run(count); checksum += sample.checksum; }
            std::cout << std::fixed << std::setprecision(6) << count << ','
                      << median(samples,&Sample::initialization_ms) << ',' << median(samples,&Sample::execution_ms) << ','
                      << median(samples,&Sample::metrics_ms) << ',' << median(samples,&Sample::total_ms) << ','
                      << samples[0].storage_bytes << ',' << samples[0].events << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
}
