#include "checkpoint_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
using namespace simforge;
namespace cp=analysis::checkpoint;
namespace {
glm::vec2 center(){const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
void input(const char* label,std::string& value){std::array<char,4097> b{};std::copy_n(value.data(),std::min(value.size(),b.size()-1),b.data());ImGui::TextUnformatted(label);ImGui::PushID(label);ImGui::SetNextItemWidth(-1);if(ImGui::InputText("##value",b.data(),b.size()))value=b.data();ImGui::PopID();}
std::uint64_t integer(const std::string& value){std::uint64_t result=0;auto end=std::from_chars(value.data(),value.data()+value.size(),result);if(end.ec!=std::errc{} || end.ptr!=value.data()+value.size())throw std::invalid_argument("Enter an unsigned integer; checkpoints use exact engine ticks");return result;}
template<class F>void action(CheckpointUI& ui,F f){try{f();ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}
void number(const char* label,std::optional<double> value){if(value)ImGui::Text("%s: %.6g",label,*value);else ImGui::Text("%s: unavailable",label);}
void effects(const cp::Report& r,std::size_t metric,bool post){
    ImGui::TextUnformatted("Per-seed paired effects: intervention minus control");
    ImGui::TextWrapped("Y: signed %s | X: seed order in frozen plan | zero line shown",cp::metric_unit(metric).data());
    const float width=std::max(160.f,ImGui::GetContentRegionAvail().x),height=160;const auto p=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("Checkpoint effects plot",{width,height});auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 a{p.x+42,p.y+12},b{p.x+width-10,p.y+height-28};double extent=1;
    std::vector<std::optional<double>> deltas;for(const auto& row:r.outcomes){const auto c=cp::metric_value(row,false,metric,post,r.request),i=cp::metric_value(row,true,metric,post,r.request);deltas.push_back(c&&i?std::optional<double>{*i-*c}:std::nullopt);if(deltas.back())extent=std::max(extent,std::abs(*deltas.back()));}
    const auto y=[&](double v){return static_cast<float>((a.y+b.y)/2-v/extent*(b.y-a.y)/2);};draw->AddRectFilled(p,{p.x+width,p.y+height},IM_COL32(18,25,34,255));draw->AddLine({a.x,y(0)},{b.x,y(0)},IM_COL32(170,180,190,255));
    char label[48];std::snprintf(label,sizeof(label),"+%.3g",extent);draw->AddText({p.x,a.y},IM_COL32_WHITE,label);draw->AddText({p.x,y(0)-6},IM_COL32_WHITE,"0");std::snprintf(label,sizeof(label),"-%.3g",extent);draw->AddText({p.x,b.y-10},IM_COL32_WHITE,label);
    for(std::size_t i=0;i<deltas.size();++i){const auto x=a.x+(b.x-a.x)*(static_cast<float>(i)+.5f)/static_cast<float>(deltas.size());if(deltas[i]){const auto color=*deltas[i]>=0?IM_COL32(63,204,185,255):IM_COL32(241,159,98,255);draw->AddLine({x,y(0)},{x,y(*deltas[i])},color,2);draw->AddCircleFilled({x,y(*deltas[i])},3,color);}else draw->AddText({x,y(0)},IM_COL32(150,150,150,255),"NA");}
    draw->AddText({a.x,b.y+8},IM_COL32_WHITE,std::to_string(r.outcomes.front().seed).c_str());const auto last=std::to_string(r.outcomes.back().seed);draw->AddText({std::max(a.x,b.x-80),b.y+8},IM_COL32_WHITE,last.c_str());
}
void interval(const analysis::PairedSummary& s){
    ImGui::TextUnformatted("Mean paired effect and nominal 95% Student-t interval");if(!s.mean_difference){ImGui::TextDisabled("Unavailable: undefined pairs");return;}
    const auto p=ImGui::GetCursorScreenPos();const float width=std::max(160.f,ImGui::GetContentRegionAvail().x);ImGui::InvisibleButton("Checkpoint confidence interval",{width,55});auto* draw=ImGui::GetWindowDrawList();const auto lo=std::min(0.,s.lower.value_or(*s.mean_difference)),hi=std::max(0.,s.upper.value_or(*s.mean_difference));const auto span=std::max(1.,hi-lo);const auto x=[&](double v){return p.x+24+static_cast<float>((v-lo)/span)*(width-48);};const auto color=IM_COL32(174,146,251,255);
    draw->AddLine({x(0),p.y+2},{x(0),p.y+30},IM_COL32(160,170,185,255));if(s.lower){draw->AddLine({x(*s.lower),p.y+16},{x(*s.upper),p.y+16},color,3);for(auto v:{*s.lower,*s.upper})draw->AddLine({x(v),p.y+8},{x(v),p.y+24},color,2);}draw->AddCircleFilled({x(*s.mean_difference),p.y+16},5,color);char label[128];if(s.lower)std::snprintf(label,sizeof(label),"[%.6g, %.6g] | mean %.6g",*s.lower,*s.upper,*s.mean_difference);else std::snprintf(label,sizeof(label),"mean %.6g | interval unavailable",*s.mean_difference);draw->AddText({p.x,p.y+35},IM_COL32_WHITE,label);
}
}
cp::Request checkpoint_request(const workspace::ScenarioWorkspace& w,const CheckpointUI& ui){
    cp::Request r;r.source=ui.source==0?w.baseline():w.candidates().at(static_cast<std::size_t>(ui.source-1)).definition;r.checkpoint_tick=integer(ui.tick);r.intervention.resource=static_cast<warehouse::ResourceType>(ui.resource);r.intervention.action=static_cast<cp::Action>(ui.action);const auto value=integer(ui.value);if(value>100000)throw std::invalid_argument("Capacity action value must be 0..100000");r.intervention.value=static_cast<std::uint32_t>(value);r.objective=static_cast<analysis::Objective>(ui.objective);
    const auto count=integer(ui.count);if(count<1 || count>100)throw std::invalid_argument("Choose 1..100 paired seeds");r.plan.count=static_cast<std::uint32_t>(count);
    if(ui.seed_policy==0)r.plan.base_seed=integer(ui.base);else{r.plan.policy=analysis::SeedPolicy::Explicit;r.plan.base_seed=0;auto text=ui.seeds;std::replace(text.begin(),text.end(),',',' ');std::istringstream in(text);std::string token;while(in>>token)r.plan.explicit_seeds.push_back(integer(token));}
    cp::validate(r);return r;
}
void draw_checkpoint_setup(workspace::ScenarioWorkspace& w,CheckpointUI& ui,float){
    const auto scroll=[&](int target){if(ui.scroll_target==target){ImGui::SetScrollHereY(.35f);ui.scroll_target=-1;}};
    if(!ui.open)return;scroll(0);
    if(ImGui::CollapsingHeader("Checkpoint study setup",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextWrapped("One fresh-start prefix per seed, then matched control/intervention continuations. The new command follows all events due at the exact checkpoint tick.");
        std::vector<const char*> sources{"Loaded source scenario"};for(const auto& c:w.candidates())sources.push_back(c.definition.name.c_str());ui.source=std::clamp(ui.source,0,static_cast<int>(sources.size()-1));ImGui::TextUnformatted("Source scenario");ImGui::SetNextItemWidth(-1);ImGui::Combo("##Checkpoint source",&ui.source,sources.data(),static_cast<int>(sources.size()));
        const auto& source=ui.source==0?w.baseline():w.candidates()[static_cast<std::size_t>(ui.source-1)].definition;const auto& c=std::get<scenario::WarehouseConfig>(source.domain);
        ImGui::TextWrapped("%s | %u Hz | %u s | bays/workers/forklifts %u/%u/%u | workload %s, generator %u",source.name.c_str(),source.simulation.tick_rate,source.simulation.duration_seconds,c.loading_bays,c.workers,c.forklifts,scenario::workload::arrival_name(c.workload.arrivals),scenario::workload::generator_version(c));
        ImGui::BeginDisabled(w.execution().progress().active());input("Exact checkpoint tick",ui.tick);ui.inputs[0]=center();ImGui::TextWrapped("Start 0 -> checkpoint -> horizon %llu ticks. Events and source commands at checkpoint precede the intervention.",scenario::total_ticks(source));
        ImGui::Combo("Resource",&ui.resource,"Loading bays\0Workers\0Forklifts\0");ImGui::Combo("Capacity action",&ui.action,"Set\0Increase\0Decrease\0");input("Action value",ui.value);ui.inputs[1]=center();ImGui::Combo("Objective",&ui.objective,"Maximize completed\0Minimize mean wait\0Minimize waiting\0");
        input("Paired seed count (1..100)",ui.count);ui.inputs[2]=center();ImGui::Combo("Seed policy",&ui.seed_policy,"Sequential\0Explicit\0");input(ui.seed_policy?"Explicit seeds (comma separated)":"Base seed",ui.seed_policy?ui.seeds:ui.base);ui.inputs[3]=center();
        if(ImGui::Button("Run checkpoint study"))action(ui,[&]{w.start_checkpoint(checkpoint_request(w,ui),ui.observer);});ui.run=center();scroll(1);ImGui::EndDisabled();
        const auto progress=w.execution().progress();if(progress.active()){if(ImGui::Button("Cancel checkpoint study"))w.execution().cancel();ui.cancel=center();scroll(7);ImGui::TextWrapped("Seed %llu/%llu (%s) | completed %llu | %s | tick %llu/%llu",progress.current_replication,progress.total_replications,progress.replication_seed.c_str(),progress.completed_replications,progress.phase.c_str(),progress.tick,progress.horizon);}
        if(!ui.error.empty())ImGui::TextWrapped("%s",ui.error.c_str());
    }
}
void draw_checkpoint_results(workspace::ScenarioWorkspace& w,CheckpointUI& ui,float){
    const auto scroll=[&](int target){if(ui.scroll_target==target){ImGui::SetScrollHereY(.25f);ui.scroll_target=-1;}};
    const auto& r=*w.report()->checkpoint_study;ui.open=true;ImGui::SeparatorText("Paired checkpoint intervention results");
    ImGui::TextWrapped("Frozen source: %s | checkpoint %llu ticks (%.3f s) | horizon %llu ticks | %zu paired seeds",r.request.source.name.c_str(),r.request.checkpoint_tick,static_cast<double>(r.request.checkpoint_tick)/r.request.source.simulation.tick_rate,scenario::total_ticks(r.request.source),r.outcomes.size());
    ImGui::TextWrapped("Timeline: start -> all checkpoint events -> %s %s %u -> common final horizon",warehouse::resource_name(r.request.intervention.resource).data(),r.request.intervention.action==cp::Action::Set?"set":r.request.intervention.action==cp::Action::Increase?"increase":"decrease",r.request.intervention.value);
    ImGui::Combo("Outcome metric",&ui.metric,"Completed shipments\0Waiting shipments\0Throughput / hour\0Mean started queue wait / s\0Maximum started wait / s\0Bay utilization\0Worker utilization\0Forklift utilization\0");ui.metric_control=center();ImGui::Checkbox("Post-checkpoint interval metrics",&ui.post);const auto metric=static_cast<std::size_t>(ui.metric);const auto& summary=(ui.post?r.post_metrics:r.end_metrics)[metric];
    ImGui::TextWrapped("Scope: %s | raw intervention minus control | %s",ui.post?"post-checkpoint counter differences":"cumulative end-of-horizon",metric>=5?"utilization has no preferred direction":cp::higher_is_better(metric)?"positive is favorable":"negative is favorable");
    scroll(2);effects(r,metric,ui.post);interval(summary);scroll(3);number("Control mean",summary.baseline_mean);number("Intervention mean",summary.candidate_mean);number("Paired standard deviation",summary.standard_deviation);number("Paired standard error",summary.standard_error);ImGui::Text("Pairs %u/%u | positive %u | negative %u | tied %u",summary.defined,summary.count,summary.positive,summary.negative,summary.zero);if(metric<5)ImGui::Text("Favorable %u | unfavorable %u | tied %u",cp::higher_is_better(metric)?summary.positive:summary.negative,cp::higher_is_better(metric)?summary.negative:summary.positive,summary.zero);ImGui::TextWrapped("%s",summary.diagnostic.c_str());
    if(ImGui::CollapsingHeader("Per-seed outcomes and exact branch playback",ImGuiTreeNodeFlags_DefaultOpen)){
        std::vector<std::string> names;std::vector<const char*> labels;for(const auto& row:r.outcomes)names.push_back(std::to_string(row.seed));for(const auto& name:names)labels.push_back(name.c_str());ui.selected_seed=std::clamp(ui.selected_seed,0,static_cast<int>(names.size()-1));ImGui::Combo("Explicit seed",&ui.selected_seed,labels.data(),static_cast<int>(labels.size()));ui.seed_control=center();scroll(4);
        const auto& row=r.outcomes[static_cast<std::size_t>(ui.selected_seed)];ImGui::TextWrapped("Seed %llu | checkpoint queue %llu | active %llu | completed %llu | events %llu",row.seed,row.checkpoint.waiting,row.checkpoint.in_progress,row.checkpoint.completed,row.checkpoint_events);ImGui::TextWrapped("Checkpoint %s | external workload %s",row.checkpoint_digest.c_str(),row.workload_digest.c_str());
        ImGui::TextWrapped("Resolved command at tick %llu: %s %u -> %u | after due events",row.applied.tick,warehouse::resource_name(row.applied.resource).data(),warehouse::capacity_for(row.checkpoint_capacity,row.applied.resource),row.applied.capacity);
        if(ImGui::BeginTable("Paired control versus intervention",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)){for(const char* name:{"Metric","Control","Intervention","Raw difference"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();for(std::size_t m=0;m<cp::metric_count;++m){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextWrapped("%s (%s)",cp::metric_name(m).data(),cp::metric_unit(m).data());const auto a=cp::metric_value(row,false,m,ui.post,r.request),b=cp::metric_value(row,true,m,ui.post,r.request);for(auto value:{a,b,a&&b?std::optional<double>{*b-*a}:std::nullopt}){ImGui::TableNextColumn();if(value)ImGui::Text("%.6g",*value);else ImGui::TextUnformatted("NA");}}ImGui::EndTable();}
        if(ImGui::Button("Open control playback")){ui.playback_requested=false;ui.playback_seed=row.seed;};ui.control_playback=center();scroll(5);if(ImGui::Button("Open intervention playback")){ui.playback_requested=true;ui.playback_seed=row.seed;};ui.intervention_playback=center();scroll(6);ImGui::TextWrapped("Actual selected-seed timeline, paused at checkpoint; use playback/seek controls. Returning to results preserves evidence.");
    }
    workspace::StudyViewState view;view.metric=ui.metric;view.seed=r.outcomes[static_cast<std::size_t>(ui.selected_seed)].seed;w.study_view(view);
    if(ImGui::CollapsingHeader("Checkpoint method and limitations"))for(const auto& line:r.limitations)ImGui::TextWrapped("%s",line.c_str());if(!ui.error.empty())ImGui::TextWrapped("%s",ui.error.c_str());
}
