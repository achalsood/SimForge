#include "study_history_panel.hpp"
#include "statistical_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <simforge/viewer/presentation.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace simforge;
namespace {
glm::vec2 center() {auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
std::string completion(const std::string& epoch) {
    std::time_t seconds{};const auto p=std::from_chars(epoch.data(),epoch.data()+epoch.size(),seconds);if(p.ec!=std::errc{} || p.ptr!=epoch.data()+epoch.size())return "Legacy completion";
    std::tm utc{};
#ifdef _WIN32
    if(gmtime_s(&utc,&seconds))return "Unavailable";
#else
    if(!gmtime_r(&seconds,&utc))return "Unavailable";
#endif
    std::ostringstream text;text<<std::put_time(&utc,"%Y-%m-%d %H:%M UTC");return text.str();
}
void input(const char* label,std::string& value) {
    std::array<char,4097> data{};std::memcpy(data.data(),value.data(),std::min(value.size(),data.size()-1));
    ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);ImGui::PushID(label);if(ImGui::InputText("##value",data.data(),data.size()))value=data.data();ImGui::PopID();
}
std::uint64_t selection(const workspace::Comparison& r,const WorkspaceUI& ui) {
    if(r.interaction) {const auto i=static_cast<std::size_t>(std::max(0,ui.grid_selected));return r.interaction->cells.at(std::min(i,r.interaction->cells.size()-1)).id;}
    if(r.sensitivity) {const auto i=static_cast<std::size_t>(std::max(0,ui.sweep_selected));return r.sensitivity->points.at(std::min(i,r.sensitivity->points.size()-1)).id;}
    if(r.statistics) {for(const auto& c:r.statistics->candidates)if(c.name==ui.result_name)return c.candidate_id;return r.statistics->candidates.front().candidate_id;}
    for(std::size_t i=0;i<r.definitions.size();++i)if(r.definitions[i].name==ui.result_name)return r.candidate_ids[i];
    return r.candidate_ids.empty()?0:r.candidate_ids.front();
}
void restore_view(workspace::ScenarioWorkspace& w,WorkspaceUI& ui) {
    if(ui.history_observed==w.selected_study_id())return;ui.history_observed=w.selected_study_id();
    const auto s=std::find_if(w.studies().begin(),w.studies().end(),[&](const auto& v){return v.id==w.selected_study_id();});if(s==w.studies().end())return;
    ui.study_title=s->title;ui.study_notes=s->notes;const auto& r=*s->evidence;ui.result_name.clear();ui.result_order.clear();
    ui.grid_selected=ui.sweep_selected=ui.grid_seed=ui.sweep_seed=ui.grid_rectangle=0;ui.statistical_metric=ui.grid_metric=s->view.metric;
    if(r.confirmation && !s->view.configuration)ui.statistical_metric=static_cast<int>(r.confirmation->objective);
    if(r.interaction)for(std::size_t i=0;i<r.interaction->interactions.size();++i){const auto& rectangle=r.interaction->interactions[i];if(s->view.rectangle==std::array<std::uint32_t,4>{rectangle.x0,rectangle.x1,rectangle.y0,rectangle.y1})ui.grid_rectangle=static_cast<int>(i);}
    if(r.interaction) {for(std::size_t i=0;i<r.interaction->cells.size();++i)if(r.interaction->cells[i].id==s->view.configuration)ui.grid_selected=static_cast<int>(i);const auto seeds=analysis::replication_seeds(r.interaction->request.plan);for(std::size_t i=0;i<seeds.size();++i)if(seeds[i]==s->view.seed)ui.grid_seed=static_cast<int>(i);}
    else if(r.sensitivity) {for(std::size_t i=0;i<r.sensitivity->points.size();++i)if(r.sensitivity->points[i].id==s->view.configuration)ui.sweep_selected=static_cast<int>(i);const auto seeds=analysis::replication_seeds(r.sensitivity->request.plan);for(std::size_t i=0;i<seeds.size();++i)if(seeds[i]==s->view.seed)ui.sweep_seed=static_cast<int>(i);}
    else if(r.statistics) {for(const auto& c:r.statistics->candidates)if(c.candidate_id==s->view.configuration)ui.result_name=c.name;}
    else {for(std::size_t i=0;i<r.candidate_ids.size();++i)if(r.candidate_ids[i]==s->view.configuration)ui.result_name=r.definitions[i].name;}
}
}
void save_study_view(workspace::ScenarioWorkspace& w,const WorkspaceUI& ui) {
    if(!w.report() || w.selected_study_id().empty())return;const auto& r=*w.report();workspace::StudyViewState v;
    v.configuration=selection(r,ui);v.metric=r.interaction?ui.grid_metric:ui.statistical_metric;
    if(r.interaction) {if(!r.interaction->interactions.empty()){const auto& rect=r.interaction->interactions[std::min(static_cast<std::size_t>(std::max(0,ui.grid_rectangle)),r.interaction->interactions.size()-1)];v.rectangle={rect.x0,rect.x1,rect.y0,rect.y1};}auto seeds=analysis::replication_seeds(r.interaction->request.plan);v.seed=seeds[std::min(static_cast<std::size_t>(std::max(0,ui.grid_seed)),seeds.size()-1)];}
    if(r.sensitivity) {auto seeds=analysis::replication_seeds(r.sensitivity->request.plan);v.seed=seeds[std::min(static_cast<std::size_t>(std::max(0,ui.sweep_seed)),seeds.size()-1)];}
    w.study_view(v);
}
void draw_study_history(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float s) {
    restore_view(w,ui);const bool open=ImGui::CollapsingHeader("Study History");ui.history_header=center();
    if(!open)return;
    ImGui::Text("%zu / 32 studies | %.2f / 192 MiB",w.studies().size()+w.calibrations().size()+w.diagnostics().size(),w.history_bytes()/1048576.0);
    for(const auto& diagnostic:w.diagnostics()) {
        const auto& evidence=*diagnostic.evidence;ImGui::PushID(evidence.id.c_str());
        if(ImGui::Selectable(("Demand diagnostics: "+evidence.title).c_str(),w.diagnostic_report()==diagnostic.evidence)){w.select_diagnostic(evidence.id);ui.calibration.open=true;ui.calibration.tab=4;ui.calibration.diagnostics.tab=4;}
        if(ImGui::Button("Delete diagnostic..."))ImGui::OpenPopup("Delete diagnostic?");
        if(ImGui::BeginPopupModal("Delete diagnostic?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Permanently delete this diagnostic? The source calibration and playable runs remain available.");if(ImGui::Button("Delete")){ui.delete_study_id=evidence.id;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}ImGui::PopID();
    }
    if(ui.delete_study_id.starts_with("diagnostic-")){try{w.delete_diagnostic(ui.delete_study_id,true);ui.delete_study_id.clear();}catch(const std::exception& e){ui.error=e.what();ui.delete_study_id.clear();}}
    for(const auto& calibration:w.calibrations()) {
        const auto& evidence=*calibration.evidence;ImGui::PushID(evidence.id.c_str());
        const auto label="Demand calibration: "+evidence.provenance.source_filename+" ("+(evidence.settings.model==analysis::demand::Model::Constant?"constant":"UTC daily")+")";
        if(ImGui::Selectable(label.c_str(),w.calibration_report()==calibration.evidence)){w.select_calibration(evidence.id);ui.calibration.open=true;ui.calibration.tab=2;}
        if(ImGui::Button("Delete calibration..."))ImGui::OpenPopup("Delete calibration?");
        if(ImGui::BeginPopupModal("Delete calibration?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped("Permanently delete this calibration evidence? Existing playable runs remain available.");
            if(ImGui::Button("Delete")){ui.delete_study_id=evidence.id;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }ImGui::PopID();
    }
    if(ui.delete_study_id.starts_with("calibration-")){try{w.delete_calibration(ui.delete_study_id,true);ui.delete_study_id.clear();}catch(const std::exception& e){ui.error=e.what();ui.delete_study_id.clear();}}
    ImGui::TextDisabled("32 MiB per study; explicit deletion only. Evidence is immutable.");
    std::string next;
    const auto list_height=std::min(180.0f,28.0f+24.0f*static_cast<float>(w.studies().size()))*s;
    if(ImGui::BeginTable("Retained studies",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY,{0,list_height})) {
        ImGui::TableSetupColumn("Open study");ImGui::TableSetupColumn("Type / configurations / seeds");ImGui::TableSetupColumn("Size / completed");ImGui::TableHeadersRow();ui.history_open_controls.clear();
        for(const auto& record:w.studies()) {
            const auto& r=*record.evidence;ImGui::PushID(record.id.c_str());ImGui::TableNextRow();ImGui::TableNextColumn();
            if(ImGui::Selectable(record.title.c_str(),record.id==w.selected_study_id()))next=record.id;ui.history_open_controls.push_back(center());
            ImGui::TableNextColumn();const auto count=r.interaction?r.interaction->cells.size():r.sensitivity?r.sensitivity->points.size():r.statistics?r.statistics->candidates.size():r.candidates.size();
            const auto seeds=r.interaction?r.interaction->replications.size():r.sensitivity?r.sensitivity->replications.size():r.statistics?r.statistics->replications.size():1;
            ImGui::TextWrapped("%s | %zu configurations | %zu seeds",workspace::study_type_label(record.type),count,seeds);
            ImGui::TableNextColumn();ImGui::TextWrapped("%.2f MiB | %s",record.serialized_bytes/1048576.0,completion(record.completed).c_str());ImGui::PopID();
        }ImGui::EndTable();
    }
    if(!next.empty()) {save_study_view(w,ui);w.select_study(next);restore_view(w,ui);ui.requested_tab=2;}
    if(w.report()) {
        input("Study title",ui.study_title);input("Study notes",ui.study_notes);
        if(ImGui::Button("Save title and notes")) {try{w.rename_study(w.selected_study_id(),ui.study_title,ui.study_notes);ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}ui.study_rename=center();
        ImGui::SameLine();if(ImGui::Button("Delete study...")) {ui.delete_study_id=w.selected_study_id();ImGui::OpenPopup("Delete retained study?");}
        input("Export destination (new file)",ui.export_path);
        if(ImGui::Button("Export selected study JSON")) {try{w.export_study_json(std::filesystem::u8path(ui.export_path));}catch(const std::exception& e){ui.error=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Export history JSON")) {try{w.export_history_json(std::filesystem::u8path(ui.export_path));}catch(const std::exception& e){ui.error=e.what();}}
        const auto& r=*w.report();
        ImGui::TextWrapped("Objective: %s",r.confirmation?analysis::metric_name(r.confirmation->objective).data():r.interaction?analysis::metric_name(r.interaction->request.objective).data():r.sensitivity?analysis::metric_name(r.sensitivity->request.objective).data():workspace::objective_label(r.objective));
        if(r.confirmation && ImGui::Button("Open exploratory source")) {w.select_study(r.confirmation->source_study_id);restore_view(w,ui);}
        std::string linked;for(const auto& record:w.studies())if(record.evidence->confirmation && record.evidence->confirmation->source_study_id==w.selected_study_id()) {ImGui::PushID(record.id.c_str());if(ImGui::Button(("Open confirmation: "+record.title).c_str()))linked=record.id;ImGui::PopID();}
        if(!linked.empty()){save_study_view(w,ui);w.select_study(linked);restore_view(w,ui);}
    }
    if(ImGui::BeginPopupModal("Delete retained study?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Permanently delete %s? Linked confirmations must be deleted explicitly first.",ui.delete_study_id.c_str());
        if(ImGui::Button("Delete permanently")) {try{w.delete_study(ui.delete_study_id,true);restore_view(w,ui);ImGui::CloseCurrentPopup();}catch(const std::exception& e){ui.error=e.what();ImGui::CloseCurrentPopup();}}
        ImGui::SameLine();if(ImGui::Button("Keep study"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    if(w.has_pending_study()) {
        ImGui::TextWrapped("Completed result is pending retention. %s",w.message().c_str());
        input("Pending export destination (new file)",ui.export_path);
        if(ImGui::Button("Export pending evidence JSON")) {try{w.export_pending_json(std::filesystem::u8path(ui.export_path));}catch(const std::exception& e){ui.error=e.what();}}
        if(ImGui::Button("Retain pending result")) {try{w.retain_pending_study();restore_view(w,ui);}catch(const std::exception& e){ui.error=e.what();}}
        if(ImGui::Button("Discard pending result..."))ImGui::OpenPopup("Discard unretained evidence?");
        if(ImGui::BeginPopupModal("Discard unretained evidence?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {ImGui::TextUnformatted("This completed result has not been saved. Discard it?");if(ImGui::Button("Discard")){w.discard_pending_study(true);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep pending"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    }
}
void draw_confirmation(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float) {
    if(!w.report())return;const auto& r=*w.report();
    if(ui.scroll_target==60){ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    if(r.confirmation) {
        const auto& c=*r.confirmation;const auto& result=r.statistics->candidates.front().metrics[static_cast<std::size_t>(c.objective)];
        ImGui::Separator();ImGui::TextUnformatted("Independent-seed confirmation study");
        ImGui::TextWrapped("Frozen source %s (%s), selection %llu | %s",c.source_study_id.c_str(),c.source_title.c_str(),static_cast<unsigned long long>(c.selection_id),c.changed_inputs.c_str());
        const auto& baseline=std::get<scenario::WarehouseConfig>(c.paired.baseline.domain);const auto& candidate=std::get<scenario::WarehouseConfig>(c.paired.candidates.front().definition.domain);
        ImGui::Text("Baseline bays/workers/forklifts: %u/%u/%u | Frozen candidate: %u/%u/%u",baseline.loading_bays,baseline.workers,baseline.forklifts,candidate.loading_bays,candidate.workers,candidate.forklifts);
        ImGui::Text("Selected objective: %s | %s",analysis::metric_name(c.objective).data(),analysis::higher_is_better(c.objective)?"Higher is favorable":"Lower is favorable");
        ImGui::Text("Exploration: %zu seeds, estimated mean difference %s",c.exploratory_seeds.size(),viewer::display_decimal(c.exploratory_difference).c_str());
        ImGui::Text("Confirmation: %u new seeds, estimated mean difference %s",c.paired.plan.count,viewer::display_decimal(result.mean_difference).c_str());
        ImGui::Text("Nominal 95%% confidence interval [%s, %s]",viewer::display_decimal(result.lower).c_str(),viewer::display_decimal(result.upper).c_str());
        if(c.exploratory_difference && result.mean_difference) {ImGui::Text("Change in estimates: %s",viewer::display_decimal(*result.mean_difference-*c.exploratory_difference).c_str());const auto sign=[](double x){return (x>0)-(x<0);};ImGui::TextWrapped("Estimated direction %s exploration.",sign(*c.exploratory_difference)==sign(*result.mean_difference)?"agrees with":"differs from");}
        if(result.lower && result.upper && *result.lower<=0 && *result.upper>=0)ImGui::TextWrapped("The confirmation interval includes zero. Confirmation estimate uncertain.");
        if(!result.lower)ImGui::TextWrapped("Sample too small or metric undefined: no confidence interval available.");
        ImGui::TextWrapped("%s",c.additional_analysis?"Additional analysis: repeated confirmations do not automatically validate a revised choice.":"First completed confirmation linked to this exploratory study.");
        ImGui::TextWrapped("Disjoint PRNG seeds do not mathematically guarantee independence. No exploratory observations are pooled. Confirmation does not prove global optimality or guarantee future performance.");
        const auto seeds=analysis::replication_seeds(c.paired.plan);input("Playback seed (measured confirmation seed)",ui.confirmation_base);
        if(ImGui::Button("Play frozen configuration at seed")) {try {WorkspaceUI parse;parse.replications="1";parse.seed_policy=1;parse.seed_list=ui.confirmation_base;const auto seed=analysis::replication_seeds(statistical_plan(parse)).front();if(std::find(seeds.begin(),seeds.end(),seed)==seeds.end())throw std::invalid_argument("Choose a measured confirmation seed");auto d=c.paired.candidates.front().definition;d.simulation.random_seed=seed;w.create_run(d);}catch(const std::exception& e){ui.error=e.what();}}
        return;
    }
    ImGui::Separator();ImGui::TextUnformatted("Independent confirmation");
    ImGui::BeginDisabled(w.execution().progress().active() || w.has_pending_study());
    if(ImGui::Button("Confirm Selected Configuration")) {try {WorkspaceUI parse;parse.replications=ui.confirmation_count;parse.base_seed=ui.confirmation_base;parse.seed_policy=ui.confirmation_policy;parse.seed_list=ui.confirmation_seeds;ui.frozen_confirmation=w.freeze_confirmation(selection(r,ui),statistical_plan(parse));ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}ui.confirmation_prepare=center();
    ImGui::EndDisabled();
    if(!ui.frozen_confirmation)return;
    auto& c=*ui.frozen_confirmation;ImGui::TextWrapped("Frozen %s, selection %llu: %s",c.source_study_id.c_str(),static_cast<unsigned long long>(c.selection_id),c.changed_inputs.c_str());
    input("Confirmation replication count",ui.confirmation_count);ImGui::Combo("New seed policy",&ui.confirmation_policy,"Sequential base + index v1\0Explicit distinct seeds\0");
    if(ui.confirmation_policy==0)input("New confirmation base seed",ui.confirmation_base);else input("New confirmation seed list",ui.confirmation_seeds);
    std::string diagnostic;bool valid=false;
    try {WorkspaceUI parse;parse.replications=ui.confirmation_count;parse.base_seed=ui.confirmation_base;parse.seed_policy=ui.confirmation_policy;parse.seed_list=ui.confirmation_seeds;c.paired.plan=statistical_plan(parse);analysis::validate_confirmation_request(c);valid=true;diagnostic="Seed overlap check passed: "+std::to_string(c.paired.plan.count)+" new distinct seeds; "+std::to_string(c.exploratory_seeds.size())+" exploratory seeds excluded.";}catch(const std::exception& e){diagnostic=e.what();}
    ImGui::TextWrapped("%s",diagnostic.c_str());
    ImGui::TextWrapped("Exploratory selection can favor noisy winners. This frozen choice will be tested on new realizations. Nominal 95%% intervals rely on model assumptions.");
    ImGui::BeginDisabled(!valid || w.execution().progress().active() || w.has_pending_study());
    if(ImGui::Button("Run independent confirmation")) {try{w.start_confirmation(c);ui.frozen_confirmation.reset();ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}ui.confirmation_start=center();ImGui::EndDisabled();
    if(ui.scroll_target==62){ImGui::SetScrollHereY(0.8f);ui.scroll_target=-1;}
}
