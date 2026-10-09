#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/viewer/presentation.hpp>
#include <fstream>
#include <iostream>
#include <random>
#include <source_location>
#include <algorithm>

using namespace simforge;
using namespace simforge::workspace;
namespace
{
void check(bool v, std::source_location p=std::source_location::current()) { if(!v) throw std::runtime_error("Workspace check failed: "+std::to_string(p.line())); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw); }
scenario::ScenarioDefinition base() { return scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples/warehouse-baseline.json"); }
std::uint64_t add(ScenarioWorkspace& w, const std::string& name, std::uint32_t forklifts=3)
{ w.revert(true); w.edit(Field::Name,name); w.edit(Field::Forklifts,std::to_string(forklifts)); return w.add_candidate(); }
std::string read(const std::filesystem::path& path) { std::ifstream f{path,std::ios::binary}; return {std::istreambuf_iterator<char>{f},{}}; }
struct Temp
{
    const std::filesystem::path parent=std::filesystem::canonical(std::filesystem::current_path());
    const std::filesystem::path root=parent/("workspace-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { if(!std::filesystem::create_directory(root)) throw std::runtime_error("Temporary workspace test directory already exists"); }
    ~Temp() { if(root.parent_path()==parent) { std::error_code error; std::filesystem::remove_all(root,error); } }
};
void editing(std::string_view c)
{
    ScenarioWorkspace w{base()}; const auto saved=w.view().runtime().capture_snapshot();
    if(c=="empty") { ScenarioWorkspace empty; check(empty.status()==Status::Empty); rejects([&]{(void)empty.view();}); }
    else if(c=="valid") { w.edit(Field::Forklifts,"0"); check(w.validate_draft() && w.dirty()); w.run_draft(); w.view().seek(w.view().observation().horizon); check(w.view().observation().metrics.completed==0); }
    else if(c=="invalid") for(const auto* text:{"-1","1.5","","100001","18446744073709551616"}) { w.edit(Field::Workers,text); check(!w.validate_draft() && w.errors().front().field=="Workers"); rejects([&]{w.run_draft();}); }
    else if(c=="fields") { w.edit(Field::Name,""); check(!w.validate_draft()); w.revert(true); w.edit(Field::TickRate,"0"); check(!w.validate_draft()); w.revert(true); w.edit(Field::Arrival,"0"); check(!w.validate_draft()); }
    else if(c=="revert") { w.edit(Field::Bays,"9"); rejects([&]{w.revert();}); w.revert(true); check(!w.dirty() && w.draft().values[1]=="3"); }
    else if(c=="save") { Temp t; w.edit(Field::Name,"Saved candidate"); w.save_draft(t.root/"scenario.json"); check(!w.dirty()); auto loaded=scenario::load_scenario(t.root/"scenario.json"); check(loaded.name=="Saved candidate"); }
    else if(c=="file_safety") { Temp t; w.edit(Field::Forklifts,"4"); w.save_draft(t.root/"scenario.json"); w.edit(Field::Forklifts,"5"); rejects([&]{w.save_draft(t.root/"scenario.json");}); rejects([&]{w.open(t.root/"absent.json",true);}); check(w.dirty() && w.draft().values[3]=="5"); rejects([&]{w.load(base());}); }
    else if(c=="load") { add(w,"A"); rejects([&]{w.load(base());}); const auto before=w.candidates()[0].id; w.load(base(),true); check(w.candidates().empty()); check(add(w,"New")>before); }
    else if(c=="run") { w.edit(Field::Duration,"100"); w.edit(Field::TickRate,"30"); w.run_draft(); check(w.view().observation().tick_rate==30 && w.view().observation().horizon==3000); w.return_to_baseline(); }
    check(w.baseline_viewer().runtime().capture_snapshot()==saved);
}
void management(std::string_view c)
{
    ScenarioWorkspace w{base()}; const auto identity=scenario::serialize_scenario(w.baseline()); const auto a=add(w,"A");
    if(c=="duplicate") { auto b=w.duplicate_candidate(a,"B"); check(b!=a && w.candidates().size()==2); rejects([&]{w.rename_candidate(b,"A");}); rejects([&]{w.rename_candidate(b,"Baseline");}); }
    else if(c=="rename") { w.rename_candidate(a,"Renamed"); check(w.candidates()[0].definition.name=="Renamed"); }
    else if(c=="remove") { w.remove_candidate(a); check(w.candidates().empty()); auto b=add(w,"B"); check(b>a); rejects([&]{w.remove_candidate(a);}); }
    else if(c=="restore") { w.restore_candidate(a); check(std::get<scenario::WarehouseConfig>(w.candidates()[0].definition.domain).forklifts==2); }
    else if(c=="update") { w.edit_candidate(a); w.edit(Field::Forklifts,"4"); w.update_candidate(a,"Measured alternative"); check(w.candidates()[0].notes=="Measured alternative"); check(std::get<scenario::WarehouseConfig>(w.candidates()[0].definition.domain).forklifts==4); }
    else if(c=="limit") { for(int i=1;i<50;++i) add(w,"Candidate "+std::to_string(i)); rejects([&]{add(w,"Too many");}); check(w.candidates().size()==50); }
    else if(c=="candidate_invalid") { w.edit(Field::Name,"A"); rejects([&]{w.add_candidate();}); w.edit(Field::Name,"Invalid"); w.edit(Field::Bays,"999999"); rejects([&]{w.add_candidate();}); check(w.candidates().size()==1); }
    check(scenario::serialize_scenario(w.baseline())==identity);
}
void comparison(std::string_view c)
{
    ScenarioWorkspace w{base()}; w.view().seek(7200*60); const auto saved=w.view().runtime().capture_snapshot();
    auto a=add(w,"Add forklift"); (void)a; add(w,"Same",2);
    if(c=="checkpoint") { w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted); check(w.report()->candidates[0].metrics.completed==87); check(w.report()->baseline.metrics.completed==63); check(w.report()->executions[0].request.checkpoint==saved); }
    else if(c=="initial") { w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); check(w.report()->candidates[0].metrics.completed>w.report()->baseline.metrics.completed); check(w.report()->candidates[1].metrics==w.report()->baseline.metrics); }
    else if(c=="intervals") { w.edit_candidate(a); w.edit(Field::Arrival,"600"); w.edit(Field::Processing,"300"); w.update_candidate(a); w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); check(w.report()->candidates[0].metrics.arrived==48); rejects([&]{w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted);}); }
    else if(c=="compatibility") { w.edit_candidate(a); w.edit(Field::Duration,"14400"); w.update_candidate(a); rejects([&]{w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);}); rejects([&]{w.compare(static_cast<ComparisonMode>(99),analysis::Objective::MaximizeCompleted);}); check(checkpoint_tick(base(),"7200")==432000); for(const auto* text:{"-1","1.5","","28801","18446744073709551616"}) rejects([&]{(void)checkpoint_tick(base(),text);}); }
    else if(c=="reductions") { w.edit_candidate(a); w.edit(Field::Forklifts,"0"); w.update_candidate(a); rejects([&]{w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted);}); }
    else if(c=="determinism") { w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); const auto json=w.json(); w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); check(w.json()==json); }
    else if(c=="order") { ScenarioWorkspace reversed{base()}; add(reversed,"Same",2); add(reversed,"Add forklift"); w.compare(ComparisonMode::Initial,analysis::Objective::MinimizeWaiting); reversed.compare(ComparisonMode::Initial,analysis::Objective::MinimizeWaiting); check(w.json()==reversed.json()); }
    else if(c=="ranking") { w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); check(w.report()->ranking.front()=="Add forklift"); check(w.report()->candidates[0].delta.completed_percent && *w.report()->candidates[0].delta.completed_percent>0); check(std::string_view{change_label(1,true)}=="Improved" && std::string_view{change_label(-1,true)}=="Regressed" && std::string_view{change_label(-1,false)}=="Improved" && std::string_view{change_label(0,true)}=="No change" && std::string_view{change_label({},true)}=="N/A"); w.remove_candidate(a); w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); check(!w.report()->warnings.empty()); }
    else if(c=="bottleneck") { w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted); check(w.report()->bottlenecks[2].result->delta.completed==24); check(w.report()->bottlenecks[2].conclusion==analysis::BottleneckConclusion::LikelyBottleneck); }
    else if(c=="undefined") { w.view().reset(); w.revert(true); w.edit(Field::Forklifts,"0"); w.run_draft(); auto no=base(); std::get<scenario::WarehouseConfig>(no.domain).forklifts=0; ScenarioWorkspace blocked{no}; add(blocked,"Same",0); blocked.compare(ComparisonMode::Initial,analysis::Objective::MinimizeAverageWait); check(blocked.report()->ranking.empty() && !blocked.report()->baseline.metrics.average_wait_seconds); for(const auto& e:blocked.report()->bottlenecks) check(e.conclusion==analysis::BottleneckConclusion::Inconclusive); }
    else if(c=="checkpoint_zero") { ScenarioWorkspace zero{base()}; const auto initial=zero.view().runtime().capture_snapshot(); add(zero,"Forklift"); zero.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted); check(zero.baseline_viewer().runtime().capture_snapshot()==initial); }
    else if(c=="cache") { w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); w.edit_candidate(a); w.edit(Field::Forklifts,"4"); check(w.report().has_value()); w.update_candidate(a); check(!w.report()); }
    else throw std::invalid_argument("Unknown workspace test case");
    if(c!="undefined") check(w.baseline_viewer().runtime().capture_snapshot()==saved);
}
void visual_switch()
{
    ScenarioWorkspace w{base()}; w.view().seek(7200*60); w.view().select(viewer::VisualKey{viewer::VisualCategory::Worker,1});
    auto saved=w.view().runtime().capture_snapshot(); const auto camera=w.view().orbit_camera().camera().position; add(w,"Forklift");
    for(auto mode:{ComparisonMode::Initial,ComparisonMode::Checkpoint})
    {
        w.compare(mode,analysis::Objective::MaximizeCompleted); const auto count=w.view().present().objects.size();
        w.view_result("Forklift"); check(w.view().result_preview() && !w.view().selection()); check(w.view().observation().metrics==w.report()->candidates[0].metrics);
        check(w.view().present().objects.size()!=count); rejects([&]{w.view().seek(0);}); rejects([&]{w.view().reset();});
        w.return_to_baseline(); check(w.view().runtime().capture_snapshot()==saved && w.view().selection()); check(w.view().orbit_camera().camera().position==camera);
    }
    w.view_result("Forklift"); w.restore_candidate(w.candidates().front().id);
    check(!w.report() && !w.viewing_baseline()); w.return_to_baseline(); check(w.view().runtime().capture_snapshot()==saved);
    warehouse::WarehouseSimulation completed{base()}; completed.run(); auto incompatible=base(); incompatible.simulation.tick_rate=30;
    rejects([&]{ viewer::ViewerController invalid{incompatible,completed.capture_snapshot()}; });
}
void exports(std::string_view c)
{
    Temp t; ScenarioWorkspace w{base()}; add(w,"Forklift");
    for(auto mode:{ComparisonMode::Initial,ComparisonMode::Checkpoint})
    {
        w.compare(mode,analysis::Objective::MaximizeCompleted,mode==ComparisonMode::Checkpoint?std::optional<std::uint64_t>{432000}:std::nullopt);
        const auto suffix=mode==ComparisonMode::Initial?"initial":"checkpoint"; const auto path=t.root/(std::string{suffix}+".json");
        w.export_json(path); ScenarioWorkspace::verify_export(read(path)); w.export_csv(t.root/(std::string{suffix}+".csv")); check(read(t.root/(std::string{suffix}+".csv"))==w.csv());
        if(c=="export_failure") { const auto before=w.json(); rejects([&]{w.export_json(path);}); check(w.status()==Status::ExportFailed && w.json()==before); rejects([&]{w.export_csv(t.root/"absent"/"file.csv");}); }
        if(c=="tamper") { auto bytes=read(path); const auto found=bytes.find("completed"); check(found!=std::string::npos); bytes.replace(found,9,"destroyed"); rejects([&]{ScenarioWorkspace::verify_export(bytes);}); }
    }
}
void randomized()
{
    std::mt19937 rng{1729}; auto d=base(); d.simulation.duration_seconds=1200; ScenarioWorkspace w{d};
    for(int i=0;i<12;++i) { w.revert(true); w.edit(Field::Name,"Seeded "+std::to_string(i)); w.edit(Field::Bays,std::to_string(rng()%6)); w.edit(Field::Workers,std::to_string(rng()%8)); w.edit(Field::Forklifts,std::to_string(rng()%5)); w.add_candidate(); }
    const auto initial=w.view().runtime().capture_snapshot(); w.compare(ComparisonMode::Initial,analysis::Objective::MinimizeWaiting);
    for(const auto& r:w.report()->candidates) check(r.metrics.arrived==r.metrics.completed+r.metrics.waiting+r.metrics.in_progress);
    const auto bytes=w.json(); w.compare(ComparisonMode::Initial,analysis::Objective::MinimizeWaiting); check(bytes==w.json()); check(w.baseline_viewer().runtime().capture_snapshot()==initial);
}
void layout()
{
    viewer::PanelPreferences p; p.workspace=true;
    for(auto size:{glm::vec2{640,480},glm::vec2{1024,768},glm::vec2{1280,720},glm::vec2{1600,900},glm::vec2{1920,1080}})
        for(float scale:{1.0f,1.25f,1.5f,2.0f}) for(bool selected:{false,true})
        {
            const auto l=viewer::dashboard_layout(size,{1.5f,1.5f},scale,p,selected);
            check(l.viewport.size.x>0 && l.viewport.size.y>0 && l.metrics.size.x<=size.x*0.48f);
            check(l.metrics.origin.x+l.metrics.size.x==l.viewport.origin.x);
            check(l.workspace.origin.y>=l.header.size.y && l.workspace.origin.y+l.workspace.size.y<=size.y);
            if(size.x==640 && scale==2) check(l.workspace_overlay && l.workspace.size.y>l.metrics.size.y);
            ScenarioWorkspace w{base()}; w.view().set_viewport(l.viewport); check(renderer::finite(w.view().orbit_camera().camera().view_projection()));
        }
}
}
int main(int argc,char** argv)
{
    try
    {
        check(argc==2); const std::string_view c=argv[1];
        if(c=="empty"||c=="valid"||c=="invalid"||c=="fields"||c=="revert"||c=="save"||c=="file_safety"||c=="run"||c=="load") editing(c);
        else if(c=="duplicate"||c=="rename"||c=="remove"||c=="restore"||c=="update"||c=="limit"||c=="candidate_invalid") management(c);
        else if(c=="visualization") visual_switch(); else if(c=="layout") layout(); else if(c=="export"||c=="export_failure"||c=="tamper") exports(c); else if(c=="randomized") randomized(); else comparison(c);
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
