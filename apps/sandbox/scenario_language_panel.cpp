#include "scenario_language_panel.hpp"
#include <simforge/viewer/presentation.hpp>
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <chrono>
using namespace simforge;
namespace nl=scenario::language;
namespace
{
glm::vec2 center() {const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
bool input(const char* label,std::string& value,std::size_t limit)
{
    std::vector<char> buffer(limit+1);std::copy_n(value.data(),std::min(value.size(),limit),buffer.data());
    if(ImGui::InputText(label,buffer.data(),buffer.size())) {value=buffer.data();return true;}return false;
}
void values(LanguageUI& u)
{
    if(!u.proposal) return;
    for(std::size_t i=0;i<nl::physical_fields.size();++i) u.edits[i]=std::to_string(nl::value(u.proposal->proposed(),nl::physical_fields[i]));
    u.name=u.proposal->proposed().name;u.edited={};u.name_edited=false;u.approved=false;
}
void interpret(workspace::ScenarioWorkspace& w,LanguageUI& u)
{
    u.error.clear();u.proposal.reset();u.approved=false;
    try {
        const auto mode=static_cast<nl::Mode>(u.mode);const auto source=mode==nl::Mode::Create?nl::default_source():w.draft_definition();
        u.editor_before=w.draft().values;
        const auto start=std::chrono::steady_clock::now();u.proposal=nl::interpret(u.description,mode,source,w.language_stamp());
        u.interpretation_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        values(u);u.input_open=false;u.edit_open=false;
    } catch(const std::exception& e) {u.error=std::string{"Source draft: "}+e.what()+". Correct the manual editor or choose Create New.";}
}
void corrections(LanguageUI& u)
{
    try {
        std::vector<nl::Operation> operations;
        for(std::size_t i=0;i<nl::physical_fields.size();++i) if(u.edited[i]) {
            const auto& text=u.edits[i];std::uint64_t n{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
            if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()) throw std::invalid_argument("Review values must be exact nonnegative integers");
            const auto field=nl::physical_fields[i];const auto unit=i<3?nl::Unit::Count:i<6?nl::Unit::Seconds:field==nl::Field::TickRate?nl::Unit::Hertz:nl::Unit::Integer;
            operations.push_back({field,nl::Action::Set,{n,1},unit,{},nl::Origin::Review,nl::Pattern::Review});
        }
        u.proposal=nl::revise(*u.proposal,operations,u.name_edited?std::optional{u.name}:std::nullopt);
        values(u);u.error.clear();u.edit_open=false;
    } catch(const std::exception& e) {u.error=e.what();}
}
const char* row_label(std::size_t index) {return workspace::field_names[index+1];}
const char* action_title(nl::Action action)
{
    switch(action) {
    case nl::Action::Set:return "Set";case nl::Action::Add:return "Add";case nl::Action::Subtract:return "Subtract";
    case nl::Action::Multiply:return "Multiply";case nl::Action::Divide:return "Divide";
    case nl::Action::IncreasePercent:return "Increase by percent";case nl::Action::DecreasePercent:return "Decrease by percent";
    }return "Review operation";
}
const char* diagnostic_title(nl::Code code)
{
    switch(code) {
    case nl::Code::InputLimit:return "Description limit";case nl::Code::UnsupportedCharacter:return "Unsupported character";
    case nl::Code::InvalidNumber:return "Invalid number";case nl::Code::InvalidUnit:return "Check units";
    case nl::Code::UnsupportedInstruction:return "Unsupported request";case nl::Code::AmbiguousInstruction:return "Needs clarification";
    case nl::Code::Conflict:return "Conflicting instructions";case nl::Code::Precision:return "Exact value required";
    case nl::Code::Range:return "Invalid configuration";case nl::Code::Overflow:return "Number too large";
    case nl::Code::InvalidSource:return "Invalid source scenario";case nl::Code::NoOperations:return "No changes recognized";
    case nl::Code::InheritedDefaults:return "Inherited values";
    }return "Review message";
}
}
void draw_language(workspace::ScenarioWorkspace& w,LanguageUI& u)
{
    if(u.open_requested) {u.visible=true;u.open_requested=false;u.input_open=true;u.approved=false;ImGui::OpenPopup("Describe Your Warehouse");}
    const auto display=ImGui::GetIO().DisplaySize;const auto s=ImGui::GetStyle().FontScaleDpi;
    const bool compact_window=display.x/s<680;
    if(compact_window) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{8*s,6*s});
    ImGui::SetNextWindowPos({display.x/2,display.y/2},ImGuiCond_Always,{0.5f,0.5f});
    ImGui::SetNextWindowSize({std::min(920*s,display.x-30),std::min(680*s,display.y-30)},ImGuiCond_Always);
    if(!ImGui::BeginPopupModal("Describe Your Warehouse",&u.visible,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize)) {
        if(compact_window) ImGui::PopStyleVar();return;
    }
    const auto position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    u.modal_min={position.x,position.y};u.modal_max={position.x+size.x,position.y+size.y};
    const bool compact=ImGui::GetContentRegionAvail().x/s<600;
    const auto* interpret_label=u.proposal?"Interpret Again":"Interpret";
    if(compact) {
        const auto buttons=ImGui::CalcTextSize(interpret_label).x+ImGui::CalcTextSize("Clear").x+ImGui::GetStyle().FramePadding.x*4+ImGui::GetStyle().ItemSpacing.x*2;
        ImGui::SetNextItemWidth(std::max(80*s,ImGui::GetContentRegionAvail().x-buttons));
        if(ImGui::BeginCombo("##DescriptionMode",u.mode==0?"Create":"Modify")) {
            if(ImGui::Selectable("Create New",u.mode==0)) {u.mode=0;u.approved=false;}u.create=center();
            if(ImGui::Selectable("Modify Current Scenario",u.mode==1)) {u.mode=1;u.approved=false;}u.modify=center();
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    } else {
        if(ImGui::RadioButton("Create New",u.mode==0)) {u.mode=0;u.approved=false;}u.create=center();ImGui::SameLine();
        if(ImGui::RadioButton("Modify Current Scenario",u.mode==1)) {u.mode=1;u.approved=false;}u.modify=center();
        ImGui::TextWrapped("%s",u.mode==0?"Start with bundled warehouse-baseline defaults; inherited values are shown below.":"Current scenario means the editor draft. Only explicit changes are proposed.");
    }
    if(ImGui::Button(interpret_label)) interpret(w,u);u.interpret=center();ImGui::SameLine();
    if(ImGui::Button("Clear")) {u.description.clear();u.proposal.reset();u.error.clear();u.approved=false;u.input_open=true;}
    if(!compact) {ImGui::SameLine();ImGui::TextDisabled("Offline interpretation / explicit review");}
    const auto footer_height=ImGui::GetFrameHeightWithSpacing()*2+ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("Language review content",{0,-footer_height});
    u.review_height=ImGui::GetWindowSize().y;
    if(!compact||u.input_open||!u.proposal) {
        ImGui::SetNextItemOpen(u.input_open,ImGuiCond_Always);u.input_open=ImGui::CollapsingHeader("Description");
    }
    if(u.input_open) {
        std::array<char,nl::max_input_bytes+1> buffer{};std::copy_n(u.description.data(),std::min(u.description.size(),nl::max_input_bytes),buffer.data());
        if(ImGui::InputTextMultiline("##WarehouseDescription",buffer.data(),buffer.size(),{-1,100*s})) {u.description=buffer.data();u.approved=false;}
        u.description_input=center();
        ImGui::TextWrapped("Aggregate bays/docks, workers/staff, forklifts/lift trucks; arrivals, processing and simulation time. Counts, seconds/minutes/hours, percentages and exact factors.");
        if(ImGui::Button("Create example")) {
            u.mode=0;u.description="Create a warehouse with 4 loading bays, 12 workers and 3 forklifts. Shipments arrive every 5 minutes and take 15 minutes to process. Run the simulation for 8 hours.";
            u.proposal.reset();u.approved=false;
        }ImGui::SameLine();
        if(ImGui::Button("Modify example")) {u.mode=1;u.description="Get 3 more bays and twice as many workers.";u.proposal.reset();u.approved=false;}
        ImGui::TextDisabled("Use specific parameters; navigation, collision avoidance and optimization goals need a different model.");
    }
    if(!u.error.empty()) ImGui::TextColored({viewer::design::error.x,viewer::design::error.y,viewer::design::error.z,1},"%s",u.error.c_str());
    bool stale=false,pending=false;
    if(u.proposal) {
        const auto& p=*u.proposal;stale=p.stamp()!=w.language_stamp()||p.description()!=u.description||static_cast<int>(p.mode())!=u.mode;
        if(stale) ImGui::TextWrapped("Source, description or mode changed. Interpret Again before approval.");
        if(!compact) ImGui::TextWrapped("Source: %s",p.source().name.c_str());
        const auto status_color=p.valid()?viewer::design::completed:viewer::design::error;
        ImGui::TextColored({status_color.x,status_color.y,status_color.z,1},"%s",p.valid()?"Ready for review":"Resolve diagnostics");
        if(compact) {
            for(std::size_t i=0;i<nl::physical_fields.size();++i) {
                const auto field=nl::physical_fields[i];const auto computed=p.computed()[i];
                if(i) ImGui::Separator();
                ImGui::TextWrapped("%s: %s -> %s",row_label(i),u.editor_before[i+1].c_str(),computed?nl::format(*computed).c_str():"Unavailable");
                bool explicit_value=false;
                for(const auto& op:p.operations()) if(nl::physical_field(op.field)==field) {
                    ImGui::TextWrapped("%s: \"%s\"",action_title(op.action),p.description().substr(op.source.begin,op.source.size).c_str());explicit_value=true;
                }
                for(const auto& op:p.corrections()) if(op.field==field) {ImGui::TextUnformatted("Explicit review correction");explicit_value=true;}
                if(!explicit_value) ImGui::TextUnformatted(p.mode()==nl::Mode::Create?"Inherited baseline value":"Unchanged");
            }
            ImGui::TextWrapped("Name: %s -> %s",u.editor_before[0].c_str(),p.proposed().name.c_str());
            ImGui::TextWrapped("Source: %s",p.source().name.c_str());
        } else if(ImGui::BeginTable("Scenario proposal",4,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Parameter",ImGuiTableColumnFlags_WidthStretch,1.2f);
            ImGui::TableSetupColumn("Before (editor)",ImGuiTableColumnFlags_WidthStretch,0.7f);
            ImGui::TableSetupColumn("Proposed",ImGuiTableColumnFlags_WidthStretch,0.8f);
            ImGui::TableSetupColumn("Source / assumptions",ImGuiTableColumnFlags_WidthStretch,2.6f);ImGui::TableHeadersRow();
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("Scenario name");ImGui::TableNextColumn();ImGui::TextWrapped("%s",u.editor_before[0].c_str());
            ImGui::TableNextColumn();ImGui::TextWrapped("%s",p.proposed().name.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(p.mode()==nl::Mode::Create?"Generated suggestion; editable":"Unchanged unless reviewed");
            for(std::size_t i=0;i<nl::physical_fields.size();++i) {
                const auto field=nl::physical_fields[i];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(row_label(i));
                ImGui::TableNextColumn();ImGui::TextWrapped("%s",u.editor_before[i+1].c_str());
                ImGui::TableNextColumn();const auto computed=p.computed()[i];ImGui::TextUnformatted(computed?nl::format(*computed).c_str():"Unavailable");
                ImGui::TableNextColumn();bool explicit_value=false;
                for(const auto& op:p.operations()) if(nl::physical_field(op.field)==field) {
                    const auto quote=p.description().substr(op.source.begin,op.source.size);ImGui::TextWrapped("%s: \"%s\"",action_title(op.action),quote.c_str());explicit_value=true;
                }
                for(const auto& op:p.corrections()) if(op.field==field) {ImGui::TextUnformatted("Explicit review correction");explicit_value=true;}
                if(!explicit_value) ImGui::TextDisabled("%s",p.mode()==nl::Mode::Create?"Inherited baseline value":"Unchanged");
            }ImGui::EndTable();
        }
        for(const auto& d:p.diagnostics()) {
            const auto color=d.severity==nl::Severity::Error?viewer::design::error:viewer::design::secondary;
            ImGui::PushStyleColor(ImGuiCol_Text,{color.x,color.y,color.z,1});ImGui::TextWrapped("%s: %s",diagnostic_title(d.code),d.message.c_str());
            if(d.source.size) ImGui::TextWrapped("From: \"%s\"",p.description().substr(d.source.begin,d.source.size).c_str());ImGui::PopStyleColor();
        }
        if(ImGui::Button("Edit Proposal")) {u.edit_open=!u.edit_open;u.approved=false;}u.edit=center();
        if(u.scroll_action==1) {ImGui::SetScrollHereY(0.5f);u.scroll_action=-1;}
        if(compact) {
            if(ImGui::Button("Edit description")) {u.input_open=true;u.approved=false;ImGui::SetScrollY(0);}
        }
        if(u.edit_open) {
            ImGui::TextWrapped("Correct exact values below. Recognition errors and conflicts require revised text and Interpret Again.");
            ImGui::SetNextItemWidth(-1);if(input("##ReviewedName",u.name,256)) {u.name_edited=true;u.approved=false;}u.name_input=center();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scenario name");
            for(std::size_t i=0;i<nl::physical_fields.size();++i) {
                ImGui::PushID(static_cast<int>(i));ImGui::TextUnformatted(row_label(i));ImGui::SetNextItemWidth(-1);
                if(input("##ReviewedValue",u.edits[i],32)) {u.edited[i]=true;u.approved=false;}u.edit_inputs[i]=center();
                if(u.scroll_field==static_cast<int>(i)) {ImGui::SetScrollHereY(0.5f);u.scroll_field=-1;}ImGui::PopID();
            }
            if(ImGui::Button("Validate Corrections")) corrections(u);u.validate_edits=center();
            if(u.scroll_action==2) {ImGui::SetScrollHereY(0.5f);u.scroll_action=-1;}
        }
        pending=u.name_edited||std::any_of(u.edited.begin(),u.edited.end(),[](bool e){return e;});
    }
    ImGui::EndChild();ImGui::Separator();
    const bool can_approve=u.proposal&&u.proposal->valid()&&!stale&&!pending;
    ImGui::BeginDisabled(!can_approve);ImGui::Checkbox("I approve these values",&u.approved);u.approve=center();ImGui::EndDisabled();
    ImGui::BeginDisabled(!can_approve||!u.approved);
    if(ImGui::Button("Apply to Editor")) {u.apply_requested=true;u.visible=false;ImGui::CloseCurrentPopup();}u.apply=center();ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Discard")) {u.proposal.reset();u.approved=false;u.visible=false;ImGui::CloseCurrentPopup();}u.discard=center();
    u.footer_bottom=ImGui::GetItemRectMax().y;
    ImGui::EndPopup();
    if(compact_window) ImGui::PopStyleVar();
}
