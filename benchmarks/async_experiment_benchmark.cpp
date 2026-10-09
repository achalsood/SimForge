#include <simforge/workspace/experiment_execution.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
using namespace simforge;
using namespace simforge::workspace;
using Clock=std::chrono::steady_clock;
namespace {
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>{Clock::now()-start}.count();}
void wait(ExperimentExecution& service) {
    const auto deadline=Clock::now()+std::chrono::seconds{60};
    while(service.progress().active()) {if(Clock::now()>deadline) throw std::runtime_error("Benchmark timed out"); std::this_thread::sleep_for(std::chrono::milliseconds{1});}
}
ExperimentRequest request(int count,std::uint32_t arrivals) {
    scenario::ScenarioDefinition d; d.name="Async benchmark"; d.simulation={60,arrivals,42}; d.domain=scenario::WarehouseConfig{4,8,2,1,4};
    ExperimentRequest r; r.baseline=d;
    for(int i=0;i<count;++i) {auto c=d; c.name="Candidate "+std::to_string(i); c.domain=scenario::WarehouseConfig{4+std::uint32_t(i)%4,8+std::uint32_t(i)%5,2+std::uint32_t(i)%4,1,4}; r.candidates.push_back({std::uint64_t(i+1),c,{},{}});}
    return r;
}
double process_cpu_ms() {
#ifdef _WIN32
    FILETIME create,exit,kernel,user; if(!GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel,&user)) throw std::runtime_error("Process times unavailable");
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime; u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return static_cast<double>(k.QuadPart+u.QuadPart)/10000;
#else
    return 0;
#endif
}
}
int main() {
#ifndef NDEBUG
    std::cerr<<"Use Release for this benchmark\n"; return 1;
#else
    try {
        std::cout<<"Median of three; fresh comparison; callbacks every 256 events. Async retains preview snapshots; sync legacy does not.\n"
            <<"arrivals,candidates,sync_ms,sync_snapshots_ms,async_ms,submit_ms,snapshot_progress_ms,publications,poll_us\n";
        for(auto [arrivals,count]:{std::pair{10000u,3},std::pair{10000u,10},std::pair{10000u,50},std::pair{100000u,3}}) {
            std::array<std::array<double,8>,3> samples{};
            for(auto& sample:samples) {
                auto r=request(count,arrivals); auto start=Clock::now(); const auto expected=execute_experiment(r); sample[0]=ms(start);
                simulation::ExecutionControl control; start=Clock::now(); {auto with_snapshots=execute_experiment(r,&control); sample[1]=ms(start); if(with_snapshots.executions!=expected.executions) throw std::runtime_error("Snapshot mismatch");}
                std::uint64_t calls=0; control.progress=[&](auto,auto,auto,auto){++calls;}; start=Clock::now(); {auto measured=execute_experiment(r,&control); sample[4]=ms(start); if(measured.executions!=expected.executions) throw std::runtime_error("Progress mismatch");}
                ExperimentExecution service; std::atomic<std::uint64_t> publications=0;
                start=Clock::now(); auto id=service.start(r,[&](auto,auto,auto,auto){++publications;}); sample[3]=ms(start); wait(service); sample[2]=ms(start);
                auto actual=service.take_result(id); if(!actual || actual->executions!=expected.executions) throw std::runtime_error("Async mismatch"); sample[5]=static_cast<double>(publications.load());
                start=Clock::now(); for(int i=0;i<10000;++i) (void)service.progress(); sample[6]=ms(start)/10;
                sample[7]=static_cast<double>(calls);
            }
            std::cout<<arrivals<<','<<count<<std::fixed<<std::setprecision(4);
            for(int col=0;col<7;++col) {std::array<double,3> v{samples[0][col],samples[1][col],samples[2][col]}; std::sort(v.begin(),v.end()); std::cout<<','<<v[1];} std::cout<<'\n';
        }
        for(std::string_view mode:{"candidate","probe","checkpoint","shutdown"}) {
            std::array<double,5> latencies{};
            for(auto& latency:latencies) {
                ExperimentExecution service; std::mutex mutex; std::condition_variable cv; bool candidate=false,probe=false,reached=false;
                auto r=request(3,100000);
                if (mode=="checkpoint") {
                    warehouse::WarehouseSimulation historical{r.baseline}; historical.advance_to(50000*60); historical.set_forklift_capacity(3);
                    r.mode=ComparisonMode::Checkpoint; r.checkpoint=historical.capture_snapshot();
                    for(auto& c:r.candidates) std::get<scenario::WarehouseConfig>(c.definition.domain).forklifts=3;
                }
                service.start(std::move(r),[&](auto phase,auto,auto tick,auto){
                    if(phase=="Evaluating candidate") candidate=true;
                    if(phase=="Bottleneck probes") probe=true;
                    if((mode=="probe" ? probe : candidate) && phase=="Simulating" && tick>0) {std::lock_guard lock{mutex}; reached=true; cv.notify_one();}
                });
                {std::unique_lock lock{mutex}; if(!cv.wait_for(lock,std::chrono::seconds{20},[&]{return reached;})) throw std::runtime_error("No candidate checkpoint");}
                auto start=Clock::now(); if(mode=="shutdown") service.shutdown(); else {service.cancel(); wait(service);} latency=ms(start);
                if(service.progress().state!=ExecutionState::Cancelled) throw std::runtime_error("Cancellation lost race");
            }
            std::sort(latencies.begin(),latencies.end()); std::cout<<mode<<"_stop_ms min/median/max: "<<latencies.front()<<'/'<<latencies[2]<<'/'<<latencies.back()<<'\n';
        }
        auto d=request(1,100000).baseline; warehouse::WarehouseSimulation runtime{d}; runtime.run();
        auto start=Clock::now(); auto snapshot=runtime.capture_snapshot(); auto capture_ms=ms(start);
        start=Clock::now(); warehouse::validate_snapshot(snapshot); auto validation_ms=ms(start);
        start=Clock::now(); auto restored=warehouse::WarehouseSimulation::create_from(snapshot); auto restore_ms=ms(start);
        d.domain=scenario::WarehouseConfig{33333,33333,0,1,4}; warehouse::WarehouseSimulation queued{d}; queued.advance_to(99999*60);
        start=Clock::now(); queued.set_forklift_capacity(33333); auto dispatch_ms=ms(start);
        std::cout<<"Non-cancellable 100K snapshot capture/validation/restore ms: "<<capture_ms<<'/'<<validation_ms<<'/'<<restore_ms
            <<" | single-command 33333-start dispatch ms: "<<dispatch_ms<<'\n';
        ExperimentExecution idle; auto cpu=process_cpu_ms(); std::this_thread::sleep_for(std::chrono::milliseconds{200}); std::cout<<"idle process CPU ms over 200 ms: "<<process_cpu_ms()-cpu<<'\n';
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory); if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) std::cout<<"peak process working set MiB: "<<static_cast<double>(memory.PeakWorkingSetSize)/1048576<<'\n';
#endif
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
#endif
}
