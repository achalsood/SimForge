#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include "../scenario/json_io.hpp"
#include <algorithm>
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
    return {{"arrivals",static_cast<unsigned>(d.arrivals)},{"processing",static_cast<unsigned>(d.processing)},{"bounds",d.bounds},{"segments",d.segments}};
}
WorkloadDraft workload_draft_value(const Json& j)
{
    fields(j,{"arrivals","processing","bounds","segments"},{},"draft workload");WorkloadDraft d;
    d.arrivals=static_cast<scenario::ArrivalMode>(integer(j.at("arrivals"),2,"draft arrival mode"));
    d.processing=static_cast<scenario::ProcessingMode>(integer(j.at("processing"),1,"draft processing mode"));
    const auto& b=j.at("bounds");const auto& segments=j.at("segments");
    if(!b.is_array() || b.size()!=4 || !segments.is_array() || segments.size()>scenario::max_demand_segments) invalid("draft workload","invalid array bounds");
    for(std::size_t i=0;i<4;++i) d.bounds[i]=bounded(b[i],4096,"workload bound");
    for(const auto& s:segments) {
        if(!s.is_array() || s.size()!=3) invalid("draft workload","invalid segment");
        d.segments.push_back({bounded(s[0],4096,"start seconds"),bounded(s[1],4096,"end seconds"),bounded(s[2],4096,"interval seconds")});
    }
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
double number(const Json& j,double minimum,double maximum,const char* label)
{
    if(!j.is_number()) invalid(label,"expected numeric value");
    const auto value=j.get<double>();if(!std::isfinite(value) || value<minimum || value>maximum) invalid(label,"invalid numeric range");
    return value;
}
std::uint64_t id(const Json& j) { return integer(j,std::numeric_limits<std::uint64_t>::max()-1,"identity"); }
std::uint32_t checksum(std::string_view text)
{
    std::uint32_t crc=0xffffffff;
    for(unsigned char c:text) {crc^=c; for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320:0);}
    return ~crc;
}
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
    return {{"mode",p.application_mode},{"metrics",p.metrics},{"inspector",p.inspector},{"focus",p.focus},{"workspace",p.workspace},
        {"metrics_explicit",p.metrics_explicit},{"comparison_mode",static_cast<int>(p.comparison_mode)},{"objective",static_cast<int>(p.objective)},
        {"current_checkpoint",p.current_checkpoint},{"checkpoint_seconds",p.checkpoint_seconds},{"selected_candidate",p.selected_candidate?Json(*p.selected_candidate):Json(nullptr)},
        {"notes",p.notes},{"result_name",p.result_name},{"editor_tab",p.editor_tab}};
}
ProjectPreferences preferences_value(const Json& j)
{
    fields(j,{"mode","metrics","inspector","focus","workspace","metrics_explicit","comparison_mode","objective","current_checkpoint","checkpoint_seconds","selected_candidate","notes","result_name","editor_tab"},{},"preferences");
    ProjectPreferences p;
    p.application_mode=static_cast<int>(integer(j.at("mode"),2,"mode")); p.metrics=boolean(j.at("metrics")); p.inspector=boolean(j.at("inspector"));
    p.focus=boolean(j.at("focus")); p.workspace=boolean(j.at("workspace")); p.metrics_explicit=boolean(j.at("metrics_explicit"));
    p.comparison_mode=static_cast<ComparisonMode>(integer(j.at("comparison_mode"),1,"comparison mode"));
    p.objective=static_cast<analysis::Objective>(integer(j.at("objective"),2,"objective")); p.current_checkpoint=boolean(j.at("current_checkpoint"));
    p.checkpoint_seconds=bounded(j.at("checkpoint_seconds"),4096,"checkpoint text");
    if(!j.at("selected_candidate").is_null()) p.selected_candidate=id(j.at("selected_candidate"));
    p.notes=bounded(j.at("notes"),4096,"notes"); p.result_name=bounded(j.at("result_name"),256,"result name");
    p.editor_tab=static_cast<int>(integer(j.at("editor_tab"),2,"editor tab")); return p;
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
void transactional_write(const std::filesystem::path& destination,std::string_view bytes,bool replace,
    ProjectTimings& timings,const ProjectWriteHook& hook)
{
    path_valid(destination);
    auto temporary=destination; temporary+=L".tmp-"+std::filesystem::path(identity()).wstring();
    const auto began=Timer::now();
    bool owned=false;
    try {
        if(hook) hook("before_write");
#ifdef _WIN32
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create project temporary file");
        owned=true; DWORD written=0;
        bool success=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size();
        if(success) success=FlushFileBuffers(file)!=0;
        if(!CloseHandle(file)) success=false;
        if(!success) throw std::runtime_error("Project write or flush failed");
#else
        std::ofstream file(temporary,std::ios::binary|std::ios::noreplace);
        if(!file) throw std::runtime_error("Cannot create project temporary file");
        owned=true; file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); file.close();
        if(!file) throw std::runtime_error("Project write failed");
        const int descriptor=::open(temporary.c_str(),O_RDONLY);
        if(descriptor<0) throw std::runtime_error("Project flush open failed");
        const auto flushed=::fsync(descriptor); ::close(descriptor);
        if(flushed) throw std::runtime_error("Project flush failed");
#endif
        if(hook) hook("after_write");
        timings.write_ms=ms(began); const auto replacing=Timer::now();
        if(hook) hook("before_replace");
#ifdef _WIN32
        // Same-volume rename; replacement only for an associated explicit Save or recovery slot.
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))
            throw std::runtime_error("Project replacement failed; previous save retained");
#else
        if(replace) std::filesystem::rename(temporary,destination);
        else { std::filesystem::create_hard_link(temporary,destination); std::filesystem::remove(temporary); }
#endif
        owned=false; timings.replacement_ms=ms(replacing);
    } catch(...) {if(owned) {std::error_code ignored; std::filesystem::remove(temporary,ignored);} throw;}
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
    static Json run(const viewer::ViewerController& v,std::uint64_t identity)
    {
        Json result{{"id",identity},{"definition",definition(v.definition())},{"snapshot",snapshot(v.runtime().capture_snapshot())},
            {"clock",clock_json(v.runtime().clock().capture_snapshot())},{"camera",camera_json(v.orbit_camera().pose())},{"selection",selection_json(v.selection())},{"commands",Json::array()}};
        for(const auto& c:v.command_history()) result["commands"].push_back({c.tick,c.started_before,c.capacity,static_cast<int>(c.resource)});
        return result;
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
    static Json report(const ScenarioWorkspace& w)
    {
        if(!w.report_) return nullptr;
        const auto& r=*w.report_;
        Json j{{"request_id",r.request_id},{"mode",static_cast<int>(r.mode)},{"objective",static_cast<int>(r.objective)},
            {"baseline",definition(r.baseline_definition)},{"candidate_ids",r.candidate_ids},{"definitions",Json::array()},
            {"changed_inputs",r.changed_inputs},{"executions",Json::array()},{"export",Json::parse(w.json())},
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
        fields(j,{"request_id","mode","objective","baseline","candidate_ids","definitions","changed_inputs","executions","export","final_snapshots","execution_ms","report_ms"},{},"comparison");
        Comparison r; r.request_id=id(j.at("request_id")); r.mode=static_cast<ComparisonMode>(integer(j.at("mode"),1,"comparison mode"));
        r.objective=static_cast<analysis::Objective>(integer(j.at("objective"),2,"objective")); r.baseline_definition=definition(j.at("baseline"));
        r.execution_ms=number(j.at("execution_ms"),0,1e12,"execution time");r.report_ms=number(j.at("report_ms"),0,1e12,"report time");
        if(scenario::serialize_scenario(r.baseline_definition)!=scenario::serialize_scenario(w.baseline())) invalid("report","baseline provenance mismatch");
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
            viewer::ViewerController preview(d,state);
            if(preview.observation().metrics!=(i?r.candidates[i-1].metrics:r.baseline.metrics)) invalid("report","preview metrics mismatch");
            if(r.mode==ComparisonMode::Initial && state.scenario_identity!=scenario::serialize_scenario(d)) invalid("report","preview scenario mismatch");
            r.final_snapshots.push_back(std::move(state));
        }
        w.report_=std::move(r);
        ++w.report_generation_;
        w.execution().reserve_request_ids(w.report_->request_id);
        auto summary=Json::parse(w.json());
        if(w.report_->mode==ComparisonMode::Initial) summary.erase("executions"); else summary=nullptr;
        if(summary!=j.at("export")) invalid("report","saved summary mismatch");
    }
    static Json payload(const ScenarioWorkspace& w,const ProjectInfo& info,const ProjectPreferences& prefs)
    {
        Json j{{"identity",{{"id",info.id},{"name",info.name},{"created",info.created},{"saved",info.saved}}},
            {"baseline",run(*w.baseline_view_,0)},{"draft",w.draft_.values},{"draft_origin",w.draft_origin_.values},
            {"candidates",Json::array()},{"runs",Json::array()},{"active_run",w.active_run_},{"next_run_id",w.next_run_id_},
            {"next_candidate_id",w.next_id_},{"default_speed",w.speed_},{"preferences",preferences_json(prefs)},{"report",report(w)},{"preview",nullptr}};
        if(w.draft_.workload!=WorkloadDraft{}) j["draft_workload"]=workload_draft_json(w.draft_.workload);
        if(w.draft_origin_.workload!=WorkloadDraft{}) j["draft_origin_workload"]=workload_draft_json(w.draft_origin_.workload);
        for(const auto& c:w.candidates_) j["candidates"].push_back({{"id",c.id},{"definition",definition(c.definition)},{"notes",c.notes},{"changed_inputs",c.changed_inputs}});
        for(const auto& r:w.runs_) j["runs"].push_back(run(*r.controller,r.id));
        if(w.preview_view_ && w.report_) {
            const auto separator=w.viewed_identity_.find(": "); const auto name=w.viewed_identity_.substr(separator+2);
            j["preview"]=w.preview_report_generation_==w.report_generation_ ? Json(name) : Json{{"unavailable",true}};
        }
        return j;
    }
    static ScenarioWorkspace payload(const Json& j,ProjectInfo& info,ProjectPreferences& prefs,ProjectTimings& timings)
    {
        fields(j,{"identity","baseline","draft","draft_origin","candidates","runs","active_run","next_run_id","next_candidate_id","default_speed","preferences","report","preview"},{"draft_workload","draft_origin_workload"},"payload");
        const auto& identity=j.at("identity"); fields(identity,{"id","name","created","saved"},{},"project identity");
        info.id=bounded(identity.at("id"),32,"project ID");
        if(info.id.size()!=32 || info.id.find_first_not_of("0123456789abcdef")!=std::string::npos) invalid("project ID","invalid identity");
        info.name=bounded(identity.at("name"),256,"project name"); if(info.name.empty()) invalid("project name","empty");
        info.created=bounded(identity.at("created"),32,"created"); info.saved=bounded(identity.at("saved"),32,"saved");
        ScenarioWorkspace w;
        if(id(j.at("baseline").at("id"))!=0) invalid("baseline","identity must be zero");
        w.baseline_view_=run(j.at("baseline"),timings); w.baseline_=w.baseline_view_->definition(); w.speed_=number(j.at("default_speed"),0.01,3600,"default playback speed");
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
        for(const auto& r:runs) {
            const auto run_id=id(r.at("id"));
            if(!run_id || run_id>=w.next_run_id_ || !ids.insert(run_id).second) invalid("run","duplicate or invalid identity");
            w.runs_.push_back({run_id,run(r,timings)});
        }
        w.active_run_=id(j.at("active_run"));
        if(w.active_run_ && !ids.contains(w.active_run_)) invalid("active run","missing run");
        prefs=preferences_value(j.at("preferences"));
        if(prefs.selected_candidate && std::none_of(w.candidates_.begin(),w.candidates_.end(),[&](const auto& c){return c.id==*prefs.selected_candidate;})) prefs.selected_candidate.reset();
        report(w,j.at("report"));
        if(!w.report_ || (prefs.result_name!="Baseline" &&
            std::none_of(w.report_->candidates.begin(),w.report_->candidates.end(),[&](const auto& c){return c.candidate.name==prefs.result_name;})))
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
std::string Project::serialize() const
{
    const auto began=Timer::now();
    (void)workspace_.baseline();
    for(const auto* draft:{&workspace_.draft_,&workspace_.draft_origin_})
        for(const auto& value:draft->values) (void)bounded(Json(value),4096,"draft field");
    for(const auto& c:workspace_.candidates_) { (void)bounded(Json(c.notes),4096,"candidate notes");(void)bounded(Json(c.changed_inputs),4096,"candidate changes"); }
    auto payload=ProjectCodec::payload(workspace_,info_,preferences_); payload["identity"]["saved"]=timestamp();
    const auto text=payload.dump();
    Json envelope{{"format","SimForge"},{"version",project_format_version},{"engine",project_engine_version},{"scenario_schema",1},
        {"features",Json::array({"warehouse_snapshots","clock_phase","historical_reports","independent_runs"})},
        {"payload_size",text.size()},{"crc32",checksum(text)},{"payload",std::move(payload)}};
    if(has_workload(envelope.at("payload"))) envelope["features"].push_back("workload_v1");
    auto bytes=envelope.dump();
    if(bytes.size()>project_max_bytes) throw std::length_error("Project exceeds 256 MiB limit");
    timings_.serialization_ms=ms(began); timings_.bytes=bytes.size(); return bytes;
}
void Project::save(const std::filesystem::path& path,bool save_as,const ProjectWriteHook& hook)
{
    auto destination=path.empty()?info_.path:path;
    if(destination.empty()) throw std::invalid_argument("Choose a project filename with Save As");
    path_valid(destination);
    if(destination.extension()!=".simforge") throw std::invalid_argument("Project filename must end in .simforge");
    destination=std::filesystem::absolute(destination).lexically_normal();
    const bool associated=!info_.path.empty() && destination==info_.path;
    if(!save_as && !info_.path.empty() && !associated) throw std::invalid_argument("Use Save As to change project location");
    const auto total=Timer::now(); const auto bytes=serialize();auto saved_marker=marker();auto saved_time=timestamp();
    transactional_write(destination,bytes,associated&&!save_as,timings_,hook);
    info_.path=std::move(destination);info_.saved=std::move(saved_time);saved_marker_=std::move(saved_marker);timings_.total_ms=ms(total);
}
void Project::load(const std::filesystem::path& path,bool recovery)
{
    path_valid(path); const auto total=Timer::now(); timings_={};
    const auto bytes=load_text(path,project_max_bytes); timings_.bytes=bytes.size();
    const auto parsing=Timer::now(); const auto j=parse_json(bytes,project_max_bytes); timings_.parsing_ms=ms(parsing);
    const auto validation=Timer::now();
    fields(j,{"format","version","engine","scenario_schema","features","payload_size","crc32","payload"},{},"project");
    if(bounded(j.at("format"),16,"format")!="SimForge") invalid("format","not a SimForge project");
    if(integer(j.at("version"),std::numeric_limits<std::uint32_t>::max(),"version")!=project_format_version) invalid("version","unsupported project version");
    if(bounded(j.at("engine"),64,"engine")!=project_engine_version || integer(j.at("scenario_schema"),1,"scenario schema")!=1) invalid("engine","incompatible simulation/schema version");
    const auto legacy_features=Json::array({"warehouse_snapshots","clock_phase","historical_reports","independent_runs"});
    auto workload_features=legacy_features;workload_features.push_back("workload_v1");
    if(j.at("features")!=(has_workload(j.at("payload")) ? workload_features : legacy_features)) invalid("features","unsupported or missing persistence features");
    const auto payload=j.at("payload").dump();
    if(integer(j.at("payload_size"),project_max_bytes,"payload size")!=payload.size() ||
        integer(j.at("crc32"),std::numeric_limits<std::uint32_t>::max(),"crc32")!=checksum(payload)) invalid("project","corrupt project payload");
    ProjectInfo info; ProjectPreferences prefs;
    auto replacement=ProjectCodec::payload(j.at("payload"),info,prefs,timings_);
    timings_.validation_ms=ms(validation)-timings_.restoration_ms-timings_.scene_ms;
    if(!recovery) info.path=std::filesystem::absolute(path).lexically_normal();
    auto replacement_marker=ProjectCodec::marker(replacement,info,prefs);
    auto saved_marker=recovery?std::string{}:replacement_marker;
    // No current state has changed until here, including metadata/dirty markers.
    // Destruction joins the old worker.
    workspace_.execution().cancel(); workspace_.execution().shutdown();
    workspace_=std::move(replacement); info_=std::move(info); preferences_=std::move(prefs);
    saved_marker_=std::move(saved_marker);recovery_marker_=std::move(replacement_marker); last_recovery_=Timer::now(); timings_.total_ms=ms(total);
}
void Project::open(const std::filesystem::path& path) {load(path,false);}
void Project::recover(const std::filesystem::path& path) {load(path,true);workspace_.message_="Recovery restored. Use Save As to choose a primary project file.";}
void Project::create(const scenario::ScenarioDefinition& baseline,std::string name)
{
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
    const auto current=marker(); if(current==recovery_marker_) return false;
    path_valid(recovery);
    if(recovery.extension()!=".recovery") throw std::invalid_argument("Recovery requires a separate .recovery file");
    if(!info_.path.empty() && std::filesystem::absolute(recovery).lexically_normal()==info_.path) throw std::invalid_argument("Recovery cannot overwrite primary project");
    const auto bytes=serialize();transactional_write(recovery,bytes,true,timings_,{});recovery_marker_=current;return true;
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
    if(!recovery_available(path)) return;
    const auto j=parse_json(load_text(path,project_max_bytes),project_max_bytes);
    if(j.at("payload").at("identity").at("id")==info_.id) discard_recovery(path);
}
}
