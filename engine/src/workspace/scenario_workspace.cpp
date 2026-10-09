#include <simforge/workspace/scenario_workspace.hpp>
#include "../analysis/comparison_json.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>

namespace simforge::workspace
{
namespace
{
using Timer = std::chrono::steady_clock;
using scenario::ScenarioDefinition;
warehouse::ResourceCounts counts(const ScenarioDefinition& d)
{ const auto& c = std::get<scenario::WarehouseConfig>(d.domain); return {c.loading_bays,c.workers,c.forklifts}; }
void warehouse_only(const ScenarioDefinition& d)
{ scenario::validate(d); if (!std::holds_alternative<scenario::WarehouseConfig>(d.domain)) throw std::invalid_argument("Workspace requires a warehouse scenario"); }
bool compatible(const ScenarioDefinition& a, const ScenarioDefinition& b, bool checkpoint)
{
    const auto& x = std::get<scenario::WarehouseConfig>(a.domain); const auto& y = std::get<scenario::WarehouseConfig>(b.domain);
    return a.simulation.tick_rate == b.simulation.tick_rate && a.simulation.duration_seconds == b.simulation.duration_seconds &&
        (!checkpoint || (a.simulation.random_seed == b.simulation.random_seed && x.shipment_interval_seconds == y.shipment_interval_seconds &&
            x.processing_duration_seconds == y.processing_duration_seconds));
}
warehouse::WarehouseSnapshot final_branch(const warehouse::WarehouseSnapshot& checkpoint, warehouse::ResourceCounts desired)
{
    auto branch = warehouse::WarehouseSimulation::create_from(checkpoint);
    for (bool reduction : {true,false}) for (auto r : {warehouse::ResourceType::LoadingBay,warehouse::ResourceType::Worker,warehouse::ResourceType::Forklift})
    {
        const auto before = warehouse::capacity_for(checkpoint.capacity,r), after = warehouse::capacity_for(desired,r);
        if (before != after && ((after < before) == reduction)) branch.set_resource_capacity(r,after);
    }
    branch.run(); return branch.capture_snapshot();
}
analysis::AnalysisReport independent(const ScenarioDefinition& d, analysis::Objective objective)
{ return analysis::run_comparison(analysis::prepare_request(d,0,objective,{{d.name == "Baseline" ? "Same configuration" : d.name,counts(d)}})); }
}
const char* objective_label(analysis::Objective o)
{
    switch(o) { case analysis::Objective::MaximizeCompleted: return "Maximize completed shipments";
    case analysis::Objective::MinimizeAverageWait: return "Minimize average queue wait";
    case analysis::Objective::MinimizeWaiting: return "Minimize remaining waiting shipments"; }
    throw std::invalid_argument("Unsupported objective");
}
const char* change_label(std::optional<double> delta, bool higher)
{
    if(!delta) return "N/A";
    if(!std::isfinite(*delta)) throw std::invalid_argument("Invalid metric delta");
    if(*delta==0) return "No change";
    return (*delta>0)==higher ? "Improved" : "Regressed";
}
std::uint64_t checkpoint_tick(const ScenarioDefinition& definition, std::string_view text)
{
    warehouse_only(definition); std::uint64_t seconds{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),seconds);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || seconds>definition.simulation.duration_seconds)
        throw std::invalid_argument("Checkpoint must be integer seconds in 0.."+std::to_string(definition.simulation.duration_seconds));
    return seconds*definition.simulation.tick_rate;
}
Draft ScenarioWorkspace::make_draft(const ScenarioDefinition& d)
{
    const auto& c = std::get<scenario::WarehouseConfig>(d.domain);
    return {{{d.name,std::to_string(c.loading_bays),std::to_string(c.workers),std::to_string(c.forklifts),
        std::to_string(c.shipment_interval_seconds),std::to_string(c.processing_duration_seconds),std::to_string(d.simulation.duration_seconds),
        std::to_string(d.simulation.tick_rate),std::to_string(d.simulation.random_seed)}}};
}
ScenarioWorkspace::ScenarioWorkspace(const ScenarioDefinition& d, double speed) : speed_{speed} { load(d); }
const ScenarioDefinition& ScenarioWorkspace::baseline() const
{ if (!baseline_) throw std::logic_error("No scenario loaded"); return *baseline_; }
viewer::ViewerController& ScenarioWorkspace::view()
{ if (alternative_view_) return *alternative_view_; if (!baseline_view_) throw std::logic_error("No scenario loaded"); return *baseline_view_; }
const viewer::ViewerController& ScenarioWorkspace::baseline_viewer() const
{ if (!baseline_view_) throw std::logic_error("No scenario loaded"); return *baseline_view_; }
void ScenarioWorkspace::load(const ScenarioDefinition& d, bool discard)
{
    if ((dirty() || !candidates_.empty()) && !discard) throw std::logic_error("Confirm discarding draft/candidates/results before opening another scenario");
    warehouse_only(d); auto prepared = std::make_unique<viewer::ViewerController>(d,speed_);
    auto draft = make_draft(d); baseline_ = d; baseline_view_ = std::move(prepared); alternative_view_.reset();
    draft_ = draft_origin_ = std::move(draft); errors_.clear(); candidates_.clear(); report_.reset();
    // IDs are never reused within the workspace, including after scenario replacement.
    viewed_identity_ = "Baseline: "+d.name; status_ = Status::Ready; message_ = "Scenario loaded. Edit initial configuration; the running warehouse is unchanged by typing.";
}
void ScenarioWorkspace::open(const std::filesystem::path& path, bool discard) { load(scenario::load_scenario(path),discard); }
bool ScenarioWorkspace::dirty() const { return baseline_.has_value() && draft_ != draft_origin_; }
void ScenarioWorkspace::edit(Field field, std::string value)
{
    (void)baseline(); const auto index = static_cast<std::size_t>(field);
    if (index >= draft_.values.size()) throw std::invalid_argument("Unknown draft field");
    draft_.values[index] = std::move(value); errors_.clear(); status_ = dirty() ? Status::Modified : Status::Ready;
}
bool ScenarioWorkspace::validate_draft()
{
    (void)baseline(); errors_.clear(); std::array<std::uint64_t,9> numbers{};
    auto name_check = *baseline_; name_check.name = draft_.values[0];
    try { warehouse_only(name_check); } catch (const std::exception& e) { errors_.push_back({"Name",e.what()}); }
    constexpr std::array<std::uint64_t,9> maxima{0,100000,100000,100000,604800,604800,604800,1000,std::numeric_limits<std::uint64_t>::max()};
    for (std::size_t i = 1; i < 9; ++i)
    {
        const auto& text = draft_.values[i]; const auto parsed = std::from_chars(text.data(),text.data()+text.size(),numbers[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || numbers[i] > maxima[i] || (i >= 4 && i <= 7 && numbers[i] == 0))
            errors_.push_back({field_names[i],"Enter an integer in "+std::string{i >= 4 && i <= 7 ? "1" : "0"}+".."+std::to_string(maxima[i])});
    }
    auto d = *baseline_; d.name = draft_.values[0];
    d.simulation = {static_cast<std::uint32_t>(numbers[7]),static_cast<std::uint32_t>(numbers[6]),numbers[8]};
    d.domain = scenario::WarehouseConfig{static_cast<std::uint32_t>(numbers[1]),static_cast<std::uint32_t>(numbers[2]),
        static_cast<std::uint32_t>(numbers[3]),static_cast<std::uint32_t>(numbers[4]),static_cast<std::uint32_t>(numbers[5])};
    if (errors_.empty()) try { warehouse_only(d); } catch (const std::exception& e) { errors_.push_back({"Scenario",e.what()}); }
    status_ = errors_.empty() ? (dirty() ? Status::Modified : Status::Ready) : Status::Invalid;
    message_ = errors_.empty() ? "Draft valid. Execute explicitly to create a new run." : "Draft validation failed; the running warehouse is unchanged.";
    return errors_.empty();
}
ScenarioDefinition ScenarioWorkspace::checked_draft()
{
    if (!validate_draft()) throw std::invalid_argument(errors_.front().field+": "+errors_.front().message);
    auto d = *baseline_; d.name = draft_.values[0];
    const auto n = [&](std::size_t i){ std::uint64_t v{}; const auto& s = draft_.values[i]; std::from_chars(s.data(),s.data()+s.size(),v); return v; };
    d.simulation = {static_cast<std::uint32_t>(n(7)),static_cast<std::uint32_t>(n(6)),n(8)};
    d.domain = scenario::WarehouseConfig{static_cast<std::uint32_t>(n(1)),static_cast<std::uint32_t>(n(2)),static_cast<std::uint32_t>(n(3)),static_cast<std::uint32_t>(n(4)),static_cast<std::uint32_t>(n(5))}; return d;
}
void ScenarioWorkspace::revert(bool discard)
{
    if (dirty() && !discard) throw std::logic_error("Confirm discarding unsaved changes to revert");
    draft_ = draft_origin_ = make_draft(baseline()); errors_.clear(); status_ = Status::Ready; message_ = "Draft restored to loaded baseline.";
}
void ScenarioWorkspace::save_draft(const std::filesystem::path& path)
{
    const auto d = checked_draft(); analysis::detail::save_output(path,scenario::serialize_scenario(d));
    draft_origin_ = draft_; status_ = Status::Ready; message_ = "Saved new scenario: "+path.string();
}
void ScenarioWorkspace::run_draft()
{
    const auto d = checked_draft(); auto prepared = std::make_unique<viewer::ViewerController>(d,speed_);
    prepared->set_viewport(view().viewport()); alternative_view_ = std::move(prepared); viewed_identity_ = "Draft run from start: "+d.name;
    message_ = "New independent draft run. The original baseline is preserved.";
}
Candidate& ScenarioWorkspace::candidate(std::uint64_t id)
{
    const auto found = std::find_if(candidates_.begin(),candidates_.end(),[&](const auto& c){return c.id == id;});
    if (found == candidates_.end()) throw std::invalid_argument("Unknown candidate ID"); return *found;
}
void ScenarioWorkspace::check_name(const std::string& name, std::optional<std::uint64_t> excluding) const
{
    auto d = baseline(); d.name = name; warehouse_only(d);
    if (name == "Baseline" || std::any_of(candidates_.begin(),candidates_.end(),[&](const auto& c){return c.id != excluding && c.definition.name == name;}))
        throw std::invalid_argument("Candidate name is duplicate or reserved: "+name);
}
void ScenarioWorkspace::invalidate()
{ report_.reset(); status_ = Status::ComparisonReady; message_ = "Candidates changed. Run comparison to produce current results."; }
std::uint64_t ScenarioWorkspace::add_candidate(std::string notes)
{
    auto d = checked_draft(); check_name(d.name);
    if (candidates_.size() >= analysis::max_candidates) throw std::length_error("At most 50 candidates are supported");
    if (notes.size() > 4096) throw std::length_error("Notes exceed 4096 bytes");
    if (next_id_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Candidate identities exhausted");
    const auto changed = differences(baseline(),d);
    const auto id = next_id_++; candidates_.push_back({id,std::move(d),std::move(notes),changed}); draft_origin_ = draft_; invalidate(); return id;
}
std::uint64_t ScenarioWorkspace::duplicate_candidate(std::uint64_t id, std::string name)
{
    auto copy = candidate(id); check_name(name);
    if (candidates_.size() >= analysis::max_candidates) throw std::length_error("At most 50 candidates are supported");
    if (next_id_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Candidate identities exhausted");
    copy.id = next_id_++; copy.definition.name = std::move(name); candidates_.push_back(std::move(copy)); invalidate(); return candidates_.back().id;
}
void ScenarioWorkspace::rename_candidate(std::uint64_t id, std::string name)
{ check_name(name,id); candidate(id).definition.name = std::move(name); invalidate(); }
void ScenarioWorkspace::edit_candidate(std::uint64_t id, bool discard)
{
    if (dirty() && !discard) throw std::logic_error("Confirm discarding unsaved draft edits before editing a candidate");
    draft_ = draft_origin_ = make_draft(candidate(id).definition); errors_.clear(); status_ = Status::Ready;
}
void ScenarioWorkspace::update_candidate(std::uint64_t id, std::string notes)
{
    auto d = checked_draft(); check_name(d.name,id); if (notes.size() > 4096) throw std::length_error("Notes exceed 4096 bytes");
    const auto changed = differences(baseline(),d);
    auto& c = candidate(id); c.definition = std::move(d); c.notes = std::move(notes); c.changed_inputs = changed; draft_origin_ = draft_; invalidate();
}
void ScenarioWorkspace::remove_candidate(std::uint64_t id)
{ (void)candidate(id); std::erase_if(candidates_,[&](const auto& c){return c.id == id;}); invalidate(); }
void ScenarioWorkspace::restore_candidate(std::uint64_t id)
{ auto d = baseline(); auto& c = candidate(id); d.name = c.definition.name; c.definition = std::move(d); c.changed_inputs = "Same operational configuration"; invalidate(); }
void ScenarioWorkspace::compare(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> tick)
{
    if(mode!=ComparisonMode::Initial && mode!=ComparisonMode::Checkpoint) throw std::invalid_argument("Unsupported comparison mode");
    (void)analysis::objective_name(objective);
    if (candidates_.empty()) throw std::invalid_argument("Add at least one validated candidate");
    Comparison prepared; prepared.mode = mode; prepared.objective = objective; prepared.baseline_definition = baseline();
    auto ordered = candidates_; std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.definition.name < b.definition.name;});
    for (const auto& c : ordered)
    {
        warehouse_only(c.definition);
        if (!compatible(baseline(),c.definition,mode == ComparisonMode::Checkpoint))
            throw std::invalid_argument(c.definition.name+": comparisons require the same duration and tick rate; checkpoint mode also requires unchanged arrival, processing and seed");
        prepared.definitions.push_back(c.definition);
        prepared.changed_inputs.push_back(c.changed_inputs);
    }
    const auto started = Timer::now();
    if (mode == ComparisonMode::Checkpoint)
    {
        warehouse::WarehouseSnapshot checkpoint;
        if (tick) checkpoint = analysis::prepare_request(baseline(),*tick,objective,{{"Unchanged",counts(baseline())}}).checkpoint;
        else
        {
            // Work on a private copy; settling tick zero must never settle the viewer.
            auto clone = warehouse::WarehouseSimulation::create_from(baseline_viewer().runtime().capture_snapshot());
            clone.advance_to(clone.clock().tick_count()); checkpoint = clone.capture_snapshot();
        }
        std::vector<analysis::ScenarioCandidate> inputs; for(const auto& c : ordered) inputs.push_back({c.definition.name,counts(c.definition)});
        auto report = analysis::run_comparison({1,checkpoint.scenario_identity,checkpoint,objective,std::move(inputs)});
        prepared.baseline = report.baseline; prepared.candidates = report.candidates; prepared.ranking = report.ranking;
        prepared.bottlenecks = report.bottlenecks; prepared.warnings = report.warnings; prepared.limitations = report.limitations;
        prepared.executions.push_back(std::move(report));
    }
    else
    {
        prepared.executions.push_back(independent(baseline(),objective));
        prepared.baseline = prepared.executions.front().baseline;
        prepared.bottlenecks = prepared.executions.front().bottlenecks;
        for(const auto& c : ordered)
        {
            prepared.executions.push_back(independent(c.definition,objective));
            analysis::CandidateResult r; r.candidate = {c.definition.name,counts(c.definition)}; r.metrics = prepared.executions.back().baseline.metrics;
            r.delta = analysis::compare_metrics(prepared.baseline.metrics,r.metrics,counts(baseline()),counts(c.definition)); prepared.candidates.push_back(std::move(r));
        }
        prepared.execution_ms = std::chrono::duration<double,std::milli>{Timer::now()-started}.count();
        const auto derived = Timer::now(); analysis::rank_candidates(prepared.candidates,objective);
        auto ranked = prepared.candidates; std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.rank.value_or(1000)<b.rank.value_or(1000);});
        for(const auto& r : ranked) if(r.rank) prepared.ranking.push_back(r.candidate.name);
        if(!prepared.baseline.eligible) prepared.warnings.push_back("Baseline objective is unavailable; improvements are inconclusive.");
        else if(prepared.ranking.empty()) prepared.warnings.push_back("No candidate is eligible for the selected objective.");
        else
        {
            const auto& b = prepared.baseline.metrics; const auto& c = ranked.front().metrics;
            // Same rate/horizon and bounded runtime metrics preserve the engine's
            // exact rational mean-wait interpretation (products <= 1e17).
            const bool improved = objective == analysis::Objective::MaximizeCompleted ? c.completed > b.completed :
                objective == analysis::Objective::MinimizeWaiting ? c.waiting < b.waiting : c.total_wait_ticks*b.started < b.total_wait_ticks*c.started;
            if(!improved) prepared.warnings.push_back("Best tested candidate does not improve on baseline for this objective.");
        }
        prepared.limitations = prepared.executions.front().limitations;
        prepared.limitations.push_back("Independent initial scenarios; multiple changed variables do not establish single-resource causation. Equal horizon and tick rate required.");
        prepared.report_ms = std::chrono::duration<double,std::milli>{Timer::now()-derived}.count();
    }
    if (mode == ComparisonMode::Checkpoint) prepared.execution_ms = std::chrono::duration<double,std::milli>{Timer::now()-started}.count();
    report_ = std::move(prepared); status_ = Status::Results; message_ = "Comparison complete. Best among tested configurations; active baseline preserved.";
}
void ScenarioWorkspace::view_result(std::optional<std::string> name)
{
    if (!report_) throw std::logic_error("Run a comparison first"); const auto& r = *report_;
    auto d = r.baseline_definition; auto expected = r.baseline.metrics; auto capacity = r.baseline.candidate.resources; std::size_t index = 0;
    if(name)
    {
        const auto found = std::find_if(r.candidates.begin(),r.candidates.end(),[&](const auto& c){return c.candidate.name == *name;});
        if(found == r.candidates.end()) throw std::invalid_argument("Unknown report candidate");
        index = static_cast<std::size_t>(found-r.candidates.begin())+1; d = r.definitions.at(index-1); expected = found->metrics; capacity = found->candidate.resources;
    }
    warehouse::WarehouseSnapshot snapshot;
    if(r.mode == ComparisonMode::Initial) { warehouse::WarehouseSimulation runtime{d}; runtime.run(); snapshot = runtime.capture_snapshot(); }
    else
    {
        snapshot = final_branch(r.executions.front().request.checkpoint,capacity);
        auto& c = std::get<scenario::WarehouseConfig>(d.domain); c.loading_bays = capacity.loading_bays; c.workers = capacity.workers; c.forklifts = capacity.forklifts;
    }
    auto prepared = std::make_unique<viewer::ViewerController>(d,snapshot);
    if(prepared->observation().metrics != expected) throw std::runtime_error("Result preview metrics do not match report");
    prepared->set_viewport(view().viewport()); alternative_view_ = std::move(prepared);
    viewed_identity_ = "Final result preview: "+(name ? *name : std::string{"Baseline"});
}
void ScenarioWorkspace::return_to_baseline()
{ alternative_view_.reset(); if(baseline_) viewed_identity_ = "Baseline: "+baseline_->name; }
std::string ScenarioWorkspace::csv() const
{ if(!report_) throw std::logic_error("No comparison report"); return analysis::serialize_comparison_csv(report_->baseline,report_->candidates); }
std::string ScenarioWorkspace::json() const
{
    if(!report_) throw std::logic_error("No comparison report"); const auto& r = *report_;
    if(r.mode == ComparisonMode::Checkpoint) return analysis::serialize_report(r.executions.front());
    using scenario::detail::Json;
    Json result{{"workspace_schema_version",1},{"mode","independent_initial_scenarios"},{"objective",analysis::objective_name(r.objective)},
        {"executions",Json::array()},{"comparison_csv",csv()},{"ranking",r.ranking},{"warnings",r.warnings},{"limitations",r.limitations}};
    for(const auto& execution : r.executions) result["executions"].push_back(Json::parse(analysis::serialize_report(execution)));
    const auto bytes = result.dump(); if(bytes.size() > analysis::max_report_bytes) throw std::length_error("Workspace report exceeds byte limit"); return bytes;
}
void ScenarioWorkspace::export_json(const std::filesystem::path& path)
{
    try { if(report_ && report_->mode == ComparisonMode::Checkpoint) analysis::save_report(path,report_->executions.front());
        else analysis::detail::save_output(path,json()); status_ = Status::Results; message_ = "JSON exported: "+path.string(); }
    catch(...) { status_ = Status::ExportFailed; message_ = "JSON export failed: "+path.string()+"; report retained."; throw; }
}
void ScenarioWorkspace::export_csv(const std::filesystem::path& path)
{
    try { if(report_ && report_->mode == ComparisonMode::Checkpoint) analysis::save_csv(path,report_->executions.front());
        else analysis::detail::save_output(path,csv()); status_ = Status::Results; message_ = "CSV exported: "+path.string(); }
    catch(...) { status_ = Status::ExportFailed; message_ = "CSV export failed: "+path.string()+"; report retained."; throw; }
}
void ScenarioWorkspace::verify_export(std::string_view bytes)
{
    const auto j = scenario::detail::parse_json(bytes,analysis::max_report_bytes);
    if(!j.contains("workspace_schema_version")) { (void)analysis::replay_report(bytes); return; }
    const auto& executions = j.at("executions");
    if(!executions.is_array() || executions.size() < 2 || executions.size() > analysis::max_candidates+1) throw std::invalid_argument("Invalid independent execution count");
    const auto objective = analysis::parse_objective(j.at("objective").get<std::string>());
    const auto first = analysis::replay_report(executions[0].dump());
    ScenarioWorkspace reconstructed{scenario::parse_scenario(first.request.scenario_identity)};
    for(std::size_t i = 1; i < executions.size(); ++i)
    {
        const auto execution = analysis::replay_report(executions[i].dump());
        reconstructed.draft_ = make_draft(scenario::parse_scenario(execution.request.scenario_identity)); reconstructed.add_candidate();
    }
    reconstructed.compare(ComparisonMode::Initial,objective);
    if(scenario::detail::Json::parse(reconstructed.json()) != j) throw std::invalid_argument("Workspace report replay diverged");
}
std::string differences(const ScenarioDefinition& a, const ScenarioDefinition& b)
{
    warehouse_only(a); warehouse_only(b); const auto& x = std::get<scenario::WarehouseConfig>(a.domain); const auto& y = std::get<scenario::WarehouseConfig>(b.domain);
    std::string result;
    const auto field = [&](const char* name, std::uint64_t before, std::uint64_t after)
    { if(before != after) { if(!result.empty()) result += "; "; result += std::string{name}+" "+std::to_string(before)+" -> "+std::to_string(after); } };
    field("Bays",x.loading_bays,y.loading_bays); field("Workers",x.workers,y.workers); field("Forklifts",x.forklifts,y.forklifts);
    field("Arrival (s)",x.shipment_interval_seconds,y.shipment_interval_seconds); field("Processing (s)",x.processing_duration_seconds,y.processing_duration_seconds);
    field("Duration (s)",a.simulation.duration_seconds,b.simulation.duration_seconds); field("Tick rate",a.simulation.tick_rate,b.simulation.tick_rate); field("Seed",a.simulation.random_seed,b.simulation.random_seed);
    return result.empty() ? "Same operational configuration" : result;
}
}
