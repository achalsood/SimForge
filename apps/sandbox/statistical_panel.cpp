#include "statistical_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <sstream>
#include <cmath>
using namespace simforge;
namespace {
glm::vec2 center() {const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
void input(const char* label,std::string& value) {std::array<char,4097> text{};std::copy_n(value.data(),std::min(value.size(),text.size()-1),text.data());ImGui::PushID(label);ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);if(ImGui::InputText("##Value",text.data(),text.size())) value=text.data();ImGui::PopID();}
std::uint64_t integer(const std::string& text) {std::uint64_t n=0;const auto r=std::from_chars(text.data(),text.data()+text.size(),n);if(r.ec!=std::errc{} || r.ptr!=text.data()+text.size()) throw std::invalid_argument("Enter unsigned integer replication counts and seeds");return n;}
std::string display(std::optional<double> x) {return viewer::display_decimal(x);}
}
analysis::ReplicationPlan statistical_plan(const WorkspaceUI& ui) {
    analysis::ReplicationPlan p;const auto count=integer(ui.replications);if(count>analysis::max_replications) throw std::invalid_argument("At most 100 replications");p.count=static_cast<std::uint32_t>(count);
    if(ui.seed_policy==0) p.base_seed=integer(ui.base_seed);else {p.policy=analysis::SeedPolicy::Explicit;p.base_seed=0;
        auto text=ui.seed_list;std::replace(text.begin(),text.end(),',',' ');std::istringstream values(text);std::string value;while(values>>value) {if(p.explicit_seeds.size()==analysis::max_replications) throw std::length_error("At most 100 seeds");p.explicit_seeds.push_back(integer(value));}}
    (void)analysis::replication_seeds(p);return p;
}
void draw_statistical_setup(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float s) {
    ImGui::Checkbox("Multi-Seed Statistical",&ui.statistical);ui.statistical_toggle=center();
    if(!ui.statistical) return;
    if(ui.close_statistical_settings) {ImGui::SetNextItemOpen(false);ui.close_statistical_settings=false;}
    const bool settings=ImGui::CollapsingHeader("Statistical settings",ImGuiTreeNodeFlags_DefaultOpen);ui.statistical_settings_header=center();
    if(settings) {
        if(ImGui::BeginTable("Replication settings",ImGui::GetContentRegionAvail().x/s<650?1:3,ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();input("Paired replications (1..100)",ui.replications);ui.statistical_count=center();
            ImGui::TableNextColumn();ImGui::TextUnformatted("Seed policy");ImGui::SetNextItemWidth(-1);ImGui::Combo("##SeedPolicy",&ui.seed_policy,"Sequential base + index\0Explicit distinct seeds\0");
            ImGui::TableNextColumn();if(ui.seed_policy==0) input("Base seed",ui.base_seed);else input("Seed list (comma or space separated)",ui.seed_list);ui.statistical_seed=center();ImGui::EndTable();
        }
        ImGui::TextUnformatted("Objective for point ranking");ImGui::SetNextItemWidth(-1);ImGui::Combo("##StatisticalObjective",&ui.objective,"Maximize completed shipments\0Minimize average queue wait\0Minimize remaining waiting shipments\0");
        ImGui::TextUnformatted("Comparison mode");ImGui::SetNextItemWidth(-1);ImGui::Combo("##StatisticalComparisonMode",&ui.mode,"Fresh-start configurations\0Shared checkpoint (Single Seed only)\0");
        ImGui::TextWrapped("95%% paired Student-t intervals. Fresh starts only; identical external workloads per seed. Uses all %zu validated candidates (maximum 20).",w.candidates().size());
        ImGui::TextWrapped("One/two seeds: descriptive summaries only. Deterministic workloads: no stochastic interval. Work budget: 20 million conservative arrival executions including probes.");
        ImGui::BeginDisabled(w.candidates().empty() || w.execution().progress().active());
        if(ImGui::Button("Run paired statistical experiment",{-1,32*s})) {
            try {WorkspaceCommand c{WorkspaceAction::Compare};c.mode=static_cast<workspace::ComparisonMode>(ui.mode);c.objective=static_cast<analysis::Objective>(ui.objective);c.replications=statistical_plan(ui);ui.pending.push_back(std::move(c));ui.close_statistical_settings=true;ui.error.clear();}
            catch(const std::exception& e) {ui.error=e.what();}
        }ui.statistical_start=center();ImGui::EndDisabled();
        if(ui.mode!=0) ImGui::TextWrapped("Select fresh-start comparison. Shared-checkpoint statistical experiments are deferred; use Single Seed for historical checkpoints.");
    }
}
void draw_statistical_results(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float s) {
    const auto& r=*w.report()->statistics;
    ImGui::Text("%u paired seeds | 95%% paired-t method v1 | %llu simulation executions",r.request.plan.count,static_cast<unsigned long long>(r.simulation_executions));
    ImGui::TextWrapped("Submitted baseline: %s | Fresh-start capacity comparisons",r.request.baseline.name.c_str());
    ImGui::TextWrapped("Point ranking objective: %s",workspace::objective_label(r.request.objective));
    ImGui::TextWrapped("Point-estimate ranking is separate from uncertainty. Nominal intervals are not adjusted for multiple candidates or metrics.");
    constexpr std::array labels{"Completed shipments","Remaining waiting shipments","Throughput per hour","Average started queue wait","Maximum started queue wait"};
    ImGui::TextUnformatted("Metric (raw candidate - baseline effect)");ImGui::SetNextItemWidth(-1);
    if(ui.scroll_target==31) {ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    const bool metric_open=ImGui::BeginCombo("##StatisticalMetric",labels[static_cast<std::size_t>(ui.statistical_metric)]);ui.statistical_metric_control=center();
    if(metric_open) {for(int j=0;j<5;++j) {if(ImGui::Selectable(labels[static_cast<std::size_t>(j)],ui.statistical_metric==j)) ui.statistical_metric=j;ui.statistical_metric_options[static_cast<std::size_t>(j)]=center();}ImGui::EndCombo();}
    const auto selected_metric=static_cast<analysis::StatisticalMetric>(ui.statistical_metric);
    ImGui::Text("%s | %s | %s",analysis::metric_name(selected_metric).data(),analysis::metric_unit(selected_metric).data(),analysis::higher_is_better(selected_metric)?"Higher is better":"Lower is better");
    float extent=1;for(const auto& c:r.candidates) {const auto& m=c.metrics[static_cast<std::size_t>(selected_metric)];for(auto x:{m.mean_difference,m.lower,m.upper}) if(x) extent=std::max(extent,static_cast<float>(std::abs(*x)));}
    const auto pos=ImGui::GetCursorScreenPos();const float label_width=std::min(220*s,ImGui::GetContentRegionAvail().x*0.4f);
    const float width=std::max(25.0f,ImGui::GetContentRegionAvail().x-label_width-12*s),left=pos.x+label_width,zero=left+width/2;
    auto* draw=ImGui::GetWindowDrawList();const float height=28*s*static_cast<float>(r.candidates.size());draw->AddLine({zero,pos.y},{zero,pos.y+height},IM_COL32(170,180,195,255));
    std::size_t i=0;for(const auto& c:r.candidates) {const auto& m=c.metrics[static_cast<std::size_t>(selected_metric)];const float y=pos.y+(i+++0.5f)*28*s;
        draw->AddText({pos.x,y-7*s},IM_COL32(225,230,240,255),c.name.c_str());const auto x=[&](double value){return zero+static_cast<float>(value)/extent*width*0.45f;};
        if(m.lower && m.upper) {draw->AddLine({x(*m.lower),y},{x(*m.upper),y},IM_COL32(80,190,240,255),2*s);draw->AddLine({x(*m.lower),y-5*s},{x(*m.lower),y+5*s},IM_COL32(80,190,240,255));draw->AddLine({x(*m.upper),y-5*s},{x(*m.upper),y+5*s},IM_COL32(80,190,240,255));}
        if(m.mean_difference) draw->AddCircleFilled({x(*m.mean_difference),y},4*s,IM_COL32(245,210,100,255));
    }ImGui::Dummy({ImGui::GetContentRegionAvail().x,height});ImGui::TextDisabled("Zero-effect line; markers are means, whiskers are nominal 95%% intervals. Missing intervals are omitted.");
    ImGui::TextDisabled("Effect scale: -%.3g to +%.3g %s",extent,extent,analysis::metric_unit(selected_metric).data());
    if(ImGui::BeginTable("Paired summary",6,ImGuiTableFlags_ScrollX|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg,{0,0},900*s)) {
        for(auto col:{"Candidate / point rank","Baseline mean","Candidate mean","Paired mean delta","95% interval","Favorable / n"}) ImGui::TableSetupColumn(col);ImGui::TableHeadersRow();
        for(const auto& c:r.candidates) {const auto& m=c.metrics[static_cast<std::size_t>(selected_metric)];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s / %s",c.name.c_str(),c.rank?std::to_string(*c.rank).c_str():"unavailable");
            for(const auto& value:{display(m.baseline_mean),display(m.candidate_mean),display(m.mean_difference),m.lower?"["+display(m.lower)+", "+display(m.upper)+"]":"unavailable",std::to_string(analysis::higher_is_better(selected_metric)?m.positive:m.negative)+" / "+std::to_string(m.count)}) {ImGui::TableNextColumn();ImGui::TextUnformatted(value.c_str());}}
        ImGui::EndTable();
    }
    if(ui.result_name.empty() || std::none_of(r.candidates.begin(),r.candidates.end(),[&](const auto& c){return c.name==ui.result_name;})) ui.result_name=r.candidates.front().name;
    ImGui::TextUnformatted("Inspect candidate");if(ui.scroll_target==34) {ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    ImGui::TextWrapped("Selected candidate: %s",ui.result_name.c_str());
    ImGui::SetNextItemWidth(-1);const bool candidate_open=ImGui::BeginCombo("##StatisticalCandidate","Choose configuration");ui.statistical_candidate_control=center();
    if(candidate_open) {ui.statistical_candidate_options.clear();for(const auto& c:r.candidates) {const auto identity=std::to_string(c.candidate_id);ImGui::PushID(identity.c_str());
        const bool selected=ImGui::Selectable("##Candidate",c.name==ui.result_name);const auto pos=ImGui::GetItemRectMin();ImGui::GetWindowDrawList()->AddText(pos,ImGui::GetColorU32(ImGuiCol_Text),c.name.c_str());if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",c.name.c_str());
        if(selected) ui.result_name=c.name;ui.statistical_candidate_options.push_back(center());ImGui::PopID();}ImGui::EndCombo();}
    const auto found=std::find_if(r.candidates.begin(),r.candidates.end(),[&](const auto& c){return c.name==ui.result_name;});const auto index=static_cast<std::size_t>(found-r.candidates.begin());const auto& summary=found->metrics[static_cast<std::size_t>(selected_metric)];
    ImGui::TextWrapped("%s",summary.diagnostic.c_str());ImGui::Text("Defined pairs %u / %u | Positive %u, negative %u, zero %u",summary.defined,summary.count,summary.positive,summary.negative,summary.zero);
    ImGui::Text("SD %s | SE %s | Observed range [%s, %s] | Median %s",display(summary.standard_deviation).c_str(),display(summary.standard_error).c_str(),display(summary.minimum).c_str(),display(summary.maximum).c_str(),display(summary.median).c_str());
    ImGui::Text("Descriptive relative change in means: %s%% (no percentage interval)",display(summary.relative_change_percent).c_str());
    if(ui.scroll_target==32) {ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    if(ImGui::CollapsingHeader("Per-seed paired observations",ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("Per seed",4,ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg,{0,180*s})) {
        for(auto col:{"Seed","Baseline","Candidate","Raw paired delta"}) ImGui::TableSetupColumn(col);ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        for(const auto& row:r.replications) {const auto b=analysis::metric_value(row.baseline,selected_metric),c=analysis::metric_value(row.candidates[index],selected_metric);ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%llu",static_cast<unsigned long long>(row.seed));for(const auto& value:{display(b),display(c),display(b&&c?std::optional<double>{*c-*b}:std::nullopt)}) {ImGui::TableNextColumn();ImGui::TextUnformatted(value.c_str());}}ImGui::EndTable();
    }
    if(ImGui::CollapsingHeader("Paired add-one probe summaries")) for(std::size_t p=0;p<3;++p) {const auto& m=r.probes[p][static_cast<std::size_t>(selected_metric)];ImGui::Text("Add one %s: mean delta %s | 95%% [%s, %s] | n=%u",warehouse::resource_name(static_cast<warehouse::ResourceType>(p)).data(),display(m.mean_difference).c_str(),display(m.lower).c_str(),display(m.upper).c_str(),m.count);}
    if(ImGui::CollapsingHeader("Statistical assumptions and limitations")) for(const auto& line:r.limitations) ImGui::TextWrapped("%s",line.c_str());
    ImGui::TextWrapped("Aggregate report only. Live baseline/custom runs remain independent; no average warehouse snapshot is shown.");
    input("Export destination (new file)",ui.export_path);ui.export_field=center();
    if(ui.scroll_target==33) {ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    if(ImGui::Button("Export statistical JSON")) ui.pending.push_back({WorkspaceAction::Json,ui.export_path});ui.export_json=center();ImGui::SameLine();
    if(ImGui::Button("Export summary CSV")) ui.pending.push_back({WorkspaceAction::Csv,ui.export_path});ui.export_csv=center();
    if(ImGui::Button("Export per-seed CSV")) ui.pending.push_back({WorkspaceAction::CsvSeeds,ui.export_path});ui.export_seeds=center();
}
