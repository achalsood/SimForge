#include <simforge/warehouse/warehouse_input.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <string_view>

using namespace simforge;
using namespace simforge::warehouse;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Warehouse snapshot check failed at line " + std::to_string(location.line()));
}
template<class Function> void rejects(Function&& function, std::source_location location = std::source_location::current())
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, location);
}
void equal(const WarehouseSnapshot& a, const WarehouseSnapshot& b)
{
    if (auto difference = first_difference(a, b)) throw std::runtime_error(*difference);
    check(a == b);
}
void near(double a, double b) { check(std::isfinite(a) && std::abs(a - b) < 1e-10); }
scenario::ScenarioDefinition definition(std::uint32_t duration = 12, std::uint32_t interval = 1,
    std::uint32_t processing = 4, ResourceCounts capacity = {3,3,1}, std::uint32_t rate = 1)
{
    scenario::ScenarioDefinition result;
    result.name = "Warehouse snapshot correctness"; result.simulation = {rate, duration, 12345};
    result.domain = scenario::WarehouseConfig{capacity.loading_bays, capacity.workers, capacity.forklifts, interval, processing};
    return result;
}
void invariants(const WarehouseSimulation& runtime)
{
    const auto m = runtime.metrics(), again = runtime.metrics(); check(m == again);
    check(m.arrived == m.waiting + m.in_progress + m.completed && m.started == m.in_progress + m.completed);
    const auto capacity = runtime.resource_capacity(), usage = runtime.resource_usage();
    check(usage.loading_bays <= capacity.loading_bays && usage.workers <= capacity.workers && usage.forklifts <= capacity.forklifts);
    check(m.elapsed_ticks <= runtime.horizon_ticks()); validate_snapshot(runtime.capture_snapshot());
}
void incremental()
{
    for (std::uint32_t rate : {1,7,60,1000})
    {
        const auto config = definition(10,2,3,{1,1,1},rate);
        WarehouseSimulation whole{config}, chunks{config}; whole.run();
        chunks.advance_to(0); check(chunks.metrics().arrived == 1 && chunks.metrics().started == 1);
        const auto zero = chunks.capture_snapshot(); chunks.advance_by(0); equal(zero, chunks.capture_snapshot());
        chunks.advance_to(2 * rate); const auto middle = chunks.metrics();
        check(middle.completed == 0 && middle.waiting == 1 && middle.occupied_resource_ticks == 2 * rate);
        near(*middle.loading_bay_utilization, 1); near(*middle.throughput_per_hour, 0);
        for (auto tick : {3ULL,5ULL,6ULL,8ULL,9ULL,10ULL})
        { chunks.advance_to(tick * rate); invariants(chunks); }
        equal(whole.capture_snapshot(), chunks.capture_snapshot());
        const auto before = chunks.capture_snapshot();
        rejects([&] { chunks.advance_to(chunks.horizon_ticks() - 1); });
        rejects([&] { chunks.advance_to(chunks.horizon_ticks() + 1); });
        rejects([&] { chunks.advance_by(std::numeric_limits<std::uint64_t>::max()); });
        equal(before, chunks.capture_snapshot()); chunks.run(); equal(before, chunks.capture_snapshot());
        check(chunks.shipments().back().arrival_tick == 8 * rate && chunks.pending_events() == 1);
    }
    WarehouseSimulation gap{definition(100,100,1)}; gap.advance_to(1); const auto events = gap.processed_events();
    gap.advance_to(99); check(gap.processed_events() == events && gap.pending_events() == 0); invariants(gap);
}
void capture()
{
    const auto config = definition(); WarehouseSnapshot detached;
    {
        WarehouseSimulation source{config}; const auto initial = source.capture_snapshot();
        check(!initial.settled && initial.shipments.empty() && initial.events.front().tick == 0);
        auto initial_branch = WarehouseSimulation::create_from(initial); equal(initial, initial_branch.capture_snapshot());
        source.advance_to(0); check(source.capture_snapshot().settled && source.metrics().started == 1);
        source.advance_to(3); detached = source.capture_snapshot();
        check(detached.waiting_queue == std::vector<ShipmentId>{1,2,3});
        check(detached.events[0].type == EventType::ShipmentCompletion && detached.events[1].type == EventType::ShipmentArrival);
        equal(detached, source.capture_snapshot()); source.run(); check(detached.current_tick == 3 && detached.shipments.size() == 4);
        auto horizon_branch = WarehouseSimulation::create_from(source.capture_snapshot());
        equal(source.capture_snapshot(), horizon_branch.capture_snapshot()); horizon_branch.run();
    }
    auto restored = WarehouseSimulation::create_from(detached); equal(detached, restored.capture_snapshot()); invariants(restored);
    restored.set_forklift_capacity(3); const auto equal_tick = restored.capture_snapshot();
    check(equal_tick.events.size() == 4 && equal_tick.events[2].tick == equal_tick.events[3].tick);
    check(equal_tick.events[2].sequence < equal_tick.events[3].sequence);
    validate_snapshot(equal_tick);
    WarehouseSimulation empty_queue{definition(10,2,1)}; empty_queue.advance_to(1);
    check(empty_queue.capture_snapshot().waiting_queue.empty()); invariants(empty_queue);
}
void restore()
{
    WarehouseSimulation runtime{definition()}; runtime.advance_to(3); const auto checkpoint = runtime.capture_snapshot();
    runtime.run(); const auto final = runtime.capture_snapshot();
    for (int i = 0; i < 3; ++i)
    { runtime.restore_snapshot(checkpoint); equal(checkpoint, runtime.capture_snapshot()); runtime.run(); equal(final, runtime.capture_snapshot()); }
    runtime.restore_snapshot(checkpoint); runtime.set_forklift_capacity(3); const auto changed = runtime.capture_snapshot();
    runtime.run(); const auto changed_final = runtime.capture_snapshot(); runtime.restore_snapshot(changed); runtime.run();
    equal(changed_final, runtime.capture_snapshot());
}
void validation()
{
    WarehouseSimulation source{definition()}; source.advance_to(3); const auto valid = source.capture_snapshot();
    WarehouseSimulation destination{definition()}; destination.run(); const auto before = destination.capture_snapshot();
    const auto reject_mutation = [&](auto mutate)
    {
        auto invalid = valid; mutate(invalid);
        rejects([&] { destination.restore_snapshot(invalid); }); equal(before, destination.capture_snapshot());
        rejects([&] { auto branch = WarehouseSimulation::create_from(invalid); (void)branch; });
        rejects([&] { (void)serialize_snapshot(invalid); });
    };
    reject_mutation([](auto& s) { ++s.version; });
    reject_mutation([](auto& s) { s.scenario_identity += " "; });
    // A different valid scenario can form a branch, but must not restore here.
    auto other = definition(); other.name += " other"; WarehouseSimulation unrelated{other};
    rejects([&] { destination.restore_snapshot(unrelated.capture_snapshot()); }); equal(before, destination.capture_snapshot());
    reject_mutation([](auto& s) { s.current_tick = std::numeric_limits<std::uint64_t>::max(); });
    reject_mutation([](auto& s) { ++s.tick_rate; }); reject_mutation([](auto& s) { ++s.horizon_ticks; });
    reject_mutation([](auto& s) { s.processing_ticks = std::numeric_limits<std::uint64_t>::max(); });
    reject_mutation([](auto& s) { s.settled = false; }); reject_mutation([](auto& s) { ++s.next_shipment; });
    reject_mutation([](auto& s) { s.shipments[1].id = 0; });
    reject_mutation([](auto& s) { s.shipments[0].status = static_cast<ShipmentStatus>(255); });
    reject_mutation([](auto& s) { s.shipments[1].start_tick = 3; });
    reject_mutation([](auto& s) { s.shipments[0].start_tick = std::numeric_limits<std::uint64_t>::max(); });
    reject_mutation([](auto& s) { s.shipments[0].completion_tick = 1; });
    reject_mutation([](auto& s) { s.shipments[0].arrival_tick = 1; });
    reject_mutation([](auto& s) { s.waiting_queue[0] = 99; });
    reject_mutation([](auto& s) { s.waiting_queue[1] = s.waiting_queue[0]; });
    reject_mutation([](auto& s) { std::swap(s.waiting_queue[0], s.waiting_queue[1]); });
    reject_mutation([](auto& s) { s.reservations.clear(); });
    reject_mutation([](auto& s) { s.reservations.push_back(1); });
    reject_mutation([](auto& s) { s.usage.workers = 2; });
    reject_mutation([](auto& s) { s.capacity.forklifts = 0; });
    reject_mutation([](auto& s) { s.events[0].tick = s.current_tick; });
    reject_mutation([](auto& s) { ++s.events[0].tick; });
    reject_mutation([](auto& s) { s.events[0].type = static_cast<EventType>(255); });
    reject_mutation([](auto& s) { s.events[0].shipment = 99; });
    reject_mutation([](auto& s) { s.events[1].shipment = 0; });
    reject_mutation([](auto& s) { s.events[1].tick = s.horizon_ticks; });
    reject_mutation([](auto& s) { s.events[1].sequence = s.events[0].sequence; });
    reject_mutation([](auto& s) { s.events.push_back(s.events[0]); });
    reject_mutation([](auto& s) { s.events.clear(); });
    reject_mutation([](auto& s) { std::swap(s.events[0], s.events[1]); });
    reject_mutation([](auto& s) { s.next_event_sequence = std::numeric_limits<std::uint64_t>::max(); });
    reject_mutation([](auto& s) { ++s.peak_pending_events; });
    for (auto member : {&WarehouseTotals::started, &WarehouseTotals::completed, &WarehouseTotals::total_wait_ticks,
        &WarehouseTotals::max_wait_ticks, &WarehouseTotals::completed_processing_ticks, &WarehouseTotals::occupied_resource_ticks,
        &WarehouseTotals::processed_events, &WarehouseTotals::available_bay_ticks, &WarehouseTotals::available_worker_ticks,
        &WarehouseTotals::available_forklift_ticks})
        reject_mutation([&](auto& s) { s.totals.*member = std::numeric_limits<std::uint64_t>::max(); });
    source.set_forklift_capacity(3); source.advance_to(8); const auto history = source.capture_snapshot();
    const auto bad_history = [&](auto mutate)
    { auto s = history; mutate(s); rejects([&] { destination.restore_snapshot(s); }); equal(before, destination.capture_snapshot()); };
    bad_history([](auto& s) { s.capacity_changes[0].forklifts = 0; });
    bad_history([](auto& s) { s.capacity_changes[0].started_before = 100000; });
    bad_history([](auto& s) { s.capacity_changes[0].tick = s.current_tick + 1; });
    bad_history([](auto& s) { s.capacity_changes[0].tick = 0; });
    bad_history([](auto& s) { s.capacity_changes.resize(max_capacity_changes + 1); });
    // Lifecycle transitions must agree with completed timestamps and held state.
    auto completed = history; completed.shipments[0].completion_tick.reset();
    rejects([&] { destination.restore_snapshot(completed); }); equal(before, destination.capture_snapshot());
}
std::uint64_t checksum(std::span<const std::byte> bytes)
{
    std::uint64_t value = 14695981039346656037ULL;
    for (auto byte : bytes) { value ^= std::to_integer<std::uint8_t>(byte); value *= 1099511628211ULL; }
    return value;
}
void seal(std::vector<std::byte>& bytes)
{
    const auto value = checksum(std::span{bytes}.first(bytes.size() - 8));
    for (std::size_t i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<std::byte>((value >> (8 * i)) & 255);
}
void serialization()
{
    WarehouseSimulation runtime{definition()}; runtime.advance_to(3); runtime.set_forklift_capacity(3);
    const auto snapshot = runtime.capture_snapshot(), before = snapshot;
    const auto bytes = serialize_snapshot(snapshot); check(bytes == serialize_snapshot(snapshot));
    equal(snapshot, deserialize_snapshot(bytes)); equal(before, runtime.capture_snapshot());
    auto branch = WarehouseSimulation::create_from(deserialize_snapshot(bytes)); branch.run(); runtime.run();
    equal(runtime.capture_snapshot(), branch.capture_snapshot());
    for (std::size_t length = 0; length < bytes.size(); ++length)
        rejects([&] { (void)deserialize_snapshot(std::span{bytes}.first(length)); });
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        auto damaged = bytes; damaged[i] ^= std::byte{1}; rejects([&] { (void)deserialize_snapshot(damaged); });
    }
    const auto edit = [&](std::size_t offset, std::byte value)
    { auto invalid = bytes; invalid[offset] = value; seal(invalid); rejects([&] { (void)deserialize_snapshot(invalid); }); };
    edit(0, std::byte{0}); edit(8, std::byte{2}); // Correct-checksum magic/version attacks.
    for (std::size_t i = 12; i < 16; ++i) edit(i, std::byte{255}); // Invalid identity length.
    const auto timing = 16 + snapshot.scenario_identity.size();
    edit(timing + 28, std::byte{2}); // Nonboolean settled.
    const auto record_count = timing + 33;
    edit(record_count + 3, std::byte{255}); // Hostile vector length before allocation.
    edit(record_count + 4 + 4, std::byte{255}); // Status enum, first record.
    const auto event_count_offset = record_count + 4 + 23 * snapshot.shipments.size() -
        8 * snapshot.waiting_queue.size() + 4 + 4 * snapshot.waiting_queue.size() + 4 + 4 * snapshot.reservations.size() + 24;
    edit(event_count_offset + 4 + 8, std::byte{255}); // Event enum with a valid checksum.
    edit(event_count_offset + 4 + 17, std::byte{255}); // Invalid shipment payload.
    auto trailing = bytes; trailing.insert(trailing.end() - 8, std::byte{0}); seal(trailing);
    rejects([&] { (void)deserialize_snapshot(trailing); });
    std::vector<std::byte> oversized(warehouse_snapshot_max_bytes + 1);
    rejects([&] { (void)deserialize_snapshot(oversized); });
    const auto path = std::filesystem::path{SIMFORGE_TEST_DIR} / "warehouse-checkpoint.bin";
    std::filesystem::remove(path); save_snapshot(path, snapshot); equal(snapshot, load_snapshot(path));
    rejects([&] { save_snapshot(path, snapshot); }); equal(snapshot, load_snapshot(path)); std::filesystem::remove(path);
    rejects([&] { (void)load_snapshot(path); });
    WarehouseSimulation initial{definition()}; equal(initial.capture_snapshot(), deserialize_snapshot(serialize_snapshot(initial.capture_snapshot())));
    equal(runtime.capture_snapshot(), deserialize_snapshot(serialize_snapshot(runtime.capture_snapshot())));
}
void branching()
{
    WarehouseSimulation baseline{definition()}; baseline.advance_to(3); const auto checkpoint = baseline.capture_snapshot();
    auto a = WarehouseSimulation::create_from(checkpoint), b = WarehouseSimulation::create_from(checkpoint);
    equal(a.capture_snapshot(), b.capture_snapshot());
    const auto original_completion = b.capture_snapshot().events[0];
    b.set_forklift_capacity(3); check(b.shipments()[0].start_tick == 0 && b.capture_snapshot().events[0] == original_completion);
    check(b.shipments()[1].start_tick == 3 && b.shipments()[2].start_tick == 3 && !b.shipments()[3].start_tick);
    const auto changed = b.capture_snapshot(); rejects([&] { b.set_forklift_capacity(2); }); equal(changed, b.capture_snapshot());
    rejects([&] { b.set_forklift_capacity(100000); }); equal(changed, b.capture_snapshot());
    rejects([&] { b.set_forklift_capacity(std::numeric_limits<std::uint32_t>::max()); }); equal(changed, b.capture_snapshot());
    equal(checkpoint, a.capture_snapshot()); equal(checkpoint, baseline.capture_snapshot());
    a.run(); b.run(); invariants(a); invariants(b);
    check(a.metrics().completed == 3 && a.metrics().waiting == 9);
    check(b.metrics().completed == 7 && b.metrics().started == 9 && b.metrics().waiting == 3 && b.metrics().in_progress == 2);
    check(b.metrics().occupied_resource_ticks == 30 && b.capture_snapshot().totals.available_forklift_ticks == 30);
    near(*b.metrics().forklift_utilization, 1); near(*b.metrics().loading_bay_utilization, 30.0 / 36);
    check(b.metrics().total_wait_ticks == 18 && b.metrics().max_wait_ticks == 4);
    b.set_forklift_capacity(2); check(b.shipments()[7].start_tick == 11 && b.pending_events() == 2); invariants(b);
    auto repeated = WarehouseSimulation::create_from(checkpoint); repeated.set_forklift_capacity(3); repeated.run(); repeated.set_forklift_capacity(2);
    equal(b.capture_snapshot(), repeated.capture_snapshot());
    check(first_difference(a.capture_snapshot(), b.capture_snapshot()).has_value());
    auto discrepant = checkpoint; discrepant.shipments[1].status = ShipmentStatus::InProgress;
    const auto path = first_difference(checkpoint, discrepant); check(path && path->find("shipments[1].status") != std::string::npos);
    WarehouseSimulation fresh{definition()}; const auto fresh_before = fresh.capture_snapshot();
    rejects([&] { fresh.set_forklift_capacity(3); }); equal(fresh_before, fresh.capture_snapshot());
    WarehouseSimulation zero{definition(12,1,4,{3,3,0})}; zero.advance_to(3); check(zero.metrics().started == 0);
    zero.set_forklift_capacity(3); check(zero.metrics().started == 3); invariants(zero);
    WarehouseSimulation limit{definition()}; limit.advance_to(0);
    for (std::size_t i = 0; i < max_capacity_changes; ++i) limit.set_forklift_capacity(1);
    const auto full_history = limit.capture_snapshot();
    rejects([&] { limit.set_forklift_capacity(1); }); equal(full_history, limit.capture_snapshot());
    validate_snapshot(full_history); equal(full_history, deserialize_snapshot(serialize_snapshot(full_history)));
}
void replay()
{
    WarehouseSimulation runtime{definition()}; runtime.advance_to(3); const auto checkpoint = runtime.capture_snapshot();
    WarehouseInputRecorder recorder{runtime}; recorder.set_forklift_capacity(2); recorder.set_forklift_capacity(3);
    const auto before = runtime.capture_snapshot(); rejects([&] { recorder.set_forklift_capacity(2); }); equal(before, runtime.capture_snapshot());
    rejects([&] { recorder.submit({2,2,WarehouseCommandType::SetForkliftCapacity,3}); });
    rejects([&] { recorder.submit({3,99,WarehouseCommandType::SetForkliftCapacity,3}); });
    rejects([&] { recorder.submit({3,2,static_cast<WarehouseCommandType>(255),3}); });
    recorder.advance_to(12); const auto recording = recorder.capture_recording();
    check(recording.commands.size() == 2 && recording.commands[0].tick == recording.commands[1].tick);
    auto dest = WarehouseSimulation::create_from(checkpoint); replay_recording(recording, dest); equal(runtime.capture_snapshot(), dest.capture_snapshot());
    const auto text = serialize_recording(recording); check(text == serialize_recording(recording));
    const auto loaded = parse_recording(text); dest.restore_snapshot(deserialize_snapshot(serialize_snapshot(checkpoint)));
    replay_recording(loaded, dest); equal(runtime.capture_snapshot(), dest.capture_snapshot());
    // Replay into a different starting state is rejected transactionally.
    const auto final = dest.capture_snapshot(); rejects([&] { replay_recording(recording, dest); }); equal(final, dest.capture_snapshot());
    dest.restore_snapshot(checkpoint);
    const auto invalid = [&](auto mutate)
    { auto bad = recording; mutate(bad); rejects([&] { replay_recording(bad, dest); }); equal(checkpoint, dest.capture_snapshot()); };
    invalid([](auto& r) { ++r.schema_version; }); invalid([](auto& r) { r.scenario_identity += " "; });
    invalid([](auto& r) { ++r.initial_tick; }); invalid([](auto& r) { --r.end_tick; });
    invalid([](auto& r) { r.commands[0].sequence = 1; }); invalid([](auto& r) { r.commands[1].tick = 2; });
    invalid([](auto& r) { r.commands[0].forklifts = 0; });
    invalid([](auto& r) { r.expected_final.events[0].sequence = r.expected_final.next_event_sequence; });
    // A valid logical state with different accepted input history still fails
    // isolated replay verification after executing the commands.
    WarehouseSimulation alternate{definition()}; alternate.advance_to(3); alternate.set_forklift_capacity(3); alternate.set_forklift_capacity(3); alternate.run();
    auto forged = recording; forged.expected_final = alternate.capture_snapshot();
    forged.commands[0].forklifts = 3; // Now a valid different recording.
    replay_recording(forged, dest); equal(alternate.capture_snapshot(), dest.capture_snapshot()); dest.restore_snapshot(checkpoint);
    forged.expected_final.events[0].sequence = forged.expected_final.events[1].sequence - 1; // Different legal, unique sequence if not original.
    if (forged.expected_final.events[0].sequence == alternate.capture_snapshot().events[0].sequence)
        forged.expected_final.events[0].sequence = 0;
    validate_snapshot(forged.expected_final);
    rejects([&] { replay_recording(forged, dest); }); equal(checkpoint, dest.capture_snapshot());
    auto json = nlohmann::json::parse(text);
    for (const auto& key : {"domain", "schema_version", "commands"})
    { auto missing = json; missing.erase(key); rejects([&] { (void)parse_recording(missing.dump()); }); }
    json["commands"][0]["type"] = "unsupported"; rejects([&] { (void)parse_recording(json.dump()); });
    json = nlohmann::json::parse(text); json["commands"][0]["forklifts"] = -1; rejects([&] { (void)parse_recording(json.dump()); });
    json = nlohmann::json::parse(text); json["unexpected"] = 1; rejects([&] { (void)parse_recording(json.dump()); });
    const auto file = std::filesystem::path{SIMFORGE_TEST_DIR} / "warehouse-inputs.json";
    std::filesystem::remove(file); save_recording(file, recording); const auto from_file = load_recording(file);
    replay_recording(from_file, dest); equal(runtime.capture_snapshot(), dest.capture_snapshot());
    rejects([&] { save_recording(file, recording); }); std::filesystem::remove(file);
    WarehouseSimulation untouched{definition()}; WarehouseInputRecorder empty{untouched};
    auto blank_dest = WarehouseSimulation::create_from(untouched.capture_snapshot()); replay_recording(empty.capture_recording(), blank_dest);
    equal(untouched.capture_snapshot(), blank_dest.capture_snapshot());
}
void randomized()
{
    constexpr std::uint64_t seed = 0x5F092026; std::mt19937_64 random{seed};
    for (unsigned iteration = 0; iteration < 150; ++iteration)
    {
        const auto rate = std::array<std::uint32_t,4>{1,7,60,1000}[random() % 4];
        const auto config = definition(10 + static_cast<std::uint32_t>(random() % 90), 1 + static_cast<std::uint32_t>(random() % 7),
            1 + static_cast<std::uint32_t>(random() % 12), {4,4,static_cast<std::uint32_t>(random() % 5)}, rate);
        unsigned operation = 0;
        try
        {
            WarehouseSimulation runtime{config}; runtime.advance_to(0);
            auto saved = runtime.capture_snapshot();
            for (; operation < 80; ++operation)
            {
                switch (random() % 6)
                {
                case 0:
                    runtime.advance_by(random() % (runtime.horizon_ticks() - runtime.clock().tick_count() + 1)); break;
                case 1: saved = runtime.capture_snapshot(); break;
                case 2: runtime.restore_snapshot(saved); break;
                case 3:
                {
                    const auto current = runtime.capture_snapshot(); auto branch = WarehouseSimulation::create_from(current);
                    const auto usage = branch.resource_usage().forklifts; branch.set_forklift_capacity(usage + static_cast<std::uint32_t>(random() % (5 - usage)));
                    branch.run(); invariants(branch); equal(current, runtime.capture_snapshot());
                    auto repeated = WarehouseSimulation::create_from(current);
                    repeated.set_forklift_capacity(branch.resource_capacity().forklifts); repeated.run(); equal(branch.capture_snapshot(), repeated.capture_snapshot());
                    break;
                }
                case 4:
                {
                    const auto usage = runtime.resource_usage().forklifts;
                    runtime.set_forklift_capacity(usage + static_cast<std::uint32_t>(random() % (5 - usage))); break;
                }
                case 5:
                {
                    const auto current = runtime.capture_snapshot(); const auto decoded = deserialize_snapshot(serialize_snapshot(current));
                    equal(current, decoded); runtime.restore_snapshot(decoded); break;
                }
                }
                invariants(runtime);
                // Continuation is checked from every boundary, independent of
                // operations subsequently restoring or changing this runtime.
                const auto checkpoint = runtime.capture_snapshot();
                auto whole = WarehouseSimulation::create_from(checkpoint), chunks = WarehouseSimulation::create_from(checkpoint);
                whole.run(); const auto distance = chunks.horizon_ticks() - chunks.clock().tick_count();
                chunks.advance_by(distance / 2); (void)chunks.metrics(); chunks.run(); equal(whole.capture_snapshot(), chunks.capture_snapshot());
            }
        }
        catch (...)
        {
            std::cerr << "seed=" << seed << " iteration=" << iteration << " operation=" << operation
                      << " scenario=" << scenario::serialize_scenario(config) << '\n'; throw;
        }
    }
}
void stress()
{
    WarehouseSimulation large{definition(100000,1,100000,{10000,10000,10000})}; large.advance_to(9999);
    const auto checkpoint = large.capture_snapshot(); check(checkpoint.reservations.size() == 10000 && checkpoint.events.size() == 10001);
    const auto decoded = deserialize_snapshot(serialize_snapshot(checkpoint)); equal(checkpoint, decoded);
    auto branch = WarehouseSimulation::create_from(decoded); large.run(); branch.run(); equal(large.capture_snapshot(), branch.capture_snapshot());
    check(branch.metrics().arrived == 100000 && branch.metrics().completed == 1 && branch.metrics().in_progress == 9999);
    invariants(branch);
    WarehouseSimulation queued{definition(100000,1,4,{4,8,1},60)}; queued.advance_to(3000000);
    const auto before = queued.capture_snapshot(); auto fork = WarehouseSimulation::create_from(before); fork.set_forklift_capacity(4); fork.run();
    invariants(fork); equal(before, queued.capture_snapshot());
}
}
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Warehouse snapshot test case required");
        const std::string_view name{argv[1]};
        if (name == "incremental") incremental(); else if (name == "capture") capture(); else if (name == "restore") restore();
        else if (name == "validation") validation(); else if (name == "serialization") serialization(); else if (name == "branching") branching();
        else if (name == "replay") replay(); else if (name == "randomized") randomized(); else if (name == "stress") stress();
        else throw std::invalid_argument("Unknown warehouse snapshot test case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
