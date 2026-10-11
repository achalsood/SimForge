#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
using namespace simforge;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void wait(workspace::ExperimentExecution& service) {while(service.progress().active())std::this_thread::yield();}
analysis::SensitivityRequest source(unsigned points,unsigned seeds,unsigned horizon) {
    analysis::SensitivityRequest r;r.source={1,"Sensitivity benchmark",{60,horizon,42},scenario::WarehouseConfig{16,16,2,0,0}};
    auto& w=std::get<scenario::WarehouseConfig>(r.source.domain).workload;w.arrivals=scenario::ArrivalMode::Uniform;w.arrival_min_seconds=1;w.arrival_max_seconds=3;w.processing=scenario::ProcessingMode::Uniform;w.processing_min_seconds=1;w.processing_max_seconds=8;
    for(unsigned i=1;i<=points;++i)r.values.push_back(i);r.multi_seed=seeds>1;r.plan.count=seeds;r.plan.base_seed=1001;return r;
}
int main()try {
    std::cout<<"points,seeds,horizon,executions,baseline_runs,baseline_runs_saved,events,sync_ms,aggregation_ms,events_per_second,submit_ms,async_ms,json_bytes,project_bytes,serialize_ms,peak_working_set_bytes\n"<<std::fixed<<std::setprecision(3);
    for(const auto& settings:{std::array<unsigned,3>{6,1,10000},{6,20,10000},{20,50,10000},{50,100,3600}}) {
        auto r=source(settings[0],settings[1],settings[2]);const auto start=Clock::now();auto report=analysis::run_sensitivity(r);const double sync=ms(start);
        workspace::ScenarioWorkspace workspace(r.source);workspace::ExperimentRequest request;request.sensitivity=r;request.baseline=r.source;
        const auto async_start=Clock::now();workspace.execution().start(request);const double submit=ms(async_start);wait(workspace.execution());workspace.poll_experiment();const double async=ms(async_start);
        auto serialize=Clock::now();auto json=analysis::serialize_sensitivity_report(report);const double serialize_ms=ms(serialize);
        workspace::Project project(workspace);auto path=std::filesystem::temp_directory_path()/"simforge-sensitivity-benchmark.simforge";project.save(path);const auto project_bytes=project.timings().bytes;std::filesystem::remove(path);
        std::size_t peak=0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))peak=counters.PeakWorkingSetSize;
#endif
        std::cout<<settings[0]<<','<<settings[1]<<','<<settings[2]<<','<<report.simulation_executions<<','<<settings[1]<<','<<(settings[0]-1)*settings[1]<<','<<report.processed_events<<','<<sync<<','<<report.aggregation_ms<<','<<report.processed_events/(sync/1000)<<','<<submit<<','<<async<<','<<json.size()<<','<<project_bytes<<','<<serialize_ms<<','<<peak<<'\n';
    }
    std::vector<double> cancellations;for(unsigned i=0;i<5;++i) {auto r=source(50,100,3600);workspace::ExperimentExecution execution;workspace::ExperimentRequest request;request.sensitivity=r;std::atomic<bool> simulating{false};execution.start(request,[&](auto phase,auto,auto tick,auto){if(phase=="Simulating"&&tick>0)simulating=true;});while(!simulating&&execution.progress().active())std::this_thread::yield();auto start=Clock::now();execution.cancel();wait(execution);cancellations.push_back(ms(start));}std::sort(cancellations.begin(),cancellations.end());std::cerr<<"Cancellation ms min/median/max: "<<cancellations.front()<<'/'<<cancellations[2]<<'/'<<cancellations.back()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
