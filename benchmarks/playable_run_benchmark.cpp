#include <simforge/workspace/scenario_workspace.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
using namespace simforge;
using Clock=std::chrono::steady_clock;
namespace {
template<class F> double measure(F f,int count=100) {
    const auto start=Clock::now(); for(int i=0;i<count;++i) f();
    return std::chrono::duration<double,std::micro>{Clock::now()-start}.count()/count;
}
std::size_t private_bytes(bool peak=false) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX c{}; c.cb=sizeof(c);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c))) throw std::runtime_error("Memory measurement failed");
    return peak?c.PeakWorkingSetSize:c.PrivateUsage;
#else
    (void)peak; return 0;
#endif
}
}
int main() {
    try {
        scenario::ScenarioDefinition d; d.name="Playable benchmark"; d.simulation={60,28800,42}; d.domain=scenario::WarehouseConfig{3,10,3,300,900};
        workspace::ScenarioWorkspace w{d}; std::cout<<std::fixed<<std::setprecision(3);
        std::cout<<"validation_us="<<measure([&]{scenario::validate(d);})<<'\n';
        std::cout<<"layout_us="<<measure([&]{(void)visualization::build_warehouse_layout(d);})<<'\n';
        std::cout<<"creation_us="<<measure([&]{const auto id=w.create_run(d); w.remove_run(id);})<<'\n';
        const auto id=w.create_run(d);
        std::cout<<"switch_us="<<measure([&]{w.select_run(0); w.select_run(id);})/2<<'\n';
        std::cout<<"seek_end_us="<<measure([&]{w.view().reset(); w.view().seek(scenario::total_ticks(d));})<<'\n';
        const auto& v=w.view();
        visualization::LiveWarehouseScene scene{d};
        const auto observation=v.observation();
        std::cout<<"observation_us="<<measure([&]{(void)v.runtime().capture_observation(scene.waiting_limit(),scene.processing_limit());})<<'\n';
        std::cout<<"scene_update_us="<<measure([&]{scene.set_state(observation);})<<'\n';
        w.view().reset();
        std::cout<<"playback_tick_us="<<measure([&]{w.view().step();},1000)<<'\n';
        w.view().seek(scenario::total_ticks(d));
        std::cout<<"runtime_payload_bytes="<<v.runtime().approximate_storage_bytes()<<'\n';
        const auto before=private_bytes();
        for(std::size_t i=w.runs().size();i<workspace::max_custom_runs;++i) { w.create_run(d); w.view().seek(scenario::total_ticks(d)); }
        std::size_t payload=w.baseline_viewer().runtime().approximate_storage_bytes();
        for(const auto& run:w.runs()) payload+=run.controller->runtime().approximate_storage_bytes();
        std::cout<<"nine_run_runtime_payload_bytes="<<payload<<" private_growth_seven_runs_bytes="<<private_bytes()-before
            <<" private_total_bytes="<<private_bytes()<<" peak_working_set_bytes="<<private_bytes(true)<<'\n';
        // A larger accepted scenario exposes the retained operational-history cost.
        d.simulation={60,100000,42}; std::get<scenario::WarehouseConfig>(d.domain).shipment_interval_seconds=1;
        workspace::ScenarioWorkspace large{d};
        for(std::size_t i=0;i<workspace::max_custom_runs;++i) {large.create_run(d); large.view().seek(scenario::total_ticks(d));}
        std::cout<<"100k_arrivals_per_run_payload_bytes="<<large.view().runtime().approximate_storage_bytes()
            <<" large_private_total_bytes="<<private_bytes()<<" large_peak_working_set_bytes="<<private_bytes(true)<<'\n';
        w.view().reset(); w.view().toggle_playback();
        std::cout<<"frame_tick_us="<<measure([&]{w.view().advance(std::chrono::milliseconds{16});},1000)<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
