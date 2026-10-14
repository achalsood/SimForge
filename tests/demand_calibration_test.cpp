#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <source_location>

using namespace simforge;
namespace demand=analysis::demand;
using Json=nlohmann::json;
namespace {
void check(bool ok,std::source_location p=std::source_location::current()){if(!ok)throw std::runtime_error("Demand test line "+std::to_string(p.line()));}
void near(double actual,double expected,double tolerance=1e-10){check(std::abs(actual-expected)<=tolerance*std::max(1.0,std::abs(expected)));}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
const auto epoch=demand::parse_timestamp("2026-01-05T00:00:00Z");
demand::ImportSettings exposure(std::int64_t length=3*86400){return {epoch,epoch+length,true};}
demand::FitSettings settings(demand::Model model=demand::Model::Constant){return {epoch+2*86400,model,3600,3600};}
auto imported(std::vector<std::int64_t> times,const demand::ImportSettings& window=exposure()) {
    std::string csv="timestamp\n";for(auto time:times)csv+=demand::timestamp(time)+'\n';return demand::import_csv(csv,"source.csv",window);
}
auto source(){return scenario::ScenarioDefinition{1,"Source",{10,600,99},scenario::WarehouseConfig{2,4,3,10,20}};}
struct Temporary {
    std::filesystem::path path;
    explicit Temporary(const std::string& name){path=std::filesystem::temp_directory_path()/("simforge-demand-"+name+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}
    ~Temporary(){std::error_code e;std::filesystem::remove_all(path,e);}
};
void wait(workspace::CalibrationExecution& worker){const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds{10};while(worker.progress().active){check(std::chrono::steady_clock::now()<limit);std::this_thread::sleep_for(std::chrono::milliseconds{1});}}
void run(const std::string& name) {
    if(name=="hash") {
        check(demand::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        check(demand::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check(demand::sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    } else if(name=="timestamps") {
        check(demand::parse_timestamp("1970-01-01T00:00:00Z")==0);check(demand::parse_timestamp("2000-02-29T12:34:56Z")==951827696);
        for(auto s:{"1970-01-01T00:00:00Z","2000-02-29T12:34:56Z","2099-12-31T23:59:59Z","2100-01-01T00:00:00Z"})check(demand::timestamp(demand::parse_timestamp(s))==s);
    } else if(name=="bad_dates") {
        for(auto s:{"2026-02-29T00:00:00Z","2100-02-29T00:00:00Z","1969-12-31T23:59:59Z","2100-01-02T00:00:00Z","2026-13-01T00:00:00Z","2026-01-01T24:00:00Z","2026-01-01T00:60:00Z","2026-01-01T00:00:60Z"})rejects([&]{(void)demand::parse_timestamp(s);});
    } else if(name=="timezone_precision") {
        for(auto s:{"2026-01-05T00:00:00+00:00","2026-01-05 00:00:00","2026-11-01T01:30:00","01/05/2026","2026-01-05T00:00:00.5Z","2026-01-05T00:00:00z"})rejects([&]{(void)demand::parse_timestamp(s);});
    } else if(name=="csv") {
        auto data=demand::import_csv("\xef\xbb\xbftimestamp\r\n\"2026-01-05T00:00:00Z\"\r\n2026-01-05T00:00:00Z", "C:\\private\\source.csv",exposure());
        check(data->provenance.accepted==2 && data->provenance.duplicates==1 && data->provenance.source_filename=="source.csv" && !data->provenance.reordered);
        check(data->overview_counts[0]==2);
    } else if(name=="sorting") {
        auto data=imported({epoch+3,epoch+1,epoch+3,epoch});check(data->provenance.reordered && data->provenance.duplicates==1);
        check(data->timestamps==std::vector<std::int64_t>{epoch,epoch+1,epoch+3,epoch+3});
    } else if(name=="malformed_csv") {
        for(auto s:{"", "time\n2026-01-05T00:00:00Z\n", "timestamp,other\n", "timestamp\n\n", "timestamp\n\"2026-01-05T00:00:00Z\n", "timestamp\n2026-01-05T00:00:00Z,2\n", "timestamp\n2026-01-05T00:00:0", "timestamp\n\"2026-01-05T00:00:00Z\"x\n", "timestamp\n\"2026-01-05T00:\"\"00:00Z\"\n"})rejects([&]{(void)demand::import_csv(s,"source.csv",exposure());});
        try{(void)demand::import_csv("timestamp\n2026-01-05T00:00:00Z\ninvalid\n","source.csv",exposure());check(false);}catch(const std::invalid_argument& e){check(std::string(e.what()).find("row 3")!=std::string::npos);}
    } else if(name=="encoding") {
        for(auto s:{std::string("\xff\xfet\0",4),std::string("timestamp\n\0",11),std::string("timestamp\n\xc3\xa9\n")})rejects([&]{(void)demand::import_csv(s,"source.csv",exposure());});
    } else if(name=="limits") {
        rejects([&]{(void)demand::import_csv("timestamp\n"+std::string(129,'x'),"source.csv",exposure());});
        rejects([&]{(void)demand::import_csv(std::string(demand::max_csv_bytes+1,'x'),"source.csv",exposure());});
        std::string csv="timestamp\n";for(std::size_t i=0;i<=demand::max_records;++i)csv+="2026-01-05T00:00:00Z\n";
        rejects([&]{(void)demand::import_csv(csv,"source.csv",exposure());});
    } else if(name=="coverage") {
        for(auto w:{demand::ImportSettings{epoch,epoch+1,false},{epoch,epoch,true},{epoch+2,epoch+1,true},{-1,epoch,true},{epoch,epoch+demand::max_exposure_seconds+1,true},{epoch,INT64_MAX,true}})rejects([&]{demand::validate_exposure(w);});
    } else if(name=="boundary") {
        auto data=imported({epoch,epoch+2*86400-1,epoch+2*86400,epoch+3*86400-1});auto r=demand::fit(data,settings());check(r->training.observed==2 && r->holdout.observed==2);
        rejects([&]{(void)imported({epoch-1});});rejects([&]{(void)imported({epoch+3*86400});});
        check(r->bins.front().start==epoch && r->bins.back().end==epoch+3*86400);
    } else if(name=="rate") {
        auto r=demand::estimate_rate(12,3600);near(r.per_second,1.0/300);near(r.per_second*3600,12);check(r.lower_per_second<r.per_second && r.upper_per_second>r.per_second);
        near(demand::estimate_rate(12,60).per_second,0.2);near(demand::estimate_rate(1,1).per_second,1);near(demand::estimate_rate(1,366*86400).per_second,1.0/(366*86400));
        for(auto args:{std::pair<std::uint64_t,std::uint64_t>{1,0},{1000001,1},{1,UINT64_MAX}})rejects([&]{(void)demand::estimate_rate(args.first,args.second);});
    } else if(name=="uncertainty") {
        auto zero=demand::estimate_rate(0,10);near(zero.lower_per_second,0);near(zero.upper_per_second,0.36888794541139363);
        // Independent Newton root fixtures, using n=1, m-1-log(m)=log(40).
        auto one=demand::estimate_rate(1,1);near(one.lower_per_second,0.0092827568938924371);near(one.upper_per_second,6.571643390938898);
        for(std::uint64_t n:{1ULL,5ULL,100ULL,1000000ULL}){auto r=demand::estimate_rate(n,3600);check(std::isfinite(r.upper_per_second) && r.lower_per_second>=0);near(demand::estimate_rate(n,7200).upper_per_second,r.upper_per_second/2);}
    } else if(name=="interval_coverage") {
        // Deterministic Poisson probability sums independently check coverage at low/moderate means.
        for(double mean:{0.1,1.0,3.0,10.0,50.0,100.0}) {
            double probability=std::exp(-mean),covered=0,total=0;
            for(std::uint64_t n=0;n<500;++n){auto interval=demand::estimate_rate(n,1);total+=probability;if(interval.lower_per_second<=mean && mean<=interval.upper_per_second)covered+=probability;probability*=mean/static_cast<double>(n+1);}
            near(total,1);check(covered>=0.95);
        }
    } else if(name=="zero") {
        auto r=demand::fit(demand::import_csv("timestamp\n","empty.csv",exposure()),settings());check(r->training.observed==0 && r->holdout.observed==0 && r->rates[0].per_second==0);near(*r->holdout.deviance,0);
        auto c=demand::convert(*r,source(),"Zero demand",7,epoch,true);check(scenario::workload::arrival_count(c.scenario)==0);
    } else if(name=="metrics") {
        auto r=demand::fit(imported({epoch,epoch+1,epoch+2,epoch+3,epoch+4,epoch+5,epoch+172800,epoch+172801}),settings());
        near(r->holdout.expected,3);near(r->holdout.signed_error,-1);near(r->holdout.absolute_error,1);near(r->holdout.error_per_hour,-1.0/24);
        check(r->training.observed==6 && r->holdout.observed==2);near(r->bins[48].expected,0.125);near(r->bins[48].residual,1.875);
        // 24 hourly holdout bins: first n=2,m=.125; others n=0,m=.125.
        near(*r->holdout.deviance,4*std::log(16.0)+2);near(*r->holdout.log_likelihood,2*std::log(0.125)-3-std::log(2.0));
    } else if(name=="zero_prediction") {
        auto r=demand::fit(imported({epoch+172800}),settings());check(!r->holdout.deviance && !r->holdout.log_likelihood);check(r->training.deviance==0);
        check(Json::parse(demand::report_json(*r))["holdout"]["poisson_deviance"].is_null());check(demand::report_csv(*r).find("zero_prediction_positive_count")!=std::string::npos);
    } else if(name=="split_invalid") {
        for(auto boundary:{epoch,epoch-1,epoch+3*86400}){auto s=settings();s.split=boundary;rejects([&]{(void)demand::fit(imported({epoch}),s);});}
    } else if(name=="profile") {
        std::vector<std::int64_t> times;for(int day=0;day<3;++day)for(int h=0;h<24;++h)for(int i=0;i<(h<12?6:2);++i)times.push_back(epoch+day*86400+h*3600+i*100);
        auto r=demand::fit(imported(times),settings(demand::Model::DailyProfile));check(r->rates.size()==24);near(r->rates[0].per_second*3600,6);near(r->rates[23].per_second*3600,2);near(r->holdout.expected,96);near(*r->holdout.deviance,0);check(*r->baseline_holdout.deviance>0);
    } else if(name=="partial_days") {
        auto window=demand::ImportSettings{epoch+1800,epoch+2*86400+7200,true};auto s=demand::FitSettings{epoch+86400+3600,demand::Model::DailyProfile,3600,10000};
        auto r=demand::fit(imported({epoch+1800,epoch+86400+1800,epoch+86400+3599,epoch+86400+3600},window),s);
        check(r->rates[0].exposure_seconds==5400 && r->rates[0].count==3);near(r->rates[0].per_second,1.0/1800);check(r->training.observed==3 && r->holdout.observed==1);
        check(r->rates[1].exposure_seconds==3600);for(const auto& b:r->bins)check(b.start/3600==(b.end-1)/3600);
    } else if(name=="sparse_profile") {
        auto r=demand::fit(imported({epoch+3600,epoch+2*86400+3600}),settings(demand::Model::DailyProfile));check(r->rates[0].count==0 && r->rates[1].count==1);check(r->warnings.size()>=24);check(r->rates[0].upper_per_second>0);
    } else if(name=="bin_invalid") {
        for(auto width:{0U,1U,1801U,86401U}){auto s=settings();s.daily_bin_seconds=width;rejects([&]{(void)demand::fit(imported({}),s);});}
        auto s=settings(demand::Model::DailyProfile);s.split=epoch+86399;rejects([&]{(void)demand::fit(imported({}),s);});s=settings();s.diagnostic_bin_seconds=1;rejects([&]{(void)demand::fit(imported({}),s);});
    } else if(name=="leakage_constant" || name=="leakage_profile") {
        const auto s=settings(name=="leakage_constant"?demand::Model::Constant:demand::Model::DailyProfile);
        auto a=demand::fit(imported({epoch,epoch+3600,epoch+86400,epoch+172800}),s);
        auto b=demand::fit(imported({epoch,epoch+3600,epoch+86400,epoch+172800,epoch+172801,epoch+172801,epoch+180000}),s);
        near(a->baseline.per_second,b->baseline.per_second);check(a->rates.size()==b->rates.size());
        for(std::size_t i=0;i<a->rates.size();++i){check(a->rates[i].count==b->rates[i].count && a->rates[i].exposure_seconds==b->rates[i].exposure_seconds);near(a->rates[i].per_second,b->rates[i].per_second);near(a->rates[i].upper_per_second,b->rates[i].upper_per_second);}
        check(a->bins.size()==b->bins.size());for(std::size_t i=0;i<a->bins.size();++i)check(a->bins[i].start==b->bins[i].start && a->bins[i].end==b->bins[i].end);
        check(a->training.observed==3 && b->training.observed==3 && a->holdout.observed==1 && b->holdout.observed==4);near(a->holdout.expected,b->holdout.expected);
        auto ca=demand::convert(*a,source(),"A",10,epoch,true),cb=demand::convert(*b,source(),"B",10,epoch,true);check(std::get<scenario::WarehouseConfig>(ca.scenario.domain).workload==std::get<scenario::WarehouseConfig>(cb.scenario.domain).workload);
        check(demand::report_json(*a).find("not independent final-test")!=std::string::npos);
    } else if(name=="partition") {
        std::vector<std::int64_t> times{epoch,epoch+3599,epoch+3600,epoch+86400,epoch+172799,epoch+172800,epoch+259199};
        auto r=demand::fit(imported(times),settings(demand::Model::DailyProfile));std::uint64_t total=0,train_seconds=0,holdout_seconds=0;
        for(const auto& b:r->bins){total+=b.observed;(b.holdout?holdout_seconds:train_seconds)+=b.end-b.start;for(auto t:times)if(t>=b.start && t<b.end)check(b.holdout==(t>=r->settings.split));}
        check(total==times.size() && train_seconds==172800 && holdout_seconds==86400 && r->training.observed==5 && r->holdout.observed==2);
    } else if(name=="conversion" || name=="processing" || name=="replay") {
        auto src=source();auto& config=std::get<scenario::WarehouseConfig>(src.domain);config.processing_duration_seconds=0;config.workload.processing=scenario::ProcessingMode::Uniform;config.workload.processing_min_seconds=2;config.workload.processing_max_seconds=8;
        std::vector<std::int64_t> times;for(int i=0;i<20;++i)times.push_back(epoch+i*30);
        auto s=settings();s.split=epoch+600;auto data=imported(times,{epoch,epoch+1200,true});auto r=demand::fit(data,s);
        rejects([&]{(void)demand::convert(*r,src,"Converted",12,epoch,false);});auto c=demand::convert(*r,src,"Converted",12,epoch,true);
        check(c.seed==12 && c.scenario.simulation.random_seed==12 && src.simulation.random_seed==99);check(c.semantics.find("Not a Poisson")!=std::string::npos);
        const auto& out=std::get<scenario::WarehouseConfig>(c.scenario.domain);check(out.workload.processing==config.workload.processing && out.processing_duration_seconds==0 && out.workload.processing_min_seconds==2 && out.loading_bays==config.loading_bays);near(c.represented_per_second[0],1.0/30);
        warehouse::WarehouseSimulation run(c.scenario);run.advance_to(1000);auto snapshot=run.capture_snapshot();auto restored=warehouse::WarehouseSimulation::create_from(snapshot);run.run();restored.run();
        check(warehouse::serialize_snapshot(run.capture_snapshot())==warehouse::serialize_snapshot(restored.capture_snapshot()));
        check(run.metrics().arrived==20);for(const auto& shipment:run.shipments()){check(shipment.arrival_tick==shipment.id*300);check(shipment.required_processing_ticks==scenario::workload::processing_ticks(out,10,12,shipment.id));}
    } else if(name=="rounding") {
        auto s=settings();s.split=epoch+10;auto r=demand::fit(imported({epoch,epoch+1,epoch+2},{epoch,epoch+20,true}),s);
        auto c=demand::convert(*r,source(),"Rounded",2,epoch,true);near(c.fitted_per_second[0],0.3);near(c.represented_per_second[0],1.0/3);check(std::get<scenario::WarehouseConfig>(c.scenario.domain).workload.segments[0].interval_seconds==3);
        auto fast=demand::fit(imported({epoch,epoch,epoch},{epoch,epoch+2,true}),{epoch+1,demand::Model::Constant,3600,3600});rejects([&]{(void)demand::convert(*fast,source(),"Too fast",1,epoch,true);});
    } else if(name=="profile_conversion") {
        auto r=demand::fit(imported({epoch+3600,epoch+86400+3600}),settings(demand::Model::DailyProfile));auto src=source();src.simulation.duration_seconds=7200;
        auto c=demand::convert(*r,src,"Profile",3,epoch+1800,true);const auto& segments=std::get<scenario::WarehouseConfig>(c.scenario.domain).workload.segments;
        check(segments.size()==1 && segments[0].start_seconds==1800 && segments[0].end_seconds==5400 && segments[0].interval_seconds==3600);check(scenario::workload::arrival_count(c.scenario)==1);
    } else if(name=="exports" || name=="tamper") {
        auto r=demand::fit(imported({epoch,epoch+172800}),settings());auto bytes=demand::report_json(*r);check(demand::report_json(*demand::parse_report(bytes))==bytes);
        for(auto key:{"rates","holdout","algorithm_version","dataset","split","bins"}){
            auto j=Json::parse(bytes);if(std::string(key)=="rates")j[key][0]["rate_per_second"]=2;else if(std::string(key)=="holdout")j[key]["expected"]=4;else if(std::string(key)=="algorithm_version")j[key]=2;else if(std::string(key)=="dataset")j[key]["id"]="wrong";else if(std::string(key)=="split")j[key]["utc_boundary"]="2026-01-05T00:00:00Z";else j[key][0]["end"]="2026-01-05T02:00:00Z";rejects([&]{(void)demand::parse_report(j.dump());});
        }
        auto duplicate=bytes;duplicate.insert(1,"\"algorithm_version\":1,");rejects([&]{(void)demand::parse_report(duplicate);});
        Temporary tmp(name);demand::save_export(tmp.path/"report.json",bytes);rejects([&]{demand::save_export(tmp.path/"report.json",bytes);});check(demand::report_csv(*r).starts_with("record_type,calibration_id,dataset_id,content_sha256"));
    } else if(name=="persistence" || name=="conversion_persistence" || name=="dirty" || name=="streaming") {
        Temporary tmp(name);workspace::ScenarioWorkspace w(source());workspace::Project project(w);auto r=demand::fit(imported({epoch,epoch+172800}),settings());
        w.retain_calibration(r);auto c=demand::convert(*r,source(),"Calibration playback",7,epoch,true);w.record_calibration_conversion(c);w.create_run(c.scenario);
        project.save(tmp.path/"project.simforge");check(!project.dirty());auto saved=demand::report_json(*w.calibration_report());
        workspace::ScenarioWorkspace opened;workspace::Project restored(opened);restored.open(tmp.path/"project.simforge");check(!restored.dirty());
        check(opened.calibrations().size()==1 && opened.runs().size()==1 && opened.calibration_report()->conversions.size()==1);check(demand::report_json(*opened.calibration_report())==saved);
        check(opened.calibration_dataset()==nullptr);opened.delete_calibration(r->id,true);check(restored.dirty() && opened.calibrations().empty());
        auto capture=Json::parse(project.serialize());check(capture["version"]==2 && capture["features"].back()=="demand_calibration_v1");
        check(capture["payload"]["demand_calibrations"][0]["dataset"]["accepted_count"]==2);check(!capture["payload"]["demand_calibrations"][0].contains("timestamps"));
    } else if(name=="async" || name=="failure" || name=="import_snapshot") {
        const auto path=std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration/constant.json";
        auto request=demand::load_request(path);workspace::ScenarioWorkspace w(source());w.import_calibration(request);wait(w.calibration_execution());w.poll_calibration();check(w.calibration_dataset()->provenance.accepted==288);
        w.fit_calibration(request.fit);wait(w.calibration_execution());check(w.poll_calibration());auto previous=w.calibration_report();check(previous->training.observed==192);
        w.retain_calibration(previous);request.csv=path.parent_path()/"malformed.csv";w.start_calibration(request);wait(w.calibration_execution());check(!w.poll_calibration() && !w.calibration_execution().progress().error.empty());check(w.calibration_report()==previous);
        w.fit_calibration(previous->settings);wait(w.calibration_execution());check(w.poll_calibration());near(w.calibration_report()->rates[0].per_second,previous->rates[0].per_second);
    } else if(name=="cancellation") {
        auto request=demand::load_request(std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration/constant.json");
        workspace::ScenarioWorkspace w(source());auto r=demand::calibrate(request);w.retain_calibration(r);
        std::mutex mutex;std::condition_variable cv;bool reached=false,released=false;
        w.start_calibration(request,[&](auto phase){if(phase!="Parsing")return;std::unique_lock lock(mutex);reached=true;cv.notify_all();check(cv.wait_for(lock,std::chrono::seconds{5},[&]{return released;}));});
        {std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds{5},[&]{return reached;}));}
        w.calibration_execution().cancel();{std::lock_guard lock(mutex);released=true;}cv.notify_all();wait(w.calibration_execution());check(w.calibration_execution().progress().cancelled && !w.poll_calibration() && w.calibration_report()==r);
    } else if(name=="history_limit") {
        workspace::ScenarioWorkspace w(source());for(std::size_t i=0;i<workspace::max_studies;++i){auto data=imported({epoch+static_cast<std::int64_t>(i)});w.retain_calibration(demand::fit(data,settings()));}
        check(w.calibrations().size()==32);rejects([&]{w.retain_calibration(demand::fit(imported({epoch+1000}),settings()));});
        auto id=w.calibrations().front().evidence->id;rejects([&]{w.delete_calibration(id,false);});w.delete_calibration(id,true);w.retain_calibration(demand::fit(imported({epoch+1000}),settings()));check(w.calibrations().size()==32);
    } else if(name=="request_schema") {
        Temporary tmp(name);const auto request_path=std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration/constant.json";
        std::ifstream file(request_path);Json j;file>>j;
        for(auto key:{"continuous","timestamp_format","model","daily_bin_seconds","split","coverage_gaps"}) {
            auto malformed=j;if(std::string(key)=="continuous")malformed[key]=false;else if(std::string(key)=="timestamp_format")malformed[key]="local_time";else if(std::string(key)=="model")malformed[key]="automatic";else if(std::string(key)=="daily_bin_seconds")malformed[key]=1.5;else if(std::string(key)=="split")malformed[key]="2026-01-08T00:00:00Z";else malformed[key]=Json::array();
            const auto path=tmp.path/(std::string(key)+".json");{std::ofstream out(path);out<<malformed;}rejects([&]{(void)demand::load_request(path);});
        }
        auto request=demand::load_request(request_path);check(request.csv==request_path.parent_path()/"constant.csv");
    } else if(name=="maximum_valid") {
        std::string csv="timestamp\n";csv.reserve(21*demand::max_records+10);for(std::size_t i=0;i<demand::max_records;++i)csv+="2026-01-05T00:00:00Z\n";
        auto dataset=demand::import_csv(csv,"maximum.csv",exposure());check(dataset->provenance.accepted==demand::max_records && dataset->provenance.duplicates==demand::max_records-1);
        auto r=demand::fit(dataset,settings());check(r->training.observed==demand::max_records && r->holdout.observed==0);check(demand::report_json(*r).size()<demand::max_report_bytes);
    } else if(name=="conversion_limits") {
        auto r=demand::fit(imported({epoch+3600,epoch+86400+3600}),settings(demand::Model::DailyProfile));auto src=source();src.simulation.tick_rate=1;src.simulation.duration_seconds=604800;
        // Many zero bins are omitted; only 7 nonzero daily segments fit the schema.
        auto c=demand::convert(*r,src,"Seven days",1,epoch,true);check(std::get<scenario::WarehouseConfig>(c.scenario.domain).workload.segments.size()==7);
        std::vector<std::int64_t> events;for(int i=0;i<48;++i)events.push_back(epoch+i*3600);r=demand::fit(imported(events),settings(demand::Model::DailyProfile));
        rejects([&]{(void)demand::convert(*r,src,"Too many segments",1,epoch,true);});rejects([&]{(void)demand::convert(*r,src,"Overflow",1,INT64_MAX,true);});
        auto sparse=demand::fit(imported({epoch},exposure(20*86400)),{epoch+10*86400,demand::Model::Constant,86400,86400});
        rejects([&]{(void)demand::convert(*sparse,source(),"Too sparse",1,epoch,true);});
    } else if(name=="selection") {
        Temporary tmp(name);workspace::ScenarioWorkspace w(source());workspace::Project project(w);
        auto first=demand::fit(imported({epoch}),settings()),second=demand::fit(imported({epoch+1}),settings());w.retain_calibration(first);w.retain_calibration(second);w.select_calibration(first->id);
        project.save(tmp.path/"selection.simforge");project.open(tmp.path/"selection.simforge");check(w.calibration_report()->id==first->id && !project.dirty());
    } else if(name=="history_export") {
        workspace::ScenarioWorkspace w(source());auto report=demand::fit(imported({epoch}),settings());w.retain_calibration(report);
        const auto json=Json::parse(w.history_json());check(json["demand_calibrations"].size()==1 && json["demand_calibrations"][0]["calibration_id"]==report->id);
    } else if(name=="retained_conversion") {
        auto request=demand::load_request(std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration/constant.json");workspace::ScenarioWorkspace w(source());auto r=demand::calibrate(request);w.retain_calibration(r);
        auto c=demand::convert(*r,source(),"First conversion",1,epoch,true);w.record_calibration_conversion(c);
        w.start_calibration(request);wait(w.calibration_execution());check(w.poll_calibration());check(w.has_pending_calibration());
        c=demand::convert(*w.calibration_report(),source(),"Second conversion",2,epoch,true);w.record_calibration_conversion(c);
        check(w.calibrations().size()==1 && w.calibrations()[0].evidence->conversions.size()==2 && !w.has_pending_calibration());
    } else if(name=="corrupt_project") {
        Temporary tmp(name);workspace::ScenarioWorkspace w(source());workspace::Project project(w);auto r=demand::fit(imported({epoch}),settings());w.retain_calibration(r);
        auto malformed=Json::parse(project.serialize());malformed["payload"]["demand_calibrations"][0]["rates"][0]["count"]=3;
        {std::ofstream out(tmp.path/"bad.simforge");out<<malformed;}auto before=w.calibration_report();rejects([&]{project.open(tmp.path/"bad.simforge");});check(w.calibration_report()==before && w.calibrations().size()==1);
    } else if(name=="async_save" || name=="capture_revision" || name=="async_open") {
        Temporary tmp(name);workspace::ScenarioWorkspace w(source());workspace::Project project(w);auto r=demand::fit(imported({epoch}),settings());w.retain_calibration(r);
        std::mutex mutex;std::condition_variable cv;bool reached=false,released=false;
        const auto gate=[&](std::string_view phase){if(phase!="calibration_serialization")return;std::unique_lock lock(mutex);reached=true;cv.notify_all();check(cv.wait_for(lock,std::chrono::seconds{5},[&]{return released;}));};
        project.start_save(tmp.path/"save.simforge",true,gate);
        {std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds{5},[&]{return reached;}));}
        auto c=demand::convert(*r,source(),"Newer conversion",5,epoch,true);w.record_calibration_conversion(c);
        {std::lock_guard lock(mutex);released=true;}cv.notify_all();
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds{10};while(project.busy()){check(std::chrono::steady_clock::now()<limit);project.poll();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        check(project.dirty() && w.calibration_report()->conversions.size()==1);
        workspace::ScenarioWorkspace saved;workspace::Project saved_project(saved);saved_project.open(tmp.path/"save.simforge");check(saved.calibration_report()->conversions.empty());
        auto request=demand::load_request(std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration/constant.json");
        w.start_calibration(request);project.start_open(tmp.path/"save.simforge");
        while(project.busy()){check(std::chrono::steady_clock::now()<limit);project.poll();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        check(w.calibrations().size()==1 && w.calibration_report()->conversions.empty() && !w.calibration_execution().progress().active && !project.dirty());
    } else if(name=="examples") {
        const auto root=std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/demand-calibration";
        auto constant=demand::calibrate(demand::load_request(root/"constant.json"));near(constant->holdout.expected,96);check(constant->holdout.observed==96);
        auto surge=demand::calibrate(demand::load_request(root/"morning-surge.json"));near(*surge->holdout.deviance,0);check(*surge->baseline_holdout.deviance>100);
        auto sparse=demand::calibrate(demand::load_request(root/"sparse.json"));check(sparse->training.observed==2 && sparse->rates[0].upper_per_second>sparse->rates[0].per_second*2);
        auto shift=demand::calibrate(demand::load_request(root/"distribution-shift.json"));near(shift->holdout.expected,144);check(shift->holdout.observed==432);near(shift->holdout.signed_error,288);
    } else throw std::invalid_argument("Unknown demand test "+name);
}
}
int main(int argc,char** argv){try{check(argc==2);run(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
