#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
using namespace simforge;
std::size_t memory()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};info.cb=sizeof(info);
    GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof(info));return info.PeakWorkingSetSize;
#else
    return 0;
#endif
}
int main()
{
    const auto dir=std::filesystem::temp_directory_path()/("simforge-project-bench-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{dir};
    std::cout<<"case,shipments,custom_runs,bytes,serialize_ms,write_ms,replace_ms,save_total_ms,parse_ms,validate_ms,restore_ms,scene_ms,open_total_ms,recovery_ms,process_peak_working_set_bytes\n";
    for(const auto count:{96u,10000u,100000u}) for(const auto runs:{0u,3u,8u}) {
        scenario::ScenarioDefinition d{1,"Benchmark warehouse",{1,count,42},scenario::WarehouseConfig{3,4,2,1,30}};
        workspace::ScenarioWorkspace w(d);workspace::Project project(w);w.view().seek(count/2);
        for(unsigned i=0;i<runs;++i) {d.name="Custom "+std::to_string(i);w.create_run(d);w.view().seek(count/(i+2));}
        const auto path=dir/"bench.simforge"; if(std::filesystem::exists(path)) std::filesystem::remove(path);
        project.save(path);project.save();const auto save=project.timings();
        workspace::ScenarioWorkspace restored;workspace::Project loaded(restored);loaded.open(path);const auto load=loaded.timings();
        const auto recovery_start=std::chrono::steady_clock::now(); w.view().set_speed(123);
        project.recover_if_due(dir/"bench.recovery",recovery_start+std::chrono::seconds(61));
        const auto recovery=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-recovery_start).count();
        std::cout<<std::fixed<<std::setprecision(3)<<"runs,"<<count<<','<<runs<<','<<save.bytes<<','<<save.serialization_ms<<','<<save.write_ms<<','<<save.replacement_ms<<','<<save.total_ms
            <<','<<load.parsing_ms<<','<<load.validation_ms<<','<<load.restoration_ms<<','<<load.scene_ms<<','<<load.total_ms<<','<<recovery<<','<<memory()<<'\n';
    }
    for(unsigned candidates:{1u,3u,8u}) {
        scenario::ScenarioDefinition d{1,"Report benchmark",{1,96,42},scenario::WarehouseConfig{3,4,2,1,30}};
        workspace::ScenarioWorkspace w(d);workspace::Project project(w);
        for(unsigned i=0;i<candidates;++i) {w.edit(workspace::Field::Name,"Candidate "+std::to_string(i));w.edit(workspace::Field::Forklifts,std::to_string(i+1));w.add_candidate();}
        w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
        const auto path=dir/"reports.simforge";if(std::filesystem::exists(path)) std::filesystem::remove(path);
        project.save(path);project.save();const auto save=project.timings();workspace::ScenarioWorkspace restored;workspace::Project loaded(restored);loaded.open(path);const auto load=loaded.timings();
        std::cout<<"report_"<<candidates<<",96,0,"<<save.bytes<<','<<save.serialization_ms<<','<<save.write_ms<<','<<save.replacement_ms<<','<<save.total_ms<<','<<load.parsing_ms<<','<<load.validation_ms<<','<<load.restoration_ms<<','<<load.scene_ms<<','<<load.total_ms<<",0,"<<memory()<<'\n';
    }
}
