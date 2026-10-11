#include "project_panel.hpp"
#include "save_dialog.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <ctime>
#include <iomanip>
#include <sstream>
using namespace simforge;
namespace
{
glm::vec2 center() {const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
std::optional<std::filesystem::path> choose(ProjectUI& ui,bool open,const std::string& name)
{
    if(!ui.test_path.empty()) return ui.test_path;
    ui.dialog_used=true;return project_dialog(open,name,ui.error);
}
std::string saved_time(const std::string& timestamp)
{
    try {const auto time=static_cast<std::time_t>(std::stoll(timestamp));std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc,&time);
#else
        gmtime_r(&time,&utc);
#endif
        std::ostringstream out;out<<std::put_time(&utc,"%Y-%m-%d %H:%M UTC");return out.str();
    } catch(...) {return "Previous save";}
}
bool save(workspace::Project& project,ProjectUI& ui,bool save_as)
{
    if(save_as || project.info().path.empty()) {
        const auto path=choose(ui,false,"project.simforge");if(!path) return false;
        project.save(*path,true);
    } else project.save();
    ui.notice="Project saved."; return true;
}
void restore_ui(const workspace::ProjectPreferences& p,DashboardState& d,WorkspaceUI& e)
{
    d.panels.mode=static_cast<viewer::ApplicationMode>(p.application_mode);d.panels.metrics=p.metrics;d.panels.inspector=p.inspector;
    d.panels.focus=p.focus;d.panels.workspace=p.workspace;d.panels.metrics_explicit=p.metrics_explicit;
    d.panel_changed=d.workspace_changed=true;d.seeking=false;d.last_selection.reset();d.feedback.clear();d.feedback_tick.reset();
    e.editing=p.selected_candidate;e.mode=static_cast<int>(p.comparison_mode);e.objective=static_cast<int>(p.objective);
    e.current_checkpoint=p.current_checkpoint;e.checkpoint_seconds=p.checkpoint_seconds;e.notes=p.notes;e.result_name=p.result_name;
    e.current_tab=p.editor_tab;e.requested_tab=p.application_mode==1?p.editor_tab:-1; e.pending.clear();e.confirmation.reset();e.error.clear();e.result_order.clear();
    e.sensitivity=p.sensitivity;e.sweep_multi=p.sweep_multi;e.sweep_explicit=p.sweep_explicit;e.sweep_parameter=p.sweep_parameter;e.sweep_objective=p.sweep_objective;e.sweep_selected=p.sweep_selected;e.sweep_seed=p.sweep_seed;e.sweep_min=p.sweep_min;e.sweep_max=p.sweep_max;e.sweep_step=p.sweep_step;e.sweep_values=p.sweep_values;e.sweep_threshold=p.sweep_threshold;e.sweep_name=p.sweep_name;e.statistical=p.statistical;e.replications=p.replications;e.base_seed=p.base_seed;e.seed_list=p.seed_list;e.seed_policy=p.seed_policy;e.statistical_metric=p.statistical_metric;
    e.language={}; // Review state is temporary; reopened projects never reapply a proposal.
}
}
void capture_project_preferences(workspace::Project& p,const DashboardState& d,const WorkspaceUI& e)
{
    auto value=p.preferences();
    value.application_mode=static_cast<int>(d.panels.mode);value.metrics=d.panels.metrics;value.inspector=d.panels.inspector;
    value.focus=d.panels.focus;value.workspace=d.panels.workspace;value.metrics_explicit=d.panels.metrics_explicit;
    value.comparison_mode=static_cast<workspace::ComparisonMode>(e.mode);value.objective=static_cast<analysis::Objective>(e.objective);
    value.current_checkpoint=e.current_checkpoint;value.checkpoint_seconds=e.checkpoint_seconds;value.selected_candidate=e.editing;
    value.notes=e.notes;value.result_name=e.result_name;value.editor_tab=e.current_tab;
    value.sensitivity=e.sensitivity;value.sweep_multi=e.sweep_multi;value.sweep_explicit=e.sweep_explicit;value.sweep_parameter=e.sweep_parameter;value.sweep_objective=e.sweep_objective;value.sweep_selected=e.sweep_selected;value.sweep_seed=e.sweep_seed;value.sweep_min=e.sweep_min;value.sweep_max=e.sweep_max;value.sweep_step=e.sweep_step;value.sweep_values=e.sweep_values;value.sweep_threshold=e.sweep_threshold;value.sweep_name=e.sweep_name;value.statistical=e.statistical;value.replications=e.replications;value.base_seed=e.base_seed;value.seed_list=e.seed_list;value.seed_policy=e.seed_policy;value.statistical_metric=e.statistical_metric;
    p.preferences(std::move(value));
}
void process_project(workspace::Project& project,workspace::ScenarioWorkspace& w,ProjectUI& ui,DashboardState& d,WorkspaceUI& e)
{
    ui.changed_workspace=false;
    if(!ui.pending) return;
    const auto action=*ui.pending;ui.pending.reset();
    try {
        ui.error.clear();
        if(action==ProjectAction::Close) {ui.close_requested=true;return;}
        if(action==ProjectAction::Save) { (void)save(project,ui,false);return; }
        if(action==ProjectAction::SaveAs) { (void)save(project,ui,true);return; }
        if(action==ProjectAction::DiscardRecovery) {workspace::Project::discard_recovery(ui.recovery_path);ui.show_recovery=ui.recovery_deferred=false;return;}
        if(action==ProjectAction::Recover) {project.recover(ui.recovery_path);ui.show_recovery=ui.recovery_deferred=false;ui.notice="Project recovery restored."; }
        if(action==ProjectAction::Open) {const auto path=choose(ui,true,"project.simforge");if(!path) return;project.open(*path);ui.notice="Project opened.";}
        if(action==ProjectAction::New) {const auto baseline=w.baseline();project.create(baseline);ui.notice="New project created.";}
        ui.changed_workspace=true;restore_ui(project.preferences(),d,e);
        if(w.report()) {
            e.result_order=w.report()->ranking;for(const auto& c:w.report()->candidates) if(!c.rank) e.result_order.push_back(c.candidate.name);
            if(e.result_name.empty()) e.result_name=w.report()->ranking.empty()?"Baseline":w.report()->ranking.front();
        }
    } catch(const std::exception& ex) {ui.error=ex.what();ui.notice.clear();}
}
void draw_project(workspace::Project& project,workspace::ScenarioWorkspace& w,ProjectUI& ui,DashboardState& dashboard,const DashboardFrame& frame)
{
    const auto transition=[&](ProjectAction action) {
        if(project.dirty() || w.execution().progress().active()) ui.confirmation=action;
        else ui.pending=action;
    };
    if(dashboard.show_file_menu) {ImGui::OpenPopup("Project file");dashboard.show_file_menu=false;}
    ImGui::SetNextWindowPos({frame.file_menu.x,frame.file_menu.y+20});
    if(ImGui::BeginPopup("Project file")) {
        ImGui::Text("%s%s",project.info().name.c_str(),project.dirty()?" *":"");
        ImGui::TextWrapped("%s",project.info().path.empty()?"Choose a location with Save As":reinterpret_cast<const char*>(project.info().path.u8string().c_str()));
        if(!project.info().saved.empty()) ImGui::TextDisabled("Last saved %s",saved_time(project.info().saved).c_str());
        ImGui::Separator();
        if(ui.recovery_deferred && ImGui::MenuItem("Recovery available...")) {ui.show_recovery=true;ImGui::CloseCurrentPopup();}
        if(ImGui::MenuItem("New Project")) {transition(ProjectAction::New);ImGui::CloseCurrentPopup();} ui.new_button=center();
        if(ImGui::MenuItem("Open Project...")) {transition(ProjectAction::Open);ImGui::CloseCurrentPopup();} ui.open_button=center();
        if(ImGui::MenuItem("Save Project")) {ui.pending=ProjectAction::Save;ImGui::CloseCurrentPopup();} ui.save_button=center();
        if(ImGui::MenuItem("Save Project As...")) {ui.pending=ProjectAction::SaveAs;ImGui::CloseCurrentPopup();} ui.save_as_button=center();
        ImGui::Separator();
        std::array<char,257> name{};std::copy_n(project.info().name.data(),std::min(project.info().name.size(),name.size()-1),name.data());
        if(ImGui::InputText("Project name",name.data(),name.size()) && name[0]) project.rename(name.data());
        if(w.execution().progress().active()) ImGui::TextWrapped("Saving retains submitted settings and the last successful report. The running experiment is not saved.");
        ImGui::EndPopup();
    }
    if(ui.confirmation) ImGui::OpenPopup("Unsaved project");
    if(ImGui::BeginPopupModal("Unsaved project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before closing this project?");
        if(w.execution().progress().active()) ImGui::TextWrapped("Replacement cancels and joins the active experiment after the new project validates.");
        if(ImGui::Button("Save")) {
            try {if(save(project,ui,false)) {ui.pending=ui.confirmation;ui.confirmation.reset();ImGui::CloseCurrentPopup();}}
            catch(const std::exception& ex) {ui.error=ex.what();}
        } ui.confirm_save=center();ImGui::SameLine();
        if(ImGui::Button("Discard")) {ui.pending=ui.confirmation;ui.confirmation.reset();ImGui::CloseCurrentPopup();}ui.discard=center();ImGui::SameLine();
        if(ImGui::Button("Cancel")) {ui.confirmation.reset();ImGui::CloseCurrentPopup();}ui.cancel=center();
        if(!ui.error.empty()) ImGui::TextWrapped("%s",ui.error.c_str());
        ImGui::EndPopup();
    }
    if(ui.show_recovery) ImGui::OpenPopup("Recovery available");
    if(ImGui::BeginPopupModal("Recovery available",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Unsaved project recovery is available. Restoring opens it as an unsaved project.");
        if(ImGui::Button("Restore recovery")) {ui.pending=ProjectAction::Recover;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Discard recovery")) {ui.pending=ProjectAction::DiscardRecovery;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Later")) {ui.show_recovery=false;ui.notice="Recovery retained. Choose Restore or Discard from File to enable autosave.";ImGui::CloseCurrentPopup();}
        if(!ui.error.empty()) ImGui::TextWrapped("%s",ui.error.c_str());ImGui::EndPopup();
    }
    if(!ui.error.empty() || !ui.notice.empty()) {
        ImGui::SetNextWindowPos({20,ImGui::GetIO().DisplaySize.y-12},ImGuiCond_Always,{0,1});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{10,4});
        ImGui::Begin("Project notice",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(ui.error.empty()?ui.notice.c_str():ui.error.c_str());ImGui::End();ImGui::PopStyleVar();
    }
}
