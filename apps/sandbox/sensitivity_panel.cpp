#include "sensitivity_panel.hpp"
#include "statistical_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <chrono>
using namespace simforge;
namespace {
glm::vec2 center() {auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
void input(const char* label,std::string& value) {std::array<char,4097> b{};std::copy_n(value.data(),std::min(value.size(),b.size()-1),b.data());ImGui::PushID(label);ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);if(ImGui::InputText("##Value",b.data(),b.size()))value=b.data();ImGui::PopID();}
std::uint32_t integer(const std::string& value) {std::uint32_t n=0;auto result=std::from_chars(value.data(),value.data()+value.size(),n);if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size())throw std::invalid_argument("Sweep values must be exact unsigned integers; times use whole seconds");return n;}
double real(const std::string& value) {double n=0;auto result=std::from_chars(value.data(),value.data()+value.size(),n);if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size())throw std::invalid_argument("Invalid practical threshold");return n;}
std::string display(std::optional<double> v) {return viewer::display_decimal(v);}
void chart(const analysis::SensitivityReport& r,bool marginal,float scale,glm::vec2& control) {
    const auto metric=static_cast<std::size_t>(r.request.objective);double lo=0,hi=1;const auto value=[&](const auto& p){return marginal?p.adjacent[metric].mean_difference:p.means[metric];};
    if(std::none_of(r.points.begin(),r.points.end(),[&](const auto& p){return value(p).has_value();})){ImGui::TextDisabled("No defined values for this chart across the submitted seed plan.");ImGui::Dummy({ImGui::GetContentRegionAvail().x,130*scale});control=center();return;}
    for(const auto& p:r.points) {const auto& s=marginal?p.adjacent[metric]:p.against_baseline[metric];for(auto v:{value(p),marginal?s.lower:p.mean_lower[metric],marginal?s.upper:p.mean_upper[metric]})if(v){lo=std::min(lo,*v);hi=std::max(hi,*v);}}
    const auto baseline=r.baseline_means[metric];if(!marginal&&baseline){lo=std::min(lo,*baseline);hi=std::max(hi,*baseline);}
    const auto pos=ImGui::GetCursorScreenPos();const float width=std::max(100.f,ImGui::GetContentRegionAvail().x),height=160*scale,left=pos.x+65*scale,right=pos.x+width-15*scale,top=pos.y+12*scale,bottom=pos.y+height-30*scale;
    auto* d=ImGui::GetWindowDrawList();const double xmin=r.points.front().value,xmax=std::max(xmin+1.,double(r.points.back().value));
    const auto x=[&](double v){return left+float((v-xmin)/(xmax-xmin))*(right-left);};const auto y=[&](double v){return bottom-float((v-lo)/(hi-lo))*(bottom-top);};
    d->AddLine({left,top},{left,bottom},IM_COL32(160,170,185,255));d->AddLine({left,y(0)},{right,y(0)},IM_COL32(100,110,125,255));
    d->AddText({pos.x,top},IM_COL32(200,210,225,255),display(hi).c_str());d->AddText({pos.x,bottom-12*scale},IM_COL32(200,210,225,255),display(lo).c_str());
    if(!marginal&&baseline)d->AddLine({left,y(*baseline)},{right,y(*baseline)},IM_COL32(245,190,70,255),1.5f*scale);
    std::optional<ImVec2> previous;for(std::size_t i=0;i<r.points.size();++i) {const auto& p=r.points[i];if(marginal&&!i)continue;const auto v=value(p);const auto& s=marginal?p.adjacent[metric]:p.against_baseline[metric];if(!v){previous.reset();continue;}const ImVec2 point{x(p.value),y(*v)};
        if(!marginal&&previous)d->AddLine(*previous,point,IM_COL32(90,195,235,255),2*scale);previous=point;
        if(marginal)d->AddLine({point.x,y(0)},point,IM_COL32(90,195,235,255),7*scale);
        if(marginal&&s.lower&&s.upper)d->AddLine({point.x,y(*s.lower)},{point.x,y(*s.upper)},IM_COL32(245,210,100,255),2*scale);
        if(!marginal&&p.mean_lower[metric]&&p.mean_upper[metric]){d->AddLine({point.x,y(*p.mean_lower[metric])},{point.x,y(*p.mean_upper[metric])},IM_COL32(90,195,235,255),2*scale);for(double bound:{*p.mean_lower[metric],*p.mean_upper[metric]})d->AddLine({point.x-5*scale,y(bound)},{point.x+5*scale,y(bound)},IM_COL32(90,195,235,255));}
        d->AddCircleFilled(point,4*scale,IM_COL32(245,210,100,255));const auto stride=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(r.points.size()*45*scale/std::max(1.f,right-left))));if(i==0||i+1==r.points.size()||i%stride==0)d->AddText({point.x-5*scale,bottom+7*scale},IM_COL32(200,210,225,255),std::to_string(p.value).c_str());
        if(ImGui::IsMouseHoveringRect({point.x-9*scale,top},{point.x+9*scale,bottom}))ImGui::SetTooltip("Value %u | %s: %s\n%s",p.value,analysis::metric_name(r.request.objective).data(),display(v).c_str(),marginal?p.marginal_interpretation.c_str():"Actual measured mean; gold line is source baseline");
    }ImGui::Dummy({width,height});control=center();
}
}
void draw_sensitivity_setup(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float scale) {
    ImGui::Checkbox("Sensitivity Analysis",&ui.sensitivity);ui.sweep_toggle=center();if(!ui.sensitivity)return;
    if(ui.scroll_target==40){ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    if(ui.close_sweep_settings){ImGui::SetNextItemOpen(false);ui.close_sweep_settings=false;}
    const bool settings=ImGui::CollapsingHeader("Sweep settings",ImGuiTreeNodeFlags_DefaultOpen);ui.sweep_settings_header=center();if(!settings)return;
    ImGui::TextUnformatted("Parameter (exactly one field)");ImGui::SetNextItemWidth(-1);
    constexpr std::array parameters{"Forklift capacity","Worker capacity","Loading-bay capacity","Constant arrival interval (seconds)","Constant processing duration (seconds)"};
    const auto& source=std::get<scenario::WarehouseConfig>(w.baseline().domain);
    const bool parameter_open=ImGui::BeginCombo("##SweepParameter",parameters[static_cast<std::size_t>(ui.sweep_parameter)]);ui.sweep_parameter_control=center();
    if(parameter_open) {for(int parameter=0;parameter<5;++parameter) {
        const bool compatible=(parameter!=3||source.workload.arrivals==scenario::ArrivalMode::Constant)&&(parameter!=4||source.workload.processing==scenario::ProcessingMode::Constant);
        ImGui::BeginDisabled(!compatible);if(ImGui::Selectable(parameters[static_cast<std::size_t>(parameter)],ui.sweep_parameter==parameter))ui.sweep_parameter=parameter;
        ui.sweep_parameter_options[static_cast<std::size_t>(parameter)]=center();if(!compatible&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Requires the corresponding constant workload mode; source distribution is preserved.");ImGui::EndDisabled();
    }ImGui::EndCombo();}
    ImGui::Checkbox("Explicit discrete values",&ui.sweep_explicit);
    if(ui.sweep_explicit)input("Values (comma or space separated)",ui.sweep_values);
    else if(ImGui::BeginTable("Sweep range",3)) {ImGui::TableNextColumn();input("Minimum",ui.sweep_min);ui.sweep_range_controls[0]=center();ImGui::TableNextColumn();input("Maximum",ui.sweep_max);ui.sweep_range_controls[1]=center();ImGui::TableNextColumn();input("Step",ui.sweep_step);ui.sweep_range_controls[2]=center();ImGui::EndTable();}
    ImGui::TextUnformatted("Objective");ImGui::SetNextItemWidth(-1);ImGui::Combo("##SweepObjective",&ui.sweep_objective,"Completed shipments\0Waiting shipments\0Throughput per hour\0Average started wait seconds\0Maximum started wait seconds\0");
    input("Practical improvement threshold per parameter unit",ui.sweep_threshold);
    ImGui::Checkbox("Paired multi-seed sweep",&ui.sweep_multi);ui.sweep_multi_control=center();
    if(ui.sweep_multi) {input("Replication count",ui.replications);input("Base seed",ui.base_seed);}
    ImGui::TextWrapped("Source: %s. Fresh starts; source baseline evaluated separately and reused if tested. Maximum 50 points, 100 seeds, 20 million conservative arrival executions. Duration sweeps require compatible constant modes.",w.baseline().name.c_str());
    ImGui::BeginDisabled(w.execution().progress().active());if(ImGui::Button("Run Sweep",{-1,32*scale})) {
        try {analysis::SensitivityRequest r;r.source=w.baseline();r.parameter=static_cast<analysis::SweepParameter>(ui.sweep_parameter);r.objective=static_cast<analysis::StatisticalMetric>(ui.sweep_objective);r.multi_seed=ui.sweep_multi;r.practical_threshold=real(ui.sweep_threshold);
            if(ui.sweep_explicit) {auto text=ui.sweep_values;std::replace(text.begin(),text.end(),',',' ');std::istringstream stream(text);std::string v;while(stream>>v){if(r.values.size()>=analysis::max_sweep_points)throw std::length_error("At most 50 points");r.values.push_back(integer(v));}}
            else r.values=analysis::sweep_range(integer(ui.sweep_min),integer(ui.sweep_max),integer(ui.sweep_step));
            if(ui.sweep_multi){auto settings=ui;settings.seed_policy=0;r.plan=statistical_plan(settings);}else {r.plan.count=1;r.plan.base_seed=w.baseline().simulation.random_seed;}
            analysis::validate_sensitivity_request(r);WorkspaceCommand command{WorkspaceAction::Sweep};command.sensitivity=std::move(r);ui.pending.push_back(std::move(command));ui.error.clear();ui.requested_tab=2;ui.close_sweep_settings=true;
        }catch(const std::exception& e){ui.error=e.what();}
    }ui.sweep_start=center();ImGui::EndDisabled();
}
void draw_sensitivity_results(workspace::ScenarioWorkspace& w,WorkspaceUI& ui,float scale) {
    const auto& r=*w.report()->sensitivity;const auto metric=static_cast<std::size_t>(r.request.objective);
    ImGui::TextWrapped("Sensitivity: %s | %s (%s) | %u seeds | %llu executions",analysis::parameter_name(r.request.parameter).data(),analysis::metric_name(r.request.objective).data(),analysis::metric_unit(r.request.objective).data(),r.request.plan.count,static_cast<unsigned long long>(r.simulation_executions));
    ImGui::Text("Source baseline value: %u | Practical threshold: %.4g per unit",analysis::parameter_value(r.request.source,r.request.parameter),r.request.practical_threshold);
    ImGui::TextWrapped("%s | %s. Utilization is a diagnostic, not an optimization objective.",analysis::higher_is_better(r.request.objective)?"Higher objective is favorable":"Lower objective is favorable",r.request.multi_seed?"Paired replication plan; deterministic workload intervals remain suppressed":"One realization; no confidence intervals");
    if(r.request.parameter==analysis::SweepParameter::ArrivalInterval||r.request.parameter==analysis::SweepParameter::ProcessingDuration)ImGui::TextWrapped("Workload sensitivity: demand or processing changes by design. This is not a resource-capacity optimization claim.");
    auto best=std::find_if(r.points.begin(),r.points.end(),[&](const auto& p){return r.best_point&&p.id==*r.best_point;});if(best!=r.points.end())ImGui::Text("Best observed in tested range: %u | mean %s",best->value,display(best->means[metric]).c_str());
    if(r.request.multi_seed)ImGui::TextWrapped("Gold reference is source mean when defined. Markers are actual means; blue whiskers, when available, are nominal 95%% mean intervals, not paired effects or prediction ranges. Zero variance is not a guarantee. No smoothing.");
    else ImGui::TextWrapped("Gold reference is source mean when defined. Markers are actual outcomes for one realization, with no confidence intervals. No smoothing.");
    if(r.request.multi_seed&&r.request.plan.count<20)ImGui::TextWrapped("Small sample: available Student-t mean/effect intervals may be unreliable for skewed or discrete outcomes.");
    if(!r.baseline_means[metric])ImGui::TextWrapped("Source objective is undefined. Tested means can still be inspected; paired baseline effects are unavailable.");
    const auto chart_start=std::chrono::steady_clock::now();ImGui::Text("Objective vs %s | %s",analysis::parameter_name(r.request.parameter).data(),analysis::metric_unit(r.request.objective).data());chart(r,false,scale,ui.sweep_curve);
    ImGui::TextUnformatted(r.request.multi_seed?"Adjacent change (next minus previous), nominal paired 95% intervals when available":"Adjacent change (next minus previous), one realization with no intervals");if(ui.scroll_target==42){ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}chart(r,true,scale,ui.sweep_marginal);
    ui.sweep_chart_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-chart_start).count();++ui.sweep_chart_frames;
    ImGui::TextWrapped("Intervals are not simultaneous; best-point selection introduces bias. One-factor evidence cannot establish all resource interactions or global optimality.");
    if(ImGui::BeginTable("Tested configurations",6,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,{0,std::min(320*scale,static_cast<float>(r.points.size()+1)*30*scale)},std::max(ImGui::GetContentRegionAvail().x,800*scale))) {
        constexpr std::array labels{"Value","Mean objective","Baseline delta","Baseline 95% CI","Adjacent delta / unit","Interpretation"};constexpr std::array weights{0.8f,1.2f,1.2f,1.5f,1.5f,3.5f};
        for(std::size_t column=0;column<labels.size();++column)ImGui::TableSetupColumn(labels[column],ImGuiTableColumnFlags_WidthStretch,weights[column]);ImGui::TableHeadersRow();
        for(const auto& p:r.points) {const auto& s=p.against_baseline[metric];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%u%s",p.value,p.baseline?" (baseline)":"");ImGui::TableNextColumn();ImGui::TextUnformatted(display(p.means[metric]).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(display(s.mean_difference).c_str());ImGui::TableNextColumn();ImGui::Text("%s, %s",display(s.lower).c_str(),display(s.upper).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(display(p.marginal_per_unit).c_str());ImGui::TableNextColumn();ImGui::TextWrapped("%s",p.marginal_interpretation.c_str());}ImGui::EndTable();
    }
    if(ui.scroll_target==41){ImGui::Dummy({0,1});ImGui::SetScrollHereY(0.1f);ui.scroll_target=-1;}
    ui.sweep_selected=std::clamp(ui.sweep_selected,0,static_cast<int>(r.points.size()-1));const auto& selected=r.points[static_cast<std::size_t>(ui.sweep_selected)];
    const bool point_open=ImGui::BeginCombo("Tested value",std::to_string(selected.value).c_str());ui.sweep_point_control=center();
    if(point_open) {ui.sweep_point_options.resize(r.points.size());for(std::size_t i=0;i<r.points.size();++i){if(ImGui::Selectable(std::to_string(r.points[i].value).c_str(),i==static_cast<std::size_t>(ui.sweep_selected)))ui.sweep_selected=static_cast<int>(i);ui.sweep_point_options[i]=center();}ImGui::EndCombo();}
    const auto& effect=r.points[static_cast<std::size_t>(ui.sweep_selected)].against_baseline[metric];ImGui::Text("Paired n %u, defined %u | favorable %u, unfavorable %u, zero %u",effect.count,effect.defined,analysis::higher_is_better(r.request.objective)?effect.positive:effect.negative,analysis::higher_is_better(r.request.objective)?effect.negative:effect.positive,effect.zero);ImGui::TextWrapped("%s",effect.diagnostic.c_str());
    const auto& inspected=r.points[static_cast<std::size_t>(ui.sweep_selected)];if(inspected.mean_lower[metric])ImGui::Text("Absolute mean nominal 95%% CI: %s, %s | all %u configured seeds",display(inspected.mean_lower[metric]).c_str(),display(inspected.mean_upper[metric]).c_str(),r.request.plan.count);
    if(effect.mean_difference)ImGui::TextWrapped("With other operational fields held fixed, changing %s from %u to %u changed mean %s by %s across %u measured realizations.",analysis::parameter_name(r.request.parameter).data(),analysis::parameter_value(r.request.source,r.request.parameter),r.points[static_cast<std::size_t>(ui.sweep_selected)].value,analysis::metric_name(r.request.objective).data(),display(effect.mean_difference).c_str(),effect.count);
    const auto seeds=analysis::replication_seeds(r.request.plan);ui.sweep_seed=std::clamp(ui.sweep_seed,0,static_cast<int>(seeds.size()-1));if(ImGui::BeginCombo("Actual playback seed",std::to_string(seeds[static_cast<std::size_t>(ui.sweep_seed)]).c_str())) {for(std::size_t i=0;i<seeds.size();++i)if(ImGui::Selectable(std::to_string(seeds[i]).c_str(),i==static_cast<std::size_t>(ui.sweep_seed)))ui.sweep_seed=static_cast<int>(i);ImGui::EndCombo();}
    input("Candidate name",ui.sweep_name);if(ImGui::Button("Add as Candidate")) {WorkspaceCommand c{WorkspaceAction::SweepCandidate};c.sensitivity=r.request;c.id=r.points[static_cast<std::size_t>(ui.sweep_selected)].id;c.text=ui.sweep_name;c.seed=seeds[static_cast<std::size_t>(ui.sweep_seed)];ui.pending.push_back(std::move(c));}ui.sweep_add=center();ImGui::SameLine();
    if(ImGui::Button("Simulate Configuration")) {WorkspaceCommand c{WorkspaceAction::SweepRun};c.sensitivity=r.request;c.id=r.points[static_cast<std::size_t>(ui.sweep_selected)].id;c.seed=seeds[static_cast<std::size_t>(ui.sweep_seed)];ui.pending.push_back(std::move(c));}ui.sweep_run=center();
    if(ImGui::Button("Return to original baseline"))ui.pending.push_back({WorkspaceAction::Baseline});
    if(ImGui::CollapsingHeader("Inspect per-seed outcomes and utilization")) {const auto index=static_cast<std::size_t>(ui.sweep_selected);for(const auto& row:r.replications){const auto& m=row.points[index];ImGui::Text("Seed %llu | completed %llu waiting %llu | objective %s | bay/worker/forklift utilization %s / %s / %s",static_cast<unsigned long long>(row.seed),static_cast<unsigned long long>(m.completed),static_cast<unsigned long long>(m.waiting),display(analysis::metric_value(m,r.request.objective)).c_str(),display(m.loading_bay_utilization).c_str(),display(m.worker_utilization).c_str(),display(m.forklift_utilization).c_str());}}
    if(ImGui::CollapsingHeader("Controlled add-one bottleneck evidence"))for(std::size_t p=0;p<3;++p){const auto& s=r.probes[p][metric];ImGui::Text("%s +1: mean objective delta %s | nominal CI %s, %s",p==0?"Bays":p==1?"Workers":"Forklifts",display(s.mean_difference).c_str(),display(s.lower).c_str(),display(s.upper).c_str());}
    input("Export path (exclusive new file)",ui.export_path);if(ImGui::Button("Export JSON"))ui.pending.push_back({WorkspaceAction::Json,ui.export_path});ImGui::SameLine();if(ImGui::Button("Summary CSV"))ui.pending.push_back({WorkspaceAction::Csv,ui.export_path});ImGui::SameLine();if(ImGui::Button("Per-seed CSV"))ui.pending.push_back({WorkspaceAction::CsvSeeds,ui.export_path});ImGui::SameLine();if(ImGui::Button("Marginal CSV"))ui.pending.push_back({WorkspaceAction::CsvMarginal,ui.export_path});
}
