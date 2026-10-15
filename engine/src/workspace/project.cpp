#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include "../scenario/json_io.hpp"
#include "../analysis/report_value.hpp"
#include "persistence_stream.hpp"
#include <mutex>
#include <thread>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace simforge::workspace
{
namespace
{
using namespace scenario::detail;
using Timer=std::chrono::steady_clock;
double ms(Timer::time_point start) { return std::chrono::duration<double,std::milli>(Timer::now()-start).count(); }
std::string identity()
{
    std::random_device random; std::ostringstream out;
    for(int i=0;i<4;++i) out<<std::hex<<std::setfill('0')<<std::setw(8)<<random();
    return out.str();
}
std::string timestamp()
{
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string bounded(const Json& j, std::size_t maximum, const char* label)
{
    auto value=string(j,label);
    if(value.size()>maximum || value.find('\0')!=std::string::npos) invalid(label,"invalid string length or NUL");
    return value;
}
bool boolean(const Json& j) { if(!j.is_boolean()) invalid("project","expected boolean"); return j.get<bool>(); }
Json workload_draft_json(const WorkloadDraft& d)
{
    (void)scenario::workload::arrival_name(d.arrivals);(void)scenario::workload::processing_name(d.processing);
    if(d.segments.size()>scenario::max_demand_segments) invalid("draft workload","too many segments");
    for(const auto& value:d.bounds) (void)bounded(Json(value),4096,"workload bound");
    for(const auto& segment:d.segments) for(const auto& value:segment) (void)bounded(Json(value),4096,"workload segment");
    Json j{{"arrivals",static_cast<unsigned>(d.arrivals)},{"processing",static_cast<unsigned>(d.processing)},{"bounds",d.bounds},{"segments",d.segments}};
    if(d.arrivals==scenario::ArrivalMode::Poisson || d.arrivals==scenario::ArrivalMode::PiecewisePoisson || !d.intensities.empty() || d.rate_per_second!=WorkloadDraft{}.rate_per_second) {
        if(d.intensities.size()>scenario::max_demand_segments)invalid("intensities","too many segments");
        j["rate_per_second"]=bounded(Json(d.rate_per_second),4096,"rate");j["intensities"]=Json::array();
        for(const auto& s:d.intensities){for(const auto& v:s)(void)bounded(Json(v),4096,"intensity field");j["intensities"].push_back(s);}
    }
    return j;
}
WorkloadDraft workload_draft_value(const Json& j)
{
    fields(j,{"arrivals","processing","bounds","segments"},{"rate_per_second","intensities"},"draft workload");WorkloadDraft d;
    d.arrivals=static_cast<scenario::ArrivalMode>(integer(j.at("arrivals"),4,"draft arrival mode"));
    d.processing=static_cast<scenario::ProcessingMode>(integer(j.at("processing"),1,"draft processing mode"));
    const auto& b=j.at("bounds");const auto& segments=j.at("segments");
    if(!b.is_array() || b.size()!=4 || !segments.is_array() || segments.size()>scenario::max_demand_segments) invalid("draft workload","invalid array bounds");
    for(std::size_t i=0;i<4;++i) d.bounds[i]=bounded(b[i],4096,"workload bound");
    for(const auto& s:segments) {
        if(!s.is_array() || s.size()!=3) invalid("draft workload","invalid segment");
        d.segments.push_back({bounded(s[0],4096,"start seconds"),bounded(s[1],4096,"end seconds"),bounded(s[2],4096,"interval seconds")});
    }
    if(j.contains("rate_per_second"))d.rate_per_second=bounded(j.at("rate_per_second"),4096,"rate");
    if(j.contains("intensities")){const auto& a=j.at("intensities");if(!a.is_array()||a.size()>scenario::max_demand_segments)invalid("intensities","at most 64 segments");for(const auto& s:a){if(!s.is_array()||s.size()!=3)invalid("intensity","three fields required");d.intensities.push_back({bounded(s[0],4096,"start"),bounded(s[1],4096,"end"),bounded(s[2],4096,"rate")});}}
    return d;
}
bool has_workload(const Json& j)
{
    if(j.is_object()) {
        if(j.contains("workload") || j.contains("draft_workload") || j.contains("draft_origin_workload")) return true;
        for(const auto& child:j) if(has_workload(child)) return true;
    } else if(j.is_array()) for(const auto& child:j) if(has_workload(child)) return true;
    return false;
}
bool has_poisson(const Json& j) {
    if(j.is_object()) {
        if(j.contains("intensities"))return true; // Retained inactive Poisson draft fields also require the new codec.
        if(j.contains("mode") && j.at("mode").is_string() && (j.at("mode")=="poisson" || j.at("mode")=="piecewise_poisson"))return true;
        if(j.contains("arrivals") && j.at("arrivals").is_number_integer() && (j.at("arrivals")==3 || j.at("arrivals")==4))return true;
        for(const auto& value:j)if(has_poisson(value))return true;
    }else if(j.is_array())for(const auto& value:j)if(has_poisson(value))return true;
    return false;
}
bool has_statistics(const Json& j)
{
    if(j.is_object()) {if(j.contains("statistical_report_version") || j.contains("statistics")) return true;for(const auto& child:j) if(has_statistics(child)) return true;}
    else if(j.is_array()) for(const auto& child:j) if(has_statistics(child)) return true;return false;
}
bool has_interaction(const Json& j) {
    if(j.is_object()){if(j.contains("interaction_report_version")||j.contains("interaction"))return true;for(const auto& child:j)if(has_interaction(child))return true;}else if(j.is_array())for(const auto& child:j)if(has_interaction(child))return true;return false;
}
bool has_sensitivity(const Json& j) {
    if(j.is_object()) {if(j.contains("sensitivity_report_version") || j.contains("sensitivity"))return true;for(const auto& child:j)if(has_sensitivity(child))return true;}
    else if(j.is_array())for(const auto& child:j)if(has_sensitivity(child))return true;return false;
}
double number(const Json& j,double minimum,double maximum,const char* label)
{
    if(!j.is_number()) invalid(label,"expected numeric value");
    const auto value=j.get<double>();if(!std::isfinite(value) || value<minimum || value>maximum) invalid(label,"invalid numeric range");
    return value;
}
std::uint64_t id(const Json& j) { return integer(j,std::numeric_limits<std::uint64_t>::max()-1,"identity"); }
Json definition(const scenario::ScenarioDefinition& d) { return Json::parse(scenario::serialize_scenario(d)); }
scenario::ScenarioDefinition definition(const Json& j)
{
    auto d=scenario::parse_scenario(j.dump());
    if(!std::holds_alternative<scenario::WarehouseConfig>(d.domain)) invalid("scenario","warehouse required");
    return d;
}
Json snapshot(const warehouse::WarehouseSnapshot& state)
{
    const auto bytes=warehouse::serialize_snapshot(state);
    std::string hex; hex.reserve(bytes.size()*2); constexpr char digits[]="0123456789abcdef";
    for(auto b:bytes) {auto n=std::to_integer<unsigned>(b); hex+=digits[n>>4]; hex+=digits[n&15];}
    return hex;
}
warehouse::WarehouseSnapshot snapshot(const Json& j)
{
    const auto hex=bounded(j,warehouse::warehouse_snapshot_max_bytes*2,"snapshot");
    if(hex.size()%2) invalid("snapshot","truncated hex");
    const auto digit=[](char c)->unsigned {if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; invalid("snapshot","invalid hex");};
    std::vector<std::byte> bytes; bytes.reserve(hex.size()/2);
    for(std::size_t i=0;i<hex.size();i+=2) bytes.push_back(static_cast<std::byte>((digit(hex[i])<<4)|digit(hex[i+1])));
    return warehouse::deserialize_snapshot(bytes);
}
Json clock_json(const simulation::ClockSnapshot& c)
{
    return {{"version",c.version},{"rate",c.ticks_per_second},{"tick",c.ticks},{"remainder",c.remainder},{"paused",c.paused},{"speed",c.time_scale}};
}
simulation::ClockSnapshot clock_value(const Json& j)
{
    fields(j,{"version","rate","tick","remainder","paused","speed"},{},"clock");
    if(!j.at("speed").is_number()) invalid("clock","speed must be numeric");
    simulation::ClockSnapshot c{static_cast<std::uint32_t>(integer(j.at("version"),1,"clock version")),
        static_cast<std::uint32_t>(integer(j.at("rate"),1000,"clock rate")),id(j.at("tick")),id(j.at("remainder")),
        boolean(j.at("paused")),j.at("speed").get<double>()};
    simulation::validate_snapshot(c); return c;
}
Json camera_json(const viewer::CameraPose& c) {return {c.yaw,c.pitch,c.distance,c.target.x,c.target.y,c.target.z};}
viewer::CameraPose camera_value(const Json& j)
{
    if(!j.is_array() || j.size()!=6) invalid("camera","expected six coordinates");
    for(const auto& n:j) if(!n.is_number() || !std::isfinite(n.get<double>()) || std::abs(n.get<double>())>1e9) invalid("camera","invalid coordinate");
    return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>(),{j[3].get<float>(),j[4].get<float>(),j[5].get<float>()}};
}
Json selection_json(std::optional<viewer::VisualKey> key)
{
    if(!key) return nullptr;
    return Json::array({static_cast<int>(key->category),key->id});
}
std::optional<viewer::VisualKey> selection_value(const Json& j)
{
    if(j.is_null()) return {};
    if(!j.is_array() || j.size()!=2) invalid("selection","invalid shape");
    return viewer::VisualKey{static_cast<viewer::VisualCategory>(integer(j[0],static_cast<unsigned>(viewer::VisualCategory::Rack),"selection category")),static_cast<std::uint32_t>(integer(j[1],100000,"selection identity"))};
}
Json preferences_json(const ProjectPreferences& p)
{
    Json j{{"mode",p.application_mode},{"metrics",p.metrics},{"inspector",p.inspector},{"focus",p.focus},{"workspace",p.workspace},
        {"metrics_explicit",p.metrics_explicit},{"comparison_mode",static_cast<int>(p.comparison_mode)},{"objective",static_cast<int>(p.objective)},
        {"current_checkpoint",p.current_checkpoint},{"checkpoint_seconds",p.checkpoint_seconds},{"selected_candidate",p.selected_candidate?Json(*p.selected_candidate):Json(nullptr)},
        {"notes",p.notes},{"result_name",p.result_name},{"editor_tab",p.editor_tab}};
    if(p.statistical || p.replications!="20" || p.base_seed!="1001" || !p.seed_list.empty() || p.seed_policy || p.statistical_metric)
        j["statistics"]={{"enabled",p.statistical},{"replications",p.replications},{"base_seed",p.base_seed},{"seed_list",p.seed_list},{"seed_policy",p.seed_policy},{"metric",p.statistical_metric}};
    const ProjectPreferences defaults;
    if(p.interaction||p.grid_multi||p.grid_delta||p.grid_interactions||p.grid_x||p.grid_y!=1||p.grid_objective||p.grid_metric||p.grid_selected||p.grid_rectangle||p.grid_seed||p.grid_explicit!=defaults.grid_explicit||p.grid_min!=defaults.grid_min||p.grid_max!=defaults.grid_max||p.grid_step!=defaults.grid_step||p.grid_values!=defaults.grid_values||p.grid_name!=defaults.grid_name)
        j["interaction"]={{"enabled",p.interaction},{"multi",p.grid_multi},{"delta",p.grid_delta},{"interactions",p.grid_interactions},{"x",p.grid_x},{"y",p.grid_y},{"objective",p.grid_objective},{"metric",p.grid_metric},{"selected",p.grid_selected},{"rectangle",p.grid_rectangle},{"seed",p.grid_seed},{"explicit",p.grid_explicit},{"minimum",p.grid_min},{"maximum",p.grid_max},{"step",p.grid_step},{"values",p.grid_values},{"name",p.grid_name}};
    if(p.sensitivity || p.sweep_multi || p.sweep_explicit || p.sweep_parameter || p.sweep_objective || p.sweep_selected || p.sweep_seed || p.sweep_min!=defaults.sweep_min || p.sweep_max!=defaults.sweep_max || p.sweep_step!=defaults.sweep_step || p.sweep_values!=defaults.sweep_values || p.sweep_threshold!=defaults.sweep_threshold || p.sweep_name!=defaults.sweep_name)
        j["sensitivity"]={{"enabled",p.sensitivity},{"multi",p.sweep_multi},{"explicit",p.sweep_explicit},{"parameter",p.sweep_parameter},{"objective",p.sweep_objective},{"selected",p.sweep_selected},{"seed",p.sweep_seed},{"minimum",p.sweep_min},{"maximum",p.sweep_max},{"step",p.sweep_step},{"values",p.sweep_values},{"threshold",p.sweep_threshold},{"name",p.sweep_name}};
    return j;
}
ProjectPreferences preferences_value(const Json& j)
{
    fields(j,{"mode","metrics","inspector","focus","workspace","metrics_explicit","comparison_mode","objective","current_checkpoint","checkpoint_seconds","selected_candidate","notes","result_name","editor_tab"},{"statistics","sensitivity","interaction"},"preferences");
    ProjectPreferences p;
    p.application_mode=static_cast<int>(integer(j.at("mode"),2,"mode")); p.metrics=boolean(j.at("metrics")); p.inspector=boolean(j.at("inspector"));
    p.focus=boolean(j.at("focus")); p.workspace=boolean(j.at("workspace")); p.metrics_explicit=boolean(j.at("metrics_explicit"));
    p.comparison_mode=static_cast<ComparisonMode>(integer(j.at("comparison_mode"),1,"comparison mode"));
    p.objective=static_cast<analysis::Objective>(integer(j.at("objective"),2,"objective")); p.current_checkpoint=boolean(j.at("current_checkpoint"));
    p.checkpoint_seconds=bounded(j.at("checkpoint_seconds"),4096,"checkpoint text");
    if(!j.at("selected_candidate").is_null()) p.selected_candidate=id(j.at("selected_candidate"));
    p.notes=bounded(j.at("notes"),4096,"notes"); p.result_name=bounded(j.at("result_name"),256,"result name");
    p.editor_tab=static_cast<int>(integer(j.at("editor_tab"),2,"editor tab"));
    if(j.contains("statistics")) {const auto& s=j.at("statistics");fields(s,{"enabled","replications","base_seed","seed_list","seed_policy","metric"},{},"statistical preferences");
        p.statistical=boolean(s.at("enabled"));p.replications=bounded(s.at("replications"),4096,"replications");p.base_seed=bounded(s.at("base_seed"),4096,"base seed");p.seed_list=bounded(s.at("seed_list"),4096,"seed list");
        p.seed_policy=static_cast<int>(integer(s.at("seed_policy"),1,"seed policy"));p.statistical_metric=static_cast<int>(integer(s.at("metric"),4,"metric"));}
    if(j.contains("sensitivity")) {const auto& s=j.at("sensitivity");fields(s,{"enabled","multi","explicit","parameter","objective","selected","seed","minimum","maximum","step","values","threshold","name"},{},"sensitivity preferences");
        p.sensitivity=boolean(s.at("enabled"));p.sweep_multi=boolean(s.at("multi"));p.sweep_explicit=boolean(s.at("explicit"));
        p.sweep_parameter=static_cast<int>(integer(s.at("parameter"),4,"parameter"));p.sweep_objective=static_cast<int>(integer(s.at("objective"),4,"objective"));p.sweep_selected=static_cast<int>(integer(s.at("selected"),49,"selected"));p.sweep_seed=static_cast<int>(integer(s.at("seed"),99,"seed"));
        p.sweep_min=bounded(s.at("minimum"),4096,"minimum");p.sweep_max=bounded(s.at("maximum"),4096,"maximum");p.sweep_step=bounded(s.at("step"),4096,"step");p.sweep_values=bounded(s.at("values"),4096,"values");p.sweep_threshold=bounded(s.at("threshold"),4096,"threshold");p.sweep_name=bounded(s.at("name"),4096,"name");}
    if(j.contains("interaction")){const auto& g=j.at("interaction");fields(g,{"enabled","multi","delta","interactions","x","y","objective","metric","selected","rectangle","seed","explicit","minimum","maximum","step","values","name"},{},"interaction preferences");
        p.interaction=boolean(g.at("enabled"));p.grid_multi=boolean(g.at("multi"));p.grid_delta=boolean(g.at("delta"));p.grid_interactions=boolean(g.at("interactions"));
        p.grid_x=static_cast<int>(integer(g.at("x"),4,"x"));p.grid_y=static_cast<int>(integer(g.at("y"),4,"y"));p.grid_objective=static_cast<int>(integer(g.at("objective"),4,"objective"));p.grid_metric=static_cast<int>(integer(g.at("metric"),4,"metric"));p.grid_selected=static_cast<int>(integer(g.at("selected"),99,"selected"));p.grid_rectangle=static_cast<int>(integer(g.at("rectangle"),80,"rectangle"));p.grid_seed=static_cast<int>(integer(g.at("seed"),99,"seed"));
        for(auto name:{"explicit","minimum","maximum","step","values"})if(!g.at(name).is_array()||g.at(name).size()!=2)invalid(name,"two axis settings required");
        for(std::size_t a=0;a<2;++a){p.grid_explicit[a]=boolean(g.at("explicit")[a]);p.grid_min[a]=bounded(g.at("minimum")[a],4096,"minimum");p.grid_max[a]=bounded(g.at("maximum")[a],4096,"maximum");p.grid_step[a]=bounded(g.at("step")[a],4096,"step");p.grid_values[a]=bounded(g.at("values")[a],4096,"values");}p.grid_name=bounded(g.at("name"),4096,"name");}
    return p;
}
void path_valid(const std::filesystem::path& path)
{
    const auto name=path.generic_u8string();
    if(path.empty() || path.filename().empty() || path.filename()=="." || path.filename()==".." ||
        std::find(name.begin(),name.end(),char8_t{0})!=name.end()) throw std::invalid_argument("Project path requires a valid filename");
    // No device namespaces or alternate streams on Windows.
#ifdef _WIN32
    const auto file=path.filename().wstring();
    auto base=file;base.resize(base.find(L'.')==std::wstring::npos?base.size():base.find(L'.'));
    std::transform(base.begin(),base.end(),base.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});
    const bool device=base==L"CON" || base==L"PRN" || base==L"AUX" || base==L"NUL" ||
        (base.size()==4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) && base[3]>='1' && base[3]<='9');
    if(device || file.find_first_of(L"<>:\"|?*")!=std::wstring::npos || file.back()==L' ' || file.back()==L'.' ||
        name.starts_with(u8"//?/") || name.starts_with(u8"//./"))
        throw std::invalid_argument("Unsupported project path");
#endif
}
warehouse::ResourceCounts counts(const scenario::ScenarioDefinition& d)
{const auto& c=std::get<scenario::WarehouseConfig>(d.domain);return {c.loading_bays,c.workers,c.forklifts};}
std::vector<std::string> strings(const Json& j,std::size_t maximum)
{
    if(!j.is_array() || j.size()>maximum) invalid("report","text count");
    std::vector<std::string> values; for(const auto& v:j) values.push_back(bounded(v,4096,"report text")); return values;
}
}
struct ProjectCodec
{
    struct RunCapture {
        std::uint64_t id;
        scenario::ScenarioDefinition definition;
        warehouse::WarehouseSnapshot snapshot;
        simulation::ClockSnapshot clock;
        viewer::CameraPose camera;
        std::optional<viewer::VisualKey> selection;
        std::vector<warehouse::CapacityChange> commands;
    };
    struct Capture {
        Json metadata;
        std::vector<StudyRecord> studies;
        std::vector<CalibrationStudy> calibrations;
        std::vector<DiagnosticStudy> diagnostics;
        std::vector<RunCapture> runs;
        std::string marker,saved;
    };
    struct RestoredRuns {
        std::unique_ptr<viewer::ViewerController> baseline;
        std::vector<PlayableRun> runs;
    };
    static Capture capture(const ScenarioWorkspace& w,const ProjectInfo& info,const ProjectPreferences& prefs) {
        (void)w.baseline();
        if(w.has_pending_study())throw std::logic_error("Resolve the unretained completed study before saving or recovering the project");
        if(w.has_pending_calibration())throw std::logic_error("Save or explicitly discard the completed calibration study before saving the project");
        if(w.has_pending_diagnostic())throw std::logic_error("Save or explicitly discard the completed diagnostic before saving the project");
        for(const auto* draft:{&w.draft_,&w.draft_origin_})for(const auto& value:draft->values)(void)bounded(Json(value),4096,"draft field");
        for(const auto& c:w.candidates_){(void)bounded(Json(c.notes),4096,"candidate notes");(void)bounded(Json(c.changed_inputs),4096,"candidate changes");}
        Capture result;result.metadata=payload(w,info,prefs);result.studies=w.studies_;result.marker=marker(w,info,prefs);result.saved=timestamp();result.metadata["identity"]["saved"]=result.saved;
        const auto add=[&](const viewer::ViewerController& v,std::uint64_t id){result.runs.push_back({id,v.definition(),v.runtime().capture_snapshot(),v.runtime().clock().capture_snapshot(),v.orbit_camera().pose(),v.selection(),v.command_history()});};
        result.calibrations=w.calibrations_;
        result.diagnostics=w.diagnostics_;
        add(*w.baseline_view_,0);for(const auto& r:w.runs_)add(*r.controller,r.id);return result;
    }
    static Json run(const RunCapture& r) {
        Json j{{"id",r.id},{"definition",definition(r.definition)},{"snapshot",snapshot(r.snapshot)},{"clock",clock_json(r.clock)},{"camera",camera_json(r.camera)},{"selection",selection_json(r.selection)},{"commands",Json::array()}};
        for(const auto& c:r.commands)j["commands"].push_back({c.tick,c.started_before,c.capacity,static_cast<int>(c.resource)});return j;
    }
    static std::string marker(const ScenarioWorkspace& w,const ProjectInfo& info,const ProjectPreferences& prefs)
    {
        Json j{{"generation",w.persistent_generation_},{"active",w.active_run_},{"preferences",preferences_json(prefs)},{"name",info.name},{"runs",Json::array()},{"preview",w.viewed_identity_}};
        const auto add=[&](const viewer::ViewerController& v) {
            const auto& r=v.runtime();const auto m=r.metrics();const auto c=r.resource_capacity();
            j["runs"].push_back({clock_json(r.clock().capture_snapshot()),camera_json(v.orbit_camera().pose()),selection_json(v.selection()),
                Json::array({m.arrived,m.started,m.completed,m.waiting,m.total_wait_ticks,m.occupied_resource_ticks,r.processed_events(),r.pending_events(),c.loading_bays,c.workers,c.forklifts})});
        };
        if(w.baseline_view_) add(*w.baseline_view_);
        for(const auto& r:w.runs_) add(*r.controller);
        return j.dump();
    }
    static std::unique_ptr<viewer::ViewerController> run(const Json& j,ProjectTimings& timings)
    {
        fields(j,{"id","definition","snapshot","clock","camera","selection","commands"},{},"run");
        const auto d=definition(j.at("definition")); const auto state=snapshot(j.at("snapshot")); const auto clock=clock_value(j.at("clock"));
        if(state.scenario_identity!=scenario::serialize_scenario(d)) invalid("run","scenario identity mismatch");
        const auto start=Timer::now();
        auto v=std::make_unique<viewer::ViewerController>(d,clock.time_scale); timings.scene_ms+=ms(start);
        const auto& commands=j.at("commands");
        if(!commands.is_array() || commands.size()>warehouse::max_capacity_changes) invalid("commands","count exceeds limit");
        std::vector<warehouse::CapacityChange> history;
        for(const auto& c:commands) {
            if(!c.is_array() || c.size()!=4) invalid("commands","invalid shape");
            history.push_back({integer(c[0],scenario::max_execution_ticks,"command tick"),integer(c[1],scenario::max_warehouse_shipments,"command reservations"),
                static_cast<std::uint32_t>(integer(c[2],100000,"command capacity")),static_cast<warehouse::ResourceType>(integer(c[3],2,"command resource"))});
        }
        const auto restoring=Timer::now(); v->restore_session(state,clock,camera_value(j.at("camera")),selection_value(j.at("selection")),std::move(history));
        timings.restoration_ms+=ms(restoring); return v;
    }
    static Json report(const Comparison& r)
    {

        if(r.confirmation)return {{"kind","confirmation"},{"confirmation",Json::parse(analysis::serialize_confirmation_report({*r.confirmation,*r.statistics}))}};
        if(r.interaction)return {{"kind","interaction"},{"interaction",Json::parse(analysis::serialize_interaction_report(*r.interaction))}};
        if(r.sensitivity)return {{"kind","sensitivity"},{"sensitivity",Json::parse(analysis::serialize_sensitivity_report(*r.sensitivity))}};
        if(r.statistics) return {{"kind","paired_statistical"},{"statistics",Json::parse(analysis::serialize_statistical_report(*r.statistics))}};
        Json j{{"request_id",r.request_id},{"mode",static_cast<int>(r.mode)},{"objective",static_cast<int>(r.objective)},
            {"baseline",definition(r.baseline_definition)},{"candidate_ids",r.candidate_ids},{"definitions",Json::array()},
            {"changed_inputs",r.changed_inputs},{"executions",Json::array()},{"export",Json::parse(comparison_json(r))},
            {"final_snapshots",Json::array()},{"execution_ms",r.execution_ms},{"report_ms",r.report_ms}};
        if(r.mode==ComparisonMode::Initial) j["export"].erase("executions"); else j["export"]=nullptr;
        for(const auto& d:r.definitions) j["definitions"].push_back(definition(d));
        for(const auto& e:r.executions) j["executions"].push_back(Json::parse(analysis::serialize_report(e)));
        for(const auto& s:r.final_snapshots) j["final_snapshots"].push_back(snapshot(s));
        return j;
    }
    static void report(ScenarioWorkspace& w,const Json& j)
    {
        if(j.is_null()) return;
        if(j.contains("kind")) {
            if(j.at("kind")=="confirmation") {
                fields(j,{"kind","confirmation"},{},"confirmation project report");
                auto c=analysis::detail::confirmation_report_value(j.at("confirmation"));Comparison r;r.confirmation=std::move(c.request);r.statistics=std::move(c.paired);
                r.request_id=r.statistics->request.id;r.baseline_definition=r.statistics->request.baseline;r.objective=r.statistics->request.objective;r.limitations=r.statistics->limitations;
                for(const auto& candidate:r.statistics->request.candidates) {r.candidate_ids.push_back(candidate.id);r.definitions.push_back(candidate.definition);r.changed_inputs.push_back(differences(r.baseline_definition,candidate.definition));}
                w.report_=std::make_shared<const Comparison>(std::move(r));w.execution().reserve_request_ids(w.report_->request_id);return;
            }
            if(j.at("kind")=="interaction") {fields(j,{"kind","interaction"},{},"interaction project report");Comparison r;r.interaction=analysis::detail::interaction_report_value(j.at("interaction"));const auto& input=r.interaction->request;r.request_id=input.id;r.baseline_definition=input.source;r.limitations=r.interaction->limitations;const auto request_id=r.request_id;w.report_=std::make_shared<const Comparison>(std::move(r));++w.report_generation_;w.execution().reserve_request_ids(request_id);return;}
            if(j.at("kind")=="sensitivity") {fields(j,{"kind","sensitivity"},{},"sensitivity project report");Comparison r;r.sensitivity=analysis::detail::sensitivity_report_value(j.at("sensitivity"));const auto& input=r.sensitivity->request;
                r.request_id=input.id;r.baseline_definition=input.source;
                r.limitations=r.sensitivity->limitations;const auto request_id=r.request_id;w.report_=std::make_shared<const Comparison>(std::move(r));++w.report_generation_;w.execution().reserve_request_ids(request_id);return;}
            fields(j,{"kind","statistics"},{},"statistical project report");if(string(j.at("kind"),"report kind")!="paired_statistical") invalid("report","unsupported kind");
            Comparison r;r.statistics=analysis::detail::statistical_report_value(j.at("statistics"));const auto& input=r.statistics->request;
            r.request_id=input.id;r.baseline_definition=input.baseline;r.objective=input.objective;

            for(const auto& c:input.candidates) {if(c.id>=w.next_id_) invalid("report","invalid statistical historical candidate identity");r.candidate_ids.push_back(c.id);r.definitions.push_back(c.definition);r.changed_inputs.push_back(differences(input.baseline,c.definition));}
            r.limitations=r.statistics->limitations;const auto request_id=r.request_id;w.report_=std::make_shared<const Comparison>(std::move(r));++w.report_generation_;w.execution().reserve_request_ids(request_id);return;
        }
        fields(j,{"request_id","mode","objective","baseline","candidate_ids","definitions","changed_inputs","executions","export","final_snapshots","execution_ms","report_ms"},{},"comparison");
        Comparison r; r.request_id=id(j.at("request_id")); r.mode=static_cast<ComparisonMode>(integer(j.at("mode"),1,"comparison mode"));
        r.objective=static_cast<analysis::Objective>(integer(j.at("objective"),2,"objective")); r.baseline_definition=definition(j.at("baseline"));
        r.execution_ms=number(j.at("execution_ms"),0,1e12,"execution time");r.report_ms=number(j.at("report_ms"),0,1e12,"report time");

        const auto& definitions=j.at("definitions"); const auto& ids=j.at("candidate_ids"); const auto& executions=j.at("executions");
        if(!definitions.is_array() || definitions.empty() || definitions.size()>analysis::max_candidates ||
            !ids.is_array() || ids.size()!=definitions.size() || !executions.is_array() ||
            executions.size()!=(r.mode==ComparisonMode::Initial?definitions.size()+1:1)) invalid("report","invalid input counts");
        std::set<std::uint64_t> unique;
        for(std::size_t i=0;i<definitions.size();++i) {
            r.definitions.push_back(definition(definitions[i])); const auto candidate_id=id(ids[i]);
            if(!candidate_id || candidate_id>=w.next_id_ || !unique.insert(candidate_id).second) invalid("report","invalid historical candidate identity");
            if(i && r.definitions[i-1].name>=r.definitions[i].name) invalid("report","invalid canonical order");
            r.candidate_ids.push_back(candidate_id);
        }
        r.changed_inputs=strings(j.at("changed_inputs"),analysis::max_candidates);
        if(r.changed_inputs.size()!=definitions.size()) invalid("report","missing changed inputs");
        for(std::size_t i=0;i<r.definitions.size();++i)
            if(r.changed_inputs[i]!=differences(r.baseline_definition,r.definitions[i])) invalid("report","changed input provenance mismatch");
        for(const auto& e:executions) {auto parsed=analysis::parse_report(e.dump()); if(parsed.request.objective!=r.objective) invalid("report","objective mismatch"); r.executions.push_back(std::move(parsed));}
        r.baseline=r.executions.front().baseline; r.bottlenecks=r.executions.front().bottlenecks;
        if(r.mode==ComparisonMode::Checkpoint) {
            const auto& e=r.executions.front();
            if(e.request.scenario_identity!=scenario::serialize_scenario(r.baseline_definition) || e.candidates.size()!=r.definitions.size()) invalid("report","checkpoint provenance mismatch");
            for(std::size_t i=0;i<r.definitions.size();++i) {
                const auto& d=r.definitions[i]; auto compatible=d;
                auto& c=std::get<scenario::WarehouseConfig>(compatible.domain); const auto& original=std::get<scenario::WarehouseConfig>(r.baseline_definition.domain);
                compatible.name=r.baseline_definition.name; c.loading_bays=original.loading_bays;c.workers=original.workers;c.forklifts=original.forklifts;
                if(scenario::serialize_scenario(compatible)!=scenario::serialize_scenario(r.baseline_definition) ||
                    e.candidates[i].candidate!=analysis::ScenarioCandidate{d.name,counts(d)}) invalid("report","checkpoint candidate mismatch");
            }
            r.candidates=e.candidates;r.ranking=e.ranking;r.warnings=e.warnings;r.limitations=e.limitations;
        } else {
            for(std::size_t i=0;i<r.executions.size();++i) {
                const auto& d=i?r.definitions[i-1]:r.baseline_definition;const auto& e=r.executions[i];
                if(e.request.scenario_identity!=scenario::serialize_scenario(d) || e.request.checkpoint.current_tick!=0 ||
                    d.simulation.tick_rate!=r.baseline_definition.simulation.tick_rate || d.simulation.duration_seconds!=r.baseline_definition.simulation.duration_seconds)
                    invalid("report","independent execution provenance mismatch");
                if(i) {analysis::CandidateResult c; c.candidate={d.name,counts(d)};c.metrics=e.baseline.metrics;
                    c.delta=analysis::compare_metrics(r.baseline.metrics,c.metrics,counts(r.baseline_definition),counts(d));r.candidates.push_back(std::move(c));}
            }
            analysis::rank_candidates(r.candidates,r.objective);
            auto ordered=r.candidates; std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.rank.value_or(1000)<b.rank.value_or(1000);});
            for(const auto& c:ordered) if(c.rank) r.ranking.push_back(c.candidate.name);
            r.warnings=strings(j.at("export").at("warnings"),100); r.limitations=strings(j.at("export").at("limitations"),100);
        }
        const auto& finals=j.at("final_snapshots");
        if(!finals.is_array() || (!finals.empty() && finals.size()!=r.definitions.size()+1)) invalid("report","invalid preview count");
        for(std::size_t i=0;i<finals.size();++i) {
            auto state=snapshot(finals[i]); const auto& d=i?r.definitions[i-1]:r.baseline_definition;
            auto preview=warehouse::WarehouseSimulation::create_from(state);
            if(preview.metrics()!=(i?r.candidates[i-1].metrics:r.baseline.metrics)) invalid("report","preview metrics mismatch");
            if(r.mode==ComparisonMode::Initial && state.scenario_identity!=scenario::serialize_scenario(d)) invalid("report","preview scenario mismatch");
            r.final_snapshots.push_back(std::move(state));
        }
        w.report_=std::make_shared<const Comparison>(std::move(r));
        ++w.report_generation_;
        w.execution().reserve_request_ids(w.report_->request_id);
        auto summary=Json::parse(comparison_json(*w.report_));
        if(w.report_->mode==ComparisonMode::Initial) summary.erase("executions"); else summary=nullptr;
        if(summary!=j.at("export")) invalid("report","saved summary mismatch");
    }
    static Json payload(const ScenarioWorkspace& w,const ProjectInfo& info,const ProjectPreferences& prefs)
    {
        Json j{{"identity",{{"id",info.id},{"name",info.name},{"created",info.created},{"saved",info.saved}}},
            {"baseline",Json(nullptr)},{"draft",w.draft_.values},{"draft_origin",w.draft_origin_.values},
            {"candidates",Json::array()},{"runs",Json::array()},{"active_run",w.active_run_},{"next_run_id",w.next_run_id_},
            {"next_candidate_id",w.next_id_},{"default_speed",w.speed_},{"preferences",preferences_json(prefs)},{"report",nullptr},{"study_history",Json::array()},{"selected_study",w.selected_study_},{"next_study_id",w.next_study_id_},{"preview",nullptr}};
        if(w.history_bytes()>max_history_bytes) invalid("history","exceeds 192 MiB");
        if(!w.calibrations_.empty()){j["demand_calibrations"]=Json::array();j["selected_calibration"]=w.calibration_report_?w.calibration_report_->id:std::string{};}
        if(!w.diagnostics_.empty()){j["demand_diagnostics"]=Json::array();j["selected_diagnostic"]=w.diagnostic_report_?w.diagnostic_report_->id:std::string{};}
        if(w.draft_.workload!=WorkloadDraft{}) j["draft_workload"]=workload_draft_json(w.draft_.workload);
        if(w.draft_origin_.workload!=WorkloadDraft{}) j["draft_origin_workload"]=workload_draft_json(w.draft_origin_.workload);
        for(const auto& c:w.candidates_) j["candidates"].push_back({{"id",c.id},{"definition",definition(c.definition)},{"notes",c.notes},{"changed_inputs",c.changed_inputs}});
        if(w.preview_view_ && w.report_ && !w.report_->statistics && !w.report_->sensitivity && !w.report_->interaction) {
            const auto separator=w.viewed_identity_.find(": "); const auto name=w.viewed_identity_.substr(separator+2);
            j["preview"]=w.preview_report_generation_==w.report_generation_ ? Json(name) : Json{{"unavailable",true}};
        }
        return j;
    }
    static StudyRecord study(ScenarioWorkspace& w,const Json& entry) {
                fields(entry,{"id","type","title","notes","completed","bytes","engine","generator_version","evidence","view","reserved_confirmation_seeds"},{},"study");
                StudyRecord study;study.id=bounded(entry.at("id"),64,"study ID");
                if(!study.id.starts_with("study-")) invalid("study","invalid or duplicate ID");
                const auto suffix=study.id.substr(6);std::uint64_t numeric=0;const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),numeric);
                if(parsed.ec!=std::errc{} || parsed.ptr!=suffix.data()+suffix.size() || !numeric || study.id!="study-"+std::to_string(numeric)) invalid("study","noncanonical ID");
                study.title=bounded(entry.at("title"),256,"study title");if(study.title.empty())invalid("study","empty title");
                study.notes=bounded(entry.at("notes"),4096,"study notes");study.completed=bounded(entry.at("completed"),32,"completed");
                if(bounded(entry.at("engine"),64,"study engine")!=project_engine_version || integer(entry.at("generator_version"),1,"study generator")!=scenario::workload_generator_version) invalid("study","unsupported provenance");
                const auto& reserved=entry.at("reserved_confirmation_seeds");if(!reserved.is_array() || reserved.size()>32)invalid("reservations","too many confirmation attempts");
                std::set<std::uint64_t> used_confirmation_seeds;
                for(const auto& seed_set:reserved) {
                    if(!seed_set.is_array() || seed_set.empty() || seed_set.size()>analysis::max_replications)invalid("reservations","invalid seed set");
                    std::vector<std::uint64_t> seeds;for(const auto& value:seed_set) {auto seed=id(value);if(!used_confirmation_seeds.insert(seed).second)invalid("reservations","reused confirmation seed");seeds.push_back(seed);}study.reserved_confirmation_seeds.push_back(std::move(seeds));
                }
                const auto evidence=entry.at("evidence").dump();study.serialized_bytes=integer(entry.at("bytes"),max_study_bytes,"study bytes");
                if(study.serialized_bytes!=evidence.size()) invalid("study","evidence size mismatch");
                report(w,entry.at("evidence"));study.evidence=w.report_;if(!study.evidence) invalid("study","missing evidence");
                study.type=study_type(*study.evidence);if(entry.at("type")!=study_type_name(study.type))invalid("study","type mismatch");
                const auto& view=entry.at("view");fields(view,{"configuration","seed","metric","rectangle"},{},"study view");
                study.view.configuration=id(view.at("configuration"));study.view.seed=id(view.at("seed"));
                study.view.metric=static_cast<int>(integer(view.at("metric"),4,"metric"));const auto& rectangle=view.at("rectangle");if(!rectangle.is_array() || rectangle.size()!=4)invalid("view","expected four rectangle endpoints");for(std::size_t i=0;i<4;++i)study.view.rectangle[i]=static_cast<std::uint32_t>(integer(rectangle[i],100000,"rectangle endpoint"));
        return study;
    }
    static void decoder_ids(ScenarioWorkspace& w) {w.next_id_=UINT64_MAX;}
    static ScenarioWorkspace payload(const Json& j,ProjectInfo& info,ProjectPreferences& prefs,ProjectTimings& timings,std::optional<std::vector<StudyRecord>> decoded={},std::unique_ptr<RestoredRuns> restored={},std::vector<CalibrationStudy> calibrations={},std::vector<DiagnosticStudy> diagnostics={})
    {
        fields(j,{"identity","baseline","draft","draft_origin","candidates","runs","active_run","next_run_id","next_candidate_id","default_speed","preferences","report","preview"},{"draft_workload","draft_origin_workload","study_history","selected_study","next_study_id","demand_calibrations","selected_calibration","demand_diagnostics","selected_diagnostic"},"payload");
        const auto& identity=j.at("identity"); fields(identity,{"id","name","created","saved"},{},"project identity");
        info.id=bounded(identity.at("id"),32,"project ID");
        if(info.id.size()!=32 || info.id.find_first_not_of("0123456789abcdef")!=std::string::npos) invalid("project ID","invalid identity");
        info.name=bounded(identity.at("name"),256,"project name"); if(info.name.empty()) invalid("project name","empty");
        info.created=bounded(identity.at("created"),32,"created"); info.saved=bounded(identity.at("saved"),32,"saved");
        ScenarioWorkspace w;
        if(id(j.at("baseline").at("id"))!=0) invalid("baseline","identity must be zero");
        w.baseline_view_=restored && restored->baseline?std::move(restored->baseline):run(j.at("baseline"),timings); w.baseline_=w.baseline_view_->definition(); w.speed_=number(j.at("default_speed"),0.01,3600,"default playback speed");
        const auto draft=[](const Json& values) {
            if(!values.is_array() || values.size()!=9) invalid("draft","invalid field count"); Draft d;
            for(std::size_t i=0;i<9;++i) d.values[i]=bounded(values[i],4096,"draft field"); return d;
        };
        w.draft_=draft(j.at("draft")); w.draft_origin_=draft(j.at("draft_origin"));
        if(j.contains("draft_workload")) w.draft_.workload=workload_draft_value(j.at("draft_workload"));
        if(j.contains("draft_origin_workload")) w.draft_origin_.workload=workload_draft_value(j.at("draft_origin_workload"));
        w.next_id_=id(j.at("next_candidate_id")); w.next_run_id_=id(j.at("next_run_id"));
        if(!w.next_id_ || !w.next_run_id_) invalid("identity","invalid next identity");
        const auto& candidates=j.at("candidates"); const auto& runs=j.at("runs");
        if(!candidates.is_array() || candidates.size()>analysis::max_candidates || !runs.is_array() || runs.size()>max_custom_runs) invalid("project","candidate or run count exceeds limit");
        std::set<std::uint64_t> ids;
        for(const auto& c:candidates) {
            fields(c,{"id","definition","notes","changed_inputs"},{},"candidate");
            const auto candidate_id=id(c.at("id")); auto d=definition(c.at("definition")); w.check_name(d.name);
            if(!candidate_id || candidate_id>=w.next_id_ || !ids.insert(candidate_id).second) invalid("candidate","duplicate or invalid identity");
            auto changed=bounded(c.at("changed_inputs"),4096,"changed inputs");
            if(changed!=differences(w.baseline(),d)) invalid("candidate","changed inputs mismatch");
            w.candidates_.push_back({candidate_id,std::move(d),bounded(c.at("notes"),4096,"notes"),std::move(changed)});
        }
        ids.clear();
        std::size_t run_index=0;
        for(const auto& r:runs) {
            const auto run_id=id(r.at("id"));
            if(!run_id || run_id>=w.next_run_id_ || !ids.insert(run_id).second) invalid("run","duplicate or invalid identity");
            w.runs_.push_back({run_id,restored?std::move(restored->runs.at(run_index++).controller):run(r,timings)});
        }
        w.active_run_=id(j.at("active_run"));
        if(w.active_run_ && !ids.contains(w.active_run_)) invalid("active run","missing run");
        prefs=preferences_value(j.at("preferences"));
        if(prefs.selected_candidate && std::none_of(w.candidates_.begin(),w.candidates_.end(),[&](const auto& c){return c.id==*prefs.selected_candidate;})) prefs.selected_candidate.reset();
        if(j.contains("study_history")) {
            if(!j.at("report").is_null()) invalid("history","v2 must use a single evidence collection");
            const auto& history=j.at("study_history");if(!history.is_array() || history.size()>max_studies) invalid("history","too many studies");
            w.next_study_id_=id(j.at("next_study_id"));if(!w.next_study_id_ || w.next_study_id_==UINT64_MAX) invalid("history","invalid next ID");
            std::set<std::string> study_ids;
            for(const auto& entry:history) {
                w.studies_.push_back(study(w,entry));
            }
            if(decoded)w.studies_=std::move(*decoded);
            for(const auto& study:w.studies_) {
                if(!study_ids.insert(study.id).second)invalid("study","duplicate ID");
                const auto suffix=study.id.substr(6);std::uint64_t numeric=0;std::from_chars(suffix.data(),suffix.data()+suffix.size(),numeric);
                if(numeric>=w.next_study_id_)invalid("study","invalid next study ID");
                if(study.type!=StudyType::Confirmation)for(auto candidate:study.evidence->candidate_ids)if(candidate>=w.next_id_)invalid("report","invalid historical candidate identity");
                w.execution().reserve_request_ids(study.evidence->request_id);
                if(w.history_bytes()>max_history_bytes)invalid("history","exceeds 192 MiB");
            }
            for(const auto& study:w.studies_) if(!study.reserved_confirmation_seeds.empty()) {
                if(study.type==StudyType::Confirmation)invalid("reservations","confirmation cannot be an exploratory source");
                const auto& r=*study.evidence;
                auto exploration=r.interaction?analysis::replication_seeds(r.interaction->request.plan):r.sensitivity?analysis::replication_seeds(r.sensitivity->request.plan):r.statistics?analysis::replication_seeds(r.statistics->request.plan):std::vector<std::uint64_t>{r.baseline_definition.simulation.random_seed};
                if(!r.interaction && !r.sensitivity && !r.statistics)for(const auto& d:r.definitions)if(std::find(exploration.begin(),exploration.end(),d.simulation.random_seed)==exploration.end())exploration.push_back(d.simulation.random_seed);
                for(const auto& seeds:study.reserved_confirmation_seeds)for(auto seed:seeds)if(std::find(exploration.begin(),exploration.end(),seed)!=exploration.end())invalid("reservations","exploratory seed overlap");
            }
            std::set<std::pair<std::string,std::uint32_t>> completed_attempts;
            for(const auto& study:w.studies_) if(study.evidence->confirmation) {
                const auto& c=*study.evidence->confirmation;
                if(!completed_attempts.insert({c.source_study_id,c.confirmation_number}).second)invalid("lineage","duplicate completed confirmation attempt");
                auto source=std::find_if(w.studies_.begin(),w.studies_.end(),[&](const auto& x){return x.id==c.source_study_id;});
                if(source==w.studies_.end() || source->type==StudyType::Confirmation) invalid("lineage","missing exploratory source");
                // Freeze again from preserved source inputs, without simulation, then compare all immutable lineage.
                auto previous=w.report_;auto previous_id=w.selected_study_;w.report_=source->evidence;w.selected_study_=source->id;
                ScenarioWorkspace source_only;source_only.studies_={*source};source_only.studies_[0].reserved_confirmation_seeds.clear();source_only.report_=source->evidence;source_only.selected_study_=source->id;
                auto expected=source_only.freeze_confirmation(c.selection_id,c.paired.plan);
                expected.source_title=c.source_title;expected.source_completed=c.source_completed;expected.additional_analysis=c.additional_analysis;expected.confirmation_number=c.confirmation_number;expected.paired.id=c.paired.id;
                if(c.confirmation_number>source->reserved_confirmation_seeds.size() || source->reserved_confirmation_seeds[c.confirmation_number-1]!=analysis::replication_seeds(c.paired.plan))invalid("lineage","missing reserved confirmation seeds");
                if(analysis::serialize_confirmation_request(expected)!=analysis::serialize_confirmation_request(c))invalid("lineage","frozen configuration or exploratory evidence mismatch");
                w.report_=std::move(previous);w.selected_study_=std::move(previous_id);
            }
            w.report_.reset();w.selected_study_.clear();const auto selected=bounded(j.at("selected_study"),64,"selected study");
            if(!selected.empty())w.select_study(selected);else if(!w.studies_.empty())w.select_study(w.studies_.back().id);
        } else {
            report(w,j.at("report"));
            if(w.report_) {
                const auto bytes=report(*w.report_).dump().size();
                w.studies_.push_back({"study-1","Legacy retained study",{},info.saved,study_type(*w.report_),w.report_,bytes,{}});w.selected_study_="study-1";w.next_study_id_=2;
            }
        }
        w.calibrations_=std::move(calibrations);
        w.diagnostics_=std::move(diagnostics);
        if(w.studies_.size()+w.calibrations_.size()+w.diagnostics_.size()>max_studies || w.history_bytes()>max_history_bytes)invalid("history","combined calibration/analysis retention limit exceeded");
        if(!w.calibrations_.empty())w.calibration_report_=w.calibrations_.back().evidence;
        if(j.contains("selected_calibration")){if(w.calibrations_.empty())invalid("selected calibration","missing retained calibration evidence");w.select_calibration(bounded(j.at("selected_calibration"),76,"selected calibration"));}
        if(!w.diagnostics_.empty())w.diagnostic_report_=w.diagnostics_.back().evidence;
        if(j.contains("selected_diagnostic")){if(w.diagnostics_.empty())invalid("selected diagnostic","missing retained diagnostic evidence");w.select_diagnostic(bounded(j.at("selected_diagnostic"),75,"selected diagnostic"));}
        if(!w.report_ || (prefs.result_name!="Baseline" &&
            (w.report_->statistics ? std::none_of(w.report_->statistics->candidates.begin(),w.report_->statistics->candidates.end(),[&](const auto& c){return c.name==prefs.result_name;}) :
            std::none_of(w.report_->candidates.begin(),w.report_->candidates.end(),[&](const auto& c){return c.candidate.name==prefs.result_name;}))))
            prefs.result_name.clear();
        w.select_run(w.active_run_);
        w.baseline_view_->select(selection_value(j.at("baseline").at("selection")));
        for(std::size_t i=0;i<w.runs_.size();++i) w.runs_[i].controller->select(selection_value(runs[i].at("selection")));
        (void)w.validate_draft();
        w.message_="Project restored. Reports retain their original submitted inputs.";
        if(j.at("preview").is_object()) {
            fields(j.at("preview"),{"unavailable"},{},"preview");
            if(!boolean(j.at("preview").at("unavailable"))) invalid("preview","invalid cache state");
            w.message_="Project restored. The previous preview belonged to a superseded report and was cleared.";
        } else if(!j.at("preview").is_null()) {
            const auto name=bounded(j.at("preview"),256,"preview");
            if(!w.report_) invalid("preview","missing report");
            if(name!="Baseline" && std::none_of(w.report_->candidates.begin(),w.report_->candidates.end(),[&](const auto& c){return c.candidate.name==name;})) invalid("preview","unknown result");
            if(!w.report_->final_snapshots.empty()) w.view_result(name=="Baseline"?std::nullopt:std::optional{name});
            else w.message_="Project restored. Preview cache unavailable; select a report result to rebuild its preview.";
        }
        return w;
    }
};
std::string comparison_storage_json(const Comparison& r) {
    if(r.confirmation)return "{\"confirmation\":"+analysis::serialize_confirmation_report({*r.confirmation,*r.statistics})+",\"kind\":\"confirmation\"}";
    if(r.interaction)return "{\"interaction\":"+analysis::serialize_interaction_report(*r.interaction)+",\"kind\":\"interaction\"}";
    if(r.sensitivity)return "{\"kind\":\"sensitivity\",\"sensitivity\":"+analysis::serialize_sensitivity_report(*r.sensitivity)+"}";
    if(r.statistics)return "{\"kind\":\"paired_statistical\",\"statistics\":"+analysis::serialize_statistical_report(*r.statistics)+"}";
    return ProjectCodec::report(r).dump();
}
std::shared_ptr<const Comparison> parse_comparison_storage_json(std::string_view bytes,std::uint64_t next_candidate) {
    ScenarioWorkspace w;w.next_id_=next_candidate;ProjectCodec::report(w,parse_json(bytes,max_study_bytes));return w.report();
}
#include "project_io.inl"
Project::Project(ScenarioWorkspace& workspace):workspace_(workspace)
{
    info_={identity(),"Untitled project",timestamp(),{},{}};
}
void Project::preferences(ProjectPreferences value)
{
    (void)preferences_value(preferences_json(value));
    if(value!=preferences_) preferences_=std::move(value);
}
void Project::rename(std::string name)
{if(name.empty() || name.size()>256 || name.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid project name"); info_.name=std::move(name);}
std::string Project::marker() const {return ProjectCodec::marker(workspace_,info_,preferences_);}

bool Project::dirty() const {return marker()!=saved_marker_;}
#include "project_methods.inl"
void Project::open(const std::filesystem::path& path) {load(path,false);}
void Project::recover(const std::filesystem::path& path) {load(path,true);}
void Project::create(const scenario::ScenarioDefinition& baseline,std::string name)
{
    require_idle();
    if(name.empty() || name.size()>256) throw std::invalid_argument("Invalid project name");
    ScenarioWorkspace replacement(baseline); replacement.view().reset();
    ProjectInfo info{identity(),std::move(name),timestamp(),{},{}};
    workspace_.execution().cancel();workspace_.execution().shutdown();workspace_=std::move(replacement);
    info_=std::move(info);preferences_={};saved_marker_.clear();recovery_marker_.clear();last_recovery_=Timer::now();
}
bool Project::recover_if_due(const std::filesystem::path& recovery,Timer::time_point now)
{
    if(now-last_recovery_<std::chrono::seconds(60)) return false;
    last_recovery_=now;
    if(!dirty()) return false;
    auto current=marker(); if(current==recovery_marker_) return false;
    path_valid(recovery);
    if(recovery.extension()!=".recovery") throw std::invalid_argument("Recovery requires a separate .recovery file");
    if(!info_.path.empty() && std::filesystem::absolute(recovery).lexically_normal()==info_.path) throw std::invalid_argument("Recovery cannot overwrite primary project");
    require_idle();timings_={};const auto began=Timer::now();const auto capture=ProjectCodec::capture(workspace_,info_,preferences_);timings_.snapshot_ms=ms(began);write_project(recovery,capture,true,timings_,{});recovery_marker_=std::move(current);timings_.total_ms=ms(began);return true;
}
bool Project::recovery_available(const std::filesystem::path& path)
{std::error_code error;return std::filesystem::is_regular_file(path,error);}
void Project::discard_recovery(const std::filesystem::path& path)
{
    path_valid(path);
    if(path.extension()!=".recovery") throw std::invalid_argument("Only the recovery slot can be discarded");
    std::error_code error;std::filesystem::remove(path,error);if(error) throw std::runtime_error("Cannot discard recovery");
}
void Project::discard_own_recovery(const std::filesystem::path& path) const
{
    require_idle();
    if(!recovery_available(path)) return;
    ProjectTimings ignored;const auto loaded=read_project(path,true,ignored,{});
    if(loaded.info.id==info_.id) discard_recovery(path);
}
}
