#include "workspace_panel.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <simforge/workspace/presentation.hpp>
#include "save_dialog.hpp"

using namespace simforge;
void apply_workspace(workspace::ScenarioWorkspace& w, const WorkspaceCommand& c)
{
    switch(c.action)
    {
    case WorkspaceAction::Validate: (void)w.validate_draft(); break;
    case WorkspaceAction::Revert: w.revert(c.discard); break;
    case WorkspaceAction::Open: w.open(std::filesystem::u8path(c.text),c.discard); break;
    case WorkspaceAction::Save: w.save_draft(std::filesystem::u8path(c.text)); break;
    case WorkspaceAction::Run: w.run_draft(); break;
    case WorkspaceAction::Add: w.add_candidate(c.notes); break;
    case WorkspaceAction::Edit: w.edit_candidate(c.id,c.discard); break;
    case WorkspaceAction::Update: w.update_candidate(c.id,c.notes); break;
    case WorkspaceAction::Rename: w.rename_candidate(c.id,c.text); break;
    case WorkspaceAction::Duplicate: w.duplicate_candidate(c.id,c.text); break;
    case WorkspaceAction::Remove: w.remove_candidate(c.id); break;
    case WorkspaceAction::Restore: w.restore_candidate(c.id); break;
    case WorkspaceAction::Compare: w.compare(c.mode,c.objective,c.tick); break;
    case WorkspaceAction::View: w.view_result(c.text.empty() ? std::nullopt : std::optional{c.text}); break;
    case WorkspaceAction::Baseline: w.return_to_baseline(); break;
    case WorkspaceAction::Json: w.export_json(std::filesystem::u8path(c.text)); break;
    case WorkspaceAction::Csv: w.export_csv(std::filesystem::u8path(c.text)); break;
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
    const auto queue=[&](WorkspaceCommand c)
    {
        if((w.dirty() && (c.action==WorkspaceAction::Open || c.action==WorkspaceAction::Revert || c.action==WorkspaceAction::Edit)) ||
            (c.action==WorkspaceAction::Open && !w.candidates().empty()))
        { ui.confirmation=std::move(c); ImGui::OpenPopup("Discard edits?"); }
        else ui.pending.push_back(std::move(c));
    };
    const auto compare=[&]
    {
        WorkspaceCommand c{WorkspaceAction::Compare}; c.mode=ui.mode==0?workspace::ComparisonMode::Initial:workspace::ComparisonMode::Checkpoint;
        c.objective=static_cast<analysis::Objective>(ui.objective);
        if(ui.mode==1 && !ui.current_checkpoint)
        {
            try { c.tick=workspace::checkpoint_tick(w.baseline(),ui.checkpoint_seconds); queue(std::move(c)); ui.requested_tab=2; }
            catch(const std::exception& e) { ui.error=e.what(); }
        }
        else { queue(std::move(c)); ui.requested_tab=2; }
    };
    if(ImGui::BeginPopupModal("Discard edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("This action discards unsaved draft edits. Opening also clears workspace candidates and results.");
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
            if(ui.scroll_target==20) { ImGui::SetScrollY(0); ui.scroll_target=-1; }
            if(!ui.error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text,{viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1}); ImGui::TextWrapped("%s",ui.error.c_str()); ImGui::PopStyleColor(); }
            return true;
        };
        if(tab("Edit",0))
        {
            ImGui::TextWrapped("Draft inputs are separate from the active warehouse. Add an alternative, then compare it against the loaded baseline.");
            if(w.dirty()) ImGui::TextColored({viewer::design::waiting.x,viewer::design::waiting.y,viewer::design::waiting.z,1},"UNSAVED CHANGES");
            const auto field=[&](std::size_t i)
            {
                const auto& info=workspace::field_presentation[i];
                ImGui::PushID(static_cast<int>(i)); ImGui::TextUnformatted(info.label);
                if(*info.unit) { ImGui::SameLine(); ImGui::TextDisabled("(%s)",info.unit); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",info.description);
                auto value=w.draft().values[i]; if(text("##Value",value)) w.edit(static_cast<workspace::Field>(i),std::move(value));
                if(i==0) ui.name_field=center(); if(i==3) ui.forklift_field=center(); scroll(static_cast<int>(i));
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
                ImGui::TableNextColumn();
                ImGui::PushFont(nullptr,viewer::design::heading); ImGui::TextUnformatted("Experiment setup"); ImGui::PopFont(); ImGui::Separator();
                ImGui::TextWrapped("Loaded baseline: %s",w.baseline().name.c_str());
                const auto& c=std::get<scenario::WarehouseConfig>(w.baseline().domain);
                ImGui::TextWrapped("%u bays / %u workers / %u forklifts",c.loading_bays,c.workers,c.forklifts);
                ImGui::TextDisabled("%zu validated alternatives",w.candidates().size());
                ImGui::Spacing();
                if(ImGui::CollapsingHeader("Input ranges and model limits")) ImGui::TextWrapped("Resources: 0..100000, sum <=100000; zero blocks service. 3D views support at most 256 of each resource. Seconds: 1..604800; Hz: 1..1000; <=10M ticks and 100K arrivals. Seed is retained; this warehouse has no random behavior.");

                for(const auto& e:w.errors()) if(e.field=="Scenario") ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"%s",e.message.c_str());
                if(ImGui::Button("Validate",{-1,36*s})) queue({WorkspaceAction::Validate}); ui.validate=center();
                if(ImGui::Button("Add as candidate",{-1,36*s})) queue({WorkspaceAction::Add,{},ui.notes}); ui.add=center();
                scroll(10);
                if(ui.editing && ImGui::Button("Update edited candidate",{-1,36*s})) queue({WorkspaceAction::Update,{},ui.notes,*ui.editing});
                if(ImGui::Button("Run draft from start",{-1,36*s})) queue({WorkspaceAction::Run});
                ImGui::BeginDisabled(w.candidates().empty());
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
            ImGui::BeginDisabled(w.candidates().empty());
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
                ImGui::PushFont(nullptr,28); ImGui::TextUnformatted("No comparison yet"); ImGui::PopFont();
                ImGui::TextWrapped("Add candidates and run a comparison. Completed reports are cached until candidates change.");
                if(ImGui::Button("Configure an experiment",{240*s,36*s})) ui.requested_tab=0;
                if(!w.viewing_baseline() && ImGui::Button("Return to preserved baseline",{-1,36*s})) queue({WorkspaceAction::Baseline});
            }
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
                if(ImGui::Button("Return to preserved baseline",{-1,36*s})) queue({WorkspaceAction::Baseline}); ui.baseline_view=center();
                scroll(12);
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
                ui.export_field=center(); scroll(15);
                if(ImGui::Button("Save JSON report...")) { ui.dialog_used=true; const auto path=save_report_dialog(false,ui.export_path,ui.error); if(path) { const auto bytes=path->u8string(); queue({WorkspaceAction::Json,std::string{bytes.begin(),bytes.end()}}); } } ImGui::SameLine();
                if(ImGui::Button("Save CSV summary...")) { const auto path=save_report_dialog(true,ui.export_path,ui.error); if(path) { const auto bytes=path->u8string(); queue({WorkspaceAction::Csv,std::string{bytes.begin(),bytes.end()}}); } }
                if(ImGui::Button("Export JSON to path")) queue({WorkspaceAction::Json,ui.export_path}); ui.export_json=center(); ImGui::SameLine();
                if(ImGui::Button("Export CSV to path")) queue({WorkspaceAction::Csv,ui.export_path}); ui.export_csv=center();
                ImGui::TextWrapped("Choose a new .json or .csv filename. Existing files are never overwritten. JSON can be replay-verified; CSV is a summary of that report.");
            }
            ui.results_ui_us+=std::chrono::duration<double,std::micro>{std::chrono::steady_clock::now()-start}.count(); ++ui.results_frames;
            ImGui::TextWrapped("%s",w.message().c_str());
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(tab("Files",3))
        {
            ImGui::TextWrapped("Open a local warehouse JSON scenario. Paths are explicit; no new file-picker dependency is required.");
            text("Scenario path",ui.path); if(ImGui::Button("Open scenario",{-1,36*s})) queue({WorkspaceAction::Open,ui.path});
            ImGui::Separator(); text("Save draft as new scenario",ui.save_path); if(ImGui::Button("Save as new scenario",{-1,36*s})) queue({WorkspaceAction::Save,ui.save_path});
            ImGui::TextWrapped("Opening a scenario clears workspace candidates/results. Unsaved draft changes require confirmation; failed file operations retain edits.");
            ImGui::TextWrapped("%s",w.message().c_str());
            ImGui::EndChild(); if(!ui.analysis_page) ImGui::EndTabItem();
        }
        if(!ui.analysis_page) ImGui::EndTabBar();
    }
    ImGui::End();
}
