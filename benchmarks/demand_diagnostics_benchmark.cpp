#include <simforge/analysis/demand_diagnostics.hpp>
#include <simforge/workspace/project.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif
using namespace simforge;
namespace demand=analysis::demand;
namespace diagnostics=analysis::diagnostics;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::size_t peak(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))return memory.PeakWorkingSetSize;
#endif
    return 0;
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("simforge-diagnostic-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temporary(){std::filesystem::create_directory(path);}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int main() {
    try {
        Temporary temporary;const auto epoch=demand::parse_timestamp("2026-03-02T00:00:00Z");
        std::cout<<std::setprecision(12)<<"case,observations,valid_bins,coverage_windows,model_parameters,replications,total_ms,preparation_ms,simulation_ms,serialization_ms,parse_ms,reimport_ms,save_ms,open_ms,report_bytes,project_bytes,project_increase_bytes,peak_working_set_bytes,cancel_ms\n";
        for(const auto bins:{100,1000,1024})for(const auto reps:{1U,100U,1000U}) {
            if(static_cast<std::uint64_t>(bins)*reps>1'000'000)continue;std::string csv="timestamp\n";for(int h=0;h<2*bins;++h)csv+=demand::timestamp(epoch+h*3600+12)+"\n";
            auto data=demand::import_csv(csv,"benchmark.csv",{epoch,epoch+2*bins*3600,true});
            auto source=demand::fit(data,{epoch+bins*3600,demand::Model::Constant,86400,7200});
            diagnostics::Request request;request.source=source;request.settings.prediction_start=source->provenance.exposure.end;request.settings.replications=reps;
            const auto started=Clock::now();auto result=diagnostics::run(request,data);const auto total=ms(started);
            auto stage=Clock::now();auto json=diagnostics::report_json(*result);const auto serialization=ms(stage);stage=Clock::now();(void)diagnostics::parse_report(json);const auto parse=ms(stage);
            workspace::ScenarioWorkspace w;workspace::Project project(w);project.create({1,"Benchmark source",{1,120,42},scenario::WarehouseConfig{2,4,2,10,20}});
            w.retain_calibration(source);const auto before=project.serialize().size();w.retain_diagnostic(result);const auto file=temporary.path/(std::to_string(bins)+"-"+std::to_string(reps)+".simforge");
            project.save(file);const auto save=project.timings().total_ms;const auto size=project.timings().bytes;workspace::ScenarioWorkspace reopened;workspace::Project loaded(reopened);loaded.open(file);const auto open=loaded.timings().total_ms;
            std::stop_source stop;Clock::time_point cancelled;simulation::ExecutionControl control{stop.get_token(),[&](auto phase,auto,auto,auto){if(phase=="Simulating fixed-parameter reference"){cancelled=Clock::now();stop.request_stop();}}};
            try{(void)diagnostics::run(request,data,&control);}catch(const simulation::ExecutionCancelled&){}const auto cancel=ms(cancelled);
            std::cout<<"diagnostic,"<<data->timestamps.size()<<','<<result->observed.valid_bins<<",0,1,"<<reps<<','<<total<<','<<result->preparation_ms<<','<<result->simulation_ms<<','<<serialization<<','<<parse<<",0,"<<save<<','<<open<<','<<json.size()<<','<<size<<','<<size-before<<','<<peak()<<','<<cancel<<'\n';
        }
        for(const auto records:{10000U,1000000U}) {
            const auto end=epoch+366LL*86400,split=end-1024;
            std::string csv="timestamp\n";csv.reserve(records*21+10);for(std::uint32_t i=0;i<records;++i)csv+=demand::timestamp(epoch)+"\n";
            const auto csv_path=temporary.path/(std::to_string(records)+".csv");{std::ofstream file(csv_path,std::ios::binary);file<<csv;}
            auto data=demand::import_csv(csv,"reimport.csv",{epoch,end,true});auto source=demand::fit(data,{split,demand::Model::Constant,86400,86400});
            diagnostics::Request request;request.source=source;request.csv=csv_path;request.settings.bin_seconds=1;request.settings.replications=100;request.settings.prediction_start=end;
            auto stage=Clock::now();(void)demand::load_csv(csv_path,source->provenance.exposure);const auto reimport=ms(stage);stage=Clock::now();auto result=diagnostics::run(request);const auto total=ms(stage);
            stage=Clock::now();auto bytes=diagnostics::report_json(*result);const auto serialization=ms(stage);stage=Clock::now();(void)diagnostics::parse_report(bytes);const auto parse=ms(stage);
            std::cout<<"verified_reimport,"<<records<<",1024,0,1,100,"<<total<<','<<result->preparation_ms<<','<<result->simulation_ms<<','<<serialization<<','<<parse<<','<<reimport<<",0,0,"<<bytes.size()<<",0,0,"<<peak()<<",0\n";
        }
        for(const auto* name:{"time-varying","gaps-and-shift"})for(const auto reps:{100U,1000U}) {
            auto request=diagnostics::load_request(std::filesystem::path("examples/demand-diagnostics")/(std::string(name)+".json"));request.settings.replications=reps;
            const auto stage=Clock::now();auto result=diagnostics::run(request);const auto total=ms(stage);auto timed=Clock::now();const auto json=diagnostics::report_json(*result);const auto serialization=ms(timed);timed=Clock::now();(void)diagnostics::parse_report(json);const auto parse=ms(timed);
            std::cout<<name<<','<<request.source->provenance.accepted<<','<<result->observed.valid_bins<<','<<request.source->provenance.exposure.windows.size()<<','<<request.source->rates.size()<<','<<reps<<','<<total<<','<<result->preparation_ms<<','<<result->simulation_ms<<','<<serialization<<','<<parse<<",0,0,0,"<<json.size()<<",0,0,"<<peak()<<",0\n";
        }
        for(double mean:{0.,.1,10.,1000.,80000.}){const auto start=Clock::now();for(int i=0;i<100;++i)(void)diagnostics::process_interval(mean,.95);std::cout<<"quantiles_mu_"<<mean<<",0,0,0,0,0,"<<ms(start)<<",0,0,0,0,0,0,0,0,0,0,"<<peak()<<",0\n";}
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
