#include <simforge/analysis/demand_diagnostics.hpp>
#include <simforge/scenario/scenario.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using namespace simforge;
namespace demand=analysis::demand;
namespace diagnostics=analysis::diagnostics;
using Json=nlohmann::json;
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("Pass an existing output directory; outputs must be new files");
        const std::filesystem::path output=argv[1];if(!std::filesystem::is_directory(output))throw std::invalid_argument("Output directory must exist");
        const auto epoch=demand::parse_timestamp("2026-03-02T00:00:00Z");
        for(int kind=0;kind<5;++kind) {
            const char* names[]{"poisson-compatible","overdispersed","underdispersed","time-varying","gaps-and-shift"};const std::string name=names[kind];
            demand::ImportSettings exposure{epoch,epoch+14*86400,true};
            if(kind==4){exposure.continuous=false;for(int day=0;day<14;++day){const auto start=epoch+day*86400;exposure.windows.insert(exposure.windows.end(),{{start,start+8*3600,demand::CoverageStatus::Closed},{start+8*3600,start+12*3600,demand::CoverageStatus::Observed},{start+12*3600,start+13*3600,demand::CoverageStatus::Interrupted},{start+13*3600,start+18*3600,demand::CoverageStatus::Observed},{start+18*3600,start+86400,demand::CoverageStatus::Unobserved}});}}
            std::vector<std::int64_t> times;
            if(kind==1 || kind==2)for(int h=0;h<14*24;++h){const auto n=kind==1?(h%2?20:0):5;for(int i=0;i<n;++i)times.push_back(epoch+h*3600+(kind==1?30+i*150:i*720));}
            else for(int week=0;week<2;++week) {
                scenario::WarehouseConfig config;config.shipment_interval_seconds=0;config.workload.arrivals=kind==0?scenario::ArrivalMode::Poisson:scenario::ArrivalMode::PiecewisePoisson;
                if(kind==0)config.workload.rate_per_second=5./3600;
                for(std::uint32_t day=0;day<7 && kind!=0;++day){const auto start=day*86400;
                    if(kind==3)config.workload.intensities.insert(config.workload.intensities.end(),{{start,start+8*3600,1./3600},{start+8*3600,start+12*3600,20./3600},{start+12*3600,start+86400,3./3600}});
                    else {const auto rate=(week?15.:5.)/3600;config.workload.intensities.insert(config.workload.intensities.end(),{{start+8*3600,start+12*3600,rate},{start+13*3600,start+18*3600,rate}});}
                }
                scenario::ScenarioDefinition source{1,"Known historical generating model",{1,7*86400,310032+static_cast<std::uint64_t>(kind)*100+static_cast<std::uint64_t>(week)},config};
                for(auto tick:scenario::workload::poisson_ticks(source))times.push_back(epoch+week*7*86400+static_cast<std::int64_t>(tick));
            }
            std::string csv="timestamp\n";for(auto t:times)csv+=demand::timestamp(t)+"\n";
            auto data=demand::import_csv(csv,name+".csv",exposure);const demand::FitSettings fit{epoch+7*86400,kind==3?demand::Model::DailyProfile:demand::Model::Constant,14400,3600};auto calibration=demand::fit(data,fit);
            diagnostics::Request request;request.source=calibration;request.settings.prediction_start=exposure.end+8*3600;request.settings.replications=1000;request.settings.seed=320032;request.settings.tail_threshold=15;request.settings.exploratory=false;
            auto result=diagnostics::run(request,data);
            Json settings=Json::parse(diagnostics::report_json(*result)).at("settings");Json file{{"diagnostic_request_version",1},{"source_calibration",name+"-calibration.json"},{"csv",name+".csv"},{"settings",settings}};
            diagnostics::save_export(output/(name+".csv"),csv);demand::save_export(output/(name+"-calibration.json"),demand::report_json(*calibration));diagnostics::save_export(output/(name+".json"),file.dump(2));diagnostics::save_export(output/(name+"-diagnostics.json"),diagnostics::report_json(*result));
            std::cout<<name<<",records="<<times.size()<<",valid_bins="<<result->observed.valid_bins<<",dispersion="<<(result->observed.dispersion?*result->observed.dispersion:-1)<<",coverage="<<result->process_coverage.fraction.value_or(-1)<<",assessment="<<result->assessment<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
