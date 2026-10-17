#include "workspace_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <simforge/workspace/presentation.hpp>
#include "save_dialog.hpp"
#include "statistical_panel.hpp"
#include "sensitivity_panel.hpp"
#include "interaction_panel.hpp"
#include "study_history_panel.hpp"

using namespace simforge;
void apply_workspace(workspace::ScenarioWorkspace& w, const WorkspaceCommand& c)
{
    switch(c.action)
    {
    case WorkspaceAction::CheckpointPlayback:
        if(!w.report() || !w.report()->checkpoint_study || w.report()->request_id!=c.id)throw std::invalid_argument("Checkpoint study selection changed");
        w.simulate_checkpoint(c.seed,c.approved);break;
    case WorkspaceAction::Validate: (void)w.validate_draft(); break;
    case WorkspaceAction::Revert: w.revert(c.discard); break;
    case WorkspaceAction::Open: w.open(std::filesystem::u8path(c.text),c.discard); break;
    case WorkspaceAction::Save: w.save_draft(std::filesystem::u8path(c.text)); break;
    case WorkspaceAction::Run: w.run_draft(); break;
    case WorkspaceAction::SelectRun: w.select_run(c.id); break;
    case WorkspaceAction::RemoveRun: w.remove_run(c.id); break;
    case WorkspaceAction::Live: w.resume_live_view(); break;
    case WorkspaceAction::SimulateCandidate: w.simulate_candidate(c.id); break;
    case WorkspaceAction::SimulateResult: w.simulate_result(c.text); break;
    case WorkspaceAction::LoadRun: (void)w.create_run(scenario::load_scenario(std::filesystem::u8path(c.text))); break;
    case WorkspaceAction::ApplyLanguage:
        if(!c.proposal) throw std::invalid_argument("Missing reviewed proposal");
        w.apply_language_proposal(*c.proposal,c.approved);break;
    case WorkspaceAction::Add: w.add_candidate(c.notes); break;
    case WorkspaceAction::Edit: w.edit_candidate(c.id,c.discard); break;
    case WorkspaceAction::Update: w.update_candidate(c.id,c.notes); break;
    case WorkspaceAction::Rename: w.rename_candidate(c.id,c.text); break;
    case WorkspaceAction::Duplicate: w.duplicate_candidate(c.id,c.text); break;
    case WorkspaceAction::Remove: w.remove_candidate(c.id); break;
    case WorkspaceAction::Restore: w.restore_candidate(c.id); break;
    case WorkspaceAction::Compare: if(c.replications) w.start_statistical_experiment(c.mode,c.objective,*c.replications);else w.start_experiment(c.mode,c.objective,c.tick); break;
    case WorkspaceAction::Cancel: w.execution().cancel(); break;
    case WorkspaceAction::View: w.view_result(c.text.empty() ? std::nullopt : std::optional{c.text}); break;
    case WorkspaceAction::Baseline: w.return_to_baseline(); break;
    case WorkspaceAction::Json: w.export_json(std::filesystem::u8path(c.text)); break;
    case WorkspaceAction::Csv: w.export_csv(std::filesystem::u8path(c.text)); break;
    case WorkspaceAction::CsvSeeds: w.export_seed_csv(std::filesystem::u8path(c.text));break;
    case WorkspaceAction::Sweep: if(!c.sensitivity)throw std::invalid_argument("Missing sensitivity inputs");w.start_sensitivity(*c.sensitivity);break;
    case WorkspaceAction::SweepCandidate:
    case WorkspaceAction::SweepRun:
        if(!c.sensitivity||!w.report()||!w.report()->sensitivity||analysis::serialize_sensitivity_request(*c.sensitivity)!=analysis::serialize_sensitivity_request(w.report()->sensitivity->request))throw std::invalid_argument("Sensitivity results changed; select the tested configuration again");
        if(c.action==WorkspaceAction::SweepCandidate)w.add_sensitivity_candidate(c.id,c.text,c.seed);else w.simulate_sensitivity(c.id,c.seed);break;
    case WorkspaceAction::CsvMarginal: w.export_marginal_csv(std::filesystem::u8path(c.text));break;
    case WorkspaceAction::Interaction: if(!c.interaction)throw std::invalid_argument("Missing grid inputs");w.start_interaction(*c.interaction);break;
    case WorkspaceAction::InteractionCandidate:
    case WorkspaceAction::InteractionRun:
        if(!c.interaction||!w.report()||!w.report()->interaction||analysis::serialize_interaction_request(*c.interaction)!=analysis::serialize_interaction_request(w.report()->interaction->request))throw std::invalid_argument("Interaction results changed; select the tested configuration again");
        if(c.action==WorkspaceAction::InteractionCandidate)w.add_interaction_candidate(c.interaction->id,c.id,c.text,c.seed);else w.simulate_interaction(c.interaction->id,c.id,c.seed);break;
    case WorkspaceAction::CsvInteractions: w.export_interaction_csv(std::filesystem::u8path(c.text));break;
    case WorkspaceAction::CsvInteractionSeeds: w.export_interaction_csv(std::filesystem::u8path(c.text),true);break;
    }
}
namespace
{
glm::vec2 center() { const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); return {(a.x+b.x)/2,(a.y+b.y)/2}; }
bool text(const char* label, std::string& value)
{
    std::array<char,4097> buffer{};
    std::copy_n(value.data(),std::min(value.size(),buffer.size()-1),buffer.data());
    ImGui::SetNextItemWidth(-1); const bool changed=ImGui::InputText(label,buffer.data(),buffer.size());
    if(changed) value=buffer.data(); return changed;
}
bool literal_choice(const std::string& name, bool selected, int id)
{
    // Names are data, including literal ##/###; keep them out of ImGui label IDs.
    ImGui::PushID(id); const bool clicked=ImGui::Selectable("##Configuration",selected);
    const auto pos=ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(pos,ImGui::GetColorU32(ImGuiCol_Text),name.c_str());
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",name.c_str());
    ImGui::PopID(); return clicked;
}
void detail(const char* label, const std::string& value) { ImGui::TextUnformatted(label); ImGui::SameLine(); ImGui::TextUnformatted(value.c_str()); }
void change(const char* label, const std::string& value, std::optional<double> delta, bool higher)
{
    const auto* direction=workspace::change_label(delta,higher);
    const auto color=!delta || *delta==0 ? viewer::design::secondary : std::string_view{direction}=="Improved" ? viewer::design::completed : viewer::design::error;
    ImGui::PushStyleColor(ImGuiCol_Text,{color.x,color.y,color.z,1});
    ImGui::TextWrapped("%s %s | %s",label,value.c_str(),direction); ImGui::PopStyleColor();
}
void comparison_chart(const analysis::CandidateResult& baseline,const analysis::CandidateResult& selected)
{
    const auto bar=[&](const char* name,std::optional<double> a,std::optional<double> b)
    {
        const auto values=workspace::metric_bars(a,b);
        ImGui::TextUnformatted(name);
        if(!values.available) { ImGui::TextDisabled("N/A - not yet defined"); return; }
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,{viewer::design::secondary.x,viewer::design::secondary.y,viewer::design::secondary.z,1});
        const auto first="Baseline  "+viewer::display_decimal(a); ImGui::TextDisabled("%s",first.c_str());
        ImGui::ProgressBar(static_cast<float>(values.baseline),{-1,6*ImGui::GetStyle().FontScaleDpi},""); ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,{viewer::design::processing.x,viewer::design::processing.y,viewer::design::processing.z,1});
        const auto second="Selected  "+viewer::display_decimal(b); ImGui::TextUnformatted(second.c_str());
        ImGui::ProgressBar(static_cast<float>(values.candidate),{-1,6*ImGui::GetStyle().FontScaleDpi},""); ImGui::PopStyleColor();
    };
    if(ImGui::BeginTable("Measured comparison",ImGui::GetContentRegionAvail().x/ImGui::GetStyle().FontScaleDpi>=600?4:2,ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn(); bar("Completed",static_cast<double>(baseline.metrics.completed),static_cast<double>(selected.metrics.completed));
        ImGui::TableNextColumn(); bar("Throughput / h",baseline.metrics.throughput_per_hour,selected.metrics.throughput_per_hour);
        ImGui::TableNextColumn(); bar("Average wait / s",baseline.metrics.average_wait_seconds,selected.metrics.average_wait_seconds);
        ImGui::TableNextColumn(); bar("Remaining queue",static_cast<double>(baseline.metrics.waiting),static_cast<double>(selected.metrics.waiting));
        ImGui::EndTable();
    }
}
const char* mode_name(workspace::ComparisonMode mode) { return mode == workspace::ComparisonMode::Initial ? "Independent runs from start" : "Shared checkpoint capacity changes"; }
void metrics(const analysis::CandidateResult& r)
{
    const auto& m=r.metrics; const auto& d=r.delta;
    detail("Throughput / h:",viewer::display_decimal(m.throughput_per_hour));
    detail("Average queue wait:",viewer::display_duration(m.average_wait_seconds));
    detail("Utilization B/W/F:",viewer::display_percent(m.loading_bay_utilization)+" / "+viewer::display_percent(m.worker_utilization)+" / "+viewer::display_percent(m.forklift_utilization));
    const auto signed_count=[](std::int64_t v){return (v>0?"+":"")+std::to_string(v);};
    change("Completed change:",signed_count(d.completed)+" ("+(d.completed_percent?viewer::display_decimal(d.completed_percent)+"%":"N/A")+")",static_cast<double>(d.completed),true);
    change("Final queue change:",signed_count(d.waiting),static_cast<double>(d.waiting),false);
    change("Throughput change / h:",viewer::display_decimal(d.throughput_per_hour),d.throughput_per_hour,true);
    change("Mean wait change (s):",viewer::display_decimal(d.average_wait_seconds),d.average_wait_seconds,false);
    detail("Resource changes B/W/F:",signed_count(d.resources.loading_bays)+" / "+signed_count(d.resources.workers)+" / "+signed_count(d.resources.forklifts));
}
}
void draw_workspace(workspace::ScenarioWorkspace& w, WorkspaceUI& ui, const DashboardFrame& frame)
{
    const float s=frame.layout.scale; const auto r=frame.layout.workspace;
    ImGui::SetNextWindowPos({r.origin.x,r.origin.y}); ImGui::SetNextWindowSize({r.size.x,r.size.y});
    ImGui::Begin("Scenario workspace",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const auto scroll=[&](int target){ if(ui.scroll_target==target) { ImGui::SetScrollHereY(0.5f); ui.scroll_target=-1; } };
    ImGui::PushFont(nullptr,viewer::design::heading); ImGui::TextUnformatted(ui.analysis_page ? "Experiment analysis" : "Scenario studio"); ImGui::PopFont();
    if(r.size.x/s>=700)
    { ImGui::SameLine(); ImGui::TextDisabled("%s",ui.analysis_page ? "Measured outcomes. Reproducible inputs." : "Configure resources. Test alternatives."); }
    const char* status=w.status()==workspace::Status::Invalid?"Validation failed":w.status()==workspace::Status::Results?"Results available":w.status()==workspace::Status::ExportFailed?"Export failed":w.dirty()?"Unsaved draft changes":"Ready";
    if(r.size.x/s>=500) ImGui::SameLine();
    ImGui::TextDisabled("%s",status);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",w.message().c_str());
    if(ui.analysis_page){ImGui::SameLine();if(ImGui::SmallButton(ui.calibration.open?"Close calibration":"Demand Calibration"))ui.calibration.open=!ui.calibration.open;ui.calibration.toggle=center();}
    const auto queue=[&](WorkspaceCommand c)
    {
        if((w.dirty() && (c.action==WorkspaceAction::Revert || c.action==WorkspaceAction::Edit)) ||
            c.action==WorkspaceAction::Open)
        { ui.confirmation=std::move(c); ImGui::OpenPopup("Discard edits?"); }
        else ui.pending.push_back(std::move(c));
    };
    const auto compare=[&]
    {
        WorkspaceCommand c{WorkspaceAction::Compare}; c.mode=ui.mode==0?workspace::ComparisonMode::Initial:workspace::ComparisonMode::Checkpoint;
        c.objective=static_cast<analysis::Objective>(ui.objective);
        if(ui.statistical) {try {c.replications=statistical_plan(ui);queue(std::move(c));ui.requested_tab=2;}catch(const std::exception& e){ui.error=e.what();}return;}
        if(ui.mode==1 && !ui.current_checkpoint)
        {
            try { c.tick=workspace::checkpoint_tick(w.baseline(),ui.checkpoint_seconds); queue(std::move(c)); ui.requested_tab=2; }
            catch(const std::exception& e) { ui.error=e.what(); }
        }
        else { queue(std::move(c)); ui.requested_tab=2; }
    };
    if(ui.analysis_page)draw_study_history(w,ui,s);
    if(ui.analysis_page) {
        if(ui.calibration.open){draw_calibration(w,ui.calibration,s);ImGui::End();return;}
    }
    const auto progress = w.execution().progress();
    if(ui.analysis_page){
        ImGui::Checkbox("Checkpoint Intervention Study",&ui.checkpoint.open);ui.checkpoint.toggle=center();
        if(ui.checkpoint.open){
            ImGui::BeginChild("Checkpoint study workspace",{0,0});
            draw_checkpoint_setup(w,ui.checkpoint,s);
            if(w.report() && w.report()->checkpoint_study){draw_checkpoint_results(w,ui.checkpoint,s);
                if(ui.checkpoint.playback_requested){WorkspaceCommand command{WorkspaceAction::CheckpointPlayback};command.id=w.report()->request_id;command.seed=ui.checkpoint.playback_seed;command.approved=*ui.checkpoint.playback_requested;queue(std::move(command));ui.checkpoint.playback_requested.reset();}}
            if(!ui.error.empty())ImGui::TextWrapped("%s",ui.error.c_str());
            if(const auto attempt=w.execution().last_unsuccessful_attempt();attempt && !attempt->error.empty())ImGui::TextWrapped("Previous attempt: %s",attempt->error.c_str());
            ImGui::EndChild();ImGui::End();return;
        }
    }
    if(ui.analysis_page && !ui.checkpoint.open) {if(ImGui::GetContentRegionAvail().x/s>600)ImGui::SameLine();draw_interaction_setup(w,ui,s);if(!ui.interaction){if(ImGui::GetContentRegionAvail().x/s>500)ImGui::SameLine();draw_sensitivity_setup(w,ui,s);if(!ui.sensitivity)draw_statistical_setup(w,ui,s);}}
    if (progress.request_id != 0 && ui.analysis_page && !(progress.total_replications && !progress.active()))
    {
        ImGui::Separator();
        ImGui::Text("Experiment #%llu: %s",static_cast<unsigned long long>(progress.request_id),workspace::execution_state_name(progress.state));
        if (progress.active())
        {
            // Keep the cancel target fixed while variable phase/tick text changes underneath it.
            ImGui::BeginDisabled(progress.cancellation_requested);
            if (ImGui::Button(progress.cancellation_requested ? "Cancellation requested" : "Cancel Experiment")) queue({WorkspaceAction::Cancel});
            ui.cancel_experiment=center();
            ImGui::EndDisabled();
        }
        ImGui::Text("%s | Candidates %llu / %llu | Elapsed %.1f s",progress.phase.c_str(),
            static_cast<unsigned long long>(progress.completed_candidates),static_cast<unsigned long long>(progress.total_candidates),progress.elapsed_seconds);
        if(progress.total_replications) ImGui::TextWrapped("Paired seed %llu / %llu (seed %s) | Completed seeds %llu | Current candidate %llu / %llu",static_cast<unsigned long long>(progress.current_replication),static_cast<unsigned long long>(progress.total_replications),progress.replication_seed.c_str(),static_cast<unsigned long long>(progress.completed_replications),static_cast<unsigned long long>(progress.current_candidate),static_cast<unsigned long long>(progress.candidates_per_replication));
        if(progress.interaction&&progress.current_candidate)ImGui::TextWrapped("Grid: %s = %u | %s = %u | Completed cell-seed evaluations %llu / %llu",progress.x_parameter.c_str(),progress.x_value,progress.y_parameter.c_str(),progress.y_value,static_cast<unsigned long long>(progress.completed_candidates),static_cast<unsigned long long>(progress.total_candidates));
        if (progress.active() && !progress.candidate.empty()) {if(progress.sensitivity){if(!progress.sweep_value.empty())ImGui::TextWrapped("Submitted sweep parameter: %s | current value %s",progress.parameter.c_str(),progress.sweep_value.c_str());}else ImGui::TextWrapped("Submitted configuration: %s",progress.candidate.c_str());}
        if (progress.horizon) ImGui::Text("Run tick %llu / %llu",static_cast<unsigned long long>(progress.tick),static_cast<unsigned long long>(progress.horizon));
        // Candidate count measures completed evaluations; setup/probes/publication are indeterminate.
        if (progress.state == workspace::ExecutionState::Completed) ImGui::ProgressBar(1.0f,{-1,18*s},"Report ready");
        else if (progress.phase == "Evaluating candidate")
            ImGui::ProgressBar(static_cast<float>(progress.completed_candidates)/static_cast<float>(progress.total_candidates),{-1,0},"Completed candidates");
        if (progress.active())
        {
            ImGui::TextWrapped("Using submitted inputs. Edits apply to future experiments.");
        }
        if (!progress.error.empty()) ImGui::TextWrapped("Experiment failed: %s",progress.error.c_str());
        if (progress.state == workspace::ExecutionState::Completed && w.report() && !w.report()->statistics && !w.report()->sensitivity && !w.report()->interaction && w.report()->request_id == progress.request_id)
            ImGui::Text("Baseline completed shipments: %llu",static_cast<unsigned long long>(w.report()->baseline.metrics.completed));
        if (const auto attempt=w.execution().last_unsuccessful_attempt(); attempt && attempt->request_id != progress.request_id)
        {
            ImGui::TextWrapped("Previous unsuccessful attempt #%llu: %s",static_cast<unsigned long long>(attempt->request_id),workspace::execution_state_name(attempt->state));
            if (!attempt->error.empty()) ImGui::TextWrapped("%s",attempt->error.c_str());
        }
        ImGui::Separator();
    }
    if(ui.analysis_page && progress.total_replications && !progress.active()) {
        ImGui::Text("Experiment #%llu: %s | Completed seeds %llu / %llu | Candidates %llu / %llu",static_cast<unsigned long long>(progress.request_id),workspace::execution_state_name(progress.state),static_cast<unsigned long long>(progress.completed_replications),static_cast<unsigned long long>(progress.total_replications),static_cast<unsigned long long>(progress.completed_candidates),static_cast<unsigned long long>(progress.total_candidates));
        if(!progress.error.empty()) ImGui::TextWrapped("%s",progress.error.c_str());
    }
    if (w.report() && ui.analysis_page)
        ImGui::TextWrapped("Report #%llu uses its saved inputs; current edits and pending attempts are separate.",static_cast<unsigned long long>(w.report()->request_id));
    if (!ui.error.empty())
    {
        ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"Action failed: %s",ui.error.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",ui.error.c_str());
    }
    if(!ui.analysis_page && (w.draft().workload.arrivals!=scenario::ArrivalMode::Constant || w.draft().workload.processing!=scenario::ProcessingMode::Constant)) {
        ImGui::BeginDisabled(w.runs().size()>=workspace::max_custom_runs);
        if(ImGui::Button("Run workload simulation")) queue({WorkspaceAction::Run});
        ui.run_workload=center();
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::TextDisabled("Validate before launch; active runs keep their submitted workload.");
    }
    if(ImGui::BeginPopupModal("Discard edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("This action discards unsaved draft edits. Opening replaces the original baseline and removes runs and candidates. Completed Study History stays available. Load file as playable run preserves the current workspace.");
        if(ImGui::Button("Discard and continue") && ui.confirmation) { ui.confirmation->discard=true; ui.pending.push_back(*ui.confirmation); ui.confirmation.reset(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if(ImGui::Button("Keep editing")) { ui.confirmation.reset(); ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
    }
    if(ui.analysis_page || ImGui::BeginTabBar("Workspace tabs"))
    {
        const int requested=ui.requested_tab; ui.requested_tab=-1;
        const auto tab=[&](const char* name,int index)
        {
            if(ui.analysis_page) { if(index!=2) return false; }
            else if(index==2 || !ImGui::BeginTabItem(name,nullptr,requested==index?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None)) return false;
            ImGui::BeginChild(name,{0,0});
            ui.current_tab=index;
            if(ui.scroll_target==20) { ImGui::SetScrollY(0); ui.scroll_target=-1; }
            return true;
        };
        if(tab("Edit",0))
        {
            if(ImGui::Button("Describe Your Warehouse",{-1,36*s})) ui.language.open_requested=true;
            ui.describe=center();scroll(19);
            ImGui::TextWrapped("Draft inputs are separate from the active warehouse. Add an alternative, then compare it against the loaded baseline.");
            if(w.dirty()) ImGui::TextColored({viewer::design::waiting.x,viewer::design::waiting.y,viewer::design::waiting.z,1},"UNSAVED CHANGES");
            const auto field=[&](std::size_t i)
            {
                const auto& info=workspace::field_presentation[i];
                ImGui::PushID(static_cast<int>(i)); ImGui::TextUnformatted(info.label);
                if(*info.unit) { ImGui::SameLine(); ImGui::TextDisabled("(%s)",info.unit); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",info.description);
                const bool workload_timing=(i==4 && w.draft().workload.arrivals!=scenario::ArrivalMode::Constant) ||
                    (i==5 && w.draft().workload.processing!=scenario::ProcessingMode::Constant);
                ImGui::BeginDisabled(workload_timing);
                auto value=w.draft().values[i]; if(text("##Value",value)) w.edit(static_cast<workspace::Field>(i),std::move(value));
                ImGui::EndDisabled();
                if(i==0) ui.name_field=center(); if(i==3) ui.forklift_field=center();if(i==8)ui.seed_field=center(); scroll(static_cast<int>(i));
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",info.description);
                for(const auto& error:w.errors()) if(error.field==workspace::field_names[i]) ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"%s",error.message.c_str());
                ImGui::PopID();
            };
            const auto group=[&](const char* title,std::size_t first,std::size_t last,int columns)
            {
                ImGui::Spacing(); ImGui::PushFont(nullptr,viewer::design::heading); ImGui::TextUnformatted(title); ImGui::PopFont(); ImGui::Separator();
                if(ImGui::BeginTable(title,columns,ImGuiTableFlags_SizingStretchSame))
                { for(auto i=first;i<=last;++i) { ImGui::TableNextColumn(); field(i); } ImGui::EndTable(); }
            };
            const bool spacious=ImGui::GetContentRegionAvail().x/s>=700;
            if(ImGui::BeginTable("Editor composition",spacious?2:1,ImGuiTableFlags_SizingStretchProp))
            {
                if(spacious) { ImGui::TableSetupColumn("Inputs",ImGuiTableColumnFlags_WidthStretch,0.65f); ImGui::TableSetupColumn("Overview",ImGuiTableColumnFlags_WidthStretch,0.35f); }
                ImGui::TableNextColumn();
                group("Scenario details",0,0,1);
                const int columns=ImGui::GetContentRegionAvail().x/s>=450?3:ImGui::GetContentRegionAvail().x/s>=300?2:1;
                group("Warehouse resources",1,3,columns); group("Operations",4,5,columns>1?2:1); group("Simulation",6,8,columns);
                ImGui::SetNextItemOpen(w.draft().workload.arrivals!=scenario::ArrivalMode::Constant || w.draft().workload.processing!=scenario::ProcessingMode::Constant,ImGuiCond_Once);
                if(ImGui::CollapsingHeader("Workload patterns"))
                {
                    auto workload=w.draft().workload;bool changed=false;
                    const auto workload_text=[&](const char* label,std::string& value) {
                        ImGui::PushID(label);ImGui::TextUnformatted(label);const bool edit=text("##WorkloadValue",value);ImGui::PopID();return edit;
                    };
                    int arrivals=static_cast<int>(workload.arrivals),processing=static_cast<int>(workload.processing);
                    ImGui::TextUnformatted("Arrival pattern");scroll(22);
                    ImGui::SetNextItemWidth(-1);
                    constexpr std::array arrival_labels{"Fixed interval","Time-varying profile","Seeded uniform intervals","Homogeneous Poisson","Piecewise Poisson"};
                    const bool arrivals_open=ImGui::BeginCombo("##ArrivalPattern",arrival_labels[static_cast<std::size_t>(arrivals)]);
                    ui.workload_arrival_mode=center();
                    if(arrivals_open) {
                        for(int i=0;i<5;++i) {if(ImGui::Selectable(arrival_labels[static_cast<std::size_t>(i)],i==arrivals)) {workload.arrivals=static_cast<scenario::ArrivalMode>(i);changed=true;}ui.workload_arrival_options[static_cast<std::size_t>(i)]=center();}
                        ImGui::EndCombo();
                    }
                    ImGui::TextWrapped("All window times are simulated seconds from the start. The seed above controls repeatable random samples.");
                    if(workload.arrivals==scenario::ArrivalMode::Poisson || workload.arrivals==scenario::ArrivalMode::PiecewisePoisson) {
                        ImGui::TextWrapped("Generator 2 | explicit seed | engine tick grid | no forced initial arrival. Counts vary across seeds; expected counts are not realized counts.");
                        if(workload.arrivals==scenario::ArrivalMode::Poisson){changed|=workload_text("Intensity (arrivals/second)",workload.rate_per_second);ui.poisson_rate=center();}
                        else {
                            ImGui::BeginChild("Poisson intensity windows",{-1,200*s},ImGuiChildFlags_Borders);
                            for(std::size_t i=0;i<workload.intensities.size();++i){ImGui::PushID(static_cast<int>(i));changed|=workload_text("Start (s)",workload.intensities[i][0]);changed|=workload_text("End (s)",workload.intensities[i][1]);changed|=workload_text("Intensity (arrivals/second)",workload.intensities[i][2]);if(ImGui::Button("Remove intensity")){workload.intensities.erase(workload.intensities.begin()+i);changed=true;ImGui::PopID();break;}ImGui::Separator();ImGui::PopID();}ImGui::EndChild();
                            ImGui::BeginDisabled(workload.intensities.size()>=scenario::max_demand_segments);
                            if(ImGui::Button("Add intensity segment")){workload.intensities.push_back({workload.intensities.empty()?"0":workload.intensities.back()[1],w.draft().values[6],"0.016666666666666666"});changed=true;}ui.add_intensity=center();ImGui::EndDisabled();
                        }
                        if(w.errors().empty()) {
                            try {
                                auto definition=w.draft_definition();const auto& cfg=std::get<scenario::WarehouseConfig>(definition.domain);
                                ImGui::Text("Expected horizon arrivals: %.6g",scenario::workload::expected_arrivals(cfg,definition.simulation.duration_seconds));
                            } catch(const std::invalid_argument&) {
                                ImGui::TextDisabled("Validate the draft to update expected arrivals.");
                            }
                        }
                    } else if(workload.arrivals==scenario::ArrivalMode::Uniform) {
                        changed|=workload_text("Minimum arrival interval (s)",workload.bounds[0]);ui.workload_bounds[0]=center();
                        changed|=workload_text("Maximum arrival interval (s)",workload.bounds[1]);ui.workload_bounds[1]=center();
                        ImGui::TextWrapped("Each delay is chosen uniformly on the tick grid inside these bounds. Arrivals start at tick zero.");
                    } else if(workload.arrivals==scenario::ArrivalMode::Profile) {
                        ImGui::TextWrapped("Each [start, end) window starts with an arrival, then repeats its interval. Gaps are idle; no windows means no arrivals.");
                        if(!workload.segments.empty()) {
                            ImGui::BeginChild("Demand windows",{-1,200*s},ImGuiChildFlags_Borders);
                            for(std::size_t i=0;i<workload.segments.size();++i) {
                                ImGui::PushID(static_cast<int>(i));
                                ImGui::Text("Window %zu",i+1);
                                changed|=workload_text("Start (s)",workload.segments[i][0]);changed|=workload_text("End (s)",workload.segments[i][1]);changed|=workload_text("Interval (s)",workload.segments[i][2]);
                                if(ImGui::Button("Remove window")) {workload.segments.erase(workload.segments.begin()+static_cast<std::ptrdiff_t>(i));changed=true;ImGui::PopID();break;}
                                ImGui::Separator();ImGui::PopID();
                            }
                            ImGui::EndChild();
                        }
                        ImGui::BeginDisabled(workload.segments.size()>=scenario::max_demand_segments);
                        if(ImGui::Button("Add demand window")) {workload.segments.push_back({workload.segments.empty()?"0":workload.segments.back()[1],w.draft().values[6],"300"});changed=true;}
                        ImGui::EndDisabled();
                    } else ImGui::TextWrapped("Uses the fixed Arrival interval above; existing scenarios retain their original timing.");
                    ImGui::TextUnformatted("Processing duration");scroll(23);ImGui::SetNextItemWidth(-1);
                    constexpr std::array processing_labels{"Fixed duration","Seeded uniform duration"};
                    const bool processing_open=ImGui::BeginCombo("##ProcessingDuration",processing_labels[static_cast<std::size_t>(processing)]);
                    ui.workload_processing_mode=center();
                    if(processing_open) {
                        for(int i=0;i<2;++i) {if(ImGui::Selectable(processing_labels[static_cast<std::size_t>(i)],i==processing)) {workload.processing=static_cast<scenario::ProcessingMode>(i);changed=true;}ui.workload_processing_options[static_cast<std::size_t>(i)]=center();}
                        ImGui::EndCombo();
                    }
                    if(workload.processing==scenario::ProcessingMode::Uniform) {
                        changed|=workload_text("Minimum processing duration (s)",workload.bounds[2]);ui.workload_bounds[2]=center();
                        changed|=workload_text("Maximum processing duration (s)",workload.bounds[3]);ui.workload_bounds[3]=center();
                        ImGui::TextWrapped("Each shipment gets one sampled requirement at arrival. Waiting or changing capacity does not resample it.");
                    }
                    if(changed) w.edit_workload(std::move(workload));
                    for(const auto& error:w.errors()) if(error.field=="Workload" || error.field=="Scenario")
                        ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"%s",error.message.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(nullptr,viewer::design::heading); ImGui::TextUnformatted("Experiment setup"); ImGui::PopFont(); ImGui::Separator();
                ImGui::TextWrapped("Loaded baseline: %s",w.baseline().name.c_str());
                const auto& c=std::get<scenario::WarehouseConfig>(w.baseline().domain);
                ImGui::TextWrapped("%u bays / %u workers / %u forklifts",c.loading_bays,c.workers,c.forklifts);
                ImGui::TextDisabled("%zu validated alternatives",w.candidates().size());
                ImGui::Spacing();
                if(ImGui::CollapsingHeader("Input ranges and model limits")) ImGui::TextWrapped("Resources: 0..100000, sum <=100000; zero blocks service. 3D views support at most 256 of each resource. Seconds: 1..604800; Hz: 1..1000; <=10M ticks and 100K worst-case arrivals. Random workloads use the explicit seed; results are one realization.");

                for(const auto& e:w.errors()) if(e.field=="Scenario") ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"%s",e.message.c_str());
                if(ImGui::Button("Validate",{-1,36*s})) queue({WorkspaceAction::Validate}); ui.validate=center();
                if(ImGui::Button("Add as candidate",{-1,36*s})) queue({WorkspaceAction::Add,{},ui.notes}); ui.add=center();
                scroll(10);
                if(ui.editing && ImGui::Button("Update edited candidate",{-1,36*s})) queue({WorkspaceAction::Update,{},ui.notes,*ui.editing});
                ImGui::BeginDisabled(w.runs().size() >= workspace::max_custom_runs);
                if(ImGui::Button("Run Simulation",{-1,36*s})) queue({WorkspaceAction::Run}); ui.run_simulation=center();
                scroll(17);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Starts paused at tick zero. Press Play in Simulation. Up to 8 retained custom runs.");
                ImGui::BeginDisabled(w.candidates().empty() || w.execution().progress().active());
                if(ImGui::Button("Compare alternatives",{-1,36*s})) compare(); ui.editor_compare=center(); scroll(16); ImGui::EndDisabled();
                ImGui::TextWrapped("Choose comparison mode and objective in Candidates. Inputs are exact integers; zero resources block service.");
                if(ImGui::Button("Revert to loaded baseline",{-1,36*s})) queue({WorkspaceAction::Revert});
                text("Candidate notes (workspace only)",ui.notes);
            ImGui::EndTable(); }
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(tab("Candidates",1))
        {
            ImGui::Text("%zu / 50 validated candidates",w.candidates().size());
            ImGui::TextWrapped("Change arrival/processing only for runs from start. Checkpoint mode accepts capacity changes only. Comparisons require equal duration and tick rate.");
            for(const auto& c:w.candidates())
            {
                const auto id=std::to_string(c.id); ImGui::PushID(id.c_str());
                const bool expanded=ImGui::TreeNode("##Candidate"); ImGui::SameLine(); ImGui::TextUnformatted(c.definition.name.c_str());
                const auto& capacities=std::get<scenario::WarehouseConfig>(c.definition.domain);
                ImGui::TextDisabled("%u bays / %u workers / %u forklifts   |   Validated",capacities.loading_bays,capacities.workers,capacities.forklifts);
                ImGui::TextWrapped("%s",c.changed_inputs.c_str());
                ImGui::BeginDisabled(w.runs().size() >= workspace::max_custom_runs);
                if(ImGui::Button("Simulate initial configuration from start")) queue({WorkspaceAction::SimulateCandidate,{},{},c.id});
                ImGui::EndDisabled();
                if(expanded)
                {
                    if(!c.notes.empty()) ImGui::TextWrapped("Notes: %s",c.notes.c_str());
                    text("New name",ui.candidate_name); text("Notes",ui.notes);
                    if(ImGui::Button("Rename")) queue({WorkspaceAction::Rename,ui.candidate_name,{},c.id}); ImGui::SameLine();
                    if(ImGui::Button("Duplicate")) queue({WorkspaceAction::Duplicate,ui.candidate_name,{},c.id});
                    if(ImGui::Button("Edit")) queue({WorkspaceAction::Edit,{},{},c.id});
                    ImGui::SameLine(); if(ImGui::Button("Remove")) { queue({WorkspaceAction::Remove,{},{},c.id}); if(ui.editing==c.id) ui.editing.reset(); }
                    if(ImGui::Button("Restore baseline values")) queue({WorkspaceAction::Restore,{},{},c.id}); ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Separator(); ImGui::TextUnformatted("Comparison mode"); ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##Mode",&ui.mode,"Run independent scenarios from start\0Compare from shared checkpoint\0");
            ImGui::TextUnformatted("Objective"); ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##Objective",&ui.objective,"Maximize completed shipments\0Minimize average queue wait\0Minimize remaining waiting shipments\0");
            if(ui.mode==1)
            {
                ImGui::Checkbox("Use preserved baseline's current time",&ui.current_checkpoint);
                if(!ui.current_checkpoint) text("Checkpoint (integer seconds)",ui.checkpoint_seconds);
            }
            ImGui::BeginDisabled(w.candidates().empty() || w.execution().progress().active());
            if(ImGui::Button("Run comparison",{-1,36*s}))
            {
                compare();
            }
            ui.compare=center(); ImGui::EndDisabled();
            scroll(11);
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(tab("Results",2))
        {
            const auto start=std::chrono::steady_clock::now();
            if(!w.report())
            {
                ImGui::PushFont(nullptr,28); ImGui::TextUnformatted(ui.sensitivity?"No sweep yet":"No comparison yet"); ImGui::PopFont();
                ImGui::TextWrapped("%s",ui.sensitivity?"Choose a parameter and bounded values, then Run Sweep. The loaded scenario is the immutable source.":"Add candidates and run a comparison. Completed reports retain the submitted inputs.");
                if(ImGui::Button("Configure an experiment",{240*s,36*s})) ui.requested_tab=0;
                if(!w.viewing_baseline() && ImGui::Button("Return to preserved baseline",{-1,36*s})) queue({WorkspaceAction::Baseline});
            }
            else if(w.report()->checkpoint_study) {draw_checkpoint_results(w,ui.checkpoint,s);if(ui.checkpoint.playback_requested){WorkspaceCommand command{WorkspaceAction::CheckpointPlayback};command.id=w.report()->request_id;command.seed=ui.checkpoint.playback_seed;command.approved=*ui.checkpoint.playback_requested;queue(std::move(command));ui.checkpoint.playback_requested.reset();}}
            else if(w.report()->interaction) draw_interaction_results(w,ui,s);
            else if(w.report()->sensitivity) draw_sensitivity_results(w,ui,s);
            else if(w.report()->statistics) draw_statistical_results(w,ui,s);
            else
            {
                const auto& report=*w.report();
                const auto* best=workspace::best_tested(report);
                ImGui::TextDisabled("%s  /  %s",workspace::objective_label(report.objective),mode_name(report.mode));
                ImGui::TextWrapped("Baseline: %s",report.baseline_definition.name.c_str());
                if(report.mode==workspace::ComparisonMode::Checkpoint) detail("Checkpoint:",viewer::format_time(report.executions.front().request.checkpoint.current_tick,report.baseline_definition.simulation.tick_rate));
                ImGui::Spacing(); ImGui::TextDisabled("BEST TESTED CONFIGURATION");
                ImGui::PushFont(nullptr,28); ImGui::TextUnformatted(best?best->candidate.name.c_str():"No eligible candidate"); ImGui::PopFont();
                if(best && ImGui::BeginTable("Outcome overview",ImGui::GetContentRegionAvail().x/s>=650?4:2,ImGuiTableFlags_SizingStretchSame))
                {
                    const auto card=[&](const char* label,const std::string& value,std::optional<double> delta,bool higher)
                    {
                        ImGui::TableNextColumn(); ImGui::TextDisabled("%s",label); ImGui::PushFont(nullptr,28); ImGui::TextUnformatted(value.c_str()); ImGui::PopFont();
                        const auto* direction=workspace::change_label(delta,higher); const auto c=!delta || *delta==0?viewer::design::secondary:std::string_view{direction}=="Improved"?viewer::design::completed:viewer::design::error;
                        ImGui::TextColored({c.x,c.y,c.z,1},"%s vs baseline",direction);
                    };
                    const auto signed_value=[](std::int64_t v){return (v>0?"+":"")+std::to_string(v);};
                    card("Completed shipments",signed_value(best->delta.completed),static_cast<double>(best->delta.completed),true);
                    card("Throughput / hour",viewer::display_decimal(best->delta.throughput_per_hour),best->delta.throughput_per_hour,true);
                    card("Average wait change",viewer::display_decimal(best->delta.average_wait_seconds)+" s",best->delta.average_wait_seconds,false);
                    card("Remaining queue",signed_value(best->delta.waiting),static_cast<double>(best->delta.waiting),false);
                    ImGui::EndTable(); ImGui::Spacing();
                }
                ImGui::TextDisabled("CONTROLLED RESOURCE EVIDENCE");
                for(const auto& probe:report.bottlenecks)
                {
                    if(probe.conclusion!=analysis::BottleneckConclusion::LikelyBottleneck || !probe.result) continue;
                    ImGui::TextColored({viewer::design::processing.x,viewer::design::processing.y,viewer::design::processing.z,1},"%s: %u -> %u  /  %+lld completed  /  %+lld waiting",warehouse::resource_name(probe.resource).data(),probe.baseline_capacity,probe.tested_capacity,static_cast<long long>(probe.result->delta.completed),static_cast<long long>(probe.result->delta.waiting));
                }
                ImGui::TextDisabled("Add-one probes on the baseline; conclusions apply only to tested conditions.");
                if(!report.warnings.empty())
                {
                    const auto c=viewer::design::waiting; ImGui::PushStyleColor(ImGuiCol_Text,{c.x,c.y,c.z,1});
                    for(const auto& warning:report.warnings) ImGui::TextWrapped("%s",warning.c_str()); ImGui::PopStyleColor();
                }
                if(ImGui::BeginTable("Result metrics",7,ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg,{0,std::min(170.0f,60.0f+24*static_cast<float>(report.candidates.size()+1))*s},760*s))
                {
                    for(const auto* col:{"Configuration","Resources B/W/F","Completed","Throughput / h","Mean wait","Waiting","Rank"})
                    {
                        const std::string_view name{col}; const float width=name=="Configuration"?165.0f:name=="Rank"?40.0f:name=="Completed"?82.0f:name=="Waiting"?60.0f:name=="Resources B/W/F"?125.0f:110.0f;
                        ImGui::TableSetupColumn(col,ImGuiTableColumnFlags_WidthFixed,width*s);
                    }
                    ImGui::TableSetupScrollFreeze(1,1); ImGui::TableHeadersRow();
                    const auto row=[&](const analysis::CandidateResult& c)
                    {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(c.candidate.name.c_str());
                        if(ImGui::IsItemClicked()) ui.result_name=c.candidate.name;
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",c.candidate.name.c_str());
                        const auto& m=c.metrics; const auto& a=c.candidate.resources;
                        for(const auto& value:{std::to_string(a.loading_bays)+" / "+std::to_string(a.workers)+" / "+std::to_string(a.forklifts),std::to_string(m.completed),viewer::display_decimal(m.throughput_per_hour),viewer::display_duration(m.average_wait_seconds),std::to_string(m.waiting),c.rank?std::to_string(*c.rank):std::string{"Baseline"}}) { ImGui::TableNextColumn(); ImGui::TextUnformatted(value.c_str()); }
                    };
                    row(report.baseline);
                    for(const auto& name:ui.result_order)
                    { const auto found=std::find_if(report.candidates.begin(),report.candidates.end(),[&](const auto& c){return c.candidate.name==name;}); if(found!=report.candidates.end()) row(*found); }
                    ImGui::EndTable();
                }
                ImGui::TextWrapped("Selected: %s",ui.result_name.empty()?"Baseline":ui.result_name.c_str()); ImGui::SetNextItemWidth(-1);
                if(ImGui::BeginCombo("##Result","Choose configuration"))
                {
                    if(literal_choice("Baseline",ui.result_name=="Baseline",0)) ui.result_name="Baseline";
                    int id=1; for(const auto& c:report.candidates) if(literal_choice(c.candidate.name,ui.result_name==c.candidate.name,id++)) ui.result_name=c.candidate.name;
                    ImGui::EndCombo();
                }
                const auto found=std::find_if(report.candidates.begin(),report.candidates.end(),[&](const auto& c){return c.candidate.name==ui.result_name;});
                const auto& selected=found==report.candidates.end()?report.baseline:*found;
                ImGui::TextWrapped("Arrivals %llu | Completed %llu | Waiting %llu",static_cast<unsigned long long>(selected.metrics.arrived),static_cast<unsigned long long>(selected.metrics.completed),static_cast<unsigned long long>(selected.metrics.waiting));
                if(ui.scroll_target==14) { ImGui::SetScrollHereY(0.1f); ui.scroll_target=-1; }
                comparison_chart(report.baseline,selected);
                metrics(selected);
                if(found!=report.candidates.end()) ImGui::TextWrapped("Changed inputs: %s",report.changed_inputs.at(static_cast<std::size_t>(found-report.candidates.begin())).c_str());
                if(ImGui::Button("View this final result",{-1,36*s})) queue({WorkspaceAction::View,found==report.candidates.end()?"":ui.result_name}); ui.candidate_view=center();
                scroll(12);
                ImGui::BeginDisabled(report.mode == workspace::ComparisonMode::Checkpoint || w.runs().size() >= workspace::max_custom_runs);
                if(ImGui::Button("Simulate This Candidate",{-1,36*s})) queue({WorkspaceAction::SimulateResult,found==report.candidates.end()?"":ui.result_name}); ui.simulate_result=center();
                scroll(18);
                ImGui::EndDisabled();
                if(report.mode == workspace::ComparisonMode::Checkpoint) ImGui::TextWrapped("Checkpoint branches are final previews only. To simulate initial configuration from start, use Candidates; that creates a separate timeline.");
                if(ImGui::Button("Return to selected live run",{-1,36*s})) queue({WorkspaceAction::Live});
                if(ImGui::Button("Return to preserved baseline",{-1,36*s})) queue({WorkspaceAction::Baseline}); ui.baseline_view=center();
                const bool probes=ImGui::CollapsingHeader("Controlled resource probes"); ui.probe_toggle=center();
                if(probes)
                {
                    ImGui::TextWrapped("Measured add-one changes on the baseline. Likely bottleneck inference applies only to this objective, history and horizon.");
                    for(const auto& b:report.bottlenecks)
                    {
                        ImGui::Separator(); ImGui::Text("%s: %u -> %u",warehouse::resource_name(b.resource).data(),b.baseline_capacity,b.tested_capacity);
                        if(b.resource==warehouse::ResourceType::Forklift && ui.scroll_target==13) { ImGui::SetScrollHereY(0); ui.scroll_target=-1; }
                        const char* conclusion=b.conclusion==analysis::BottleneckConclusion::LikelyBottleneck?"Likely bottleneck under tested conditions":b.conclusion==analysis::BottleneckConclusion::NoPositiveEffect?"No positive objective effect":b.conclusion==analysis::BottleneckConclusion::Unavailable?"Probe unavailable":"Inconclusive";
                        ImGui::TextWrapped("%s",conclusion);
                        if(b.result)
                        {
                            detail("Completed change:",std::to_string(b.result->delta.completed));
                            detail("Queue change:",std::to_string(b.result->delta.waiting));
                            detail("Mean wait change (s):",viewer::display_decimal(b.result->delta.average_wait_seconds));
                            detail("Probe utilization B/W/F:",viewer::display_percent(b.result->metrics.loading_bay_utilization)+" / "+viewer::display_percent(b.result->metrics.worker_utilization)+" / "+viewer::display_percent(b.result->metrics.forklift_utilization));
                        }
                        if(ImGui::TreeNode((std::string{"Measured observation##"}+std::string{warehouse::resource_name(b.resource)}).c_str())) { ImGui::TextWrapped("%s",b.summary.c_str()); ImGui::TreePop(); }
                    }
                }
                if(ImGui::CollapsingHeader("Interpretation and limits")) for(const auto& line:report.limitations) ImGui::TextWrapped("%s",line.c_str());
                text("Export destination (new file)",ui.export_path);
                ui.export_field=center(); scroll(90);
                if(ImGui::Button("Save JSON report...")) { ui.dialog_used=true; const auto path=save_report_dialog(false,ui.export_path,ui.error); if(path) { const auto bytes=path->u8string(); queue({WorkspaceAction::Json,std::string{bytes.begin(),bytes.end()}}); } } ImGui::SameLine();
                if(ImGui::Button("Save CSV summary...")) { const auto path=save_report_dialog(true,ui.export_path,ui.error); if(path) { const auto bytes=path->u8string(); queue({WorkspaceAction::Csv,std::string{bytes.begin(),bytes.end()}}); } }
                if(ImGui::Button("Export JSON to path")) queue({WorkspaceAction::Json,ui.export_path}); ui.export_json=center(); ImGui::SameLine();
                if(ImGui::Button("Export CSV to path")) queue({WorkspaceAction::Csv,ui.export_path}); ui.export_csv=center(); scroll(15);
                ImGui::TextWrapped("Choose a new .json or .csv filename. Existing files are never overwritten. JSON can be replay-verified; CSV is a summary of that report.");
            }
            if(w.report()) {draw_confirmation(w,ui,s);save_study_view(w,ui);}
            ui.results_ui_us+=std::chrono::duration<double,std::micro>{std::chrono::steady_clock::now()-start}.count(); ++ui.results_frames;
            ImGui::TextWrapped("%s",w.message().c_str());
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(tab("Files",3))
        {
            ImGui::TextWrapped("Open a local warehouse JSON scenario. Paths are explicit; no new file-picker dependency is required.");
            text("Scenario path",ui.path); if(ImGui::Button("Open scenario",{-1,36*s})) queue({WorkspaceAction::Open,ui.path});
            if(ImGui::Button("Load file as playable run",{-1,36*s})) queue({WorkspaceAction::LoadRun,ui.path});
            ImGui::Separator(); text("Save draft as new scenario",ui.save_path); if(ImGui::Button("Save as new scenario",{-1,36*s})) queue({WorkspaceAction::Save,ui.save_path});
            ImGui::TextWrapped("Opening a scenario clears workspace candidates/results. Unsaved draft changes require confirmation; failed file operations retain edits.");
            ImGui::TextWrapped("%s",w.message().c_str());
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(!ui.analysis_page) ImGui::EndTabBar();
    }
    ImGui::End();
}
