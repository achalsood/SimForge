#include "dashboard.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace
{
using namespace simforge;
using namespace simforge::viewer;
ImVec4 color(glm::vec3 rgb) { return {rgb.x,rgb.y,rgb.z,1}; }
ImU32 packed(glm::vec3 rgb) { return ImGui::ColorConvertFloat4ToU32(color(rgb)); }
struct Font
{
    explicit Font(float size) { ImGui::PushFont(nullptr,size); }
    ~Font() { ImGui::PopFont(); }
};
constexpr auto fixed_panel = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
void panel(const char* name, Rect r, float vertical_padding = -1)
{
    ImGui::SetNextWindowPos({r.origin.x,r.origin.y}); ImGui::SetNextWindowSize({r.size.x,r.size.y});
    if (vertical_padding >= 0) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{ImGui::GetStyle().WindowPadding.x,vertical_padding});
    ImGui::Begin(name,nullptr,fixed_panel);
    if (vertical_padding >= 0) ImGui::PopStyleVar();
}
glm::vec2 center()
{ const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); return {(a.x+b.x)*0.5f,(a.y+b.y)*0.5f}; }
void tooltip(const char* text)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    const float width = std::min(340*ImGui::GetStyle().FontScaleDpi,ImGui::GetIO().DisplaySize.x-2*ImGui::GetStyle().WindowPadding.x);
    ImGui::SetNextWindowSize({width,0},ImGuiCond_Always);
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
}
void property(const char* name, const std::string& value, glm::vec3 accent = design::text, float size = design::body)
{
    const float width = ImGui::GetContentRegionAvail().x;
    float label_width = 0, value_width = 0;
    { Font font{design::label}; label_width = ImGui::CalcTextSize(name).x; ImGui::TextColored(color(design::secondary),"%s",name); }
    { Font font{size}; value_width = ImGui::CalcTextSize(value.c_str()).x; }
    if (label_width+value_width+ImGui::GetStyle().ItemSpacing.x <= width)
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-value_width);
    { Font font{size}; ImGui::TextColored(color(accent),"%s",value.c_str()); }
}
void section(const char* name)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,4*ImGui::GetStyle().FontScaleDpi});
    { Font font{design::heading}; ImGui::TextUnformatted(name); }
    ImGui::Separator(); ImGui::PopStyleVar();
}
void kpi(const char* name, std::uint64_t value, glm::vec3 accent)
{
    const float s=ImGui::GetStyle().FontScaleDpi; const auto p=ImGui::GetCursorScreenPos(); const auto width=ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+width,p.y+52*s},packed(design::navy),6*s);
    ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+3*s,p.y+52*s},packed(accent),2*s);
    ImGui::BeginGroup(); ImGui::Indent(10*s);
    { Font font{design::label}; ImGui::TextColored(color(design::secondary),"%s",name); }
    { Font font{design::metric}; ImGui::TextColored(color(accent),"%llu",static_cast<unsigned long long>(value)); }
    ImGui::Unindent(10*s); ImGui::EndGroup(); ImGui::Dummy({0,8*s});
}
void utilization(const char* label, std::optional<double> value, std::uint32_t usage, std::uint32_t capacity)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,4*ImGui::GetStyle().FontScaleDpi});
    const auto caption = std::string{label}+"  "+std::to_string(usage)+"/"+std::to_string(capacity);
    property(caption.c_str(),display_percent(value),value ? design::processing : design::secondary);
    tooltip("Active usage / aggregate capacity; the percentage is cumulative utilization.");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,color(design::processing));
    ImGui::ProgressBar(value ? static_cast<float>(*value) : 0,{-1,6*ImGui::GetStyle().FontScaleDpi},"");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
enum class Icon { Play, Pause, Step, Reset, Sidebar, Inspect, Focus };
bool icon_button(const char* id, const char* label, Icon icon, ImVec2 size, const char* help)
{
    const std::string caption = *label ? std::string{"    "}+label+"##"+id : std::string{"##"}+id;
    const bool clicked = ImGui::Button(caption.c_str(),size); tooltip(help);
    const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    const float s = ImGui::GetStyle().FontScaleDpi, x = low.x+(*label ? 18 : size.x/s*0.5f)*s, y = (low.y+high.y)*0.5f;
    auto* d = ImGui::GetWindowDrawList(); const auto ink = packed(ImGui::IsItemHovered()?design::text:design::secondary);
    const float r = 6*s, line = 1.7f*s;
    if (icon == Icon::Play || icon == Icon::Step)
    {
        d->AddTriangleFilled({x-r*0.65f,y-r},{x+r*0.75f,y},{x-r*0.65f,y+r},ink);
        if (icon == Icon::Step) d->AddLine({x+r,y-r},{x+r,y+r},ink,line);
    }
    else if (icon == Icon::Pause)
    { for (float offset : {-3.0f,3.0f}) d->AddRectFilled({x+(offset-1)*s,y-r},{x+(offset+1)*s,y+r},ink); }
    else if (icon == Icon::Reset)
    {
        d->PathArcTo({x,y},r,0.6f,5.5f,16); d->PathStroke(ink,ImDrawFlags_None,line);
        d->AddTriangleFilled({x+r,y-4*s},{x+r-6*s,y-5*s},{x+r-1*s,y+1*s},ink);
    }
    else if (icon == Icon::Sidebar)
    { d->AddRect({x-r,y-r},{x+r,y+r},ink,1.0f,line,ImDrawFlags_None); d->AddLine({x-2*s,y-r},{x-2*s,y+r},ink,line); }
    else if (icon == Icon::Inspect)
    { d->AddCircle({x-1*s,y-1*s},r*0.7f,ink,12,line); d->AddLine({x+3*s,y+3*s},{x+7*s,y+7*s},ink,line); }
    else
    {
        for (float sign : {-1.0f,1.0f})
        { d->AddLine({x+sign*r,y-sign*r},{x+sign*r,y-sign*2*s},ink,line); d->AddLine({x+sign*r,y-sign*r},{x+sign*2*s,y-sign*r},ink,line); }
    }
    return clicked;
}
const char* category(VisualCategory c)
{
    switch(c)
    {
    case VisualCategory::Shipment: return "Shipment";
    case VisualCategory::LoadingBay: return "Loading bay";
    case VisualCategory::Worker: return "Worker";
    case VisualCategory::Forklift: return "Forklift";
    default: return "Storage rack";
    }
}
DashboardLayout layout_for(const ViewerController& v, const DashboardState& state)
{
    const auto& io = ImGui::GetIO();
    return dashboard_layout({io.DisplaySize.x,io.DisplaySize.y},{io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y},
        state.scale,state.panels,v.selection().has_value(),state.feedback_error);
}
}
void style_dashboard(float scale)
{
    using namespace simforge::viewer;
    ImGuiStyle style; ImGui::StyleColorsDark(&style);
    style.FontSizeBase = design::body; style.FontScaleDpi = scale;
    style.WindowPadding = {design::padding,design::padding}; style.FramePadding = {12,7}; style.ItemSpacing = {design::gap,8};
    style.WindowRounding = 8; style.FrameRounding = design::corner; style.GrabRounding = design::corner; style.WindowBorderSize = 1;
    style.Colors[ImGuiCol_Text] = color(design::text); style.Colors[ImGuiCol_TextDisabled] = color(design::secondary);
    style.Colors[ImGuiCol_WindowBg] = color(design::navy); style.Colors[ImGuiCol_Border] = color(design::border);
    style.Colors[ImGuiCol_PopupBg] = color(design::panel);
    for(auto id : {ImGuiCol_FrameBg,ImGuiCol_Button,ImGuiCol_Header,ImGuiCol_Tab,ImGuiCol_TableHeaderBg}) style.Colors[id]=color(design::elevated);
    for(auto id : {ImGuiCol_ButtonHovered,ImGuiCol_HeaderHovered,ImGuiCol_TabHovered,ImGuiCol_FrameBgHovered}) style.Colors[id]=color(design::border);
    for(auto id : {ImGuiCol_ButtonActive,ImGuiCol_HeaderActive,ImGuiCol_TabSelected,ImGuiCol_FrameBgActive}) style.Colors[id]=color(design::border);
    style.Colors[ImGuiCol_TableRowBg]={0,0,0,0}; style.Colors[ImGuiCol_TableRowBgAlt]=color(design::panel);
    style.Colors[ImGuiCol_TableBorderStrong]=color(design::border); style.Colors[ImGuiCol_TableBorderLight]=color(design::border);
    style.Colors[ImGuiCol_SliderGrab]=color(design::processing); style.Colors[ImGuiCol_SliderGrabActive]=color(design::text);
    style.Colors[ImGuiCol_CheckMark]=color(design::processing);
    style.ScaleAllSizes(scale); ImGui::GetStyle() = style;
}
simforge::viewer::Viewport dashboard_viewport(const simforge::viewer::ViewerController& v, const DashboardState& state)
{ return layout_for(v,state).viewport; }
DashboardFrame draw_dashboard(const simforge::viewer::ViewerController& v, DashboardState& draft)
{
    using namespace simforge;
    using namespace simforge::viewer;
    const auto layout = layout_for(v,draft);
    const auto& state = v.observation(); const auto& m = state.metrics; const auto inspection = v.inspect();
    const float s = draft.scale, width = ImGui::GetIO().DisplaySize.x;
    DashboardFrame result; result.layout = layout; result.viewport = layout.viewport;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{12*s,8*s});
    panel("Header",layout.header,6*s);
    const bool tiny = width/s < 500;
    { Font font{20}; ImGui::TextColored(color(design::processing),"%s",tiny ? "SF" : "SIMFORGE"); }
    ImGui::SameLine();
    const auto status = playback_status(state.at_horizon,v.runtime().clock().paused(),draft.seeking);
    const auto accent = status == PresentationStatus::Completed ? design::completed : status == PresentationStatus::Paused ? design::waiting : design::processing;
    const float status_width = (tiny ? 85 : 195)*s;
    auto name = v.name();
    const float available = std::max(20.0f,width-status_width-ImGui::GetCursorPosX()-36*s);
    { Font font{design::label};
      while (!name.empty() && ImGui::CalcTextSize((name+"...").c_str()).x > available)
      { auto end = name.size()-1; while (end && (static_cast<unsigned char>(name[end])&0xC0) == 0x80) --end; name.resize(end); }
      ImGui::TextUnformatted(name == v.name() ? name.c_str() : (name+"...").c_str()); tooltip(v.name().c_str()); }
    ImGui::SameLine(width-status_width-18*s);
    { Font font{design::label}; ImGui::TextColored(color(accent),"%s",v.result_preview() ? "FINAL PREVIEW" : status_label(status).data());
      if (!tiny) { ImGui::SameLine(); ImGui::TextUnformatted(format_time(state.tick,state.tick_rate).c_str()); } }
    const auto mode_button = [&](const char* label, ApplicationMode mode, float button_width)
    {
        const bool active = draft.panels.mode == mode;
        ImGui::PushStyleColor(ImGuiCol_Button,color(active ? design::elevated : design::navy));
        ImGui::PushStyleColor(ImGuiCol_Text,color(active ? design::processing : design::secondary));
        if(ImGui::Button(label,{button_width*s,layout.control_height}))
        { draft.panels.mode=mode; draft.panels.workspace=mode!=ApplicationMode::Simulation; draft.panel_changed=true; draft.workspace_changed=true; }
        ImGui::PopStyleColor(2);
        if(active) { const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); ImGui::GetWindowDrawList()->AddLine({a.x+8*s,b.y-1*s},{b.x-8*s,b.y-1*s},packed(design::processing),2*s); }
    };
    mode_button(tiny ? "Sim" : "Simulation",ApplicationMode::Simulation,tiny?64:116); result.simulation_mode=center(); ImGui::SameLine();
    mode_button(tiny ? "Editor" : "Scenario Editor",ApplicationMode::Editor,tiny?76:154); result.workspace_toggle=center(); ImGui::SameLine();
    mode_button("Analysis",ApplicationMode::Analysis,tiny?88:110); result.analysis_mode=center();
    if(!tiny)
    {
        ImGui::SameLine();
        if(icon_button("Metrics","",Icon::Sidebar,{36*s,layout.control_height},"Show or hide operational metrics"))
        { draft.panels.metrics=!layout.metrics_visible; draft.panels.metrics_explicit=true; draft.panel_changed=true; }
        result.metrics_toggle=center(); ImGui::SameLine();
        if(icon_button("Focus","",Icon::Focus,{36*s,layout.control_height},"Warehouse focus: hide or restore side panels"))
        { draft.panels.focus=!draft.panels.focus; draft.panel_changed=true; }
        result.focus_toggle=center(); ImGui::SameLine(); ImGui::BeginDisabled(!inspection);
        if(icon_button("Inspector","",Icon::Inspect,{36*s,layout.control_height},"Show selected-object details"))
        { draft.panels.inspector=!draft.panels.inspector; draft.panel_changed=true; }
        result.inspector_toggle=center(); ImGui::EndDisabled();
        if(width/s>850)
        {
            ImGui::SameLine(width-168*s);
            const auto* action=draft.panels.mode==ApplicationMode::Editor?"Add candidate":draft.panels.mode==ApplicationMode::Analysis?"Preview result":draft.viewing_baseline?"Frame warehouse":"Return baseline";
            ImGui::BeginDisabled(draft.panels.mode==ApplicationMode::Analysis && !draft.results_available);
            if(ImGui::Button(action,{150*s,layout.control_height})) draft.context_action=true;
            result.context_action=center();
            tooltip(draft.panels.mode==ApplicationMode::Editor?"Validate and add the draft as a named alternative":draft.panels.mode==ApplicationMode::Analysis?draft.results_available?"Open the selected result as a read-only warehouse":"Add a candidate in Scenario Editor and run a comparison first":draft.viewing_baseline?"Fit the current warehouse to the viewport (F)":"Return to the preserved baseline, including its time and camera");
            ImGui::EndDisabled();
        }
    }
    ImGui::End(); ImGui::PopStyleVar();
    if(layout.workspace_overlay) return result;
    const auto inspect = [&]
    {
        if (!inspection) return;
        const auto& i = *inspection;
        section("Selected object");
        { Font font{24}; ImGui::Text("%s #%u",category(i.key.category),i.key.id); }
        if (i.shipment)
        {
            const auto& record = *i.shipment;
            property("Status",record.status == warehouse::ShipmentStatus::Waiting ? "Waiting" : "Processing",
                record.status == warehouse::ShipmentStatus::Waiting ? design::waiting : design::processing);
            section("Event timestamps");
            const auto timestamp = [&](std::optional<std::uint64_t> tick){ return tick ? format_time(*tick,state.tick_rate) : "N/A"; };
            property("Arrival",format_time(record.arrival_tick,state.tick_rate));
            property("Service start",timestamp(record.start_tick)); property("Completion",timestamp(record.completion_tick));
            section("Elapsed durations"); property("Queue wait",timestamp(i.waiting_ticks)); property("Processing",timestamp(i.processing_ticks));
            ImGui::Spacing(); { Font font{design::label}; ImGui::TextWrapped("N/A means not yet recorded. Processing time is elapsed, not a predicted completion."); }
        }
        else if (i.key.category != VisualCategory::Rack)
        {
            property("Display slot",i.busy ? "Busy" : "Idle",i.busy ? design::processing : design::secondary);
            utilization("Aggregate utilization",i.utilization,i.usage,i.capacity);
            section("Capacity representation");
            { Font font{design::label}; ImGui::TextWrapped("This is a deterministic visual capacity slot. Physical resource identity and shipment assignments are not tracked."); }
        }
        section("Visual position"); property("X / Y / Z",display_decimal(i.position.x)+" / "+display_decimal(i.position.y)+" / "+display_decimal(i.position.z));
        if (ImGui::Button("Clear selection",{-1,40*s})) result.commands.push_back({ViewerAction::ClearSelection});
    };
    const auto metrics = [&]
    {
        section("Operations");
        const bool two_columns = ImGui::GetContentRegionAvail().x/s >= 155;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,2*s});
        if (ImGui::BeginTable("Operations",two_columns ? 2 : 1,ImGuiTableFlags_SizingStretchSame))
        {
            for (const auto& k : {std::pair{"Arrivals",std::pair{m.arrived,design::text}},
                std::pair{"Completed",std::pair{m.completed,design::completed}},
                std::pair{"Waiting",std::pair{m.waiting,design::waiting}},
                std::pair{"In progress",std::pair{m.in_progress,design::processing}}})
            { ImGui::TableNextColumn(); kpi(k.first,k.second.first,k.second.second); }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        property("Hidden queue",std::to_string(v.scene().visibility().hidden_waiting),design::waiting);
        tooltip("Real waiting shipments beyond the bounded visible FIFO queue.");
        if (!draft.performance_user_choice) ImGui::SetNextItemOpen(layout.metrics.size.y/s >= 430,ImGuiCond_Always);
        const bool performance = ImGui::CollapsingHeader("Performance");
        if (ImGui::IsItemClicked()) draft.performance_user_choice = true;
        if (performance)
        {
            property("Started",std::to_string(m.started));
            property("Throughput / h",display_decimal(m.throughput_per_hour),design::text,20); tooltip("Completed shipments per simulated hour.");
            property("Average wait",display_duration(m.average_wait_seconds),design::text,20);
            property("Maximum wait",display_duration(m.maximum_wait_seconds),design::text,20);
        }
        section("Resources");
        utilization("Bays",m.loading_bay_utilization,state.usage.loading_bays,state.capacity.loading_bays);
        utilization("Workers",m.worker_utilization,state.usage.workers,state.capacity.workers);
        utilization("Forklifts",m.forklift_utilization,state.usage.forklifts,state.capacity.forklifts);
        { Font font{design::label}; ImGui::TextWrapped("Utilization = cumulative busy time / available capacity time."); }
    };
    if (layout.metrics.size.x > 0 && !draft.panels.workspace)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg,color(design::panel)); panel("Metrics",layout.metrics);
        if (layout.shared_inspector && layout.metrics_visible && ImGui::BeginTabBar("Sidebar"))
        {
            if (ImGui::BeginTabItem("Operations")) { metrics(); ImGui::EndTabItem(); }
            const auto tab_flags = draft.select_inspector_tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Inspector",nullptr,tab_flags)) { inspect(); ImGui::EndTabItem(); }
            ImGui::EndTabBar(); draft.select_inspector_tab = false;
        }
        else if (layout.shared_inspector) inspect(); else metrics();
        ImGui::End(); ImGui::PopStyleColor();
    }
    if (layout.inspector.size.x > 0 && !layout.shared_inspector)
    { ImGui::PushStyleColor(ImGuiCol_WindowBg,color(design::panel)); panel("Inspector",layout.inspector); inspect(); ImGui::End(); ImGui::PopStyleColor(); }
    panel("Playback",layout.playback,10*s);
    ImGui::BeginDisabled(state.at_horizon || v.result_preview());
    const bool paused = v.runtime().clock().paused();
    const char* play = state.at_horizon ? "Complete" : paused ? state.tick == 0 && m.arrived == 0 ? "Play" : "Resume" : "Pause";
    if (icon_button("Playback",layout.compact ? "" : play,paused ? Icon::Play : Icon::Pause,
        {layout.compact ? 56*s : 104*s,layout.control_height},state.at_horizon ? "Run complete. Reset or seek to continue." : "Play / pause (Space)"))
        result.commands.push_back({ViewerAction::Toggle});
    result.play = center(); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!paused || state.at_horizon);
    if (icon_button("Step",layout.compact ? "" : "Step",Icon::Step,{layout.compact ? 48*s : 84*s,layout.control_height},"Advance exactly one simulation tick while paused (Right arrow)"))
        result.commands.push_back({ViewerAction::Step});
    result.step = center(); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(v.result_preview());
    if (icon_button("Reset",layout.compact ? "" : "Reset",Icon::Reset,{layout.compact ? 48*s : 88*s,layout.control_height},"Restore untouched initial state and pause"))
        result.commands.push_back({ViewerAction::Reset});
    result.reset = center(); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(v.result_preview());
    ImGui::SetNextItemWidth(96*s);
    const std::string speed_label = display_speed(v.speed());
    if (ImGui::BeginCombo("##Speed",speed_label.c_str()))
    {
        for (double speed : {1.0,10.0,60.0,300.0,900.0})
        { const auto label = display_speed(speed); if (ImGui::Selectable(label.c_str(),speed == v.speed())) result.commands.push_back({ViewerAction::Speed,speed}); }
        if (ImGui::Selectable("Custom speed...")) { draft.custom_requested = true; draft.custom_speed = v.speed(); }
        ImGui::EndCombo();
    }
    result.speed = center(); tooltip("Simulated time / real time. Range 0.01-3600x.");
    if (draft.custom_requested) { ImGui::OpenPopup("CustomSpeed"); draft.custom_requested = false; }
    if (ImGui::BeginPopup("CustomSpeed"))
    {
        ImGui::TextUnformatted("Playback speed (0.01-3600x)"); ImGui::SetNextItemWidth(210*s);
        const bool entered = ImGui::InputDouble("##Value",&draft.custom_speed,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Apply") || entered)
        {
            if (std::isfinite(draft.custom_speed) && draft.custom_speed >= 0.01 && draft.custom_speed <= 3600)
            { result.commands.push_back({ViewerAction::Speed,draft.custom_speed}); draft.feedback_error = false; ImGui::CloseCurrentPopup(); }
            else { draft.feedback = "Speed must be 0.01-3600x"; draft.feedback_error = true; }
        }
        if (draft.feedback_error) ImGui::TextColored(color(design::error),"%s",draft.feedback.c_str());
        ImGui::EndPopup();
    }
    if (!layout.compact)
    {
        ImGui::SameLine(); ImGui::SetNextItemWidth(88*s);
        double speed = v.speed();
        if (ImGui::InputDouble("x##CustomSpeed",&speed,0,0,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (std::isfinite(speed) && speed >= 0.01 && speed <= 3600) { result.commands.push_back({ViewerAction::Speed,speed}); draft.feedback_error = false; }
            else { draft.feedback = "Speed must be 0.01-3600x"; draft.feedback_error = true; }
        }
        result.speed = center(); tooltip(draft.feedback_error ? draft.feedback.c_str() : "Custom playback speed; press Enter to apply.");
    }
    ImGui::EndDisabled();
    if (!layout.compact) ImGui::SameLine(layout.playback.size.x-210*s);
    {
        Font font{20}; if (layout.compact) ImGui::Spacing();
        ImGui::Text("%s / %s",format_time(state.tick,state.tick_rate).c_str(),format_time(state.horizon,state.tick_rate).c_str());
    }
    if (!draft.seeking) draft.seek_tick = state.tick;
    const std::uint64_t zero = 0;
    if (!layout.compact) { Font font{design::label}; ImGui::TextColored(color(design::secondary),"Plain scenario"); ImGui::SameLine(layout.padding+108*s); }
    const auto display = format_time(draft.seek_tick,state.tick_rate);
    ImGui::SetNextItemWidth(-1); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{8*s,5*s});
    ImGui::BeginDisabled(v.result_preview());
    ImGui::SliderScalar("##Timeline",ImGuiDataType_U64,&draft.seek_tick,&zero,&state.horizon,display.c_str());
    ImGui::PopStyleVar();
    tooltip("Plain, unmodified scenarios only. Drag and release to restore/replay all events to the target tick.");
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    result.timeline_min = {a.x,a.y}; result.timeline_max = {b.x,b.y};
    if (ImGui::IsItemDeactivatedAfterEdit()) result.commands.push_back({ViewerAction::Seek,0,draft.seek_tick});
    draft.seeking = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (draft.feedback_error) { Font font{design::label}; ImGui::TextColored(color(design::error),"%s",draft.feedback.c_str()); }
    ImGui::End();
    auto* draw = ImGui::GetForegroundDrawList(); const auto p = layout.viewport.origin, end = p+layout.viewport.size;
    draw->PushClipRect({p.x,p.y},{end.x,end.y});
    { Font font{design::label}; draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{p.x+16*s,p.y+12*s},packed(design::secondary),
        v.result_preview()?"FINAL RESULT / READ ONLY":draft.viewing_baseline?"BASELINE / LIVE SIMULATION":"DRAFT / INDEPENDENT RUN"); }
    const auto label_at=[&](glm::vec3 position,const std::string& caption)
    {
        auto point=v.orbit_camera().camera().view_projection()*glm::vec4{position,1};
        if(point.w<=0) return; point/=point.w;
        if(point.z<=0 || point.z>=1) return;
        const ImVec2 at{p.x+(point.x+1)*layout.viewport.size.x*0.5f,p.y+(point.y+1)*layout.viewport.size.y*0.5f};
        Font font{design::label}; const auto size=ImGui::CalcTextSize(caption.c_str());
        draw->AddRectFilled({at.x-7*s,at.y-4*s},{at.x+size.x+7*s,at.y+size.y+4*s},packed(design::navy),5*s);
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),at,packed(design::secondary),caption.c_str());
    };
    if(layout.viewport.size.x/s>=500)
    {
        using Zone=visualization::WarehouseZoneKind;
        for(const auto& zone:v.scene().layout().zones)
        {
            std::string caption;
            if(zone.kind==Zone::Docks) caption="LOADING  "+std::to_string(state.usage.loading_bays)+" / "+std::to_string(state.capacity.loading_bays);
            else if(zone.kind==Zone::Storage) caption="STORAGE";
            else if(zone.kind==Zone::Cargo) caption="FIFO QUEUE  "+std::to_string(m.waiting);
            else continue;
            label_at({zone.bounds.minimum.x+0.4f,0.06f,zone.bounds.maximum.z-0.2f},caption);
        }
    }
    if (layout.viewport.size.x/s >= 600)
    {
        Font font{design::label};
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{p.x+16*s,end.y-24*s},packed(design::secondary),"Drag to orbit  /  Click to inspect  /  Wheel to zoom  /  F to frame");
    }
    if (!draft.feedback.empty() && !draft.feedback_error)
    {
        Font font{design::label}; draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{p.x+16*s,end.y-26*s},packed(design::secondary),draft.feedback.c_str());
    }
    if (inspection)
    {
        const auto objects = v.scene().selectables();
        const auto selected = std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.key == inspection->key;});
        if (selected != objects.end())
        {
            ImVec2 low{1e9f,1e9f}, high{-1e9f,-1e9f}; bool visible = true;
            const auto matrix = v.orbit_camera().camera().view_projection(); const auto pixels = layout.viewport.pixels();
            for (int corner = 0; corner < 8; ++corner)
            {
                auto point = matrix*glm::vec4{(corner&1)?selected->bounds.maximum.x:selected->bounds.minimum.x,
                    (corner&2)?selected->bounds.maximum.y:selected->bounds.minimum.y,(corner&4)?selected->bounds.maximum.z:selected->bounds.minimum.z,1};
                if (point.w <= 0) { visible = false; break; } point /= point.w;
                const float x = (pixels.x+(point.x+1)*0.5f*pixels.width)/layout.viewport.framebuffer_scale.x;
                const float y = (pixels.y+(point.y+1)*0.5f*pixels.height)/layout.viewport.framebuffer_scale.y;
                low.x = std::min(low.x,x); low.y = std::min(low.y,y); high.x = std::max(high.x,x); high.y = std::max(high.y,y);
            }
            if (visible)
            {
                draw->AddRect(low,high,IM_COL32(8,14,22,255),3.0f,6*s,ImDrawFlags_None);
                draw->AddRect(low,high,IM_COL32(255,225,140,255),3.0f,3*s,ImDrawFlags_None);
                const auto caption = std::string{category(inspection->key.category)}+" #"+std::to_string(inspection->key.id);
                Font font{design::label}; draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{low.x,low.y-20*s},IM_COL32(255,235,190,255),caption.c_str());
            }
        }
    }
    draw->PopClipRect(); return result;
}
