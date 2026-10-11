#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
using namespace simforge;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
analysis::InteractionRequest source(unsigned axis,unsigned seeds,unsigned horizon){
    analysis::InteractionRequest r;r.source={1,"Interaction benchmark",{60,horizon,42},scenario::WarehouseConfig{16,2,2,0,0}};
    auto& w=std::get<scenario::WarehouseConfig>(r.source.domain).workload;w.arrivals=scenario::ArrivalMode::Uniform;w.arrival_min_seconds=1;w.arrival_max_seconds=3;w.processing=scenario::ProcessingMode::Uniform;w.processing_min_seconds=1;w.processing_max_seconds=8;
    for(unsigned i=1;i<=axis;++i){r.x_values.push_back(i);r.y_values.push_back(i);}r.multi_seed=seeds>1;r.plan.count=seeds;r.plan.base_seed=1001;return r;
}
int main()try{
    std::cout<<"x,y,seeds,horizon,cells,executions,baseline_runs,baseline_reused_cells,events,sync_ms,aggregation_ms,events_per_second,submit_ms,async_ms,json_bytes,project_bytes,serialize_ms,report_compact_bytes,peak_working_set_bytes\n"<<std::fixed<<std::setprecision(3);
    for(const auto& settings:{std::array<unsigned,3>{3,1,10000},{5,20,10000},{10,50,3600},{10,100,1900}}){
        auto r=source(settings[0],settings[1],settings[2]);auto start=Clock::now();auto report=analysis::run_interaction(r);const double sync=ms(start);
        workspace::ScenarioWorkspace w(r.source);const auto async_start=Clock::now();w.start_interaction(r);const double submit=ms(async_start);while(w.execution().progress().active())std::this_thread::yield();w.poll_experiment();if(!w.report()||!w.report()->interaction)throw std::runtime_error("Async benchmark failed");const double async=ms(async_start);
        auto serialize=Clock::now();const auto json=analysis::serialize_interaction_report(report);const double serialize_ms=ms(serialize);workspace::Project project(w);auto path=std::filesystem::temp_directory_path()/("simforge-interaction-benchmark-"+std::to_string(Clock::now().time_since_epoch().count())+".simforge");project.save(path);const auto bytes=project.timings().bytes;std::filesystem::remove(path);
        std::size_t compact=sizeof(report)+report.replications.capacity()*sizeof(analysis::InteractionReplication)+report.cells.capacity()*sizeof(analysis::InteractionCell)+report.interactions.capacity()*sizeof(analysis::InteractionRectangle);
        for(const auto& row:report.replications)compact+=row.cells.capacity()*sizeof(warehouse::WarehouseMetrics);
        for(const auto& a:report.interactions){compact+=a.per_seed.capacity()*sizeof(a.per_seed[0])+a.interpretation.capacity();for(const auto& s:a.effects)compact+=s.diagnostic.capacity();}
        for(const auto& c:report.cells)for(const auto& s:c.effects)compact+=s.diagnostic.capacity();for(const auto& s:report.limitations)compact+=s.capacity();
        std::size_t peak=0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))peak=counters.PeakWorkingSetSize;
#endif
        std::cout<<settings[0]<<','<<settings[0]<<','<<settings[1]<<','<<settings[2]<<','<<report.cells.size()<<','<<report.simulation_executions<<','<<settings[1]<<','<<settings[1]<<','<<report.processed_events<<','<<sync<<','<<report.aggregation_ms<<','<<report.processed_events/(sync/1000)<<','<<submit<<','<<async<<','<<json.size()<<','<<bytes<<','<<serialize_ms<<','<<compact<<','<<peak<<'\n';
    }
    std::vector<double> cancellations;for(unsigned i=0;i<5;++i){auto r=source(10,100,1900);workspace::ExperimentExecution service;workspace::ExperimentRequest request;request.interaction=r;std::atomic<bool> simulating=false;service.start(request,[&](auto phase,auto,auto tick,auto){if(phase=="Simulating"&&tick>0)simulating=true;});while(!simulating&&service.progress().active())std::this_thread::yield();const auto start=Clock::now();service.cancel();while(service.progress().active())std::this_thread::yield();cancellations.push_back(ms(start));}std::sort(cancellations.begin(),cancellations.end());std::cerr<<"Cancellation ms min/median/max: "<<cancellations.front()<<'/'<<cancellations[2]<<'/'<<cancellations.back()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
