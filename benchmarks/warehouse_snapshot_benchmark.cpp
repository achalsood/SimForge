#include <simforge/warehouse/warehouse_simulation.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
namespace
{
using namespace simforge;
using namespace simforge::warehouse;
using timer = std::chrono::steady_clock;
struct Sample
{
    double continuous, chunked, capture, restore, serialize, deserialize, branch, capacity;
    std::size_t records, bytes;
    std::uint64_t checksum;
};
double milliseconds(timer::time_point a, timer::time_point b)
{ return std::chrono::duration<double, std::milli>{b - a}.count(); }
void equal(const WarehouseSnapshot& a, const WarehouseSnapshot& b)
{ if (auto difference = first_difference(a, b)) throw std::runtime_error(*difference); }
Sample run(std::uint32_t count)
{
    scenario::ScenarioDefinition definition;
    definition.name = "Warehouse checkpoint benchmark"; definition.simulation = {60,count,0};
    definition.domain = scenario::WarehouseConfig{4,8,4,1,4};
    WarehouseSimulation whole{definition}, chunks{definition};
    const auto start = timer::now(); whole.run(); const auto continuous_end = timer::now();
    const auto chunk_start = timer::now();
    for (std::uint64_t part = 1; part <= 100; ++part) chunks.advance_to(chunks.horizon_ticks() * part / 100);
    const auto chunk_end = timer::now();
    equal(whole.capture_snapshot(), chunks.capture_snapshot());
    const auto m = whole.metrics();
    if (m.completed != count - 3 || m.started != count || m.waiting != 0 ||
        m.occupied_resource_ticks != (4ULL * count - 6) * 60)
        throw std::runtime_error("Checkpoint benchmark closed-form verification failed");
    WarehouseSimulation source{definition}; source.advance_to(source.horizon_ticks() / 2);
    const auto capture_start = timer::now(); const auto checkpoint = source.capture_snapshot(); const auto capture_end = timer::now();
    const auto restore_start = timer::now(); chunks.restore_snapshot(checkpoint); const auto restore_end = timer::now();
    equal(checkpoint, chunks.capture_snapshot());
    const auto serialize_start = timer::now(); const auto bytes = serialize_snapshot(checkpoint); const auto serialize_end = timer::now();
    const auto deserialize_start = timer::now(); const auto decoded = deserialize_snapshot(bytes); const auto deserialize_end = timer::now();
    equal(checkpoint, decoded);
    const auto branch_start = timer::now(); auto branch = WarehouseSimulation::create_from(checkpoint); const auto branch_end = timer::now();
    equal(checkpoint, branch.capture_snapshot()); chunks.run(); branch.run();
    equal(whole.capture_snapshot(), branch.capture_snapshot()); equal(whole.capture_snapshot(), chunks.capture_snapshot());
    // Measure a capacity change that actually starts two waiting jobs, using a
    // separate constrained checkpoint, without charging preparation to the command.
    std::get<scenario::WarehouseConfig>(definition.domain).forklifts = 2;
    WarehouseSimulation constrained{definition}; constrained.advance_to(constrained.horizon_ticks() / 2);
    const auto previous = constrained.capture_snapshot();
    const auto capacity_start = timer::now(); constrained.set_forklift_capacity(4); const auto capacity_end = timer::now();
    const auto changed = constrained.capture_snapshot(); validate_snapshot(changed);
    if (changed.totals.started != previous.totals.started + 2 || changed.usage.forklifts != 4)
        throw std::runtime_error("Capacity command benchmark verification failed");
    for (auto id : previous.reservations)
        if (changed.shipments[id].start_tick != previous.shipments[id].start_tick)
            throw std::runtime_error("Capacity command restarted active service");
    std::uint64_t checksum = m.occupied_resource_ticks + m.completed + decoded.totals.started + changed.totals.started;
    for (const auto& record : decoded.shipments) checksum += record.arrival_tick + record.start_tick.value_or(0);
    for (auto byte : bytes) checksum += std::to_integer<unsigned>(byte);
    return {milliseconds(start,continuous_end), milliseconds(chunk_start,chunk_end), milliseconds(capture_start,capture_end),
        milliseconds(restore_start,restore_end), milliseconds(serialize_start,serialize_end), milliseconds(deserialize_start,deserialize_end),
        milliseconds(branch_start,branch_end), milliseconds(capacity_start,capacity_end), checkpoint.shipments.size(), bytes.size(), checksum};
}
double median(const std::array<Sample,7>& samples, double Sample::*field)
{
    std::array<double,7> values;
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = samples[i].*field;
    std::ranges::sort(values); return values[3];
}
}
#endif
int main()
{
#ifndef NDEBUG
    std::cerr << "Warehouse checkpoint benchmarks require Release\n"; return 1;
#else
    try
    {
        std::cout << "Warehouse checkpoints: one warmup + seven measured trials; original 4/8/4 workload; 100 incremental advances.\n"
                  << "Checkpoint at H/2 includes its due arrival (N/2+1 records). Validation included in restore/persistence/branch.\n"
                  << "Capacity change: separate 4/8/2 checkpoint, increase to 4 after due events; two FIFO starts. Preparation/verification/console excluded.\n"
                  << "arrivals,checkpoint_records,continuous_ms,chunked_ms,capture_ms,restore_ms,serialize_ms,deserialize_ms,branch_ms,capacity_ms,binary_bytes,checksum\n";
        for (std::uint32_t count : {1000,10000,100000})
        {
            (void)run(count); std::array<Sample,7> samples; std::uint64_t checksum = 0;
            for (auto& sample : samples) { sample = run(count); checksum += sample.checksum; }
            std::cout << std::fixed << std::setprecision(6) << count << ',' << samples[0].records;
            for (auto field : {&Sample::continuous, &Sample::chunked, &Sample::capture, &Sample::restore,
                &Sample::serialize, &Sample::deserialize, &Sample::branch, &Sample::capacity})
                std::cout << ',' << median(samples, field);
            std::cout << ',' << samples[0].bytes << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
}
