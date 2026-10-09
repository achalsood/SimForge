#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <iostream>
#include <source_location>
#include <thread>
using namespace simforge;
using namespace simforge::workspace;
using namespace std::chrono_literals;
namespace {
void check(bool value, std::source_location where=std::source_location::current())
{ if (!value) throw std::runtime_error("Playable run check at line "+std::to_string(where.line())); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw); }
auto baseline() { return scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples/warehouse-baseline.json"); }
auto custom() { auto d=baseline(); d.name="Three forklifts"; std::get<scenario::WarehouseConfig>(d.domain).forklifts=3; return d; }
void equivalent(viewer::ViewerController& v, const scenario::ScenarioDefinition& d, std::uint64_t tick)
{
    v.seek(tick); warehouse::WarehouseSimulation headless{d}; headless.advance_to(tick);
    check(v.runtime().capture_snapshot()==headless.capture_snapshot()); check(v.observation().metrics==headless.metrics());
    check(v.observation()==headless.capture_observation(v.scene().waiting_limit(),v.scene().processing_limit()));
    for (const auto& visual:v.scene().shipments()) {
        const auto& record=headless.shipments()[visual.id]; check(record.id==visual.id && record.status==visual.status);
    }
}
void test(std::string_view name)
{
    const auto b=baseline(); auto d=custom(); ScenarioWorkspace w{b,1};
    if(name=="creation") {
        w.edit(Field::Name,d.name); w.edit(Field::Forklifts,"3"); w.run_draft(); const auto id=w.active_run_id();
        check(id && w.runs().size()==1 && w.view().runtime().clock().paused() && w.view().observation().tick==0);
        w.edit(Field::Forklifts,"0"); check(w.view().observation().capacity.forklifts==3);
        w.edit(Field::Arrival,"-1"); rejects([&]{w.run_draft();}); check(w.active_run_id()==id && !w.errors().empty());
        auto invalid=d; invalid.simulation.tick_rate=0; rejects([&]{w.create_run(invalid);}); check(w.runs().size()==1);
        check(w.create_run(d)>id); check(w.baseline_viewer().observation().tick==0);
    } else if(name=="limit") {
        for(std::size_t i=0;i<max_custom_runs;++i) (void)w.create_run(d);
        const auto id=w.active_run_id(); rejects([&]{w.create_run(d);}); check(w.active_run_id()==id && w.runs().size()==max_custom_runs);
        w.remove_run(id); check(w.viewing_baseline()); check(w.create_run(d)>id); rejects([&]{w.remove_run(0);}); rejects([&]{w.select_run(id);});
    } else if(name=="switching") {
        w.view().seek(120*b.simulation.tick_rate); const auto base=w.view().runtime().capture_snapshot();
        const auto id=w.create_run(d); w.view().set_speed(2); w.view().toggle_playback(); w.view().advance(120s);
        const auto saved=w.view().runtime().capture_snapshot(); check(saved.current_tick==240*d.simulation.tick_rate);
        const auto another=w.create_run(d); w.view().seek(600*d.simulation.tick_rate);
        w.remove_run(another); w.select_run(id); check(w.view().runtime().capture_snapshot()==saved);
        w.select_run(0); check(w.view().runtime().capture_snapshot()==base); w.view().advance(30s);
        check(w.runs()[0].controller->runtime().capture_snapshot()==saved);
        w.select_run(id); check(!w.view().runtime().clock().paused() && w.view().speed()==2 && w.view().runtime().capture_snapshot()==saved);
        w.view().reset(); check(w.view().observation().tick==0 && w.baseline_viewer().runtime().capture_snapshot()==base);
        w.remove_run(id); check(w.viewing_baseline() && w.view().runtime().capture_snapshot()==base);
    } else if(name=="playback") {
        (void)w.create_run(d); auto& v=w.view(); v.advance(1s); check(v.observation().tick==0); v.step(); check(v.observation().tick==1);
        v.set_speed(3); v.toggle_playback(); v.advance(1s); check(v.observation().tick==181); v.toggle_playback(); v.advance(1s); check(v.observation().tick==181);
        v.seek(v.observation().horizon); check(v.observation().at_horizon); v.toggle_playback(); v.step(); check(v.runtime().clock().paused());
        v.seek(0); check(v.observation().metrics.arrived==1); v.reset(); check(v.observation().metrics.arrived==0 && v.speed()==3);
    } else if(name=="replay") {
        (void)w.create_run(d); auto& v=w.view(); equivalent(v,d,7200*60); const auto saved=v.runtime().capture_snapshot();
        v.seek(14400*60); v.seek(1000*60); v.seek(7200*60); check(v.runtime().capture_snapshot()==saved);
        v.seek(0); equivalent(v,d,v.observation().horizon);
    } else if(name=="selection") {
        w.view().seek(600*60); w.view().select(viewer::VisualKey{viewer::VisualCategory::Shipment,0}); check(w.view().inspect().has_value());
        const auto id=w.create_run(d); check(!w.view().selection()); w.view().seek(600*60);
        w.view().select(viewer::VisualKey{viewer::VisualCategory::Shipment,0}); check(w.view().inspect()->shipment->id==0);
        w.select_run(0); check(!w.view().selection()); w.select_run(id); check(!w.view().selection());
        const auto use=w.view().observation().usage;
        for(const auto& r:w.view().scene().resources()) {
            const auto count=r.kind==visualization::WarehouseVisualKind::LoadingBay?use.loading_bays:r.kind==visualization::WarehouseVisualKind::Worker?use.workers:use.forklifts;
            check(r.busy==(r.id<=count));
        }
    } else if(name=="candidate" || name=="async" || name=="checkpoint") {
        w.edit(Field::Name,d.name); w.edit(Field::Forklifts,"3"); const auto candidate=w.add_candidate(); w.simulate_candidate(candidate);
        const auto id=w.active_run_id(); w.view().seek(1000*60); const auto saved=w.view().runtime().capture_snapshot();
        const auto base=w.baseline_viewer().runtime().capture_snapshot();
        if(name=="checkpoint") {
            w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted,7200*60);
            rejects([&]{w.simulate_result(d.name);}); w.view_result(d.name); check(w.view().result_preview());
            rejects([&]{w.view().seek(0);}); rejects([&]{w.view().reset();}); w.view().toggle_playback(); w.view().step();
            check(w.view().observation().at_horizon); w.resume_live_view(); check(w.active_run_id()==id && w.view().runtime().capture_snapshot()==saved);
        } else {
            if(name=="candidate") w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
            else {
                w.start_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
                const auto deadline=std::chrono::steady_clock::now()+10s;
                while(w.execution().progress().active()) { check(std::chrono::steady_clock::now()<deadline); std::this_thread::sleep_for(1ms); }
                check(w.poll_experiment());
            }
            check(w.report().has_value() && w.view().runtime().capture_snapshot()==saved);
            const auto finals=w.report()->final_snapshots; const auto report=w.json(); w.view_result(d.name); check(w.view().result_preview());
            w.resume_live_view(); check(w.active_run_id()==id && w.view().runtime().capture_snapshot()==saved);
            w.edit(Field::Forklifts,"0"); w.update_candidate(candidate); w.simulate_result(d.name); check(w.view().observation().tick==0 && w.view().observation().capacity.forklifts==3);
            equivalent(w.view(),d,w.view().observation().horizon); check(w.view().observation().metrics.completed==94);
            check(w.report()->final_snapshots==finals && w.json()==report); w.remove_run(w.active_run_id());
        }
        check(w.baseline_viewer().runtime().capture_snapshot()==base);
    } else {
        if(name=="arrival") std::get<scenario::WarehouseConfig>(d.domain).shipment_interval_seconds=120;
        else if(name=="processing") std::get<scenario::WarehouseConfig>(d.domain).processing_duration_seconds=1800;
        else if(name=="workers") std::get<scenario::WarehouseConfig>(d.domain).workers=1;
        else if(name=="bays") std::get<scenario::WarehouseConfig>(d.domain).loading_bays=1;
        else if(name=="timing") d.simulation={30,600,99};
        else if(name=="large_capacity") { d.simulation={60,600,42}; d.domain=scenario::WarehouseConfig{1000,2000,3000,1,300}; }
        else if(name!="finals" && name!="file") throw std::invalid_argument("Unknown test");
        if(name=="file") d=scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples/warehouse-higher-capacity.json");
        const auto id=w.create_run(d); equivalent(w.view(),d,scenario::total_ticks(d)/4); equivalent(w.view(),d,scenario::total_ticks(d));
        if(name=="large_capacity") { check(w.view().observation().capacity==warehouse::ResourceCounts{1000,2000,3000}); check(w.view().scene().resources().size()==3*visualization::max_visual_capacity); }
        if(name=="finals") check(w.view().observation().metrics.completed==94 && w.view().observation().metrics.waiting==0);
        w.select_run(0); equivalent(w.view(),b,scenario::total_ticks(b));
        const auto m=w.view().observation().metrics; check(m.arrived==96 && m.started==64 && m.completed==63 && m.waiting==32 && m.in_progress==1);
        w.select_run(id); check(w.view().observation().at_horizon);
    }
}
}
int main(int argc,char** argv) { try { test(argc>1?argv[1]:"creation"); } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
