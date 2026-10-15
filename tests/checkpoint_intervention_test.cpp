#include <simforge/analysis/checkpoint_intervention.hpp>
#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <source_location>
#include <thread>
#include <fstream>
using namespace simforge;
namespace cp=analysis::checkpoint;
using Json=nlohmann::json;
namespace {
void check(bool x,std::source_location p=std::source_location::current()){if(!x)throw std::runtime_error("Checkpoint test failed at line "+std::to_string(p.line()));}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
void near(double a,double b,double e=1e-9){check(std::abs(a-b)<e);}
cp::Request source(scenario::ArrivalMode mode=scenario::ArrivalMode::Constant){
    cp::Request r;r.source={1,"Checkpoint fixture",{10,600,42},scenario::WarehouseConfig{4,5,1,5,20}};r.checkpoint_tick=2000;r.plan.count=3;r.plan.base_seed=1001;
    auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);c.workload.arrivals=mode;
    if(mode==scenario::ArrivalMode::Uniform){c.shipment_interval_seconds=0;c.workload.arrival_min_seconds=2;c.workload.arrival_max_seconds=8;}
    if(mode==scenario::ArrivalMode::Profile){c.shipment_interval_seconds=0;c.workload.segments={{0,300,3},{300,600,7}};}
    if(mode==scenario::ArrivalMode::Poisson){c.shipment_interval_seconds=0;c.workload.rate_per_second=.2;}
    if(mode==scenario::ArrivalMode::PiecewisePoisson){c.shipment_interval_seconds=0;c.workload.intensities={{0,150,.1},{150,400,.5},{400,600,.05}};}
    return r;
}
void wait(workspace::ExperimentExecution& e){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(e.progress().active()){check(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}}
void test(std::string_view name){
    auto r=source();
    if(name=="request"){cp::validate(r);check(cp::request_json(cp::parse_request(cp::request_json(r)))==cp::request_json(r));auto old=r;old.id=99;check(cp::identity(r)==cp::identity(old));old.checkpoint_tick++;check(cp::identity(r)!=cp::identity(old));return;}
    if(name=="invalid"){auto bad=r;bad.version=2;rejects([&]{cp::validate(bad);});bad=r;bad.checkpoint_tick=6001;rejects([&]{cp::validate(bad);});bad=r;bad.intervention.resource=static_cast<warehouse::ResourceType>(99);rejects([&]{cp::validate(bad);});bad=r;bad.intervention.action=static_cast<cp::Action>(99);rejects([&]{cp::validate(bad);});bad=r;bad.plan.count=101;rejects([&]{cp::validate(bad);});bad=r;bad.plan.base_seed=UINT64_MAX;rejects([&]{cp::validate(bad);});bad=r;bad.plan.confidence=.9;rejects([&]{cp::validate(bad);});return;}
    if(name=="arithmetic"){r.intervention.value=100000;rejects([&]{cp::validate(r);});r.intervention.value=2;r.intervention.action=cp::Action::Decrease;rejects([&]{cp::validate(r);});r.intervention={warehouse::ResourceType::Worker,cp::Action::Set,99999};rejects([&]{cp::validate(r);});return;}
    if(name=="capacity_actions"){
        for(auto resource:{warehouse::ResourceType::LoadingBay,warehouse::ResourceType::Worker,warehouse::ResourceType::Forklift})for(auto action:{cp::Action::Set,cp::Action::Increase,cp::Action::Decrease}){r.checkpoint_tick=1;r.intervention={resource,action,action==cp::Action::Set?3u:1u};if(resource==warehouse::ResourceType::Forklift && action==cp::Action::Decrease){r.checkpoint_tick=6000;}const auto branches=cp::reconstruct(r,1001);const auto before=warehouse::capacity_for(branches.checkpoint.capacity,resource);check(branches.outcome.applied.capacity==(action==cp::Action::Set?3u:action==cp::Action::Increase?before+1:before-1));}
        r=source(scenario::ArrivalMode::Poisson);std::get<scenario::WarehouseConfig>(r.source.domain).forklifts=2;r.checkpoint_tick=0;r.intervention={warehouse::ResourceType::Forklift,cp::Action::Decrease,1};const auto reduction=cp::run(r);check(*reduction.end_metrics[0].mean_difference<0 && reduction.end_metrics[0].negative==3);
        return;
    }
    if(name=="replay"){
        const auto branches=cp::reconstruct(r,1001);warehouse::WarehouseRecording recording;recording.schema_version=2;recording.scenario_identity=branches.checkpoint.scenario_identity;recording.initial_tick=r.checkpoint_tick;recording.end_tick=6000;recording.initial_snapshot=branches.checkpoint;recording.expected_final=branches.intervention;recording.commands={branches.outcome.applied};auto restored=warehouse::WarehouseSimulation::create_from(branches.checkpoint);warehouse::replay_recording(recording,restored);check(restored.capture_snapshot()==branches.intervention);
        auto corrupt=branches.checkpoint;corrupt.totals.started++;rejects([&]{restored.restore_snapshot(corrupt);});check(restored.capture_snapshot()==branches.intervention);return;
    }
    if(name=="reductions"){r.intervention={warehouse::ResourceType::Forklift,cp::Action::Set,0};rejects([&]{cp::run(r);});r.checkpoint_tick=scenario::total_ticks(r.source);auto result=cp::run(r);check(result.outcomes[0].control==result.outcomes[0].intervention);check(!result.post_metrics[0].mean_difference);return;}
    if(name=="source_commands"){r.source_commands={{100,0,warehouse::WarehouseCommandType::SetResourceCapacity,3,warehouse::ResourceType::Worker},{3000,1,warehouse::WarehouseCommandType::SetResourceCapacity,3,warehouse::ResourceType::LoadingBay}};auto branches=cp::reconstruct(r,1001);check(branches.checkpoint.capacity.workers==3 && branches.control.capacity.loading_bays==3 && branches.intervention.capacity.loading_bays==3);check(branches.control.capacity_changes.size()==2 && branches.intervention.capacity_changes.size()==3);auto report=cp::run(r);check(cp::report_json(cp::parse_report(cp::report_json(report)))==cp::report_json(report));return;}
    if(name=="command_conflict"){r.source_commands={{r.checkpoint_tick,0,warehouse::WarehouseCommandType::SetResourceCapacity,2,warehouse::ResourceType::Forklift}};rejects([&]{cp::validate(r);});r.source_commands[0].resource=warehouse::ResourceType::Worker;r.source_commands[0].type=static_cast<warehouse::WarehouseCommandType>(99);rejects([&]{cp::validate(r);});r.source_commands[0].type=warehouse::WarehouseCommandType::SetResourceCapacity;r.source_commands[0].sequence=2;rejects([&]{cp::validate(r);});return;}
    if(name.starts_with("boundary_")){
        std::uint64_t tick=0;if(name=="boundary_one")tick=1;else if(name=="boundary_idle")tick=11;else if(name=="boundary_arrival")tick=50;else if(name=="boundary_completion" || name=="boundary_simultaneous")tick=200;else if(name=="boundary_before_end")tick=5999;else if(name=="boundary_end")tick=6000;else if(name=="boundary_command")tick=100;
        r.checkpoint_tick=tick;if(name=="boundary_command")r.source_commands={{tick,0,warehouse::WarehouseCommandType::SetResourceCapacity,3,warehouse::ResourceType::Worker}};
        const auto branches=cp::reconstruct(r,1001);check(branches.checkpoint.current_tick==tick && branches.checkpoint.settled);for(const auto& e:branches.checkpoint.events)check(e.tick>tick);
        auto d=r.source;d.simulation.random_seed=1001;warehouse::WarehouseSimulation independent(d);for(const auto& c:r.source_commands){independent.advance_to(c.tick);independent.set_resource_capacity(c.resource,c.capacity);}independent.advance_to(tick);check(branches.checkpoint==independent.capture_snapshot());
        check(branches.intervention.capacity_changes.back().tick==tick && branches.intervention.capacity_changes.back().started_before==branches.checkpoint.totals.started);
        if(tick==200){check(branches.checkpoint.totals.completed==1 && branches.checkpoint.shipments.size()==5);check(branches.checkpoint.shipments[1].start_tick==200);}
        return;
    }
    if(name=="isolation" || name=="snapshot" || name=="control" || name=="immutable" || name=="fresh_start"){
        const auto original=cp::request_json(r);auto branches=cp::reconstruct(r,1001);check(cp::request_json(r)==original);
        auto restored=warehouse::WarehouseSimulation::create_from(branches.checkpoint);const auto snapshot=restored.capture_snapshot();check(warehouse::deserialize_snapshot(warehouse::serialize_snapshot(snapshot))==snapshot);
        auto independent=warehouse::WarehouseSimulation::create_from(snapshot);independent.set_resource_capacity(warehouse::ResourceType::Forklift,2);check(restored.capture_snapshot()==snapshot);independent.run();check(independent.capture_snapshot()==branches.intervention);restored.run();check(restored.capture_snapshot()==branches.control);
        auto d=r.source;d.simulation.random_seed=1001;warehouse::WarehouseSimulation uninterrupted(d);uninterrupted.run();check(uninterrupted.capture_snapshot()==branches.control);
        std::get<scenario::WarehouseConfig>(d.domain).forklifts=2;warehouse::WarehouseSimulation fresh(d);fresh.run();check(fresh.metrics()!=branches.outcome.intervention);return;
    }
    if(name.starts_with("pairing_")){
        const auto mode=name=="pairing_poisson"?scenario::ArrivalMode::Poisson:name=="pairing_piecewise"?scenario::ArrivalMode::PiecewisePoisson:name=="pairing_uniform"?scenario::ArrivalMode::Uniform:name=="pairing_profile"?scenario::ArrivalMode::Profile:scenario::ArrivalMode::Constant;r=source(mode);
        auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);c.processing_duration_seconds=0;c.workload.processing=scenario::ProcessingMode::Uniform;c.workload.processing_min_seconds=5;c.workload.processing_max_seconds=35;
        const auto branches=cp::reconstruct(r,1001);check(branches.control.shipments.size()==branches.intervention.shipments.size());
        for(std::size_t i=0;i<branches.control.shipments.size();++i){const auto& a=branches.control.shipments[i];const auto& b=branches.intervention.shipments[i];check(a.id==b.id && a.arrival_tick==b.arrival_tick && a.required_processing_ticks==b.required_processing_ticks);check(a.required_processing_ticks==scenario::workload::processing_ticks(c,10,1001,a.id));}
        auto other=cp::reconstruct(r,1002);check(other.outcome.checkpoint_digest!=branches.outcome.checkpoint_digest);return;
    }
    if(name=="repeatability" || name=="exports" || name=="tamper" || name=="metrics"){
        r=source(scenario::ArrivalMode::Poisson);auto report=cp::run(r);const auto bytes=cp::report_json(report);check(cp::report_json(cp::run(r))==bytes);check(cp::report_json(cp::parse_report(bytes))==bytes);
        check(report.prefix_executions==3 && report.control_executions==3 && report.intervention_executions==3);
        const auto& row=report.outcomes[0];near(*cp::metric_value(row,true,0,true,r),static_cast<double>(row.intervention.completed-row.checkpoint.completed));
        near(*cp::metric_value(row,true,3,true,r),static_cast<double>(row.intervention.total_wait_ticks-row.checkpoint.total_wait_ticks)/static_cast<double>(row.intervention.started-row.checkpoint.started)/10);
        check(!cp::metric_value(row,true,1,true,r) && !cp::metric_value(row,true,4,true,r));check(!report.end_metrics[7].favorable_fraction);
        if(name=="exports"){for(bool seeds:{false,true}){const auto csv=cp::report_csv(report,seeds);std::size_t start=0;while(start<csv.size()){const auto end=csv.find('\n',start);check(end!=std::string::npos);const auto line=csv.substr(start,end-start);check(std::count(line.begin(),line.end(),',')==23);start=end+1;}}}
        if(name=="tamper"){auto forged=report;for(auto& row:forged.outcomes){++row.control.arrived;++row.control.waiting;++row.intervention.arrived;++row.intervention.waiting;}cp::aggregate(forged);rejects([&]{cp::report_json(forged);});auto j=Json::parse(bytes);j["end_summaries"]["completed"]["mean_difference"]=999;rejects([&]{cp::parse_report(j.dump());});j=Json::parse(bytes);j["outcomes"][0]["control"]["completed"]=999;rejects([&]{cp::parse_report(j.dump());});j=Json::parse(bytes);j["outcomes"][0]["checkpoint_available_ticks"][0]=1;rejects([&]{cp::parse_report(j.dump());});j=Json::parse(bytes);j["request"]["boundary"]="before_events";rejects([&]{cp::parse_report(j.dump());});}
        return;
    }
    if(name=="statistics" || name=="statistical_edges"){
        std::vector<std::optional<double>> b{100,200,300,400,500},c{104,206,305,407,503};const auto summary=analysis::summarize_pairs(b,c,true);near(*summary.mean_difference,5);near(*summary.standard_deviation,std::sqrt(2.5));near(*summary.standard_error,std::sqrt(.5));near(*summary.lower,5-2.776*std::sqrt(.5));near(*summary.upper,5+2.776*std::sqrt(.5));check(summary.positive==5 && summary.zero==0);
        for(unsigned n:{1u,2u,3u}){b.assign(n,0);c.assign(n,0);auto s=analysis::summarize_pairs(b,c,true);check(s.zero==n && s.lower.has_value()==(n>=3));}
        b={0,0,0};c={-2,0,2};auto mixed=analysis::summarize_pairs(b,c,true);check(mixed.negative==1 && mixed.positive==1 && mixed.zero==1);c={-4,-6,-5};check(*analysis::summarize_pairs(b,c,false).mean_difference==-5);b[0].reset();check(!analysis::summarize_pairs(b,c,true).mean_difference);c[1]=std::numeric_limits<double>::infinity();rejects([&]{analysis::summarize_pairs(b,c,true);});
        auto deterministic=cp::run(source());check(!deterministic.end_metrics[0].lower);return;
    }
    if(name.starts_with("cancel_")){
        const std::string phase=name=="cancel_prefix"?"Pre-checkpoint":name=="cancel_control"?"Control continuation":name=="cancel_intervention"?"Intervention continuation":name=="cancel_between"?"Replication complete":"Aggregating checkpoint pairs";
        std::get<scenario::WarehouseConfig>(r.source.domain).shipment_interval_seconds=1;
        std::stop_source stop;bool in_phase=false;simulation::ExecutionControl control{stop.get_token(),[&](auto p,auto,auto tick,auto horizon){if(p==phase){in_phase=true;if(name=="cancel_between" || name=="cancel_aggregation")stop.request_stop();}if(in_phase && p=="Simulating" && tick>0 && tick<horizon)stop.request_stop();}};bool cancelled=false;try{cp::run(r,&control);}catch(const simulation::ExecutionCancelled&){cancelled=true;}check(cancelled);return;
    }
    if(name=="async" || name=="publication" || name=="failure"){
        workspace::ScenarioWorkspace w(r.source);w.start_checkpoint(r);wait(w.execution());check(w.poll_experiment() && w.studies().size()==1);const auto old=w.report();const auto active=w.view().runtime().capture_snapshot();
        std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;workspace::ExperimentRequest request;request.checkpoint_study=r;
        w.execution().start(request,[&](auto phase,auto,auto,auto){if(phase=="Control continuation"){std::unique_lock lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}});
        {std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(10),[&]{return entered;}));}w.execution().cancel();{std::lock_guard lock(mutex);released=true;}cv.notify_all();wait(w.execution());check(!w.poll_experiment() && w.report()==old && w.studies().size()==1 && w.view().runtime().capture_snapshot()==active);
        r.intervention={warehouse::ResourceType::Forklift,cp::Action::Set,0};w.start_checkpoint(r);wait(w.execution());check(w.execution().progress().state==workspace::ExecutionState::Failed && !w.poll_experiment() && w.report()==old);return;
    }
    if(name=="persistence" || name=="playback" || name=="history"){
        workspace::ScenarioWorkspace w(r.source);w.start_checkpoint(r);wait(w.execution());check(w.poll_experiment());check(w.studies().front().type==workspace::StudyType::CheckpointIntervention);w.rename_study(w.selected_study_id(),"Retained checkpoint","Notes survive");w.study_view({0,1002,7,{}});
        const auto path=std::filesystem::temp_directory_path()/("simforge-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".simforge");
        workspace::Project project(w);project.save(path,true);workspace::ScenarioWorkspace restored;workspace::Project opened(restored);opened.open(path);std::filesystem::remove(path);check(restored.studies()[0].view.metric==7 && restored.studies()[0].view.seed==1002);check(restored.studies().size()==1 && restored.studies()[0].title=="Retained checkpoint" && restored.studies()[0].notes=="Notes survive" && restored.json()==w.json());
        for(bool intervention:{false,true}){restored.simulate_checkpoint(1002,intervention);check(!restored.view().result_preview());restored.view().seek(6000);const auto& row=restored.report()->checkpoint_study->outcomes[1];check(restored.view().observation().metrics==(intervention?row.intervention:row.control));restored.view().seek(1);check(restored.view().runtime().resource_capacity().forklifts==1);restored.view().seek(2000);check(restored.view().runtime().resource_capacity().forklifts==(intervention?2u:1u));}
        rejects([&]{restored.simulate_checkpoint(777,true);});rejects([&]{restored.delete_study(restored.selected_study_id(),false);});restored.delete_study(restored.selected_study_id(),true);check(restored.studies().empty());return;
    }
    if(name=="no_effect"){r.intervention.value=0;const auto report=cp::run(r);for(const auto& row:report.outcomes)check(row.control==row.intervention);check(*report.end_metrics[0].mean_difference==0);return;}
    if(name=="retention_limit"){
        workspace::ScenarioWorkspace w(r.source);workspace::Comparison c;c.checkpoint_study=cp::run(r);c.baseline_definition=r.source;
        for(std::size_t i=0;i<workspace::max_studies;++i)w.retain_study(c);const auto previous=w.report();w.start_checkpoint(r);wait(w.execution());check(!w.poll_experiment() && w.has_pending_study() && w.report()==previous && w.studies().size()==32);
        w.delete_study(w.studies().front().id,true);check(w.retain_pending_study() && w.studies().size()==32 && !w.has_pending_study());return;
    }
    if(name=="limits"){r.source.simulation={1,100000,42};std::get<scenario::WarehouseConfig>(r.source.domain).shipment_interval_seconds=1;r.plan.count=100;rejects([&]{cp::validate(r);});return;}
    throw std::invalid_argument("Unknown checkpoint test");
}
}
int main(int argc,char** argv){try{if(argc!=2)return 2;test(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
