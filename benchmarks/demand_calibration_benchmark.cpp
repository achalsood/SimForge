#include <simforge/workspace/project.hpp>
#include <simforge/workspace/calibration_execution.hpp>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif
using namespace simforge;
namespace demand=analysis::demand;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point began){return std::chrono::duration<double,std::milli>(Clock::now()-began).count();}
std::size_t peak_memory(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))return counters.PeakWorkingSetSize;
#endif
    return 0;
}
int main(){try {
    const auto directory=std::filesystem::temp_directory_path()/("simforge-demand-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{directory};
    const auto start=demand::parse_timestamp("2026-01-05T00:00:00Z");
    std::cout<<"records,csv_bytes,exposure_seconds,model,rate_bins,diagnostic_bins,import_ms,fit_ms,validation_ms,serialization_ms,save_ms,open_ms,simulation_ms,total_ms,peak_process_working_set_bytes,project_bytes,project_size_impact_bytes,cancellation_ms\n";
    for(std::size_t count:{10'000U,100'000U,1'000'000U}) {
        auto path=directory/(std::to_string(count)+".csv");
        {std::ofstream file(path,std::ios::binary);file<<"timestamp\n";for(std::size_t i=0;i<count;++i)file<<demand::timestamp(start+static_cast<std::int64_t>(i*30ULL*86400/count))<<'\n';}
        for(auto model:{demand::Model::Constant,demand::Model::DailyProfile}) {
            const auto began=Clock::now();auto data=demand::load_csv(path,{start,start+30*86400,true});
            auto report=demand::fit(data,{start+20*86400,model,3600,86400});
            auto stamp=Clock::now();auto json=demand::report_json(*report);const auto serialization=ms(stamp);
            scenario::ScenarioDefinition source{1,"Benchmark",{10,600,42},scenario::WarehouseConfig{3,4,2,5,20}};
            auto conversion=demand::convert(*report,source,"Calibrated benchmark",42,start,true);
            workspace::ScenarioWorkspace w(source);workspace::Project baseline_project(w);const auto baseline_bytes=baseline_project.serialize().size();w.retain_calibration(report);w.record_calibration_conversion(conversion);
            workspace::Project project(w);auto project_path=directory/(std::to_string(count)+"-"+std::to_string(static_cast<int>(model))+".simforge");
            stamp=Clock::now();project.save(project_path);const auto save=ms(stamp);stamp=Clock::now();project.open(project_path);const auto open=ms(stamp);
            stamp=Clock::now();warehouse::WarehouseSimulation simulation(conversion.scenario);simulation.run();const auto simulation_ms=ms(stamp);const auto total=ms(began);
            // Gate at an actual phase, then measure cooperative cancellation + join.
            workspace::CalibrationExecution worker;std::mutex mutex;std::condition_variable cv;bool reached=false,released=false;
            worker.start({path,{start,start+30*86400,true},{start+20*86400,model,3600,86400}},[&](auto phase){if(phase!="Parsing")return;std::unique_lock lock(mutex);reached=true;cv.notify_all();cv.wait(lock,[&]{return released;});});
            {std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds{30},[&]{return reached;}))throw std::runtime_error("Cancellation phase timed out");}
            stamp=Clock::now();worker.cancel();{std::lock_guard lock(mutex);released=true;}cv.notify_all();worker.shutdown();const auto cancellation=ms(stamp);
            if(!worker.progress().cancelled)throw std::runtime_error("Cancellation was not acknowledged");
            std::cout<<count<<','<<std::filesystem::file_size(path)<<','<<30*86400<<','<<(model==demand::Model::Constant?"constant":"daily_profile")<<','<<report->rates.size()<<','<<report->bins.size()<<','
                <<data->import_ms<<','<<report->timings.fit_ms<<','<<report->timings.validation_ms<<','<<serialization<<','<<save<<','<<open<<','<<simulation_ms<<','<<total<<','<<peak_memory()<<','<<std::filesystem::file_size(project_path)<<','<<std::filesystem::file_size(project_path)-baseline_bytes<<','<<cancellation<<'\n';
        }
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
