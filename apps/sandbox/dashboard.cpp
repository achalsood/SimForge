#include "dashboard.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace
{
using namespace simforge;
constexpr auto fixed_panel = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
void panel(const char* name, ImVec2 pos, ImVec2 size)
{ ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size); ImGui::Begin(name,nullptr,fixed_panel); }
glm::vec2 center()
{ const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); return {(a.x+b.x)*0.5f,(a.y+b.y)*0.5f}; }
void row(const char* name, const std::string& value)
{
    ImGui::TextDisabled("%s",name);
    const float width = ImGui::GetWindowContentRegionMax().x-ImGui::GetCursorPosX();
    if (ImGui::CalcTextSize(name).x+ImGui::CalcTextSize(value.c_str()).x+8 <= width)
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-ImGui::CalcTextSize(value.c_str()).x);
    ImGui::TextUnformatted(value.c_str());
}
void count(const char* name, std::uint64_t value) { row(name,std::to_string(value)); }
void utilization(const char* label, std::optional<double> value)
{
    ImGui::TextDisabled("%s",label);
    const auto text = viewer::format_number(value,100,"%");
    ImGui::ProgressBar(value ? static_cast<float>(*value) : 0,ImVec2{-1,17},text.c_str());
}
const char* category(viewer::VisualCategory c)
{
    switch(c)
    {
    case viewer::VisualCategory::Shipment: return "Shipment";
    case viewer::VisualCategory::LoadingBay: return "Loading bay";
    case viewer::VisualCategory::Worker: return "Worker";
    case viewer::VisualCategory::Forklift: return "Forklift";
    default: return "Storage rack";
    }
}
}
void style_dashboard()
{
    ImGui::StyleColorsDark(); auto& s = ImGui::GetStyle();
    s.WindowPadding = {16,12}; s.FramePadding = {8,6}; s.ItemSpacing = {8,8};
    s.WindowRounding = 0; s.FrameRounding = 4; s.GrabRounding = 3; s.WindowBorderSize = 1;
    s.Colors[ImGuiCol_Text] = {0.88f,0.92f,0.95f,1}; s.Colors[ImGuiCol_TextDisabled] = {0.52f,0.63f,0.70f,1};
    s.Colors[ImGuiCol_WindowBg] = {0.008f,0.014f,0.023f,1}; s.Colors[ImGuiCol_Border] = {0.045f,0.075f,0.10f,1};
    s.Colors[ImGuiCol_FrameBg] = {0.10f,0.15f,0.20f,1}; s.Colors[ImGuiCol_Button] = {0.13f,0.21f,0.26f,1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.18f,0.34f,0.39f,1}; s.Colors[ImGuiCol_ButtonActive] = {0.20f,0.42f,0.43f,1};
    s.Colors[ImGuiCol_PlotHistogram] = {0.20f,0.68f,0.57f,1}; s.Colors[ImGuiCol_SliderGrab] = {0.95f,0.66f,0.22f,1};
    s.Colors[ImGuiCol_CheckMark] = {0.95f,0.66f,0.22f,1};
}
simforge::viewer::Viewport dashboard_viewport(const simforge::viewer::ViewerController& v)
{
    const auto& io = ImGui::GetIO(); const float width = io.DisplaySize.x, height = io.DisplaySize.y;
    const float header = width < 1100 ? 76.0f : 56.0f, footer = width < 1000 ? 154.0f : 126.0f;
    const float left = std::clamp(width*0.19f,200.0f,248.0f);
    const float right = v.selection() && width >= 1000 ? std::clamp(width*0.20f,220.0f,276.0f) : 0;
    return {{left+4,header+4},{std::max(1.0f,width-left-right-8),std::max(1.0f,height-header-footer-8)},
        {io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y}};
}
DashboardFrame draw_dashboard(const simforge::viewer::ViewerController& v, DashboardState& draft)
{
    using namespace simforge;
    const auto& io = ImGui::GetIO(); const auto& state = v.observation(); const auto& m = state.metrics;
    const auto inspector = v.inspect();
    const float width = io.DisplaySize.x, height = io.DisplaySize.y;
    const float header = width < 1100 ? 76.0f : 56.0f, footer = width < 1000 ? 154.0f : 126.0f;
    const float left = std::clamp(width*0.19f,200.0f,248.0f);
    const float right = inspector ? std::clamp(width*0.20f,220.0f,276.0f) : 0;
    // Inspector becomes a metrics tab on narrow windows, preserving a useful viewport.
    const bool narrow = width < 1000;
    const float sidebar = narrow ? 0 : right;
    DashboardFrame result; result.viewport = dashboard_viewport(v);
    panel("Header",{0,0},{width,header});
    ImGui::TextColored({0.96f,0.68f,0.27f,1},"SIMFORGE"); ImGui::SameLine();
    auto name = v.name();
    const auto available_name = width >= 1100 ? width-570 : width-150;
    while (!name.empty() && ImGui::CalcTextSize(name.c_str()).x > available_name) name.pop_back();
    if (name != v.name()) name += "...";
    ImGui::TextUnformatted(name.c_str()); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",v.name().c_str());
    if (width >= 1100) ImGui::SameLine(width-430);
    const char* status = state.at_horizon ? "Horizon reached" : v.runtime().clock().paused() ? "Paused" : "Live";
    ImGui::TextColored({0.30f,0.82f,0.68f,1},"%s",status); ImGui::SameLine();
    ImGui::Text("%s / %s",viewer::format_time(state.tick,state.tick_rate).c_str(),viewer::format_time(state.horizon,state.tick_rate).c_str());
    ImGui::End();
    const auto inspection = [&]
    {
        if (!inspector) return;
        const auto& i = *inspector;
        ImGui::TextColored({0.96f,0.68f,0.27f,1},"INSPECTOR"); ImGui::Separator();
        ImGui::Text("%s #%u",category(i.key.category),i.key.id);
        if (i.shipment)
        {
            const auto& r = *i.shipment;
            row("Status",r.status == warehouse::ShipmentStatus::Waiting ? "Waiting" : r.status == warehouse::ShipmentStatus::InProgress ? "Processing" : "Completed");
            row("Arrival",viewer::format_time(r.arrival_tick,state.tick_rate));
            const auto time = [&](std::optional<std::uint64_t> t){ return t ? viewer::format_time(*t,state.tick_rate) : "N/A"; };
            row("Service start",time(r.start_tick)); row("Completion",time(r.completion_tick));
            row("Queue wait",time(i.waiting_ticks)); row("Processing elapsed",time(i.processing_ticks));
            ImGui::Spacing(); ImGui::TextWrapped("Timestamps are actual runtime events. Completion is N/A until recorded.");
        }
        else if (i.key.category != viewer::VisualCategory::Rack)
        {
            row("Display state",i.busy ? "Busy" : "Idle"); count("Aggregate capacity",i.capacity); count("Aggregate usage",i.usage);
            utilization("Cumulative utilization",i.utilization);
            ImGui::Spacing(); ImGui::TextWrapped("Deterministic visual capacity slot. No physical resource identity or shipment assignment is tracked.");
        }
        ImGui::Separator(); ImGui::TextDisabled("Visual position"); ImGui::Text("%.1f, %.1f, %.1f",i.position.x,i.position.y,i.position.z);
        if (ImGui::Button("Clear selection")) result.commands.push_back({ViewerAction::ClearSelection});
    };
    const auto metrics = [&]
    {
        ImGui::TextDisabled("OPERATIONS"); ImGui::Separator();
        count("Arrived",m.arrived); count("Started",m.started); count("Completed",m.completed); count("Waiting",m.waiting); count("Processing",m.in_progress);
        ImGui::Separator(); ImGui::TextDisabled("Completed / hour"); ImGui::TextUnformatted(viewer::format_number(m.throughput_per_hour).c_str());
        ImGui::TextDisabled("Average queue wait"); ImGui::TextUnformatted(viewer::format_number(m.average_wait_seconds,1," s").c_str());
        ImGui::Separator(); utilization("Loading bays",m.loading_bay_utilization); utilization("Workers",m.worker_utilization); utilization("Forklifts",m.forklift_utilization);
        ImGui::Separator(); const auto visibility = v.scene().visibility();
        count("Hidden queue",visibility.hidden_waiting); count("Visible queue",visibility.visible_waiting);
        if (visibility.hidden_processing) count("Hidden processing",visibility.hidden_processing);
        ImGui::TextWrapped("Utilization is cumulative busy time / available capacity time.");
    };
    panel("Metrics",{0,header},{left,height-header-footer});
    if (narrow && inspector && ImGui::BeginTabBar("Sidebar"))
    {
        if (ImGui::BeginTabItem("Inspect")) { inspection(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Metrics"))
        {
            metrics();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    else metrics();
    ImGui::End();
    if (inspector && !narrow) { panel("Inspector",{width-sidebar,header},{sidebar,height-header-footer}); inspection(); ImGui::End(); }
    panel("Playback",{0,height-footer},{width,footer});
    const char* playback = state.at_horizon ? "Horizon" : !v.runtime().clock().paused() ? "Pause" : state.tick == 0 && m.arrived == 0 ? "Play" : "Resume";
    ImGui::BeginDisabled(state.at_horizon);
    if (ImGui::Button(playback,{90,0})) result.commands.push_back({ViewerAction::Toggle}); result.play = center();
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(!v.runtime().clock().paused() || state.at_horizon);
    if (ImGui::Button("Step 1 tick")) result.commands.push_back({ViewerAction::Step}); result.step = center(); ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Reset")) result.commands.push_back({ViewerAction::Reset}); result.reset = center();
    ImGui::SameLine(); ImGui::SetNextItemWidth(std::clamp(width-430,120.0f,190.0f));
    double speed = v.speed(); if (ImGui::InputDouble("Speed x",&speed,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(speed) && speed >= 0.01 && speed <= 3600)
        result.commands.push_back({ViewerAction::Speed,speed}); result.speed = center();
    bool first_preset = true;
    for (const int preset : {1,10,60,300,900})
    { if (!first_preset || width >= 1000) ImGui::SameLine(); first_preset = false;
      const auto label = std::to_string(preset)+"x"; if (ImGui::Button(label.c_str())) result.commands.push_back({ViewerAction::Speed,static_cast<double>(preset)}); }
    if (!draft.seeking) draft.seek_tick = state.tick;
    const std::uint64_t zero = 0;
    ImGui::SetNextItemWidth(-1); ImGui::SliderScalar("##Timeline",ImGuiDataType_U64,&draft.seek_tick,&zero,&state.horizon,"%llu ticks");
    const auto timeline_min = ImGui::GetItemRectMin(), timeline_max = ImGui::GetItemRectMax();
    result.timeline_min = {timeline_min.x,timeline_min.y}; result.timeline_max = {timeline_max.x,timeline_max.y};
    if (ImGui::IsItemDeactivatedAfterEdit()) result.commands.push_back({ViewerAction::Seek,0,draft.seek_tick});
    draft.seeking = ImGui::IsItemActive();
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Seek: plain scenarios only | Space playback | Right: 1 tick | F frame | R camera reset | Speed 0.01-3600x");
    ImGui::PopStyleColor();
    ImGui::End();
    auto* draw = ImGui::GetForegroundDrawList(); const auto p = result.viewport.origin;
    draw->PushClipRect({p.x,p.y},{p.x+result.viewport.size.x,p.y+result.viewport.size.y});
    draw->AddText({p.x+14,p.y+10},IM_COL32(150,174,187,255),result.viewport.size.x < 650 ? "LMB orbit / click inspect | Wheel zoom" : "LMB orbit / click inspect   MMB pan   Wheel zoom");
    draw->PopClipRect();
    if (inspector)
    {
        const auto& objects = v.scene().selectables();
        const auto selected = std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.key == inspector->key;});
        if (selected != objects.end())
        {
            ImVec2 low{1e9f,1e9f}, high{-1e9f,-1e9f}; bool visible = true;
            const auto matrix = v.orbit_camera().camera().view_projection();
            for (int c = 0; c < 8; ++c)
            {
                auto point = matrix*glm::vec4{(c&1)?selected->bounds.maximum.x:selected->bounds.minimum.x,
                    (c&2)?selected->bounds.maximum.y:selected->bounds.minimum.y,(c&4)?selected->bounds.maximum.z:selected->bounds.minimum.z,1};
                if (point.w <= 0) { visible = false; break; } point /= point.w;
                const float x = p.x+(point.x+1)*0.5f*result.viewport.size.x, y = p.y+(point.y+1)*0.5f*result.viewport.size.y;
                low.x = std::min(low.x,x); low.y = std::min(low.y,y); high.x = std::max(high.x,x); high.y = std::max(high.y,y);
            }
            if (visible) { draw->PushClipRect({p.x,p.y},{p.x+result.viewport.size.x,p.y+result.viewport.size.y}); draw->AddRect(low,high,IM_COL32(255,182,65,255),3.0f,2.0f,ImDrawFlags_None); draw->PopClipRect(); }
        }
    }
    return result;
}
