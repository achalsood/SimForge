#include "../scripts/checkpoint-examples.hpp"
#include <simforge/workspace/project.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif
using namespace simforge;namespace cp=analysis::checkpoint;using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::size_t peak(){
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS counters{};if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))return counters.PeakWorkingSetSize;
#endif
 return 0;
}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/("simforge-checkpoint-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{root};
    std::cout<<std::setprecision(12)<<"case,seeds,arrival_bound,horizon_ticks,checkpoint_ticks,prefix_ms,snapshot_ms,branch_ms,control_ms,intervention_ms,aggregation_ms,total_ms,prefix_executions,control_executions,intervention_executions,serialize_ms,parse_ms,save_ms,open_ms,report_bytes,project_bytes,project_increase_bytes,peak_working_set_bytes,cancel_ms\n";
    auto examples=checkpoint_examples();
    for(unsigned index=0;index<6;++index){auto r=examples[index==5?4:0];std::string name;
        if(index==0){name="short_10";r.plan.count=10;r.source.simulation.duration_seconds=600;r.checkpoint_tick=2000;}
        if(index==1){name="eight_hours_20_poisson";r.plan.count=20;}
        if(index==2){name="maximum_supported_100_fixed";r.plan.count=100;r.source.simulation.tick_rate=1;r.source.simulation.duration_seconds=66666;r.checkpoint_tick=22222;auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);c.workload={};c.shipment_interval_seconds=1;c.processing_duration_seconds=20;}
        if(index==3){name="deterministic_20";r.plan.count=20;auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);c.workload={};c.shipment_interval_seconds=120;}
        if(index==4){name="homogeneous_50";r.plan.count=50;}
        if(index==5){name="piecewise_20";r.plan.count=20;}
        const auto bound=scenario::workload::arrival_bound(std::get<scenario::WarehouseConfig>(r.source.domain),r.source.simulation.duration_seconds,r.source.simulation.tick_rate);
        const auto report=cp::run(r);auto started=Clock::now();const auto bytes=cp::report_json(report);const auto serialize_ms=ms(started);started=Clock::now();const auto parsed=cp::parse_report(bytes);const auto parse_ms=ms(started);if(cp::report_json(parsed)!=bytes)throw std::runtime_error("Benchmark roundtrip mismatch");
        workspace::ScenarioWorkspace workspace(r.source);workspace::Project project(workspace);const auto baseline_path=root/(name+"-baseline.simforge");project.save(baseline_path,true);const auto baseline_bytes=std::filesystem::file_size(baseline_path);
        workspace::Comparison comparison;comparison.checkpoint_study=report;comparison.baseline_definition=r.source;comparison.objective=r.objective;comparison.limitations=report.limitations;workspace.retain_study(std::move(comparison));const auto path=root/(name+".simforge");started=Clock::now();project.save(path,true);const auto save_ms=ms(started);workspace::ScenarioWorkspace restored;workspace::Project reopened(restored);started=Clock::now();reopened.open(path);const auto open_ms=ms(started);
        std::stop_source stop;Clock::time_point cancelled{};simulation::ExecutionControl control{stop.get_token(),[&](auto phase,auto,auto,auto){if(phase=="Control continuation"){cancelled=Clock::now();stop.request_stop();}}};double cancel_ms=0;try{cp::run(r,&control);}catch(const simulation::ExecutionCancelled&){cancel_ms=ms(cancelled);}
        const auto& t=report.timings;const auto project_bytes=std::filesystem::file_size(path);std::cout<<name<<','<<r.plan.count<<','<<bound<<','<<scenario::total_ticks(r.source)<<','<<r.checkpoint_tick<<','<<t.prefix_ms<<','<<t.snapshot_ms<<','<<t.branch_ms<<','<<t.control_ms<<','<<t.intervention_ms<<','<<t.aggregation_ms<<','<<t.total_ms<<','<<report.prefix_executions<<','<<report.control_executions<<','<<report.intervention_executions<<','<<serialize_ms<<','<<parse_ms<<','<<save_ms<<','<<open_ms<<','<<bytes.size()<<','<<project_bytes<<','<<project_bytes-baseline_bytes<<','<<peak()<<','<<cancel_ms<<'\n';
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
