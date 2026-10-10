#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
using namespace simforge;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
scenario::ScenarioDefinition definition(unsigned count,int mode)
{
    scenario::ScenarioDefinition d{1,"Workload benchmark",{60,count,42},scenario::WarehouseConfig{16,16,16,1,4}};
    auto& c=std::get<scenario::WarehouseConfig>(d.domain);
    if(mode==1) {
        c.shipment_interval_seconds=0;c.workload.arrivals=scenario::ArrivalMode::Profile;
        for(unsigned i=0;i<64;++i) c.workload.segments.push_back({count*i/64,count*(i+1)/64,1});
    }
    if(mode==2 || mode==4) {c.shipment_interval_seconds=0;c.workload.arrivals=scenario::ArrivalMode::Uniform;c.workload.arrival_min_seconds=1;c.workload.arrival_max_seconds=3;}
    if(mode>=3) {c.processing_duration_seconds=0;c.workload.processing=scenario::ProcessingMode::Uniform;c.workload.processing_min_seconds=1;c.workload.processing_max_seconds=8;}
    return d;
}
void wait(workspace::ExperimentExecution& service)
{
    const auto deadline=Clock::now()+std::chrono::seconds(60);
    while(service.progress().active()) {if(Clock::now()>deadline) throw std::runtime_error("Benchmark timeout");std::this_thread::yield();}
}
int main()
{
    try {
        const std::array names{"constant","profile64","uniform-arrivals","uniform-processing","uniform-both"};
        std::cout<<"Median of three simulation runs; fixed seed 42; capacities 16 each; rate 60.\n"
            <<"mode,arrival_bound,arrived,events,simulation_ms,events_per_second,runtime_payload_bytes,peak_pending,snapshot_bytes,capture_ms,restore_ms,project_bytes,project_serialize_ms\n";
        for(unsigned count:{10000u,100000u}) for(int mode=0;mode<5;++mode) {
            auto d=definition(count,mode);std::array<double,3> timings{};
            for(auto& t:timings) {warehouse::WarehouseSimulation r(d);const auto start=Clock::now();r.run();t=elapsed(start);}
            std::sort(timings.begin(),timings.end());warehouse::WarehouseSimulation r(d);r.run();
            auto start=Clock::now();auto snapshot=r.capture_snapshot();const auto capture=elapsed(start);
            start=Clock::now();auto restored=warehouse::WarehouseSimulation::create_from(snapshot);const auto restore=elapsed(start);
            if(restored.capture_snapshot()!=snapshot) throw std::runtime_error("Restore mismatch");
            const auto bytes=warehouse::serialize_snapshot(snapshot).size();
            workspace::ScenarioWorkspace w(d);w.view().seek(std::uint64_t{count}*60);workspace::Project project(w);
            start=Clock::now();const auto project_bytes=project.serialize().size();const auto serialization=elapsed(start);
            std::cout<<std::fixed<<std::setprecision(3)<<names[mode]<<','<<count<<','<<r.metrics().arrived<<','<<r.processed_events()<<','<<timings[1]<<','<<r.processed_events()*1000.0/timings[1]
                <<','<<r.approximate_storage_bytes()<<','<<r.peak_pending_events()<<','<<bytes<<','<<capture<<','<<restore<<','<<project_bytes<<','<<serialization<<'\n';
        }
        workspace::ExperimentRequest request;request.baseline=definition(100000,4);
        for(unsigned i=0;i<3;++i) {auto d=request.baseline;d.name="Capacity "+std::to_string(i);std::get<scenario::WarehouseConfig>(d.domain).forklifts=2+i;request.candidates.push_back({i+1,d,{},{}});}
        auto start=Clock::now();const auto expected=workspace::execute_experiment(request);const auto sync=elapsed(start);
        workspace::ExperimentExecution service;start=Clock::now();const auto id=service.start(request);const auto submission=elapsed(start);wait(service);const auto async=elapsed(start);
        const auto actual=service.take_result(id);if(!actual || actual->executions!=expected.executions) throw std::runtime_error("Async mismatch");
        std::cout<<"combined_100K_bound_3_candidates_sync_async_submit_ms,"<<sync<<','<<async<<','<<submission<<'\n';
        std::array<double,5> latencies{};
        for(auto& latency:latencies) {
            // Atomics outlive the worker; no artificial pause is inserted into its event loop.
            std::atomic<bool> candidate{false},reached{false};workspace::ExperimentExecution worker;
            worker.start(request,[&](auto phase,auto,auto tick,auto){if(phase=="Evaluating candidate") candidate=true;if(candidate && phase=="Simulating" && tick>0) reached=true;});
            const auto deadline=Clock::now()+std::chrono::seconds(30);
            while(!reached) {if(Clock::now()>deadline) throw std::runtime_error("No candidate progress");std::this_thread::yield();}
            start=Clock::now();worker.cancel();wait(worker);latency=elapsed(start);
            if(worker.progress().state!=workspace::ExecutionState::Cancelled) throw std::runtime_error("Cancellation lost race");
        }
        std::sort(latencies.begin(),latencies.end());std::cout<<"cancel_ms_min_median_max,"<<latencies.front()<<','<<latencies[2]<<','<<latencies.back()<<'\n';
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
        if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) std::cout<<"process_peak_working_set_bytes,"<<memory.PeakWorkingSetSize<<'\n';
#endif
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
