#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/workspace/experiment_execution.hpp>
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
analysis::AnalysisReport independent(const ScenarioDefinition& d, analysis::Objective objective, const simulation::ExecutionControl* control,
    std::vector<warehouse::WarehouseSnapshot>* snapshots)
{
    simulation::ExecutionControl inner;
    if (control) {
        inner.stop = control->stop;
        inner.progress = [control](auto phase, auto name, auto tick, auto horizon) {
            if (phase != "Candidate complete" && phase != "Evaluating candidate" && phase != "Simulating baseline" && phase != "Validating") control->update(phase,name,tick,horizon);
        };
    }
    std::vector<warehouse::WarehouseSnapshot> finals;
    auto report = analysis::run_comparison(analysis::prepare_request(d,0,objective,
        {{d.name == "Baseline" ? "Same configuration" : d.name,counts(d)}},control),control ? &inner : nullptr,snapshots ? &finals : nullptr);
    if (snapshots) snapshots->push_back(std::move(finals.front()));
    return report;
}
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
ScenarioWorkspace::ScenarioWorkspace() : execution_{std::make_unique<ExperimentExecution>()} {}
ScenarioWorkspace::~ScenarioWorkspace() = default;
ScenarioWorkspace::ScenarioWorkspace(ScenarioWorkspace&&) noexcept = default;
ScenarioWorkspace& ScenarioWorkspace::operator=(ScenarioWorkspace&&) noexcept = default;
ScenarioWorkspace::ScenarioWorkspace(const ScenarioDefinition& d, double speed) : speed_{speed}, execution_{std::make_unique<ExperimentExecution>()} { load(d); }
const ScenarioDefinition& ScenarioWorkspace::baseline() const
{ if (!baseline_) throw std::logic_error("No scenario loaded"); return *baseline_; }
viewer::ViewerController& ScenarioWorkspace::view()
{ if (preview_view_) return *preview_view_; if (!baseline_view_) throw std::logic_error("No scenario loaded"); if (active_run_) { for (auto& run : runs_) if (run.id == active_run_) return *run.controller; throw std::logic_error("Missing active run"); } return *baseline_view_; }
const viewer::ViewerController& ScenarioWorkspace::baseline_viewer() const
{ if (!baseline_view_) throw std::logic_error("No scenario loaded"); return *baseline_view_; }
void ScenarioWorkspace::load(const ScenarioDefinition& d, bool discard)
{
    if (baseline_.has_value() && !discard) throw std::logic_error("Confirm replacing baseline and discarding runs/draft/candidates/results before opening another scenario");
    if (execution_->progress().active()) throw std::logic_error("Cancel the active experiment before replacing the baseline");
    warehouse_only(d); auto prepared = std::make_unique<viewer::ViewerController>(d,speed_);
    // A completion from the prior baseline must not publish after a scenario replacement.
    (void)execution_->take_result(execution_->progress().request_id);
    auto draft = make_draft(d); baseline_ = d; baseline_view_ = std::move(prepared); preview_view_.reset(); runs_.clear(); active_run_ = 0;
    ++persistent_generation_; draft_ = draft_origin_ = std::move(draft); errors_.clear(); candidates_.clear(); report_.reset();
    // IDs are never reused within the workspace, including after scenario replacement.
    viewed_identity_ = "LIVE SIMULATION - Original Baseline: "+d.name; status_ = Status::Ready; message_ = "Scenario loaded. Edit initial configuration; the running warehouse is unchanged by typing.";
}
void ScenarioWorkspace::open(const std::filesystem::path& path, bool discard) { load(scenario::load_scenario(path),discard); }
bool ScenarioWorkspace::dirty() const { return baseline_.has_value() && draft_ != draft_origin_; }
void ScenarioWorkspace::edit(Field field, std::string value)
{
    (void)baseline(); const auto index = static_cast<std::size_t>(field);
    if (index >= draft_.values.size()) throw std::invalid_argument("Unknown draft field");
    if (draft_.values[index] != value) ++persistent_generation_; draft_.values[index] = std::move(value); errors_.clear(); status_ = dirty() ? Status::Modified : Status::Ready;
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
    auto prepared=make_draft(baseline());
    if(draft_!=prepared || draft_origin_!=prepared) ++persistent_generation_;
    draft_ = draft_origin_ = std::move(prepared); errors_.clear(); status_ = Status::Ready; message_ = "Draft restored to loaded baseline.";
}
void ScenarioWorkspace::save_draft(const std::filesystem::path& path)
{
    const auto d = checked_draft(); analysis::detail::save_output(path,scenario::serialize_scenario(d));
    if(draft_origin_!=draft_) ++persistent_generation_;
    draft_origin_ = draft_; status_ = Status::Ready; message_ = "Saved new scenario: "+path.string();
}
void ScenarioWorkspace::run_draft() { (void)create_run(checked_draft()); }
std::uint64_t ScenarioWorkspace::create_run(const ScenarioDefinition& d)
{
    warehouse_only(d);
    if (runs_.size() >= max_custom_runs) throw std::length_error("At most 8 custom runs; remove a retained run first");
    if (next_run_id_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Run identities exhausted");
    auto prepared = std::make_unique<viewer::ViewerController>(d,speed_);
    prepared->reset(); // Consistent custom-run policy: untouched tick zero, paused.
    prepared->set_viewport(view().viewport());
    const auto id = next_run_id_;
    ++persistent_generation_; runs_.push_back({id,std::move(prepared)}); ++next_run_id_;
    select_run(id);
    message_ = "Independent run starts paused at tick zero. Press Play. Inactive runs do not advance.";
    return id;
}
void ScenarioWorkspace::select_run(std::uint64_t id)
{
    auto* target = baseline_view_.get();
    if (id) {
        const auto found = std::find_if(runs_.begin(),runs_.end(),[&](const auto& r){ return r.id == id; });
        if (found == runs_.end()) throw std::invalid_argument("Unknown playable run ID");
        target = found->controller.get();
    }
    if (!target) throw std::logic_error("No scenario loaded");
    const auto viewport = view().viewport();
    view().select(std::nullopt); view().input({viewer::InputKind::FocusLost},{});
    target->set_viewport(viewport,true); target->select(std::nullopt);
    preview_view_.reset(); active_run_ = id;
    viewed_identity_ = "LIVE SIMULATION - "+(id ? target->name()+" / Run #"+std::to_string(id) : "Original Baseline: "+target->name());
}
void ScenarioWorkspace::remove_run(std::uint64_t id)
{
    if (!id) throw std::invalid_argument("The original baseline cannot be removed");
    const auto found = std::find_if(runs_.begin(),runs_.end(),[&](const auto& r){ return r.id == id; });
    if (found == runs_.end()) throw std::invalid_argument("Unknown playable run ID");
    if (active_run_ == id) select_run(0);
    runs_.erase(found);
}
void ScenarioWorkspace::resume_live_view() { select_run(active_run_); }
void ScenarioWorkspace::simulate_candidate(std::uint64_t id) { (void)create_run(candidate(id).definition); }
void ScenarioWorkspace::simulate_result(std::string_view name)
{
    if (!report_) throw std::logic_error("No comparison report");
    if (report_->mode == ComparisonMode::Checkpoint) throw std::logic_error("Checkpoint interventions are preview-only; initial configuration is a separate timeline");
    if (name.empty() || name == "Baseline") { (void)create_run(report_->baseline_definition); return; }
    const auto found = std::find_if(report_->definitions.begin(),report_->definitions.end(),[&](const auto& d){ return d.name == name; });
    if (found == report_->definitions.end()) throw std::invalid_argument("Unknown report candidate");
    (void)create_run(*found);
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
{ ++persistent_generation_; status_ = Status::ComparisonReady; message_ = "Candidates changed. Previous report uses earlier submitted inputs."; }
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
    auto prepared=make_draft(candidate(id).definition);
    if(draft_!=prepared || draft_origin_!=prepared) ++persistent_generation_;
    draft_ = draft_origin_ = std::move(prepared); errors_.clear(); status_ = Status::Ready;
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
ExperimentRequest ScenarioWorkspace::capture_request(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> tick) const
{
    ExperimentRequest request{0,baseline(),candidates_,mode,objective,tick,{}};
    validate_experiment_request(request);
    if (mode == ComparisonMode::Checkpoint && !tick) request.checkpoint = baseline_viewer().runtime().capture_snapshot();
    validate_experiment_request(request);
    return request;
}
void validate_experiment_request(const ExperimentRequest& request)
{
    if (request.mode != ComparisonMode::Initial && request.mode != ComparisonMode::Checkpoint) throw std::invalid_argument("Unsupported comparison mode");
    warehouse_only(request.baseline); (void)analysis::objective_name(request.objective);
    if (request.checkpoint_tick && request.checkpoint) throw std::invalid_argument("Choose an explicit checkpoint tick or an owned historical snapshot");
    if (request.candidates.empty() || request.candidates.size() > analysis::max_candidates) throw std::invalid_argument("Add 1..50 validated candidates");
    if (request.checkpoint_tick && *request.checkpoint_tick > std::uint64_t{request.baseline.simulation.duration_seconds} * request.baseline.simulation.tick_rate)
        throw std::invalid_argument("Checkpoint exceeds simulation horizon");
    std::vector<std::string> names; std::vector<std::uint64_t> ids;
    for (const auto& c : request.candidates) {
        warehouse_only(c.definition);
        if (c.notes.size()>4096 || c.changed_inputs.size()>4096) throw std::length_error("Candidate metadata exceeds byte limit");
        if (!compatible(request.baseline,c.definition,request.mode == ComparisonMode::Checkpoint)) throw std::invalid_argument(c.definition.name+": incompatible comparison inputs");
        if (c.id == 0 || c.definition.name == "Baseline" || std::find(names.begin(),names.end(),c.definition.name) != names.end() || std::find(ids.begin(),ids.end(),c.id) != ids.end()) throw std::invalid_argument("Duplicate or invalid candidate identity");
        names.push_back(c.definition.name); ids.push_back(c.id);
    }
    if (request.checkpoint) {
        warehouse::validate_snapshot(*request.checkpoint);
        if (request.checkpoint->scenario_identity != scenario::serialize_scenario(request.baseline)) throw std::invalid_argument("Checkpoint baseline mismatch");
        if (request.checkpoint->settled) {
            std::vector<analysis::ScenarioCandidate> candidates;
            for (const auto& c:request.candidates) candidates.push_back({c.definition.name,counts(c.definition)});
            analysis::validate_checkpoint_candidates(*request.checkpoint,candidates);
        }
    }
}
void ScenarioWorkspace::compare(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> tick)
{
    if (execution_->progress().active()) throw std::logic_error("An experiment is already active");
    auto prepared = execute_experiment(capture_request(mode,objective,tick));
    (void)execution_->take_result(execution_->progress().request_id);
    ++persistent_generation_; ++report_generation_; report_ = std::move(prepared); status_ = Status::Results; message_ = "Comparison complete. Best among tested configurations; active baseline preserved.";
}
ExperimentExecution& ScenarioWorkspace::execution() { return *execution_; }
void ScenarioWorkspace::start_experiment(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> tick)
{
    if (execution_->progress().active()) throw std::logic_error("An experiment is already active");
    execution_->start(capture_request(mode,objective,tick));
    message_ = "Experiment uses submitted inputs; edits apply to future requests.";
}
bool ScenarioWorkspace::poll_experiment()
{
    const auto progress = execution_->progress();
    if (auto completed = execution_->take_result(progress.request_id)) {
        ++persistent_generation_; ++report_generation_; report_ = std::move(completed); status_ = Status::Results;
        // The preview owns its snapshot. Publication must not switch the active scene/camera.
        message_ = "Experiment complete. Report belongs to submitted inputs."; return true;
    }
    return false;
}
Comparison execute_experiment(const ExperimentRequest& request, const simulation::ExecutionControl* control)
{
    validate_experiment_request(request);
    if (request.mode == ComparisonMode::Checkpoint && !request.checkpoint_tick && !request.checkpoint) throw std::invalid_argument("Missing owned checkpoint");
    const auto mode = request.mode; const auto objective = request.objective; const auto tick = request.checkpoint_tick;
    if (control) control->update("Preparing");
    Comparison prepared; prepared.request_id = request.id; prepared.mode = mode; prepared.objective = objective; prepared.baseline_definition = request.baseline;
    auto ordered = request.candidates; std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.definition.name < b.definition.name;});
    for (const auto& c : ordered)
    {
        warehouse_only(c.definition);
        if (!compatible(request.baseline,c.definition,mode == ComparisonMode::Checkpoint))
            throw std::invalid_argument(c.definition.name+": comparisons require the same duration and tick rate; checkpoint mode also requires unchanged arrival, processing and seed");
        prepared.candidate_ids.push_back(c.id);
        prepared.definitions.push_back(c.definition);
        prepared.changed_inputs.push_back(c.changed_inputs);
    }
    const auto started = Timer::now();
    if (mode == ComparisonMode::Checkpoint)
    {
        warehouse::WarehouseSnapshot checkpoint;
        if (tick) checkpoint = analysis::prepare_request(request.baseline,*tick,objective,{{"Unchanged",counts(request.baseline)}},control).checkpoint;
        else
        {
            // Work on a private copy; settling tick zero must never settle the viewer.
            auto clone = warehouse::WarehouseSimulation::create_from(*request.checkpoint);
            clone.advance_to(clone.clock().tick_count(),control); checkpoint = clone.capture_snapshot();
        }
        std::vector<analysis::ScenarioCandidate> inputs; for(const auto& c : ordered) inputs.push_back({c.definition.name,counts(c.definition)});
        auto report = analysis::run_comparison({1,checkpoint.scenario_identity,checkpoint,objective,std::move(inputs)},control,
            control ? &prepared.final_snapshots : nullptr);
        prepared.baseline = report.baseline; prepared.candidates = report.candidates; prepared.ranking = report.ranking;
        prepared.bottlenecks = report.bottlenecks; prepared.warnings = report.warnings; prepared.limitations = report.limitations;
        prepared.executions.push_back(std::move(report));
    }
    else
    {
        if (control) control->update("Evaluating baseline",request.baseline.name);
        prepared.executions.push_back(independent(request.baseline,objective,control,control ? &prepared.final_snapshots : nullptr));
        prepared.baseline = prepared.executions.front().baseline;
        prepared.bottlenecks = prepared.executions.front().bottlenecks;
        for(const auto& c : ordered)
        {
            if (control) control->update("Evaluating candidate",c.definition.name);
            prepared.executions.push_back(independent(c.definition,objective,control,control ? &prepared.final_snapshots : nullptr));
            if (control) control->update("Candidate complete",c.definition.name);
            analysis::CandidateResult r; r.candidate = {c.definition.name,counts(c.definition)}; r.metrics = prepared.executions.back().baseline.metrics;
            r.delta = analysis::compare_metrics(prepared.baseline.metrics,r.metrics,counts(request.baseline),counts(c.definition)); prepared.candidates.push_back(std::move(r));
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
    if (control) control->update("Constructing report");
    return prepared;
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
    if (!r.final_snapshots.empty()) snapshot = r.final_snapshots.at(index);
    else if(r.mode == ComparisonMode::Initial) { warehouse::WarehouseSimulation runtime{d}; runtime.run(); snapshot = runtime.capture_snapshot(); }
    else
    {
        snapshot = final_branch(r.executions.front().request.checkpoint,capacity);
    }
    if (r.mode == ComparisonMode::Checkpoint)
    {
        auto& c = std::get<scenario::WarehouseConfig>(d.domain); c.loading_bays = capacity.loading_bays; c.workers = capacity.workers; c.forklifts = capacity.forklifts;
    }
    auto prepared = std::make_unique<viewer::ViewerController>(d,snapshot);
    if(prepared->observation().metrics != expected) throw std::runtime_error("Result preview metrics do not match report");
    prepared->set_viewport(view().viewport()); view().select(std::nullopt); view().input({viewer::InputKind::FocusLost},{}); preview_view_ = std::move(prepared);
    viewed_identity_ = "FINAL-STATE PREVIEW - Report #"+std::to_string(r.request_id)+": "+(name ? *name : std::string{"Baseline"});
    preview_report_generation_=report_generation_;
}
void ScenarioWorkspace::return_to_baseline()
{ select_run(0); }
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
