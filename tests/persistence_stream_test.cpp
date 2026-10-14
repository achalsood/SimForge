#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include "../engine/src/workspace/persistence_stream.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
using namespace simforge;
using namespace simforge::workspace;
using Json=nlohmann::json;
namespace simforge::workspace {std::string comparison_storage_json(const Comparison&);}
namespace {
void check(bool value,std::source_location line=std::source_location::current()){if(!value)throw std::runtime_error("Persistence check failed at line "+std::to_string(line.line()));}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Temporary {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/("simforge-stream-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){std::filesystem::create_directory(directory);}
    ~Temporary(){std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    std::filesystem::path file(std::string_view name="test.simforge") const{return directory/name;}
};
std::string read(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void write(const std::filesystem::path& p,std::string_view bytes){std::ofstream out(p,std::ios::binary);out<<bytes;}
void seal(Json& j){for(auto& study:j["payload"]["study_history"])study["bytes"]=study["evidence"].dump().size();auto bytes=j["payload"].dump();detail::Crc32 crc;crc.append(bytes);j["crc32"]=crc.value();j["payload_size"]=bytes.size();}
using Ordered=nlohmann::ordered_json;
Ordered reverse_keys(const Json& j){if(j.is_object()){auto result=Ordered::object();for(auto i=j.rbegin();i!=j.rend();++i)result[i.key()]=reverse_keys(i.value());return result;}if(j.is_array()){auto result=Ordered::array();for(const auto& value:j)result.push_back(reverse_keys(value));return result;}return Ordered(j);}
scenario::ScenarioDefinition baseline(){scenario::ScenarioDefinition d{1,"Streaming baseline",{10,120,42},scenario::WarehouseConfig{6,2,1,0,0}};auto& w=std::get<scenario::WarehouseConfig>(d.domain).workload;w.arrivals=scenario::ArrivalMode::Uniform;w.arrival_min_seconds=1;w.arrival_max_seconds=3;w.processing=scenario::ProcessingMode::Uniform;w.processing_min_seconds=2;w.processing_max_seconds=12;return d;}
void wait_experiment(ScenarioWorkspace& w){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(w.execution().progress().active()){check(std::chrono::steady_clock::now()<until);std::this_thread::yield();}check(w.poll_experiment());}
ScenarioWorkspace all_types(){ScenarioWorkspace w(baseline());w.edit(Field::Name,"Alternative");w.edit(Field::Forklifts,"2");w.add_candidate("Immutable inputs");w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
    analysis::ReplicationPlan plan;plan.count=3;plan.base_seed=101;w.start_statistical_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,plan);wait_experiment(w);
    analysis::ReplicationPlan confirmation=plan;confirmation.base_seed=9001;w.start_confirmation(w.freeze_confirmation(1,confirmation));wait_experiment(w);
    analysis::SensitivityRequest s;s.source=baseline();s.values={1,2,3};s.multi_seed=true;s.plan=plan;w.start_sensitivity(s);wait_experiment(w);
    analysis::InteractionRequest g;g.source=baseline();g.x_values={1,2,3};g.y_values={1,2,3};g.multi_seed=true;g.plan=plan;w.start_interaction(g);wait_experiment(w);return w;
}
ProjectCompletion wait(Project& p){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(true){if(auto completed=p.poll())return *completed;check(std::chrono::steady_clock::now()<until);std::this_thread::yield();}}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool reached=false,released=false;std::string phase;
    explicit Gate(std::string p):phase(std::move(p)){}
    void hook(std::string_view p){if(p!=phase)return;std::unique_lock lock(mutex);if(reached)return;reached=true;cv.notify_all();cv.wait(lock,[&]{return released;});}
    void arrive(){std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(20),[&]{return reached;}));}
    void release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
void run(std::string name){
    if(name=="crc") {const std::string bytes="123456789";detail::Crc32 crc;crc.append(bytes);check(crc.value()==0xcbf43926);for(std::size_t i=0;i<=bytes.size();++i){detail::Digest digest;digest.append(std::string_view(bytes).substr(0,i));digest.append(std::string_view(bytes).substr(i));check(digest.crc==crc.value() && digest.bytes==bytes.size());}return;}
    Temporary tmp;auto w=all_types();Project p(w);if(name=="canonical")p.rename("Quoted \"name\" / caf\xc3\xa9");const auto path=tmp.file();p.save(path);const auto original=read(path);const auto evidence=w.studies().front().evidence;
    if(name=="roundtrip" || name=="canonical") {
        auto j=Json::parse(original);check(j["version"]==2);auto bytes=j["payload"].dump();detail::Crc32 crc;crc.append(bytes);check(j["crc32"]==crc.value() && j["payload_size"]==bytes.size());
        if(name=="canonical")write(path,reverse_keys(j).dump(2,' ',true));
        ScenarioWorkspace loaded;Project q(loaded);q.start_open(path);check(wait(q).success);check(loaded.studies().size()==5);
        for(std::size_t i=0;i<w.studies().size();++i){check(loaded.studies()[i].id==w.studies()[i].id);check(comparison_storage_json(*loaded.studies()[i].evidence)==comparison_storage_json(*w.studies()[i].evidence));check(loaded.studies()[i].view==w.studies()[i].view);}
        check(loaded.view().runtime().capture_snapshot()==w.view().runtime().capture_snapshot());check(!q.dirty());return;
    }
    if(name=="revision" || name=="ownership" || name=="conflicts") {
        Gate gate("study_serialization");p.start_save({},false,[&](auto phase){gate.hook(phase);});gate.arrive();check(p.busy());
        if(name=="conflicts"){rejects([&]{p.start_open(path);});rejects([&]{p.save();});rejects([&]{p.create(baseline());});rejects([&]{(void)p.serialize();});}
        if(name=="ownership"){w.delete_study(w.studies().back().id,true);w.rename_study(w.studies().front().id,"Later","Later notes");}
        else {w.edit(Field::Workers,"9");w.view().seek(11);p.rename("Later project");}
        gate.release();check(wait(p).success && p.dirty());ScenarioWorkspace loaded;Project q(loaded);q.open(path);check(loaded.studies().size()==5 && loaded.studies().front().title!="Later");check(q.info().name!="Later project");check(loaded.view().observation().tick==0);check(evidence==w.studies().front().evidence);return;
    }
    if(name.starts_with("cancel_save_")) {
        const auto phase=name.substr(12);w.edit(Field::Workers,"9");Gate gate(phase);p.start_save({},false,[&](auto stage){gate.hook(stage);});gate.arrive();check(p.cancel());gate.release();auto result=wait(p);check(result.cancelled && !result.success && read(path)==original && p.dirty());check(std::distance(std::filesystem::directory_iterator(tmp.directory),std::filesystem::directory_iterator{})==1);return;
    }
    if(name.starts_with("cancel_open_")) {
        const auto phase=name.substr(12);Gate gate(phase);p.start_open(path,false,[&](auto stage){gate.hook(stage);});gate.arrive();const auto snapshot=w.view().runtime().capture_snapshot();check(p.cancel());gate.release();auto result=wait(p);check(result.cancelled && !result.success && w.studies().front().evidence==evidence && w.view().runtime().capture_snapshot()==snapshot && !p.dirty());return;
    }
    if(name=="late_cancel"){p.start_save();while(p.progress().cancellable)std::this_thread::yield();check(!p.cancel());check(wait(p).success && !p.dirty());return;}
    if(name=="save_as_race"){const auto destination=tmp.file("race.simforge");p.start_save(destination,true,[&](auto phase){if(phase=="before_replace")write(destination,"Created by another writer");});check(!wait(p).success && read(path)==original && read(destination)=="Created by another writer" && p.info().path==std::filesystem::absolute(path));return;}
    if(name=="shutdown_open"){Gate gate("study_reconstruction");p.start_open(path,false,[&](auto phase){gate.hook(phase);});gate.arrive();check(p.cancel());gate.release();p.shutdown();check(!p.busy() && w.studies().front().evidence==evidence && !p.dirty());return;}
    if(name=="analysis_save"){Gate gate("Evaluating candidate");w.execution().start(w.capture_request(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),[&](auto phase,auto,auto,auto){gate.hook(phase);});gate.arrive();p.start_save();check(wait(p).success && w.execution().progress().active());w.execution().cancel();gate.release();w.execution().shutdown();check(w.studies().size()==5 && !p.dirty());return;}
    if(name=="locked_save" || name=="locked_open") {
#ifdef _WIN32
        const auto handle=CreateFileW(path.c_str(),GENERIC_READ,name=="locked_save"?FILE_SHARE_READ:0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE);
        struct Handle {HANDLE value;~Handle(){CloseHandle(value);}}owned{handle};
        if(name=="locked_save")p.start_save();else p.start_open(path);check(!wait(p).success && w.studies().front().evidence==evidence && !p.dirty());
#endif
        return;
    }
    if(name=="shutdown") {auto isolated=all_types();auto worker=std::make_unique<Project>(isolated);worker->start_save(tmp.file("shutdown.simforge"));worker.reset();check(w.studies().front().evidence==evidence);return;}
    if(name=="recovery") {w.edit(Field::Workers,"9");auto slot=tmp.file("test.recovery");check(p.start_recovery_if_due(slot,std::chrono::steady_clock::now()+std::chrono::seconds(61)));check(wait(p).success && p.dirty());ScenarioWorkspace loaded;Project q(loaded);q.start_open(slot,true);check(wait(q).success && q.dirty() && q.info().path.empty() && loaded.draft().values[2]=="9");return;}
    if(name.starts_with("fault_")){const auto phase=name.substr(6);p.start_save({},false,[&](auto stage){if(stage==phase)throw std::runtime_error("Injected persistence failure");});auto result=wait(p);check(!result.success && !result.cancelled && read(path)==original && !p.dirty());check(std::distance(std::filesystem::directory_iterator(tmp.directory),std::filesystem::directory_iterator{})==1);return;}
    auto j=Json::parse(original);
    if(name=="truncated")write(path,original.substr(0,original.size()/2));
    else if(name=="trailing")write(path,original+" true");
    else if(name=="duplicate") {auto bytes=original;bytes.insert(1,"\"version\":2,");write(path,bytes);}
    else if(name=="nul") {auto bytes=original;bytes.insert(bytes.size()/2,1,'\0');write(path,bytes);}
    else if(name=="depth"){std::string nested(40,'[');nested+='0';nested+=std::string(40,']');write(path,nested);}
    else if(name=="oversized_project"){std::ofstream file(path,std::ios::binary);file.seekp(project_max_bytes);file.put(' ');}
    else if(name=="nested_runs"){j["payload"]["runs"]=Json::array({Json::array()});auto canonical=j["payload"];canonical["runs"]=Json::array();const auto bytes=canonical.dump();detail::Crc32 crc;crc.append(bytes);j["crc32"]=crc.value();j["payload_size"]=bytes.size();write(path,j.dump());}
    else {
        if(name=="integrity")j["crc32"]=0;
        else if(name=="version")j["version"]=3;
        else if(name=="type")j["payload"]["study_history"][0]["type"]="unknown";
        else if(name=="history_id")j["payload"]["next_study_id"]=1;
        else if(name=="candidate_id"){j["payload"]["next_candidate_id"]=1;j["payload"]["candidates"]=Json::array();}
        else if(name=="oversized_study")j["payload"]["study_history"][0]["bytes"]=max_study_bytes+1;
        else if(name=="lineage")j["payload"]["study_history"][2]["evidence"]["confirmation"]["request"]["source_study_id"]="study-999";
        else if(name=="scenario")j["payload"]["baseline"]["definition"]["schema_version"]=99;
        else if(name=="seeds")j["payload"]["study_history"][1]["evidence"]["statistics"]["replications"][1]["seed"]=101;
        else if(name=="report")j["payload"]["study_history"][4]["evidence"]["interaction"]["best_cells"]=Json::array({999});
        else if(name=="io"){p.start_open(tmp.file("missing.simforge"));check(!wait(p).success && !p.dirty() && w.studies().front().evidence==evidence);return;}
        else if(name=="allocation"){for(const auto stage:{"workspace_reconstruction","publication_preparation"}){p.start_open(path,false,[&](auto phase){if(phase==stage)throw std::bad_alloc{};});check(!wait(p).success && w.studies().front().evidence==evidence && !p.dirty());}return;}
        else throw std::runtime_error("Unknown persistence case");
        if(name!="integrity"){seal(j);if(name=="oversized_study"){j["payload"]["study_history"][0]["bytes"]=max_study_bytes+1;const auto payload=j["payload"].dump();detail::Crc32 crc;crc.append(payload);j["crc32"]=crc.value();j["payload_size"]=payload.size();}}write(path,j.dump());
    }
    const auto marker=Json::parse(p.serialize())["payload"];const auto saved_time=p.info().saved;p.start_open(path);auto result=wait(p);check(!result.success && !result.error.empty());check(w.studies().front().evidence==evidence && !p.dirty() && p.info().saved==saved_time);auto after=Json::parse(p.serialize())["payload"];after["identity"]["saved"]=marker["identity"]["saved"];check(after==marker);
}
}
int main(int argc,char** argv)try{if(argc!=2)return 2;run(argv[1]);return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
