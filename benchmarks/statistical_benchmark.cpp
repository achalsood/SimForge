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
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void wait(workspace::ExperimentExecution& service) {const auto deadline=Clock::now()+std::chrono::seconds(60);while(service.progress().active()) {if(Clock::now()>deadline) throw std::runtime_error("Benchmark timeout");std::this_thread::yield();}}
analysis::StatisticalRequest source(unsigned replications,unsigned candidates,unsigned horizon=10000) {
    analysis::StatisticalRequest r;r.baseline={1,"Statistical benchmark",{60,horizon,42},scenario::WarehouseConfig{16,16,2,0,0}};
    auto& workload=std::get<scenario::WarehouseConfig>(r.baseline.domain).workload;
    workload.arrivals=scenario::ArrivalMode::Uniform;workload.arrival_min_seconds=1;workload.arrival_max_seconds=3;
    workload.processing=scenario::ProcessingMode::Uniform;workload.processing_min_seconds=1;workload.processing_max_seconds=8;
    r.plan.count=replications;r.plan.base_seed=1001;
    for(unsigned i=0;i<candidates;++i) {auto d=r.baseline;d.name="Capacity "+std::to_string(100+i);std::get<scenario::WarehouseConfig>(d.domain).forklifts=3+i;r.candidates.push_back({i+1,std::move(d)});}return r;
}
std::size_t payload(const analysis::StatisticalReport& r) {
    std::size_t bytes=sizeof(r)+r.replications.capacity()*sizeof(analysis::ReplicationOutcome)+r.candidates.capacity()*sizeof(analysis::StatisticalResult);
    for(const auto& row:r.replications) bytes+=row.candidates.capacity()*sizeof(warehouse::WarehouseMetrics);
    for(const auto& c:r.candidates) {bytes+=c.name.capacity();for(const auto& s:c.metrics) bytes+=s.diagnostic.capacity();}
    for(const auto& p:r.probes) for(const auto& s:p) bytes+=s.diagnostic.capacity();for(const auto& s:r.limitations) bytes+=s.capacity();
    bytes+=analysis::serialize_statistical_request(r.request).size();return bytes;
}
std::size_t peak_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters))) return counters.PeakWorkingSetSize;
#endif
    return 0;
}
int main() {
    try {
        std::cout<<"Release; median of three complete synchronous runs and aggregations; 60 Hz, seed 1001+i, uniform arrivals 1..3 s, processing 1..8 s; 10000 conservative arrivals per seed.\n"
            <<"seeds,candidates,executions,baseline_executions,baseline_executions_saved_vs_per_pair,realized_arrivals,sync_ms,aggregation_ms,async_ms,submit_ms,report_payload_estimate_bytes,report_json_bytes,project_bytes,project_serialize_ms,process_peak_working_set_bytes\n";
        for(auto [seeds,candidates]:{std::pair{3u,3u},{20u,10u},{50u,20u}}) {
            auto request=source(seeds,candidates);std::array<double,3> simulation{},aggregation{};analysis::StatisticalReport report;
            for(auto& time:simulation) {auto start=Clock::now();report=analysis::run_statistical_experiment(request);time=ms(start);}std::sort(simulation.begin(),simulation.end());
            for(auto& time:aggregation) {auto copy=report;auto start=Clock::now();analysis::aggregate_statistical_report(copy);time=ms(start);}std::sort(aggregation.begin(),aggregation.end());
            workspace::ScenarioWorkspace w(request.baseline);
            for(const auto& c:request.candidates) {w.edit(workspace::Field::Name,c.definition.name);w.edit(workspace::Field::Forklifts,std::to_string(std::get<scenario::WarehouseConfig>(c.definition.domain).forklifts));w.add_candidate();}
            auto start=Clock::now();w.start_statistical_experiment(workspace::ComparisonMode::Initial,request.objective,request.plan);const auto submission=ms(start);wait(w.execution());const auto async=ms(start);
            if(!w.poll_experiment()) throw std::runtime_error("No completed statistical result");report.request.id=w.report()->statistics->request.id;
            if(analysis::serialize_statistical_report(report)!=w.json()) throw std::runtime_error("Async benchmark mismatch");
            const auto json=analysis::serialize_statistical_report(report);workspace::Project project(w);start=Clock::now();const auto project_bytes=project.serialize().size();const auto serialization=ms(start);
            std::uint64_t arrivals=0;for(const auto& row:report.replications) arrivals+=row.baseline.arrived;
            std::cout<<std::fixed<<std::setprecision(3)<<seeds<<','<<candidates<<','<<report.simulation_executions<<','<<seeds<<','<<seeds*(candidates-1)<<','<<arrivals<<','<<simulation[1]<<','<<aggregation[1]<<','<<async<<','<<submission<<','<<payload(report)<<','<<json.size()<<','<<project_bytes<<','<<serialization<<','<<peak_memory()<<'\n';
        }
        std::array<double,5> times{};
        for(auto& time:times) {
            std::atomic<bool> candidate{false},reached{false};workspace::ExperimentExecution worker;const auto input=source(20,3,100000);
            workspace::ExperimentRequest request;request.baseline=input.baseline;request.replications=input.plan;for(const auto& c:input.candidates) request.candidates.push_back({c.id,c.definition,{},{}});
            worker.start(request,[&](auto phase,auto,auto tick,auto){if(phase=="Evaluating candidate") candidate=true;if(candidate && phase=="Simulating" && tick>0) reached=true;});
            const auto deadline=Clock::now()+std::chrono::seconds(30);while(!reached) {if(Clock::now()>deadline) throw std::runtime_error("No candidate progress");std::this_thread::yield();}
            const auto start=Clock::now();worker.cancel();wait(worker);time=ms(start);if(worker.progress().state!=workspace::ExecutionState::Cancelled) throw std::runtime_error("Cancellation lost race");
        }
        std::sort(times.begin(),times.end());std::cout<<"ungated_cancel_ms_min_median_max,"<<times.front()<<','<<times[2]<<','<<times.back()<<'\n';return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
