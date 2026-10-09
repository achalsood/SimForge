#include <simforge/visualization/live_warehouse.hpp>
#include <simforge/warehouse/warehouse_input.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string_view>

using namespace simforge;
using namespace simforge::warehouse;
using namespace simforge::visualization;
using namespace std::chrono_literals;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{ if (!condition) throw std::runtime_error("Live check failed at line "+std::to_string(location.line())); }
template<class Function> void rejects(Function function)
{ bool caught = false; try { function(); } catch (const std::exception&) { caught = true; } check(caught); }
void near(double a, double b) { check(std::abs(a-b) < 1e-8); }
scenario::ScenarioDefinition definition(std::uint32_t duration = 12, std::uint32_t interval = 1,
    std::uint32_t processing = 4, ResourceCounts capacity = {3,3,1}, std::uint32_t rate = 1)
{
    scenario::ScenarioDefinition d; d.name = "Live tests"; d.simulation = {rate,duration,12345};
    d.domain = scenario::WarehouseConfig{capacity.loading_bays,capacity.workers,capacity.forklifts,interval,processing}; return d;
}
void equal_frames(const renderer::RenderFrameData& a, const renderer::RenderFrameData& b)
{
    check(a.objects.size() == b.objects.size());
    for (std::size_t i = 0; i < a.objects.size(); ++i)
    { check(a.objects[i].mesh == b.objects[i].mesh && a.objects[i].color == b.objects[i].color && a.objects[i].transform.matrix() == b.objects[i].transform.matrix()); }
}
WarehouseObservation observe(const WarehouseSimulation& runtime, LiveWarehouseScene& scene, bool reset = true)
{
    auto state = runtime.capture_observation(scene.waiting_limit(),scene.processing_limit()); scene.set_state(state,reset); return state;
}
void audit(const WarehouseObservation& s, const LiveWarehouseScene& scene)
{
    check(s.metrics.arrived == s.metrics.waiting+s.metrics.in_progress+s.metrics.completed);
    check(s.metrics.started == s.metrics.in_progress+s.metrics.completed && s.metrics.elapsed_ticks == s.tick);
    const auto v = scene.visibility();
    check(v.visible_waiting+v.hidden_waiting == s.metrics.waiting && v.visible_processing+v.hidden_processing == s.metrics.in_progress);
    check(scene.shipments().size() == v.visible_waiting+v.visible_processing);
    std::array<std::uint32_t,3> counts{}, busy{};
    for (const auto& resource : scene.resources())
    {
        const auto kind = resource.kind == WarehouseVisualKind::LoadingBay ? 0u : resource.kind == WarehouseVisualKind::Worker ? 1u : 2u;
        ++counts[kind]; busy[kind] += resource.busy ? 1u : 0u;
        check(resource.busy == (resource.id <= s.metrics.in_progress));
    }
    check(counts == std::array{s.capacity.loading_bays,s.capacity.workers,s.capacity.forklifts});
    check(busy == std::array{s.usage.loading_bays,s.usage.workers,s.usage.forklifts});
    std::optional<ShipmentId> previous;
    for (const auto& shipment : scene.shipments())
    {
        check(!previous || *previous < shipment.id); previous = shipment.id;
        check(shipment.status != ShipmentStatus::Completed && shipment.id < s.metrics.arrived);
        check(renderer::finite(shipment.target.transform.matrix()));
        const auto zone_kind = shipment.status == ShipmentStatus::Waiting ? WarehouseZoneKind::Cargo : WarehouseZoneKind::Docks;
        const auto z = std::find_if(scene.layout().zones.begin(),scene.layout().zones.end(),[&](const auto& zone) { return zone.kind == zone_kind; });
        const auto p = shipment.target.transform.position;
        check(p.x-0.65f >= z->bounds.minimum.x && p.x+0.65f <= z->bounds.maximum.x && p.z-0.6f >= z->bounds.minimum.z && p.z+0.6f <= z->bounds.maximum.z);
        for (const auto& other : scene.shipments())
            if (other.id != shipment.id) check(std::abs(p.x-other.target.transform.position.x) >= 1.3f || std::abs(p.z-other.target.transform.position.z) >= 1.2f);
    }
    renderer::validate_frame(scene.frame());
}
void observation()
{
    WarehouseSimulation runtime{definition()}; const auto original = runtime.capture_snapshot();
    const auto zero = runtime.capture_observation(); check(zero.tick == 0 && zero.metrics.arrived == 0 && zero.waiting.empty() && zero.processing.empty());
    check(!zero.metrics.throughput_per_hour && !zero.metrics.loading_bay_utilization && zero == runtime.capture_observation());
    check(original == runtime.capture_snapshot()); runtime.advance_to(0);
    const auto first = runtime.capture_observation(); check(first.processing.size() == 1 && first.processing[0].id == 0 && first.processing[0].start_tick == 0);
    check(first.metrics.arrived == 1 && first.usage == ResourceCounts{1,1,1});
    runtime.advance_to(3); const auto s = runtime.capture_observation(2,0);
    check(s.waiting.size() == 2 && s.waiting[0].id == 1 && s.waiting[1].id == 2 && s.hidden_waiting == 1 && s.hidden_processing == 1);
    check(first.metrics.arrived == 1); check(s == runtime.capture_observation(2,0));
    const auto before = runtime.capture_snapshot(); (void)format_live_metrics(s,{2,1,0,1}); check(before == runtime.capture_snapshot());
}
void lifecycle()
{
    const auto d = definition(); WarehouseSimulation runtime{d}; LiveWarehouseScene scene{d};
    audit(observe(runtime,scene),scene); runtime.advance_to(0); audit(observe(runtime,scene),scene);
    runtime.advance_to(3); auto s = observe(runtime,scene); audit(s,scene);
    check(s.waiting.size() == 3 && s.waiting.front().id == 1 && scene.shipments().front().id == 0);
    runtime.advance_to(4); s = observe(runtime,scene); audit(s,scene);
    check(s.metrics.completed == 1 && s.processing.front().id == 1 && s.waiting.front().id == 2);
    check(std::none_of(scene.shipments().begin(),scene.shipments().end(),[](const auto& o) { return o.id == 0; }));
    runtime.run(); s = observe(runtime,scene); audit(s,scene); check(s.at_horizon && s.metrics.completed == 3 && s.metrics.in_progress == 0);
    const auto end = runtime.capture_observation(); runtime.advance_elapsed(100h); check(end == runtime.capture_observation());
    check(scene.layout_generations() == 1);
}
void capacity()
{
    const auto d = definition(); WarehouseSimulation runtime{d}; LiveWarehouseScene scene{d}; runtime.advance_to(3); audit(observe(runtime,scene),scene);
    runtime.set_forklift_capacity(3); const auto changed = observe(runtime,scene); audit(changed,scene);
    check(changed.metrics.in_progress == 3 && scene.layout_generations() == 2);
    runtime.advance_to(12); rejects([&] { runtime.set_resource_capacity(ResourceType::Worker,0); });
    runtime.set_resource_capacity(ResourceType::Worker,runtime.resource_usage().workers); audit(observe(runtime,scene),scene);
    check(scene.layout_generations() == 3 && scene.resources().size() == 6+runtime.resource_usage().workers);
    const auto short_definition = definition(10,10,2,{2,2,2}); WarehouseSimulation short_run{short_definition}; LiveWarehouseScene short_scene{short_definition};
    short_run.run(); short_run.set_resource_capacity(ResourceType::Worker,0); audit(observe(short_run,short_scene),short_scene);
    runtime.set_forklift_capacity(257); const auto large=observe(runtime,scene);
    check(large.capacity.forklifts==257);
    check(std::count_if(scene.resources().begin(),scene.resources().end(),[](const auto& r){return r.kind==WarehouseVisualKind::Forklift;})==256);
}
void overflow()
{
    const auto d = definition(100000,1,10,{0,0,0}); WarehouseSimulation runtime{d}; LiveWarehouseScene scene{d}; runtime.run();
    const auto full = runtime.capture_observation(); check(full.waiting.size() == 128 && full.hidden_waiting == 99872);
    const auto state = observe(runtime,scene); audit(state,scene);
    check(scene.shipments().size() == 4 && scene.visibility().hidden_waiting == 99996 && scene.resources().empty());
    const auto text = format_live_metrics(state,scene.visibility()); check(text.find("hidden_waiting=99996") != std::string::npos && text.find("unavailable") != std::string::npos);
    for (std::size_t i = 0; i < scene.shipments().size(); ++i) check(scene.shipments()[i].id == i);
}
void timing()
{
    for (double speed : {0.5,1.0,60.0,300.0,3600.0}) for (auto frame : {10ms,17ms,100ms,2000ms})
    {
        const auto d = definition(12,1,4,{3,3,2},60); WarehouseSimulation paced{d}, reference{d}; reference.run(); paced.set_time_scale(speed);
        while (paced.clock().tick_count() < paced.horizon_ticks()) paced.advance_elapsed(frame);
        check(reference.capture_snapshot() == paced.capture_snapshot());
    }
    WarehouseSimulation a{definition(5,1,2,{1,1,1},60)}, b{definition(5,1,2,{1,1,1},60)};
    a.advance_elapsed(1s); for (int i = 0; i < 1000; ++i) b.advance_elapsed(1ms); check(a.capture_snapshot() == b.capture_snapshot());
    a.pause(); const auto paused = a.capture_snapshot(); a.advance_elapsed(10s); check(paused == a.capture_snapshot() && a.clock().paused());
    a.resume(); a.advance_elapsed(1s); check(a.clock().tick_count() == 120);
    a.advance_elapsed(100h); check(a.clock().tick_count() == a.horizon_ticks());
}
void restore()
{
    const auto d = definition(); WarehouseSimulation runtime{d}; LiveWarehouseScene scene{d};
    const auto initial = runtime.capture_snapshot(); runtime.advance_to(3); const auto checkpoint = runtime.capture_snapshot();
    const auto expected = observe(runtime,scene); const auto expected_frame = scene.frame(); runtime.run(); (void)observe(runtime,scene);
    runtime.restore_snapshot(checkpoint); const auto restored = observe(runtime,scene); check(expected == restored); equal_frames(expected_frame,scene.frame());
    runtime.restore_snapshot(initial); audit(observe(runtime,scene),scene); check(scene.shipments().empty() && runtime.clock().tick_count() == 0);
    runtime.set_time_scale(300); runtime.advance_elapsed(1s); check(runtime.clock().tick_count() == runtime.horizon_ticks());
    check(!scene.frame().objects.empty() && !expected_frame.objects.empty());
}
void replay()
{
    const auto d = definition(); WarehouseSimulation source{d}; WarehouseInputRecorder recorder{source};
    recorder.advance_to(2); recorder.set_forklift_capacity(2); recorder.advance_to(8); recorder.set_resource_capacity(ResourceType::Worker,4); recorder.advance_to(12);
    const auto recording = parse_recording(serialize_recording(recorder.capture_recording()));
    auto destination = WarehouseSimulation::create_from(recording.initial_snapshot); replay_recording(recording,destination);
    LiveWarehouseScene a{d}, b{d}; check(observe(source,a) == observe(destination,b)); equal_frames(a.frame(),b.frame()); audit(destination.capture_observation(),b);
}
void animation()
{
    const auto d = definition(); WarehouseSimulation runtime{d}; LiveWarehouseScene scene{d}; runtime.advance_to(0); (void)observe(runtime,scene);
    runtime.advance_to(1); (void)observe(runtime,scene,false);
    check(scene.shipments().back().age == 0ns && scene.shipments().back().status == ShipmentStatus::Waiting);
    const auto before = runtime.capture_snapshot(); scene.animate(100ms); check(scene.shipments().back().age == 100ms);
    const auto age = scene.shipments().back().age; (void)observe(runtime,scene,false); check(scene.shipments().back().age == age);
    scene.animate(100h); check(scene.shipments().back().age == shipment_settle_duration); check(before == runtime.capture_snapshot());
    (void)observe(runtime,scene,true); for (const auto& s : scene.shipments()) check(s.age == shipment_settle_duration);
    runtime.advance_to(4); (void)observe(runtime,scene,false); check(scene.shipments().front().id == 1 && scene.shipments().front().status == ShipmentStatus::InProgress);
    for (const auto& visual : scene.shipments()) check(visual.id != 0);
    WarehouseSimulation jump{definition(500,100,200)}; LiveWarehouseScene large{definition(500,100,200)}; (void)observe(jump,large); jump.advance_to(200); (void)observe(jump,large,false);
    for (const auto& s : large.shipments()) check(s.age == shipment_settle_duration);
}
void metrics()
{
    WarehouseSimulation runtime{definition(12,1,4,{3,3,1})}; runtime.advance_to(3); const auto state = runtime.capture_observation();
    near(*state.metrics.throughput_per_hour,0); near(*state.metrics.loading_bay_utilization,1.0/3); near(*state.metrics.forklift_utilization,1);
    runtime.advance_to(4); const auto four = runtime.capture_observation(); near(*four.metrics.throughput_per_hour,900); near(*four.metrics.average_wait_seconds,1.5);
    const auto before = runtime.capture_snapshot(); (void)format_live_metrics(four,{four.metrics.waiting,0,four.metrics.in_progress,0}); check(before == runtime.capture_snapshot());
    WarehouseSimulation history{definition(100000,1,1,{1,1,1})}; history.run(); const auto end = history.capture_observation();
    check(end.metrics.completed == 100000 && end.processing.empty() && end.waiting.empty());
}
void randomized()
{
    constexpr unsigned seed = 0x14F047; std::mt19937 random{seed};
    try
    {
        for (int repeat = 0; repeat < 80; ++repeat)
        {
            const auto d = definition(5+random()%20,1+random()%4,1+random()%8,{random()%5,random()%5,random()%5},1+random()%60);
            WarehouseSimulation source{d}, reference{d}; LiveWarehouseScene scene{d}; reference.run();
            source.set_time_scale(std::array{0.5,1.0,60.0,300.0}[random()%4]);
            while (source.clock().tick_count() < source.horizon_ticks())
            { source.advance_elapsed(std::chrono::milliseconds{100+random()%900}); audit(observe(source,scene,false),scene); scene.animate(17ms); }
            check(source.capture_snapshot() == reference.capture_snapshot());
        }
    }
    catch (...) { std::cerr << "Randomized live seed=" << seed << '\n'; throw; }
}
void invalid()
{
    WarehouseSimulation runtime{definition()}; const auto before = runtime.capture_snapshot();
    rejects([&] { (void)runtime.capture_observation(100001); }); rejects([&] { (void)runtime.capture_observation(0,100001); });
    rejects([&] { runtime.advance_elapsed(-1ns); }); rejects([&] { runtime.set_time_scale(std::numeric_limits<double>::quiet_NaN()); });
    runtime.set_time_scale(1e308); rejects([&] { runtime.advance_elapsed(1s); }); check(before == runtime.capture_snapshot());
    LiveWarehouseScene scene{definition()}; auto s = runtime.capture_observation(); s.metrics.arrived = 1; rejects([&] { scene.set_state(s); });
    rejects([&] { scene.animate(-1ns); });
    auto movement = definition(); movement.domain = scenario::MovementConfig{}; rejects([&] { LiveWarehouseScene other{movement}; });
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected live case"); const std::string_view name{argv[1]};
        if (name == "observation") observation(); else if (name == "lifecycle") lifecycle(); else if (name == "capacity") capacity();
        else if (name == "overflow") overflow(); else if (name == "timing") timing(); else if (name == "restore") restore();
        else if (name == "replay") replay(); else if (name == "animation") animation(); else if (name == "metrics") metrics();
        else if (name == "randomized") randomized(); else if (name == "invalid") invalid(); else throw std::invalid_argument("Unknown live case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
