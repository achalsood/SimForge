#include <simforge/workspace/presentation.hpp>
#include <simforge/viewer/presentation.hpp>
#include <simforge/renderer/geometry.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
using namespace simforge;
namespace
{
void check(bool v,std::source_location l=std::source_location::current())
{ if(!v) throw std::runtime_error("Visual design check at "+std::to_string(l.line())); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch(const std::exception&) { failed=true; } check(failed); }
scenario::ScenarioDefinition definition()
{ scenario::ScenarioDefinition d; d.name="Baseline"; d.simulation={60,3600,42}; d.domain=scenario::WarehouseConfig{3,10,2,300,900}; return d; }
workspace::ScenarioWorkspace experiment()
{ workspace::ScenarioWorkspace w{definition()}; w.edit(workspace::Field::Name,"Extra forklift"); w.edit(workspace::Field::Forklifts,"3"); w.add_candidate(); w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); return w; }
void modes(bool responsive)
{
    for(auto size:{glm::vec2{640,480},glm::vec2{1280,720},glm::vec2{1600,900},glm::vec2{1920,1080}})
    for(float scale:{1.0f,1.5f,2.0f}) for(auto mode:{viewer::ApplicationMode::Simulation,viewer::ApplicationMode::Editor,viewer::ApplicationMode::Analysis})
    {
        viewer::PanelPreferences p; p.mode=mode;
        const auto l=viewer::dashboard_layout(size,{1.5f,1.5f},scale,p,true);
        check(l.viewport.size.x>0 && l.viewport.size.y>0);
        if(mode!=viewer::ApplicationMode::Simulation) check(l.workspace_overlay && l.workspace.size.x==size.x && l.workspace.origin.y+l.workspace.size.y==size.y);
        else check(!l.workspace_overlay);
        if(responsive) for(const auto r:{l.header,l.workspace,l.play,l.step,l.reset,l.speed,l.timeline})
            check(r.origin.x>=0 && r.origin.y>=0 && r.origin.x+r.size.x<=size.x && r.origin.y+r.size.y<=size.y);
    }
    viewer::PanelPreferences p; p.mode=static_cast<viewer::ApplicationMode>(5);
    rejects([&]{(void)viewer::dashboard_layout({1280,720},{1,1},1,p,false);});
}
void navigation()
{
    auto w=experiment(); w.view().seek(60); w.view().select(viewer::VisualKey{viewer::VisualCategory::Shipment,0});
    const auto snapshot=w.view().runtime().capture_snapshot(); const auto draft=w.draft(); const auto json=w.json();
    for(int i=0;i<20;++i) for(auto mode:{viewer::ApplicationMode::Simulation,viewer::ApplicationMode::Editor,viewer::ApplicationMode::Analysis})
    { viewer::PanelPreferences p; p.mode=mode; w.view().set_viewport(viewer::dashboard_layout({1600,900},{1,1},1,p,true).viewport,true); }
    check(w.view().runtime().capture_snapshot()==snapshot && w.draft()==draft && w.json()==json);
    w.view_result("Extra forklift"); check(w.view().result_preview()); w.return_to_baseline(); check(w.view().runtime().capture_snapshot()==snapshot && w.view().selection());
}
void fields()
{
    check(workspace::field_presentation.size()==static_cast<std::size_t>(workspace::Field::Count));
    for(const auto& f:workspace::field_presentation) check(*f.label && *f.description);
    check(std::string_view{workspace::field_presentation[4].unit}=="seconds");
    auto w=experiment(); const auto report=w.json(); w.edit(workspace::Field::Forklifts,"-1"); check(!w.validate_draft());
    check(w.errors().front().field=="Forklifts" && w.json()==report && w.dirty()); w.revert(true); check(!w.dirty());
}
void bars(bool undefined)
{
    if(undefined)
    {
        check(!workspace::metric_bars({},1).available && !workspace::metric_bars(1,{}).available);
        const auto zero=workspace::metric_bars(0,0); check(zero.available && zero.baseline==0 && zero.candidate==0);
        rejects([]{(void)workspace::metric_bars(-1,2);}); rejects([]{(void)workspace::metric_bars(1,std::numeric_limits<double>::infinity());});
    }
    else
    {
        const auto up=workspace::metric_bars(63,94), down=workspace::metric_bars(32,0);
        check(up.available && up.baseline==63.0/94 && up.candidate==1 && down.baseline==1 && down.candidate==0);
        const auto reversed=workspace::metric_bars(94,63); check(reversed.baseline==up.candidate && reversed.candidate==up.baseline);
    }
}
void results(bool evidence)
{
    auto w=experiment(); const auto& r=*w.report(); const auto* best=workspace::best_tested(r);
    check(best && best->candidate.name=="Extra forklift" && best->rank==1);
    check(best->delta.completed==static_cast<std::int64_t>(best->metrics.completed)-static_cast<std::int64_t>(r.baseline.metrics.completed));
    if(evidence) for(const auto& probe:r.bottlenecks) if(probe.result)
        check(probe.result->delta.completed==static_cast<std::int64_t>(probe.result->metrics.completed)-static_cast<std::int64_t>(r.baseline.metrics.completed));
    auto invalid=r; invalid.ranking.front()="Missing"; rejects([&]{(void)workspace::best_tested(invalid);}); invalid.ranking.clear(); check(!workspace::best_tested(invalid));
}
void normals(bool transform)
{
    for(std::uint32_t i=1;i<=renderer::builtin_mesh_count;++i)
    {
        const auto mesh=renderer::make_builtin_mesh(static_cast<renderer::MeshId>(i));
        for(const auto& v:mesh.vertices)
        { const glm::vec3 n{v.normal[0],v.normal[1],v.normal[2]}; check(std::isfinite(glm::length(n)) && std::abs(glm::length(n)-1)<1e-5f); }
        for(std::size_t j=0;j<mesh.indices.size();j+=3)
        {
            std::array<glm::vec3,3> p;
            for(int k=0;k<3;++k) { const auto& v=mesh.vertices.at(mesh.indices[j+k]); p[k]={v.position[0],v.position[1],v.position[2]}; }
            const auto& v=mesh.vertices.at(mesh.indices[j]); const glm::vec3 n{v.normal[0],v.normal[1],v.normal[2]};
            check(glm::dot(glm::cross(p[1]-p[0],p[2]-p[0]),n)>0);
            if(transform)
            {
                renderer::RenderTransform t; t.scale={0.01f,10,2}; t.rotation={0.3f,0.5f,0.7f}; const auto m=glm::mat3{t.matrix()};
                const auto world=glm::normalize(glm::transpose(glm::inverse(m))*n);
                check(std::abs(glm::dot(world,m*(p[1]-p[0])))<1e-4f && std::abs(glm::dot(world,m*(p[2]-p[0])))<1e-4f);
            }
        }
    }
}
void capture()
{
    viewer::ViewerController v{definition()}; v.seek(60); const auto pose=v.orbit_camera().camera().position; const auto snap=v.runtime().capture_snapshot();
    viewer::InputEvent e{viewer::InputKind::Down}; e.button=viewer::MouseButton::Left; e.position={600,300}; v.input(e,{true,true}); e.kind=viewer::InputKind::Move; e.position={900,450}; v.input(e,{true,true}); e.kind=viewer::InputKind::Up; v.input(e,{true,true});
    check(v.orbit_camera().camera().position==pose && v.runtime().capture_snapshot()==snap && !v.selection());
}
}
int main(int argc,char** argv)
{
    try
    {
        check(argc==2); const std::string_view c=argv[1];
        if(c=="modes" || c=="responsive") modes(c=="responsive"); else if(c=="navigation") navigation(); else if(c=="fields") fields();
        else if(c=="bars" || c=="undefined") bars(c=="undefined"); else if(c=="results" || c=="evidence") results(c=="evidence");
        else if(c=="normals" || c=="normal_transform") normals(c=="normal_transform"); else if(c=="capture") capture(); else check(false);
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
