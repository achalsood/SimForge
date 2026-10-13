#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <source_location>
using namespace simforge;
using Json=nlohmann::json;
namespace {
void check(bool ok,std::source_location p=std::source_location::current()) {if(!ok) throw std::runtime_error("Statistical check failed at line "+std::to_string(p.line()));}
void near(double a,double b,double tolerance=1e-9) {check(std::abs(a-b)<tolerance);}
template<class F> void rejects(F f) {bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);}
analysis::StatisticalRequest source(bool stochastic=true) {
    analysis::StatisticalRequest r;r.baseline={1,"Statistical baseline",{10,600,42},scenario::WarehouseConfig{3,10,2,5,20}};r.plan.count=3;r.plan.base_seed=1001;
    if(stochastic) {auto& c=std::get<scenario::WarehouseConfig>(r.baseline.domain);c.shipment_interval_seconds=c.processing_duration_seconds=0;
        c.workload.arrivals=scenario::ArrivalMode::Uniform;c.workload.arrival_min_seconds=2;c.workload.arrival_max_seconds=8;
        c.workload.processing=scenario::ProcessingMode::Uniform;c.workload.processing_min_seconds=2;c.workload.processing_max_seconds=40;}
    for(unsigned i=0;i<3;++i) {auto d=r.baseline;d.name="Capacity "+std::to_string(i);auto& c=std::get<scenario::WarehouseConfig>(d.domain);if(i==0) ++c.forklifts;else if(i==1) ++c.workers;else ++c.loading_bays;r.candidates.push_back({i+1,d});}return r;
}
void wait(workspace::ExperimentExecution& e) {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(e.progress().active()) {check(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}}
workspace::ScenarioWorkspace workspace_source() {
    auto input=source();workspace::ScenarioWorkspace w{input.baseline};
    for(const auto& candidate:input.candidates) {const auto& c=std::get<scenario::WarehouseConfig>(candidate.definition.domain);
        w.edit(workspace::Field::Name,candidate.definition.name);w.edit(workspace::Field::Bays,std::to_string(c.loading_bays));w.edit(workspace::Field::Workers,std::to_string(c.workers));w.edit(workspace::Field::Forklifts,std::to_string(c.forklifts));w.add_candidate();}
    return w;
}
void run(std::string_view test) {
    if(test=="fixtures" || test=="edges" || test=="quantiles" || test=="numerical") {
        if(test=="quantiles") {for(auto [df,value]:{std::pair{1u,12.706}, {2u,4.303},{3u,3.182},{9u,2.262},{19u,2.093},{49u,2.010},{99u,1.984}}) near(analysis::student_t_critical_95(df),value);rejects([]{(void)analysis::student_t_critical_95(0);});rejects([]{(void)analysis::student_t_critical_95(100);});return;}
        // Strong covariance: an independent-baseline/candidate variance formula
        // would be much wider than the known paired differences {2,4,6}.
        std::vector<std::optional<double>> b{100,200,300},c{102,204,306};const auto s=analysis::summarize_pairs(b,c,true);
        near(*s.baseline_mean,200);near(*s.candidate_mean,204);near(*s.mean_difference,4);near(*s.standard_deviation,2);near(*s.standard_error,1.1547005383792515);
        near(*s.lower,-0.968676416645919);near(*s.upper,8.968676416645919);near(*s.minimum,2);near(*s.maximum,6);near(*s.median,4);near(*s.relative_change_percent,2);check(s.positive==3 && s.zero==0 && *s.favorable_fraction==1);
        if(test=="edges") {
            for(unsigned n:{1u,2u,3u,100u}) {b.assign(n,1);c.assign(n,1);auto z=analysis::summarize_pairs(b,c,true);check(*z.mean_difference==0 && z.zero==n);check(z.lower.has_value()==(n>=3));if(n>=3) check(*z.lower==0 && *z.upper==0);}
            b={10,10,10};c={7,7,7};auto negative=analysis::summarize_pairs(b,c,false);check(negative.negative==3 && *negative.favorable_fraction==1 && *negative.lower==-3 && *negative.upper==-3);
            c={7,10,13};const auto mixed=analysis::summarize_pairs(b,c,false);check(mixed.positive==1 && mixed.negative==1 && mixed.zero==1 && *mixed.lower<0 && *mixed.upper>0);near(*mixed.median,0);
            check(!analysis::summarize_pairs(b,c,true,false).lower);b={0,0,0};check(!analysis::summarize_pairs(b,c,true).relative_change_percent);
            b[0]=std::nullopt;auto missing=analysis::summarize_pairs(b,c,true);check(missing.defined==2 && !missing.lower && !missing.baseline_mean);
        }
        if(test=="numerical") {b={1e15,1e15,1e15};c={1e15+2,1e15+4,1e15+6};auto large=analysis::summarize_pairs(b,c,true);near(*large.mean_difference,4);near(*large.standard_deviation,2);
            c[0]=std::numeric_limits<double>::infinity();rejects([&]{(void)analysis::summarize_pairs(b,c,true);});c[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{(void)analysis::summarize_pairs(b,c,true);});
            b={-1e308,-1e308,-1e308};c={1e308,1e308,1e308};rejects([&]{(void)analysis::summarize_pairs(b,c,true);});rejects([&]{(void)analysis::summarize_pairs({},c,true);});}
        return;
    }
    if(test=="seeds" || test=="validation") {
        auto r=source();check(analysis::replication_seeds(r.plan)==std::vector<std::uint64_t>{1001,1002,1003});
        r.plan.base_seed=UINT64_MAX;rejects([&]{(void)analysis::replication_seeds(r.plan);});r.plan.policy=analysis::SeedPolicy::Explicit;r.plan.base_seed=0;r.plan.explicit_seeds={9,8,7};check(analysis::replication_seeds(r.plan)==r.plan.explicit_seeds);
        r.plan.explicit_seeds[2]=8;rejects([&]{(void)analysis::replication_seeds(r.plan);});
        for(double confidence:{0.9,0.99,std::numeric_limits<double>::quiet_NaN()}) {auto bad=source();bad.plan.confidence=confidence;rejects([&]{analysis::validate_statistical_request(bad);});}
        if(test=="validation") {
            r=source();std::get<scenario::WarehouseConfig>(r.candidates[0].definition.domain).workload.arrival_max_seconds=9;rejects([&]{analysis::validate_statistical_request(r);});
            r=source();++r.candidates[0].definition.simulation.duration_seconds;rejects([&]{analysis::validate_statistical_request(r);});
            r=source();r.plan.count=101;rejects([&]{analysis::validate_statistical_request(r);});r=source();r.plan.method_version=2;rejects([&]{analysis::validate_statistical_request(r);});
            r=source();r.plan.count=100;r.baseline.simulation.duration_seconds=100000;for(auto& c:r.candidates) c.definition.simulation.duration_seconds=100000;rejects([&]{analysis::validate_statistical_request(r);});
        }return;
    }
    if(test=="pairing" || test=="determinism" || test=="constant" || test=="degenerate" || test=="explicit" || test=="exports" || test=="ranking" || test=="zero_capacity") {
        auto request=source(test!="constant");if(test=="zero_capacity") {auto& c=std::get<scenario::WarehouseConfig>(request.candidates[0].definition.domain);c.forklifts=0;}
        if(test=="degenerate") {const auto fixed=[](auto& d){auto& w=std::get<scenario::WarehouseConfig>(d.domain).workload;w.arrival_min_seconds=w.arrival_max_seconds=5;w.processing_min_seconds=w.processing_max_seconds=20;};fixed(request.baseline);for(auto& c:request.candidates) fixed(c.definition);}
        if(test=="explicit") {request.plan.policy=analysis::SeedPolicy::Explicit;request.plan.base_seed=0;request.plan.explicit_seeds={7,3,12};}
        auto report=analysis::run_statistical_experiment(request);const auto bytes=analysis::serialize_statistical_report(report);check(analysis::serialize_statistical_report(analysis::parse_statistical_report(bytes))==bytes);
        if(test=="constant" || test=="degenerate") for(const auto& c:report.candidates) check(!c.metrics[0].lower);
        if(test=="explicit") for(std::size_t i=0;i<3;++i) check(report.replications[i].seed==request.plan.explicit_seeds[i]);
        if(test=="exports") {check(analysis::serialize_statistical_report(analysis::replay_statistical_report(bytes))==bytes);check(!analysis::serialize_statistical_csv(report).empty() && !analysis::serialize_statistical_csv(report,true).empty());auto damaged=Json::parse(bytes);damaged["summaries"][0]["metrics"]["completed"]["mean_difference"]=999;rejects([&]{(void)analysis::parse_statistical_report(damaged.dump());});damaged=Json::parse(bytes);damaged["replication_seeds"][0]=999;rejects([&]{(void)analysis::parse_statistical_report(damaged.dump());});}
        if(test=="determinism") {std::reverse(request.candidates.begin(),request.candidates.end());check(analysis::serialize_statistical_report(analysis::run_statistical_experiment(request))==bytes);}
        if(test=="ranking") {request.objective=analysis::Objective::MinimizeWaiting;auto lower=analysis::run_statistical_experiment(request);check(lower.candidates[0].rank==1 && *lower.candidates[0].metrics[1].mean_difference<0);}
        if(test=="zero_capacity") {for(const auto& row:report.replications) check(row.candidates[0].started==0 && !row.candidates[0].average_wait_seconds);check(!report.candidates[0].metrics[3].lower);}
        if(test=="pairing") for(const auto& row:report.replications) {
            auto d=request.baseline;d.simulation.random_seed=row.seed;warehouse::WarehouseSimulation baseline(d);baseline.run();check(row.baseline==baseline.metrics());
            for(std::size_t i=0;i<report.request.candidates.size();++i) {auto c=report.request.candidates[i].definition;c.simulation.random_seed=row.seed;warehouse::WarehouseSimulation candidate(c);candidate.run();check(row.candidates[i]==candidate.metrics());
                check(candidate.shipments().size()==baseline.shipments().size());for(std::size_t s=0;s<candidate.shipments().size();++s) check(candidate.shipments()[s].arrival_tick==baseline.shipments()[s].arrival_tick && candidate.shipments()[s].required_processing_ticks==baseline.shipments()[s].required_processing_ticks);}
            for(std::size_t p=0;p<3;++p) {auto c=d;auto& resources=std::get<scenario::WarehouseConfig>(c.domain);if(p==0) ++resources.loading_bays;else if(p==1) ++resources.workers;else ++resources.forklifts;warehouse::WarehouseSimulation probe(c);probe.run();check(row.probes[p]==probe.metrics());}
        }return;
    }
    if(test=="async") {
        const auto input=source();workspace::ExperimentRequest r;r.baseline=input.baseline;r.objective=input.objective;r.replications=input.plan;for(const auto& c:input.candidates) r.candidates.push_back({c.id,c.definition,{},{}});
        auto expected=workspace::execute_experiment(r);workspace::ExperimentExecution service;const auto id=service.start(r);wait(service);auto actual=service.take_result(id);check(actual && actual->statistics && actual->final_snapshots.empty());expected.statistics->request.id=id;
        check(analysis::serialize_statistical_report(*expected.statistics)==analysis::serialize_statistical_report(*actual->statistics));const auto p=service.progress();check(p.completed_replications==3 && p.completed_candidates==9 && p.total_candidates==9);
        r.mode=workspace::ComparisonMode::Checkpoint;rejects([&]{workspace::validate_experiment_request(r);});return;
    }
    if(test=="workspace" || test=="persistence" || test=="cancellation" || test=="between_replications" || test=="failure") {
        auto w=workspace_source();const auto live=w.view().runtime().capture_snapshot();auto plan=source().plan;
        w.start_statistical_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,plan);wait(w.execution());check(w.poll_experiment());
        const auto previous=w.json();check(w.report()->statistics && w.view().runtime().capture_snapshot()==live && w.report()->final_snapshots.empty());
        rejects([&]{w.view_result(std::nullopt);});rejects([&]{w.simulate_result("Baseline");});
        if(test=="workspace") {w.rename_candidate(w.candidates().front().id,"Edited after submission");check(w.json()==previous);workspace::ScenarioWorkspace::verify_export(previous);return;}
        if(test=="persistence") {
            const auto root=std::filesystem::temp_directory_path()/("simforge-statistical-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);
            struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}} cleanup{root};
            workspace::Project project(w);auto prefs=project.preferences();prefs.statistical=true;prefs.replications="invalid raw count";prefs.base_seed="17";prefs.result_name="Capacity 0";prefs.statistical_metric=4;project.preferences(prefs);
            project.save(root/"statistical.simforge",true);workspace::ScenarioWorkspace restored;workspace::Project opened(restored);opened.open(root/"statistical.simforge");
            check(restored.json()==previous && restored.report()->final_snapshots.empty() && restored.execution().progress().state==workspace::ExecutionState::Idle && opened.preferences()==prefs);
            Json damaged=Json::parse(project.serialize());damaged["payload"]["study_history"][0]["evidence"]["statistics"]["summaries"][0]["metrics"]["completed"]["mean_difference"]=999;
            const auto text=damaged["payload"].dump();std::uint32_t crc=0xffffffff;for(unsigned char byte:text) {crc^=byte;for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320:0);}damaged["crc32"]=~crc;damaged["payload_size"]=text.size();
            {std::ofstream file(root/"invalid.simforge");file<<damaged.dump();}const auto intact=restored.view().runtime().capture_snapshot();rejects([&]{opened.open(root/"invalid.simforge");});check(restored.json()==previous && restored.view().runtime().capture_snapshot()==intact);return;
        }
        auto request=w.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);request.replications=plan;
        if(test=="failure") {w.execution().start(request,[](auto phase,auto,auto,auto){if(phase=="Aggregating paired results") throw std::runtime_error("Injected aggregation failure");});wait(w.execution());check(w.execution().progress().state==workspace::ExecutionState::Failed && !w.poll_experiment() && w.json()==previous);return;}
        request.baseline.simulation.duration_seconds=100000;request.replications->count=20;for(auto& c:request.candidates) c.definition.simulation.duration_seconds=100000;
        struct Gate {std::mutex mutex;std::condition_variable cv;bool candidate=false,reached=false,released=false;};auto gate=std::make_shared<Gate>();
        struct Release {std::shared_ptr<Gate> gate;~Release(){std::lock_guard lock(gate->mutex);gate->released=true;gate->cv.notify_all();}} release{gate};
        const bool between=test=="between_replications";
        w.execution().start(request,[gate,between](auto phase,auto,auto tick,auto){if(phase=="Evaluating candidate") gate->candidate=true;if(between?(phase=="Replication complete" && tick==1):(gate->candidate && phase=="Simulating" && tick>0)) {std::unique_lock lock(gate->mutex);if(gate->reached) return;gate->reached=true;gate->cv.notify_all();check(gate->cv.wait_for(lock,std::chrono::seconds(20),[&]{return gate->released;}));}});
        {std::unique_lock lock(gate->mutex);check(gate->cv.wait_for(lock,std::chrono::seconds(20),[&]{return gate->reached;}));}
        check(w.execution().progress().current_replication==1 && w.execution().progress().total_replications==20);w.execution().cancel();w.execution().cancel();
        if(between) check(w.execution().progress().completed_replications==1 && w.execution().progress().completed_candidates==3);
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->cv.notify_all();}wait(w.execution());check(w.execution().progress().state==workspace::ExecutionState::Cancelled && !w.poll_experiment() && w.json()==previous && w.view().runtime().capture_snapshot()==live);return;
    }
    throw std::runtime_error("Unknown statistical test");
}
}
int main(int argc,char** argv) {try{check(argc==2);run(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
