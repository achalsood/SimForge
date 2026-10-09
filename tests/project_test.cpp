#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <simforge/warehouse/warehouse_input.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <set>
using namespace simforge;
using Json=nlohmann::json;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
template<class F> void rejects(F action) {bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid operation was accepted");}
struct Temp
{
    std::filesystem::path directory=std::filesystem::temp_directory_path()/("simforge-project-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directory(directory);}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    std::filesystem::path file(std::string name="test.simforge") const {return directory/name;}
};
scenario::ScenarioDefinition definition(std::string name="Original",unsigned forklifts=2)
{return {1,std::move(name),{10,600,42},scenario::WarehouseConfig{3,4,forklifts,10,30}};}
std::string read(const std::filesystem::path& path) {std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,std::string_view bytes) {std::ofstream out(path,std::ios::binary);out<<bytes;}
void seal(Json& j)
{
    const auto bytes=j["payload"].dump(); std::uint32_t crc=0xffffffff;
    for(unsigned char c:bytes) {crc^=c;for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320:0);}
    j["payload_size"]=bytes.size();j["crc32"]=~crc;
}
void finish(workspace::ScenarioWorkspace& w)
{w.view().seek(w.view().observation().horizon);}
std::uint64_t candidate(workspace::ScenarioWorkspace& w,std::string name="Alternative")
{w.edit(workspace::Field::Name,std::move(name));w.edit(workspace::Field::Forklifts,"3");return w.add_candidate("Saved notes");}
void wait(workspace::ScenarioWorkspace& w)
{
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(w.execution().progress().active()) {require(std::chrono::steady_clock::now()<end,"Worker timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(w.poll_experiment(),"No successful comparison");
}
void test(std::string name)
{
    Temp tmp;workspace::ScenarioWorkspace w(definition());workspace::Project project(w);project.rename("Warehouse project");
    workspace::ScenarioWorkspace restored(definition("Other"));workspace::Project loaded(restored);
    const auto path=tmp.file();
    if(name=="roundtrip" || name=="identity" || name=="new" || name=="save_as") {
        w.view().seek(1234);project.save(path);require(!project.dirty(),"Save did not clear modifications");
        loaded.open(path);require(loaded.info().id==project.info().id && loaded.info().name==project.info().name,"Identity lost");
        require(restored.view().runtime().capture_snapshot()==w.view().runtime().capture_snapshot(),"Operational state changed");
        if(name=="new") {const auto old=loaded.info().id;loaded.create(definition());require(old!=loaded.info().id && restored.view().observation().tick==0 && loaded.dirty(),"New project semantics");}
        if(name=="save_as") {const auto original=read(path);project.save(tmp.file("copy.simforge"),true);require(read(path)==original && project.info().path==tmp.file("copy.simforge"),"Save As damaged original");rejects([&]{project.save(path,true);});require(read(path)==original,"Save As overwrote original");}
        return;
    }
    if(name=="draft" || name=="invalid_draft" || name=="candidates" || name=="settings") {
        const auto first=candidate(w,"First");const auto second=candidate(w,"Second");
        w.edit_candidate(first,true);w.edit(workspace::Field::Workers,name=="invalid_draft"?"intentional invalid value":"7");
        auto prefs=project.preferences();prefs.selected_candidate=second;prefs.comparison_mode=workspace::ComparisonMode::Checkpoint;
        prefs.objective=analysis::Objective::MinimizeAverageWait;prefs.current_checkpoint=false;prefs.checkpoint_seconds="123";prefs.application_mode=2;
        prefs.notes="Editor notes";prefs.editor_tab=1;project.preferences(prefs);
        project.save(path);loaded.open(path);
        require(restored.draft()==w.draft() && restored.dirty()==w.dirty(),"Draft text or draft dirty state lost");
        require(restored.candidates().size()==2 && restored.candidates()[0].id==first && restored.candidates()[1].id==second &&
            restored.candidates()[0].notes=="Saved notes","Candidate identity/order/notes lost");
        require(loaded.preferences()==prefs,"Preferences lost");
        if(name=="invalid_draft") {require(!restored.validate_draft(),"Invalid draft became executable");rejects([&]{restored.run_draft();});}
        return;
    }
    if(name=="subtick") {project.save(path);w.view().set_speed(0.01);project.save();w.view().advance(std::chrono::nanoseconds(1));require(project.dirty(),"Arrival zero settlement not tracked");return;}
    if(name=="dirty") {
        project.save(path);require(!project.dirty(),"Clean project dirty");
        (void)w.view().present();(void)w.validate_draft();w.revert();rejects([&]{w.edit_candidate(999);});require(!project.dirty(),"Redraw or validation dirtied project");
        w.edit(workspace::Field::Workers,"8");require(project.dirty(),"Draft not tracked");project.save();
        auto c=candidate(w);require(project.dirty(),"Candidate not tracked");project.save();
        w.rename_candidate(c,"Renamed");require(project.dirty(),"Rename not tracked");project.save();
        w.view().seek(100);require(project.dirty(),"Clock not tracked");project.save();w.view().toggle_playback();require(project.dirty(),"Pause not tracked");project.save();
        w.view().set_speed(123);require(project.dirty(),"Speed not tracked");project.save();
        viewer::InputEvent zoom{viewer::InputKind::Wheel};zoom.wheel=2;zoom.position={0.5f,0.5f};w.view().input(zoom,{});require(project.dirty(),"Camera not tracked");project.save();
        (void)w.create_run(definition("Custom"));require(project.dirty(),"Run creation not tracked");return;
    }
    if(name=="runs" || name=="equivalence" || name=="clock" || name=="seek_reset" || name=="completed" || name=="commands" || name=="camera" || name=="limit") {
        w.view().seek(1200);w.view().set_speed(123);const auto one=w.create_run(definition("Three",3));w.view().seek(3000);
        const auto two=w.create_run(definition("One",1));w.view().seek(900);w.view().set_speed(0.01);w.view().toggle_playback();w.view().advance(std::chrono::nanoseconds(123456789));
        if(name=="commands") {
            warehouse::WarehouseSimulation runtime(definition("One",1));runtime.advance_to(300);runtime.set_resource_capacity(warehouse::ResourceType::Worker,3);runtime.advance_to(500);
            runtime.set_forklift_capacity(3);runtime.advance_to(900);
            auto clock=runtime.clock().capture_snapshot();clock.time_scale=0.01;clock.paused=true;
            w.view().restore_session(runtime.capture_snapshot(),clock,w.view().orbit_camera().pose());
        }
        if(name=="completed") finish(w);
        if(name=="limit") for(int i=2;i<8;++i) w.create_run(definition("More "+std::to_string(i),i+1));
        viewer::InputEvent zoom{viewer::InputKind::Wheel};zoom.wheel=3;zoom.position={0.5f,0.5f};w.view().input(zoom,{});
        w.view().select(viewer::VisualKey{viewer::VisualCategory::LoadingBay,0});
        const auto saved=w.view().runtime().capture_snapshot(), baseline=w.baseline_viewer().runtime().capture_snapshot();
        const auto clock=w.view().runtime().clock().capture_snapshot();const auto camera=w.view().orbit_camera().pose();
        project.save(path);loaded.open(path);
        require(restored.active_run_id()==w.active_run_id() && restored.runs().size()==w.runs().size(),"Run selection/count lost");
        require(restored.view().runtime().capture_snapshot()==saved && restored.baseline_viewer().runtime().capture_snapshot()==baseline,"Independent progress lost");
        require(restored.view().orbit_camera().pose()==camera && restored.view().selection()==w.view().selection(),"Camera or selection lost");
        require(restored.view().runtime().clock().capture_snapshot().remainder==clock.remainder &&
            restored.view().runtime().clock().paused()==clock.paused && restored.view().speed()==w.view().speed(),"Clock playback state lost");
        if(name=="limit") {rejects([&]{restored.create_run(definition("Ninth"));});return;}
        if(name=="seek_reset" || name=="commands") {
            restored.view().seek(100);restored.view().seek(saved.current_tick);require(restored.view().runtime().capture_snapshot()==saved,"Seek did not preserve recorded history");
            // Save while before a historical command, then reopen and cross it.
            restored.view().seek(100);loaded.save();workspace::ScenarioWorkspace again(definition());workspace::Project reopened(again);reopened.open(path);
            again.view().seek(saved.current_tick);require(again.view().runtime().capture_snapshot()==saved,"Future command timeline lost across save");
            restored.view().reset();require(restored.view().observation().tick==0 && restored.view().runtime().clock().paused(),"Reset failed");restored.view().seek(saved.current_tick);
        }
        finish(w);finish(restored);require(w.view().runtime().capture_snapshot()==restored.view().runtime().capture_snapshot(),"Complete deterministic continuation diverged");
        require(w.view().observation().metrics==restored.view().observation().metrics,"Final metrics diverged");
        restored.select_run(one);require(restored.view().observation().tick==3000,"Inactive run advanced");restored.select_run(0);require(restored.view().runtime().capture_snapshot()==baseline,"Inactive baseline advanced");
        require(two>one,"IDs out of order");if(name!="limit") {restored.create_run(definition("Next"));require(restored.view().speed()==300,"Workspace default speed changed after restoration");}return;
    }
    if(name=="report" || name=="checkpoint" || name=="historical" || name=="preview" || name=="async") {
        const auto cid=candidate(w);
        const auto mode=name=="checkpoint"?workspace::ComparisonMode::Checkpoint:workspace::ComparisonMode::Initial;
        w.start_experiment(mode,analysis::Objective::MinimizeWaiting,mode==workspace::ComparisonMode::Checkpoint?std::optional<std::uint64_t>{1000}:std::nullopt);wait(w);
        const auto report=w.json();
        if(name=="historical") {w.rename_candidate(cid,"Edited after submission");w.edit(workspace::Field::Forklifts,"4");}
        if(name=="preview") w.view_result("Alternative");
        if(name=="async") w.start_experiment(mode,analysis::Objective::MaximizeCompleted);
        project.save(path);loaded.open(path);
        require(restored.json()==report && restored.report()->candidate_ids[0]==cid,"Report or provenance lost");require(restored.report()->execution_ms==w.report()->execution_ms && restored.report()->report_ms==w.report()->report_ms,"Report timings lost");
        if(name=="historical") require(restored.candidates()[0].definition.name=="Edited after submission" && restored.report()->definitions[0].name=="Alternative","Historical inputs rebound");
        if(name=="preview") require(restored.view().result_preview() && restored.active_run_id()==0 && restored.view().observation().metrics==restored.report()->candidates[0].metrics,"Preview became playable or lost metrics");
        if(name=="preview") {
            restored.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);loaded.save();
            workspace::ScenarioWorkspace later;workspace::Project reopened(later);reopened.open(path);
            require(!later.view().result_preview() && later.message().find("superseded")!=std::string::npos,"Superseded preview rebound to new report");
        }
        require(!restored.execution().progress().active(),"Worker restored");const auto prior=restored.report()->request_id;
        restored.start_experiment(mode,analysis::Objective::MinimizeWaiting,mode==workspace::ComparisonMode::Checkpoint?std::optional<std::uint64_t>{1000}:std::nullopt);
        require(restored.execution().progress().request_id>prior,"Experiment identity reused");wait(restored);return;
    }
    if(name=="failure_worker") {
        candidate(w);w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);const auto prior=w.json();
        w.start_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MinimizeWaiting);write(path,"broken");
        rejects([&]{project.open(path);});require(w.json()==prior && w.candidates().size()==1,"Failed Open lost report/inputs");
        w.execution().cancel();w.execution().shutdown();return;
    }
    if(name=="recovery_failure") {
        const auto recovery=tmp.file("session.recovery");project.save(path);const auto original=read(path);w.edit(workspace::Field::Workers,"3");
        rejects([&]{project.recover_if_due(tmp.file("missing/session.recovery"),std::chrono::steady_clock::now()+std::chrono::seconds(61));});
        require(project.dirty() && read(path)==original,"Recovery failure damaged explicit save");
        project.recover_if_due(recovery,std::chrono::steady_clock::now()+std::chrono::seconds(122));
        loaded.discard_own_recovery(recovery);require(workspace::Project::recovery_available(recovery),"Another project's recovery was removed");
        project.discard_own_recovery(recovery);require(!workspace::Project::recovery_available(recovery),"Own recovery not removed");return;
    }
    if(name=="failure_write") {
        project.save(path);const auto original=read(path);w.edit(workspace::Field::Workers,"8");
        for(const auto* phase:{"before_write","after_write","before_replace"}) {
            rejects([&]{project.save({},false,[&](std::string_view at){if(at==phase) throw std::runtime_error("Injected failure");});});
            require(read(path)==original && project.dirty(),"Failed save damaged prior file or modifications");
            require(std::distance(std::filesystem::directory_iterator(tmp.directory),std::filesystem::directory_iterator{})==1,"Temporary file leaked");
        }
        rejects([&]{project.save(tmp.file("missing/file.simforge"),true);});return;
    }
    if(name=="recovery") {
        const auto recovery=tmp.file("session.recovery");project.save(path);const auto original=read(path);
        const auto later=std::chrono::steady_clock::now()+std::chrono::seconds(61);
        require(!project.recover_if_due(recovery,later),"Clean project autosaved");
        w.edit(workspace::Field::Workers,"invalid");require(project.recover_if_due(recovery,later+std::chrono::seconds(61)),"Dirty project not recovered");
        require(read(path)==original && workspace::Project::recovery_available(recovery),"Recovery overwrote primary");
        require(!project.recover_if_due(recovery,later+std::chrono::seconds(122)),"Unchanged project resaved recovery");
        loaded.recover(recovery);require(loaded.dirty() && loaded.info().path.empty() && restored.draft()==w.draft(),"Recovery state lost");
        write(recovery,"corrupt");const auto before=restored.draft();rejects([&]{loaded.recover(recovery);});require(before==restored.draft(),"Rejected recovery damaged workspace");
        workspace::Project::discard_recovery(recovery);require(!workspace::Project::recovery_available(recovery) && read(path)==original,"Recovery discard damaged primary");
        rejects([&]{workspace::Project::discard_recovery(path);});return;
    }
    if(name=="paths") {
#ifdef _WIN32
        rejects([&]{project.save(tmp.file("NUL.simforge"));});rejects([&]{project.save(tmp.file("COM1.simforge"));});
#endif
        rejects([&]{project.save({});});rejects([&]{project.save(tmp.file("wrong.json"));});rejects([&]{project.save(tmp.file("missing/project.simforge"));});
        project.save(path);w.edit(workspace::Field::Workers,"3");rejects([&]{project.save(tmp.file("another.simforge"));});require(project.dirty(),"Failed path cleared dirty state");const auto unicode=tmp.directory/std::filesystem::u8path(u8"warehouse-\u00e9.simforge");project.save(unicode,true);loaded.open(unicode);require(loaded.info().id==project.info().id,"Unicode project path failed");return;
    }
    // Each malformed input is resealed so field validation is exercised independently of integrity.
    candidate(w);w.create_run(definition("Run"));project.save(path);auto j=Json::parse(read(path));
    if(name=="version") j["version"]=2;
    else if(name=="truncated") {write(path,read(path).substr(0,80));}
    else if(name=="corrupt") j["payload"]["draft"][1]="999";
    else if(name=="oversized") {
        const auto big=tmp.file("large.simforge");std::ofstream out(big,std::ios::binary);out.seekp(workspace::project_max_bytes);out.put('x');out.close();
        rejects([&]{loaded.open(big);});return;
    }
    else if(name=="types") {j["payload"]["runs"][0]["clock"]["paused"]=1;seal(j);}
    else if(name=="missing") {j["payload"].erase("draft");seal(j);}
    else if(name=="duplicate") {j["payload"]["candidates"].push_back(j["payload"]["candidates"][0]);seal(j);}
    else if(name=="snapshot") {auto& hex=j["payload"]["runs"][0]["snapshot"];auto bytes=hex.get<std::string>();bytes[40]=bytes[40]=='a'?'b':'a';hex=bytes;seal(j);}
    else if(name=="inconsistent_clock") {j["payload"]["runs"][0]["clock"]["tick"]=999;seal(j);}
    else if(name=="enum") {j["payload"]["preferences"]["mode"]=99;seal(j);}
    else if(name=="provenance") {
        w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);project.save();j=Json::parse(read(path));
        j["payload"]["report"]["candidate_ids"][0]=0;seal(j);
    }
    else if(name=="report_values" || name=="report_definitions") {
        w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);project.save();j=Json::parse(read(path));
        if(name=="report_values") j["payload"]["report"]["executions"][0]["baseline"]["metrics"]["completed"]=1;
        else j["payload"]["report"]["definitions"][0]["warehouse"]["forklifts"]=99;
        seal(j);
    }
    else if(name=="future_commands") {j["payload"]["runs"][0]["commands"].push_back(Json::array({1000,99999,2,2}));seal(j);}
    else if(name=="duplicate_runs") {j["payload"]["runs"].push_back(j["payload"]["runs"][0]);seal(j);}
    else if(name=="run_count") {for(int i=0;i<8;++i) j["payload"]["runs"].push_back(j["payload"]["runs"][0]);seal(j);}
    else if(name=="strings") {j["payload"]["draft"][0]=std::string(4097,'x');seal(j);}
    else if(name=="nested") {Json child=1;for(int i=0;i<40;++i) child=Json::array({child});j["payload"]["draft"][0]=child;seal(j);}
    else if(name=="camera_invalid") {j["payload"]["runs"][0]["camera"][2]=-1;seal(j);}
    else if(name=="engine") j["engine"]="future-engine";
    else if(name=="isolation") {j["payload"]["active_run"]=123;seal(j);}
    else throw std::runtime_error("Unknown case");
    if(name!="truncated") write(path,j.dump());
    const auto baseline=restored.view().runtime().capture_snapshot();restored.edit(workspace::Field::Workers,"7");const auto draft=restored.draft();
    rejects([&]{loaded.open(path);});
    require(restored.view().runtime().capture_snapshot()==baseline && restored.draft()==draft && loaded.info().path.empty(),"Failed load damaged current project");
}
int main(int argc,char** argv)
{try{require(argc==2,"Pass a test case");test(argv[1]);std::cout<<"Project "<<argv[1]<<" passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
