#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <source_location>

using namespace simforge;
using namespace simforge::workspace;
using Json=nlohmann::json;
namespace {
void check(bool b,std::source_location p=std::source_location::current()) {if(!b)throw std::runtime_error("Study check failed at line "+std::to_string(p.line()));}
template<class F>void rejects(F f) {bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
scenario::ScenarioDefinition baseline() {
    scenario::ScenarioDefinition d{1,"Study baseline",{10,120,42},scenario::WarehouseConfig{6,2,1,0,0}};
    auto& w=std::get<scenario::WarehouseConfig>(d.domain).workload;w.arrivals=scenario::ArrivalMode::Uniform;w.arrival_min_seconds=1;w.arrival_max_seconds=3;
    w.processing=scenario::ProcessingMode::Uniform;w.processing_min_seconds=2;w.processing_max_seconds=12;return d;
}
void wait(ScenarioWorkspace& w) {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(w.execution().progress().active()){check(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}if(!w.poll_experiment())throw std::runtime_error(w.message());}
ScenarioWorkspace source() {
    ScenarioWorkspace w{baseline()};w.edit(Field::Name,"Two forklifts");w.edit(Field::Forklifts,"2");w.add_candidate();
    analysis::ReplicationPlan p;p.count=3;p.base_seed=101;w.start_statistical_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,p);wait(w);return w;
}
analysis::ReplicationPlan plan() {analysis::ReplicationPlan p;p.count=3;p.base_seed=9001;return p;}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("simforge-study-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".simforge");
    ~Temporary(){std::error_code e;std::filesystem::remove(path,e);}
};
void seal(Json& j,bool evidence_sizes=true) {
    if(evidence_sizes && j["payload"].contains("study_history"))for(auto& s:j["payload"]["study_history"])s["bytes"]=s["evidence"].dump().size();
    const auto bytes=j["payload"].dump();std::uint32_t crc=0xffffffff;
    for(unsigned char c:bytes){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320:0);}j["payload_size"]=bytes.size();j["crc32"]=~crc;
}
void write(const std::filesystem::path& path,const Json& j){std::ofstream out(path);out<<j.dump();}
void run(std::string_view test) {
    auto w=source();const auto id=w.selected_study_id();const auto original=w.json();const auto evidence=w.report();
    if(test=="history" || test=="switch" || test=="types" || test=="views") {
        analysis::SensitivityRequest sweep;sweep.source=baseline();sweep.values={1,2,3};sweep.multi_seed=true;sweep.plan=plan();w.start_sensitivity(sweep);wait(w);const auto sweep_id=w.selected_study_id();
        analysis::InteractionRequest grid;grid.source=baseline();grid.x_values={1,2,3};grid.y_values={1,2,3};grid.multi_seed=true;grid.plan=plan();w.start_interaction(grid);wait(w);const auto grid_id=w.selected_study_id();
        w.compare(ComparisonMode::Initial,analysis::Objective::MinimizeWaiting);const auto single_id=w.selected_study_id();check(w.studies().size()==4);
        const auto snapshot=w.view().runtime().capture_snapshot();
        for(int i=0;i<100;++i){w.select_study(id);check(w.json()==original && w.report()==evidence);w.select_study(sweep_id);check(bool(w.report()->sensitivity));w.select_study(grid_id);check(bool(w.report()->interaction));w.select_study(single_id);check(!w.report()->statistics);}
        check(w.view().runtime().capture_snapshot()==snapshot);w.study_view({1,9002,4,2});w.select_study(id);w.study_view({1,102,3,0});w.select_study(single_id);check(w.studies().back().view.metric==4);
        return;
    }
    if(test=="metadata") {w.rename_study(id,"Named study","Review notes");check(w.json()==original && w.report()==evidence && w.studies()[0].id==id);rejects([&]{w.rename_study(id,"","");});rejects([&]{w.rename_study(id,"Name",std::string(4097,'a'));});return;}
    if(test=="delete" || test=="identity" || test=="count_limit") {
        w.retain_study(*evidence,"Second");const auto second=w.selected_study_id();rejects([&]{w.delete_study(id,false);});w.delete_study(id,true);check(w.studies().size()==1 && w.selected_study_id()==second);
        w.retain_study(*evidence,"Third");check(w.selected_study_id()!=id && w.selected_study_id()!=second);
        if(test=="count_limit") {while(w.studies().size()<max_studies)w.retain_study(*evidence);const auto before=w.history_json();rejects([&]{w.retain_study(*evidence);});check(w.history_json()==before);w.start_statistical_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,plan());while(w.execution().progress().active())std::this_thread::yield();check(!w.poll_experiment() && w.has_pending_study() && w.studies().size()==max_studies);Temporary exported;w.export_pending_json(exported.path);std::ifstream in(exported.path);Json saved;in>>saved;in.close();check(saved.at("statistical_report_version")==1);rejects([&]{w.start_statistical_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,plan());});w.delete_study(w.studies().front().id,true);check(w.retain_pending_study() && !w.has_pending_study());}
        return;
    }
    auto c=w.freeze_confirmation(1,plan());
    if(test=="single_seed" || test=="complete_source_seeds") {
        w.edit(Field::Name,"Other exploratory seed");w.edit(Field::Seed,"909");w.edit(Field::Forklifts,"3");w.add_candidate();w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
        auto independent=w.freeze_confirmation(1,plan());check(independent.exploratory_seeds==std::vector<std::uint64_t>({42,909}));
        auto overlap=plan();overlap.base_seed=909;rejects([&]{(void)w.freeze_confirmation(1,overlap);});w.start_confirmation(independent);wait(w);
        Temporary file;Project p(w);p.save(file.path);ScenarioWorkspace restored;Project q(restored);q.open(file.path);check(restored.report()->confirmation->exploratory_seeds==independent.exploratory_seeds);return;
    }
    if(test=="seeds" || test=="duplicate" || test=="overlap" || test=="overflow") {
        check(analysis::serialize_confirmation_request(analysis::parse_confirmation_request(analysis::serialize_confirmation_request(c)))==analysis::serialize_confirmation_request(c));
        check(analysis::replication_seeds(c.paired.plan)==std::vector<std::uint64_t>({9001,9002,9003}));
        auto bad=c;bad.paired.plan.base_seed=102;rejects([&]{analysis::validate_confirmation_request(bad);});
        bad=c;bad.paired.plan.policy=analysis::SeedPolicy::Explicit;bad.paired.plan.base_seed=0;bad.paired.plan.explicit_seeds={9001,9001,9003};rejects([&]{analysis::validate_confirmation_request(bad);});
        bad.paired.plan.explicit_seeds={9001,103,9003};rejects([&]{analysis::validate_confirmation_request(bad);});bad.paired.plan.explicit_seeds={9003,9001,9002};analysis::validate_confirmation_request(bad);check(analysis::replication_seeds(bad.paired.plan)==bad.paired.plan.explicit_seeds);
        bad=c;bad.paired.plan.base_seed=UINT64_MAX;rejects([&]{analysis::validate_confirmation_request(bad);});return;
    }
    if(test=="frozen" || test=="editor" || test=="selection") {
        const auto frozen=analysis::serialize_confirmation_request(c);w.edit(Field::Forklifts,"5");w.rename_candidate(1,"Changed candidate");w.update_candidate(1);w.rename_study(id,"New title","New notes");
        w.retain_study(*evidence,"Other study");w.start_confirmation(c);wait(w);check(analysis::serialize_confirmation_request(c)==frozen);
        check(scenario::serialize_scenario(w.report()->confirmation->paired.candidates[0].definition)==scenario::serialize_scenario(c.paired.candidates[0].definition));check(w.studies().size()==3);return;
    }
    if(test=="pairing" || test=="execution" || test=="no_leakage" || test=="repeatable") {
        const auto r=analysis::run_confirmation(c);check(analysis::serialize_confirmation_report(analysis::run_confirmation(c))==analysis::serialize_confirmation_report(r));
        const auto direct=analysis::run_statistical_experiment(c.paired);check(direct.replications==r.paired.replications);
        for(const auto& row:r.paired.replications) {
            check(std::find(c.exploratory_seeds.begin(),c.exploratory_seeds.end(),row.seed)==c.exploratory_seeds.end());
            auto b=c.paired.baseline,d=c.paired.candidates[0].definition;b.simulation.random_seed=d.simulation.random_seed=row.seed;
            warehouse::WarehouseSimulation baseline_run(b),candidate_run(d);baseline_run.run();candidate_run.run();check(row.baseline==baseline_run.metrics() && row.candidates[0]==candidate_run.metrics());
            check(baseline_run.shipments().size()==candidate_run.shipments().size());for(std::size_t i=0;i<baseline_run.shipments().size();++i){check(baseline_run.shipments()[i].arrival_tick==candidate_run.shipments()[i].arrival_tick);check(baseline_run.shipments()[i].required_processing_ticks==candidate_run.shipments()[i].required_processing_ticks);}
        }return;
    }
    if(test=="eligibility" || test=="deterministic" || test=="workload") {
        auto bad=c;std::get<scenario::WarehouseConfig>(bad.paired.candidates[0].definition.domain).workload.arrival_max_seconds=4;rejects([&]{analysis::validate_confirmation_request(bad);});
        bad=c;for(auto* d:{&bad.paired.baseline,&bad.paired.candidates[0].definition}) {auto& config=std::get<scenario::WarehouseConfig>(d->domain);config.workload={};config.shipment_interval_seconds=2;config.processing_duration_seconds=6;}rejects([&]{analysis::validate_confirmation_request(bad);});
        analysis::SensitivityRequest s;s.source=baseline();s.parameter=analysis::SweepParameter::ArrivalInterval;s.values={1,2,3};s.plan.count=1;auto& config=std::get<scenario::WarehouseConfig>(s.source.domain);config.workload.arrivals=scenario::ArrivalMode::Constant;config.workload.arrival_min_seconds=config.workload.arrival_max_seconds=0;config.shipment_interval_seconds=2;w.load(s.source,true);w.start_sensitivity(s);wait(w);rejects([&]{(void)w.freeze_confirmation(w.report()->sensitivity->points.front().id,plan());});return;
    }
    if(test=="sensitivity" || test=="interaction") {
        if(test=="sensitivity"){analysis::SensitivityRequest s;s.source=baseline();s.values={1,2,3};s.multi_seed=true;s.plan=plan();w.start_sensitivity(s);wait(w);auto p=plan();p.base_seed=10001;c=w.freeze_confirmation(w.report()->sensitivity->points.back().id,p);}
        else {analysis::InteractionRequest g;g.source=baseline();g.x_values={1,2,3};g.y_values={1,2,3};g.multi_seed=true;g.plan=plan();w.start_interaction(g);wait(w);auto p=plan();p.base_seed=10001;c=w.freeze_confirmation(w.report()->interaction->cells.back().id,p);}
        w.start_confirmation(c);wait(w);check(w.report()->statistics->request.candidates.size()==1);check(w.report()->confirmation->source_type==test);return;
    }
    if(test=="cancel" || test=="cancelled_recovery") {
        std::mutex m;std::condition_variable cv;bool entered=false,release=false;
        w.start_confirmation(c,[&](auto phase,auto,auto,auto){if(phase=="Simulating"){std::unique_lock lock(m);if(!entered){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});}}});
        {std::unique_lock lock(m);check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}));}w.execution().cancel();{std::lock_guard lock(m);release=true;cv.notify_all();}
        while(w.execution().progress().active())std::this_thread::yield();check(!w.poll_experiment());check(w.studies().size()==1 && w.report()==evidence && w.json()==original);check(w.studies()[0].reserved_confirmation_seeds.size()==1);rejects([&]{(void)w.freeze_confirmation(1,plan());});
        if(test=="cancelled_recovery") {Temporary file;file.path.replace_extension(".recovery");Project p(w);check(p.recover_if_due(file.path,std::chrono::steady_clock::now()+std::chrono::seconds(61)));ScenarioWorkspace restored;Project q(restored);q.recover(file.path);check(restored.studies().size()==1 && restored.studies()[0].reserved_confirmation_seeds.size()==1);rejects([&]{(void)restored.freeze_confirmation(1,plan());});}return;
    }
    if(test=="exports" || test=="corrupt_report") {
        const auto r=analysis::run_confirmation(c);auto j=Json::parse(analysis::serialize_confirmation_report(r));check(analysis::serialize_confirmation_report(analysis::parse_confirmation_report(j.dump()))==j.dump());
        Comparison invalid;invalid.statistics=r.paired;invalid.confirmation=r.request;invalid.confirmation->confirmation_number=0;rejects([&]{w.retain_study(invalid);});check(w.report()==evidence && w.json()==original);
        auto corrupt=j;corrupt["request"]["confirmation_seeds"][0]=101;rejects([&]{analysis::parse_confirmation_report(corrupt.dump());});
        corrupt=j;corrupt["request"]["paired"]["candidates"][0]["definition"]["warehouse"]["forklifts"]=4;rejects([&]{analysis::parse_confirmation_report(corrupt.dump());});
        check(analysis::serialize_confirmation_csv(r,true,id).starts_with("study_id,source_study_id,selection_id,"));check(analysis::serialize_confirmation_csv(r,false,id).find(id)!=std::string::npos);return;
    }
    w.start_confirmation(c);wait(w);const auto confirmation_id=w.selected_study_id();check(w.studies().size()==2 && !w.report()->confirmation->additional_analysis);
    if(test=="deleted_reservations") {
        w.delete_study(confirmation_id,true);w.select_study(id);rejects([&]{(void)w.freeze_confirmation(1,plan());});auto p=plan();p.base_seed=10001;check(w.freeze_confirmation(1,p).additional_analysis);
        Temporary file;Project project(w);project.save(file.path);ScenarioWorkspace restored;Project q(restored);q.open(file.path);check(restored.studies().size()==1 && restored.studies()[0].reserved_confirmation_seeds.size()==1);rejects([&]{(void)restored.freeze_confirmation(1,plan());});return;
    }
    if(test=="lineage" || test=="repeated") {
        rejects([&]{w.delete_study(id,true);});w.select_study(id);rejects([&]{(void)w.freeze_confirmation(1,plan());});auto p=plan();p.base_seed=10001;auto again=w.freeze_confirmation(1,p);check(again.additional_analysis);w.start_confirmation(again);wait(w);check(w.studies().size()==3);w.delete_study(w.selected_study_id(),true);w.delete_study(confirmation_id,true);w.delete_study(id,true);check(w.studies().empty());return;
    }
    if(test=="persistence" || test=="recovery" || test=="migration" || test=="reorder" || test=="corrupt_project" || test=="future") {
        Temporary file;Project p(w);w.rename_study(id,"Exploration","Original evidence");w.rename_study(confirmation_id,"Independent check","Reserved seeds");w.study_view({1,9002,3,0});p.save(file.path);
        ScenarioWorkspace restored;Project q(restored);q.open(file.path);check(restored.selected_study_id()==confirmation_id && restored.studies()[0].id==id && restored.json()==w.json());check(restored.studies()[1].view.seed==9002 && restored.studies()[0].notes=="Original evidence");check(!restored.execution().progress().active());
        if(test=="recovery") {Temporary recovery;recovery.path.replace_extension(".recovery");w.rename_study(confirmation_id,"Unsaved name","Unsaved notes");check(p.recover_if_due(recovery.path,std::chrono::steady_clock::now()+std::chrono::seconds(61)));q.recover(recovery.path);check(q.dirty() && restored.studies()[1].title=="Unsaved name");w.delete_study(confirmation_id,true);check(p.recover_if_due(recovery.path,std::chrono::steady_clock::now()+std::chrono::seconds(122)));q.recover(recovery.path);check(restored.studies().size()==1);}
        auto j=Json::parse(p.serialize());
        if(test=="migration") {j["version"]=1;j["payload"]["report"]=j["payload"]["study_history"][0]["evidence"];j["payload"].erase("study_history");j["payload"].erase("selected_study");j["payload"].erase("next_study_id");seal(j);write(file.path,j);q.open(file.path);check(restored.studies().size()==1 && restored.json()==original);const auto legacy_id=restored.selected_study_id();q.open(file.path);check(restored.selected_study_id()==legacy_id);}
        if(test=="reorder") {std::reverse(j["payload"]["study_history"].begin(),j["payload"]["study_history"].end());seal(j);write(file.path,j);q.open(file.path);check(restored.selected_study_id()==confirmation_id && restored.studies()[0].id==confirmation_id);}
        if(test=="corrupt_project") {
            const auto before=restored.history_json();auto bad=j;bad["payload"]["study_history"][1]["evidence"]["confirmation"]["request"]["exploratory_seeds"][0]=999;seal(bad);write(file.path,bad);rejects([&]{q.open(file.path);});check(restored.history_json()==before);
            bad=j;bad["payload"]["study_history"][1]["id"]=id;seal(bad);write(file.path,bad);rejects([&]{q.open(file.path);});
            bad=j;bad["payload"]["study_history"].erase(0);seal(bad);write(file.path,bad);rejects([&]{q.open(file.path);});
            bad=j;bad["payload"]["study_history"][1]["bytes"]=max_study_bytes+1;seal(bad,false);write(file.path,bad);rejects([&]{q.open(file.path);});check(restored.history_json()==before);
        }
        if(test=="future") {j["version"]=3;write(file.path,j);rejects([&]{q.open(file.path);});}
        return;
    }
    throw std::invalid_argument("Unknown study test");
}
}
int main(int argc,char** argv)try {if(argc!=2)return 2;run(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
