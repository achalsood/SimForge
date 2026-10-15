#include <simforge/analysis/demand_diagnostics.hpp>
#include <simforge/workspace/project.hpp>
#include <simforge/warehouse/warehouse_simulation.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>

using namespace simforge;
namespace demand=analysis::demand;
namespace diagnostics=analysis::diagnostics;
using Json=nlohmann::json;
namespace {
void check(bool ok,std::source_location p=std::source_location::current()){if(!ok)throw std::runtime_error("Demand diagnostics test line "+std::to_string(p.line()));}
void near(double a,double b,double tolerance=1e-9){check(std::abs(a-b)<=tolerance*std::max(1.0,std::abs(b)));}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
const auto epoch=demand::parse_timestamp("2026-03-02T00:00:00Z");
struct Fixture {
    std::shared_ptr<const demand::Dataset> data;
    std::shared_ptr<const demand::Report> source;
    diagnostics::Request request;
};
Fixture fixture(int kind=0,bool gaps=false,bool daily=false) {
    std::string csv="timestamp\n";
    for(int h=0;h<120;++h) {
        if(gaps && (h%24<8 || h%24>=18 || h%24==12))continue;
        const int n=kind==1?(h%2?20:0):kind==2?5:kind==3?(h<48?5:20):kind==4?(h%24<12?12:2):kind==5?0:5+(h%3-1);
        for(int i=0;i<n;++i)csv+=demand::timestamp(epoch+h*3600+10+i*30)+"\n";
    }
    demand::ImportSettings exposure{epoch,epoch+120*3600,true};
    if(gaps){exposure.continuous=false;for(int h=0;h<120;++h)exposure.windows.push_back({epoch+h*3600,epoch+(h+1)*3600,h%24<8 || h%24>=18?demand::CoverageStatus::Closed:h%24==12?demand::CoverageStatus::Interrupted:demand::CoverageStatus::Observed});}
    Fixture f;f.data=demand::import_csv(csv,"independent-counts.csv",exposure);
    f.source=demand::fit(f.data,{epoch+48*3600,daily?demand::Model::DailyProfile:demand::Model::Constant,3600,3600});
    f.request.source=f.source;f.request.settings.prediction_start=exposure.end;f.request.settings.replications=10;return f;
}
auto run(Fixture& f){return diagnostics::run(f.request,f.data);}
Json stable(const diagnostics::Report& r){auto j=Json::parse(diagnostics::report_json(r));j.erase("timings_ms");j["source_calibration"].erase("timings_ms");return j;}
diagnostics::Bin bin(double mean,std::uint64_t count,std::int64_t start=0){diagnostics::Bin b;b.start=start;b.end=start+3600;b.exposure_seconds=3600;b.count=count;b.available=b.fully_observed=true;b.prediction.mean=mean;return b;}
double independent_cdf(double mean,std::int64_t bound) {
    if(bound<0)return 0;if(mean==0)return 1;double total=0;
    // Independent direct PMF sum from the defining formula, without recurrence,
    // mode normalization or calls to the implementation under test.
    for(std::uint64_t n=0;n<=static_cast<std::uint64_t>(bound);++n)total+=std::exp(-mean+static_cast<double>(n)*std::log(mean)-std::lgamma(static_cast<double>(n)+1));return total;
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("simforge-diagnostics-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){std::filesystem::create_directory(path);}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
auto definition(){return scenario::ScenarioDefinition{1,"Diagnostic project source",{10,120,42},scenario::WarehouseConfig{2,4,2,10,20}};}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("test case required");const std::string name=argv[1];
        if(name=="zero_prediction"){check(diagnostics::process_interval(0,.95)==diagnostics::Interval{0,0});near(diagnostics::poisson_cdf(0,0),1);near(diagnostics::poisson_cdf(0,-1),0);}
        else if(name=="small_quantiles"){check(diagnostics::process_interval(.1,.95)==diagnostics::Interval{0,1});check(diagnostics::process_interval(1,.95)==diagnostics::Interval{0,3});near(diagnostics::poisson_cdf(1,1),2/std::exp(1.0));}
        else if(name=="moderate_quantiles"){check(diagnostics::process_interval(10,.95)==diagnostics::Interval{4,17});check(diagnostics::process_interval(100,.95)==diagnostics::Interval{81,120});}
        else if(name=="large_quantiles" || name=="numerical_cdf") {for(double mean:{.01,.1,1.,10.,100.,1000.,80000.}){auto interval=diagnostics::process_interval(mean,.95);check(independent_cdf(mean,interval.lower)>=.025-1e-9);if(interval.lower)check(independent_cdf(mean,interval.lower-1)<.025+1e-9);check(independent_cdf(mean,interval.upper)>=.975-1e-9);check(independent_cdf(mean,static_cast<std::int64_t>(interval.upper)-1)<.975+1e-9);near(diagnostics::poisson_cdf(mean,interval.upper),independent_cdf(mean,interval.upper),2e-9);}}
        else if(name=="invalid_prediction"){for(double mean:{-1.,80001.,static_cast<double>(INFINITY),static_cast<double>(NAN)})rejects([&]{(void)diagnostics::process_interval(mean,.95);});for(double level:{0.,.79,1.,static_cast<double>(NAN)})rejects([&]{(void)diagnostics::process_interval(1,level);});rejects([]{(void)diagnostics::poisson_quantile(1,0);});}
        else if(name=="pearson" || name=="different_means") {std::vector<diagnostics::Bin> bins{bin(1,0),bin(4,8,3600),bin(9,6,7200)};std::vector<std::uint64_t> counts{0,8,6};auto s=diagnostics::statistics(bins,counts,{},1);near(*s.pearson,6);near(*s.dispersion,2);near(s.expected_zeros,std::exp(-1.)+std::exp(-4.)+std::exp(-9.));}
        else if(name=="underdispersion" || name=="overdispersion") {auto f=fixture(name=="overdispersion"?1:2);f.request.settings.replications=100;auto r=run(f);check(r->observed.dispersion.has_value());if(name=="overdispersion"){check(*r->observed.dispersion>9);check(r->assessment.find("exceeds")!=std::string::npos);}else{near(*r->observed.dispersion,0);check(r->assessment.find("below")!=std::string::npos);}}
        else if(name=="zero_means") {std::vector<diagnostics::Bin> bins{bin(0,0),bin(0,3,3600),bin(.1,0,7200)};std::vector<std::uint64_t> counts{0,3,0};auto s=diagnostics::statistics(bins,counts,{},0);check(s.zero_mean_positive==1 && !s.pearson && !s.dispersion);near(s.expected_zeros,2+std::exp(-.1));}
        else if(name=="degrees_of_freedom") {auto s=diagnostics::Settings{};s.partition=diagnostics::Partition::Training;std::vector<diagnostics::Bin> bins{bin(1,1)};std::vector<std::uint64_t> counts{1};auto st=diagnostics::statistics(bins,counts,s,1);check(st.residual_degrees_of_freedom==0 && !st.dispersion);}
        else if(name=="tail_threshold") {auto s=diagnostics::Settings{};s.tail_threshold=3;std::vector<diagnostics::Bin> bins{bin(1,3),bin(1,2,3600)};std::vector<std::uint64_t> counts{3,2};auto st=diagnostics::statistics(bins,counts,s,0);check(st.tail_bins==1 && st.maximum==3);near(st.expected_tail_bins,2*(1-2.5/std::exp(1.)));}
        else if(name=="correlation" || name=="correlation_gaps" || name=="correlation_zero_variance") {std::vector<diagnostics::Bin> bins;std::vector<std::uint64_t> counts;for(int i=0;i<12;++i){bins.push_back(bin(5,5,i*3600));counts.push_back(name=="correlation_zero_variance"?5:static_cast<std::uint64_t>(i));}if(name=="correlation_gaps")for(int i=1;i<12;i+=2)bins[i].fully_observed=false;auto st=diagnostics::statistics(bins,counts,{},0);if(name=="correlation")near(*st.lag_one,1);else check(!st.lag_one);}
        else if(name=="parameter_envelope" || name=="daily_prediction") {auto f=fixture(4,false,name=="daily_prediction");auto p=diagnostics::predict(*f.source,epoch+120*3600,epoch+121*3600,.95);check(p.process && p.parameter_aware);check(p.parameter_aware->lower<=p.process->lower && p.parameter_aware->upper>=p.process->upper);if(name=="daily_prediction")near(*p.mean,12);}
        else if(name=="unavailable_prediction") {auto f=fixture(0,true,true);auto p=diagnostics::predict(*f.source,epoch+120*3600,epoch+121*3600,.95);check(!p.mean && !p.process && !p.parameter_aware);}
        else if(name=="forecast_overlap") {auto f=fixture();f.request.settings.prediction_start=f.source->settings.split;rejects([&]{(void)run(f);});rejects([&]{(void)diagnostics::predict(*f.source,epoch,epoch+3600,.95);});}
        else if(name=="coverage_metrics") {auto f=fixture(3);auto r=run(f);check(r->process_coverage.evaluated==72 && r->process_coverage.above==72 && r->process_coverage.inside==0);near(*r->process_coverage.fraction,0);}
        else if(name=="coverage_gaps" || name=="partial_exposure") {auto f=fixture(0,true,true);if(name=="partial_exposure")f.request.settings.bin_seconds=7200;auto r=run(f);std::uint64_t exposure=0,total=0;for(const auto& b:r->bins){exposure+=b.exposure_seconds;total+=b.count;if(!b.exposure_seconds)check(!b.available && !b.prediction.mean);}check(exposure==27*3600 && total==141);check(!r->observed.lag_one || r->observed.adjacent_pairs<=21);}
        else if(name=="coarsening") {auto f=fixture();f.request.settings.bin_seconds=7200;auto a=diagnostics::run(f.request),b=run(f);check(a->bins.size()==36 && a->bins[0].count==b->bins[0].count && a->data_availability.find("coarsening")!=std::string::npos);}
        else if(name=="reimport_required") {auto f=fixture();f.request.settings.bin_seconds=1800;rejects([&]{(void)diagnostics::run(f.request);});auto r=run(f);check(r->bins.size()==144);}
        else if(name=="fingerprint") {auto f=fixture();auto other=fixture(1);rejects([&]{(void)diagnostics::run(f.request,other.data);});}
        else if(name=="source_tamper") {auto f=fixture();auto source=std::make_shared<demand::Report>(*f.source);source->rates[0].per_second+=1;f.request.source=source;rejects([&]{(void)run(f);});}
        else if(name=="leakage") {auto a=fixture(),b=fixture(3);check(a.source->rates[0].count==b.source->rates[0].count);near(a.source->rates[0].per_second,b.source->rates[0].per_second);near(a.source->rates[0].lower_per_second,b.source->rates[0].lower_per_second);a.request.settings.partition=b.request.settings.partition=diagnostics::Partition::Training;auto ar=run(a),br=run(b);check(ar->bins.size()==br->bins.size());for(std::size_t i=0;i<ar->bins.size();++i)near(*ar->bins[i].prediction.mean,*br->bins[i].prediction.mean);check(ar->replications[0].statistics.dispersion==br->replications[0].statistics.dispersion);}
        else if(name=="reproducible" || name=="different_seeds") {auto f=fixture();auto a=run(f);if(name=="different_seeds")++f.request.settings.seed;auto b=run(f);if(name=="reproducible")check(stable(*a)==stable(*b));else check(a->replications[0].seed!=b->replications[0].seed && a->id!=b->id);}
        else if(name=="reference_moments") {auto f=fixture();f.request.settings.replications=1000;auto r=run(f);double mean=0;for(const auto& rep:r->replications)mean+=*rep.statistics.dispersion;mean/=1000;check(std::abs(mean-1)<7*std::sqrt((5+2*25.)/(72*1000*25.)));check(r->replications.size()==1000);}
        else if(name=="independent_reference") {auto f=fixture();f.request.settings.replications=3;auto r=run(f);const std::array<std::uint64_t,3> seeds{5589919048962539584ULL,11893985631523087576ULL,3618112348595607274ULL};const std::array<double,3> pearson{66.8,67.8,87.6};const std::array<std::uint64_t,3> zeros{1,1,0},tails{0,0,4};for(std::size_t i=0;i<3;++i){check(r->replications[i].seed==seeds[i]);near(*r->replications[i].statistics.pearson,pearson[i]);near(*r->replications[i].statistics.dispersion,pearson[i]/72);check(r->replications[i].statistics.zeros==zeros[i] && r->replications[i].statistics.tail_bins==tails[i]);}}
        else if(name=="zero_reference") {auto f=fixture(5);auto r=run(f);check(r->observed.zeros==72 && r->observed.expected_zeros==72 && !r->observed.dispersion);for(const auto& rep:r->replications)check(rep.statistics.zeros==72 && rep.statistics.maximum==0);check(r->future.process==diagnostics::Interval{0,0} && r->future.parameter_aware && r->future.parameter_aware->upper>0);}
        else if(name=="limits") {auto f=fixture();for(auto count:{0U,1001U}){f.request.settings.replications=count;rejects([&]{(void)run(f);});}f.request.settings.replications=10;f.request.settings.bin_seconds=1;rejects([&]{(void)run(f);});f.request.settings.bin_seconds=3600;f.request.settings.prediction_start=INT64_MAX;rejects([&]{(void)run(f);});f.request.settings.prediction_start=f.source->provenance.exposure.end;f.request.settings.tail_threshold=0;rejects([&]{(void)run(f);});}
        else if(name=="cancel_core") {auto f=fixture();std::stop_source stop;simulation::ExecutionControl control{stop.get_token(),[&](auto phase,auto,auto n,auto){if(phase=="Simulating fixed-parameter reference" && n==2)stop.request_stop();}};bool cancelled=false;try{(void)diagnostics::run(f.request,f.data,&control);}catch(const simulation::ExecutionCancelled&){cancelled=true;}check(cancelled);}
        else if(name=="roundtrip" || name=="tamper" || name=="exports") {auto f=fixture();auto r=run(f);auto bytes=diagnostics::report_json(*r);check(diagnostics::report_json(*diagnostics::parse_report(bytes))==bytes);if(name=="tamper"){auto j=Json::parse(bytes);j["bins"][0]["observed_count"]=999;rejects([&]{(void)diagnostics::parse_report(j.dump());});}if(name=="exports"){rejects([]{(void)diagnostics::report_json(diagnostics::Report{});});auto csv=diagnostics::report_csv(*r);check(csv.find("reference_statistics")!=std::string::npos && csv.find("parameter_aware_coverage")!=std::string::npos);}}
        else if(name=="project" || name=="lineage" || name=="metadata") {auto f=fixture();auto r=run(f);workspace::ScenarioWorkspace w;workspace::Project p(w);p.create(definition(),"Demand diagnostics project");w.retain_calibration(f.source);w.retain_diagnostic(r);if(name=="lineage")w.delete_calibration(f.source->id,true);if(name=="metadata")w.rename_diagnostic(r->id,"Reviewed diagnostics","Frozen source retained");Temporary temp;p.save(temp.path/"project.simforge");workspace::ScenarioWorkspace restored;workspace::Project q(restored);q.open(temp.path/"project.simforge");check(restored.diagnostics().size()==1 && restored.diagnostic_report()->request.source->id==f.source->id);check(!q.dirty());if(name=="metadata")check(restored.diagnostic_report()->title=="Reviewed diagnostics");}
        else if(name=="async_cancel") {auto f=fixture();workspace::ScenarioWorkspace w;workspace::Project p(w);p.create(definition());w.retain_calibration(f.source);auto previous=run(f);w.retain_diagnostic(previous);Temporary saved;p.save(saved.path/"saved.simforge");const auto read_saved=[&]{std::ifstream file(saved.path/"saved.simforge",std::ios::binary);return std::string(std::istreambuf_iterator<char>{file},{});};const auto original_bytes=read_saved();const auto snapshot=w.baseline_viewer().runtime().capture_snapshot();std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;w.start_diagnostics(f.request,[&](auto phase){if(phase!="Simulating fixed-parameter reference")return;std::unique_lock lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return released;});});{std::unique_lock lock(mutex);check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}));}w.calibration_execution().cancel();{std::lock_guard lock(mutex);released=true;}condition.notify_all();w.calibration_execution().shutdown();check(!w.poll_calibration());check(w.diagnostic_report()==previous || w.diagnostic_report()->id==previous->id);check(w.baseline_viewer().runtime().capture_snapshot()==snapshot && w.calibration_execution().progress().cancelled);check(read_saved()==original_bytes);entered=released=false;++f.request.settings.seed;w.start_diagnostics(f.request,[&](auto phase){if(phase!="Simulating fixed-parameter reference")return;std::unique_lock lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return released;});});{std::unique_lock lock(mutex);check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}));}p.start_save();while(p.busy()){if(auto completion=p.poll())check(completion->success);std::this_thread::sleep_for(std::chrono::milliseconds(1));}p.start_open(saved.path/"saved.simforge");{std::lock_guard lock(mutex);released=true;}condition.notify_all();while(p.busy()){if(auto completion=p.poll())check(completion->success);std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(w.diagnostic_report()->id==previous->id && !w.has_pending_diagnostic() && !w.calibration_dataset());}
        else throw std::invalid_argument("Unknown diagnostic test");
        std::cout<<"Passed "<<name<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
