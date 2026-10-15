#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <simforge/analysis/sensitivity.hpp>
#include <simforge/analysis/interaction.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif
using namespace simforge;
namespace demand=analysis::demand;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::size_t peak(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS p{};if(GetProcessMemoryInfo(GetCurrentProcess(),&p,sizeof(p)))return p.PeakWorkingSetSize;
#endif
    return 0;
}
auto definition(double rate,std::uint32_t duration,std::uint32_t tick_rate,std::uint64_t seed){scenario::WarehouseConfig c{3,10,2,0,0};c.workload.arrivals=scenario::ArrivalMode::Poisson;c.workload.rate_per_second=rate;c.workload.processing=scenario::ProcessingMode::Uniform;c.workload.processing_min_seconds=600;c.workload.processing_max_seconds=1200;return scenario::ScenarioDefinition{1,"Benchmark",{tick_rate,duration,seed},c};}
int main(){try {
    const auto directory=std::filesystem::temp_directory_path()/("simforge-stochastic-benchmark-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{directory};
    std::cout<<std::setprecision(9)<<"kind,model,seed,rate_per_second,expected_count,realized_count,generator_ms,generated_arrivals_per_second,simulation_ms,processed_events_per_second,records,coverage_windows,import_ms,fit_ms,holdout_ms,save_ms,open_ms,project_bytes,peak_process_working_set_bytes,cancel_ms\n";
    for(auto spec:{std::pair{.0001,28800U},std::pair{.01,28800U},std::pair{1.0,28800U},std::pair{1000.0,80U}})for(std::uint64_t seed:{1,42,54321,100001}) {
        auto d=definition(spec.first,spec.second,spec.first==1000?1:60,seed);auto t=Clock::now();auto ticks=scenario::workload::poisson_ticks(d);const auto generator=ms(t);
        t=Clock::now();warehouse::WarehouseSimulation sim(d);sim.run();const auto execution=ms(t);
        std::stop_source stop;simulation::ExecutionControl control{stop.get_token(),[&](auto,auto,auto count,auto){if(count>=256)stop.request_stop();}};t=Clock::now();try{(void)scenario::workload::poisson_ticks(definition(1000,80,1,seed),&control);}catch(const simulation::ExecutionCancelled&){}const auto cancellation=ms(t);
        std::cout<<"generator,poisson,"<<seed<<','<<spec.first<<','<<spec.first*spec.second<<','<<ticks.size()<<','<<generator<<','<<ticks.size()*1000/generator<<','<<execution<<','<<sim.processed_events()*1000/execution<<",,,,,,,,,"<<peak()<<','<<cancellation<<'\n';
    }
    auto profile=definition(0,28800,60,42);auto& w=std::get<scenario::WarehouseConfig>(profile.domain).workload;w.arrivals=scenario::ArrivalMode::PiecewisePoisson;w.intensities={{0,7200,.01},{7200,14400,0},{18000,28800,.03}};
    auto t=Clock::now();auto ticks=scenario::workload::poisson_ticks(profile);auto generation=ms(t);t=Clock::now();warehouse::WarehouseSimulation sim(profile);sim.run();auto execution=ms(t);
    std::cout<<"generator,piecewise_poisson,42,,396,"<<ticks.size()<<','<<generation<<','<<ticks.size()*1000/generation<<','<<execution<<','<<sim.processed_events()*1000/execution<<",,,,,,,,,"<<peak()<<",\n";
    analysis::SensitivityRequest sweep;sweep.source=definition(.01,28800,60,42);sweep.values=analysis::sweep_range(1,20,1);sweep.multi_seed=true;sweep.plan.count=4;sweep.plan.base_seed=1001;t=Clock::now();auto sr=analysis::run_sensitivity(sweep);const auto sensitivity=ms(t);
    workspace::ScenarioWorkspace sw(sweep.source);sw.start_sensitivity(sweep);while(sw.execution().progress().active())std::this_thread::yield();sw.poll_experiment();workspace::Project sp(sw);auto path=directory/"sensitivity.simforge";t=Clock::now();sp.save(path);const auto ss=ms(t);t=Clock::now();sp.open(path);const auto so=ms(t);
    std::cout<<"study,sensitivity_20_points_4_seeds,1001,.01,288,,, ,"<<sensitivity<<','<<sr.processed_events*1000/sensitivity<<",,,,,,"<<ss<<','<<so<<','<<std::filesystem::file_size(path)<<','<<peak()<<",\n";
    analysis::InteractionRequest grid;grid.source=sweep.source;grid.x_values=analysis::sweep_range(1,8,1);grid.y_values=grid.x_values;grid.multi_seed=true;grid.plan.count=2;grid.plan.base_seed=2001;t=Clock::now();auto gr=analysis::run_interaction(grid);const auto interaction=ms(t);
    workspace::ScenarioWorkspace gw(grid.source);gw.start_interaction(grid);while(gw.execution().progress().active())std::this_thread::yield();gw.poll_experiment();workspace::Project gp(gw);auto grid_path=directory/"grid.simforge";t=Clock::now();gp.save(grid_path);const auto gs=ms(t);t=Clock::now();gp.open(grid_path);const auto go=ms(t);
    std::cout<<"study,interaction_64_cells_2_seeds,2001,.01,288,,,,"<<interaction<<','<<gr.processed_events*1000/interaction<<",,,,,,"<<gs<<','<<go<<','<<std::filesystem::file_size(grid_path)<<','<<peak()<<",\n";
    const auto start=demand::parse_timestamp("2026-02-02T00:00:00Z");demand::ImportSettings exposure{start,start+4*86400,false};
    for(std::int64_t i=0;i<256;++i)exposure.windows.push_back({start+i*1350,start+(i+1)*1350,static_cast<demand::CoverageStatus>(i%4)});
    for(std::size_t records:{10000U,1000000U}) {
        auto csv=directory/(std::to_string(records)+".csv");{std::ofstream file(csv,std::ios::binary);file<<"timestamp\n";for(std::size_t i=0;i<records;++i){const auto index=i*64/records,offset=(i*64%records)*1350/records;file<<demand::timestamp(start+static_cast<std::int64_t>(index*4*1350+offset))<<'\n';}}
        for(auto model:{demand::Model::Constant,demand::Model::DailyProfile}) {
            auto data=demand::load_csv(csv,exposure);auto r=demand::fit(data,{start+2*86400,model,3600,3600});auto conversion=demand::convert_stochastic(*r,definition(.01,600,10,42),"Calibrated",42,start,true);
            workspace::ScenarioWorkspace workspace(conversion.scenario);workspace.retain_calibration(r);workspace.record_calibration_conversion(conversion);workspace::Project project(workspace);auto file=directory/(std::to_string(records)+"-"+std::to_string(static_cast<int>(model))+".simforge");t=Clock::now();project.save(file);const auto save=ms(t);t=Clock::now();project.open(file);const auto open=ms(t);
            std::cout<<"calibration,"<<(model==demand::Model::Constant?"constant":"daily_profile")<<",42,,"<<conversion.expected_count<<','<<conversion.realized_count<<",,,,,"<<records<<",256,"<<data->import_ms<<','<<r->timings.fit_ms<<','<<r->timings.validation_ms<<','<<save<<','<<open<<','<<std::filesystem::file_size(file)<<','<<peak()<<",\n";
        }
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
