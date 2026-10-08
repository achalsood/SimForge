#include <simforge/analysis/scenario_comparison.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace simforge::analysis
{
namespace
{
constexpr ResourceType resources[]{ResourceType::LoadingBay, ResourceType::Worker, ResourceType::Forklift};
std::uint64_t total(ResourceCounts c) { return static_cast<std::uint64_t>(c.loading_bays) + c.workers + c.forklifts; }
void validate_candidates(const std::vector<ScenarioCandidate>& candidates)
{
    if (candidates.empty() || candidates.size() > max_candidates) throw std::invalid_argument("Experiment requires 1..50 candidates");
    std::set<std::string> names;
    for (const auto& candidate : candidates)
    {
        // Reuse the established UTF-8/name contract rather than introducing a second validator.
        scenario::ScenarioDefinition name_check; name_check.name = candidate.name; scenario::validate(name_check);
        if (candidate.name == "Baseline" || !names.insert(candidate.name).second)
            throw std::invalid_argument("Duplicate/reserved candidate name: " + candidate.name);
        if (total(candidate.resources) > 100000) throw std::invalid_argument("Candidate " + candidate.name + ": total resources exceeds 100000");
    }
}
void validate_candidate_at(const warehouse::WarehouseSnapshot& checkpoint, const ScenarioCandidate& candidate)
{
    std::size_t changes = 0;
    for (auto resource : resources)
    {
        const auto count = warehouse::capacity_for(candidate.resources, resource);
        if (count < warehouse::capacity_for(checkpoint.usage, resource))
            throw std::invalid_argument("Candidate " + candidate.name + ": " + std::string{warehouse::resource_name(resource)} + " capacity is below checkpoint usage");
        changes += count != warehouse::capacity_for(checkpoint.capacity, resource);
    }
    if (changes > warehouse::max_capacity_changes - checkpoint.capacity_changes.size())
        throw std::invalid_argument("Candidate " + candidate.name + ": capacity history limit exceeded");
}
std::optional<double> difference(std::optional<double> a, std::optional<double> b)
{ if (!a || !b) return {}; return *b - *a; }
std::optional<double> objective_value(const warehouse::WarehouseMetrics& metrics, Objective objective)
{
    switch (objective)
    {
    case Objective::MaximizeCompleted: return static_cast<double>(metrics.completed);
    case Objective::MinimizeAverageWait: return metrics.average_wait_seconds;
    case Objective::MinimizeWaiting: return static_cast<double>(metrics.waiting);
    }
    throw std::invalid_argument("Unsupported ranking objective");
}
bool improves(const warehouse::WarehouseMetrics& a, const warehouse::WarehouseMetrics& b, Objective objective)
{
    const auto before = objective_value(a, objective), after = objective_value(b, objective);
    if (!before || !after) return false;
    if (objective == Objective::MinimizeAverageWait)
        return b.total_wait_ticks * a.started < a.total_wait_ticks * b.started;
    return objective == Objective::MaximizeCompleted ? *after > *before : *after < *before;
}
CandidateResult result_for(const ScenarioCandidate& candidate, const warehouse::WarehouseMetrics& baseline,
    ResourceCounts baseline_resources, warehouse::WarehouseMetrics metrics)
{
    CandidateResult result; result.candidate = candidate; result.metrics = metrics;
    result.delta = compare_metrics(baseline, metrics, baseline_resources, candidate.resources); return result;
}
}
std::string_view objective_name(Objective objective)
{
    switch (objective)
    {
    case Objective::MaximizeCompleted: return "maximize_completed";
    case Objective::MinimizeAverageWait: return "minimize_average_wait";
    case Objective::MinimizeWaiting: return "minimize_waiting";
    }
    throw std::invalid_argument("Unsupported ranking objective");
}
Objective parse_objective(std::string_view name)
{
    for (auto objective : {Objective::MaximizeCompleted, Objective::MinimizeAverageWait, Objective::MinimizeWaiting})
        if (objective_name(objective) == name) return objective;
    throw std::invalid_argument("Unsupported ranking objective");
}
void validate_experiment(const ExperimentDefinition& experiment)
{
    if (experiment.schema_version != 1) throw std::invalid_argument("Unsupported experiment version");
    (void)objective_name(experiment.objective); validate_candidates(experiment.candidates);
    if (experiment.checkpoint_seconds > 604800) throw std::invalid_argument("Experiment checkpoint exceeds maximum duration");
    // Local JSON scenarios only, relative to experiment directory. Parent traversal,
    // drives/UNC/device paths and other extensions are outside this input contract.
    const auto utf8_path = experiment.scenario.generic_u8string();
    const std::string path{utf8_path.begin(), utf8_path.end()};
    if (path.empty() || path.size() > 1024 || experiment.scenario.has_root_directory() || experiment.scenario.has_root_name() ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos || experiment.scenario.extension() != ".json")
        throw std::invalid_argument("Experiment scenario must be a local relative .json path");
    for (const auto& part : experiment.scenario)
        if (part == "..") throw std::invalid_argument("Experiment scenario cannot traverse parent directories");
}
void validate_request(const ComparisonRequest& request)
{
    if (request.schema_version != 1) throw std::invalid_argument("Unsupported comparison request version");
    (void)objective_name(request.objective); validate_candidates(request.candidates);
    warehouse::validate_snapshot(request.checkpoint);
    if (!request.checkpoint.settled || request.scenario_identity != request.checkpoint.scenario_identity)
        throw std::invalid_argument("Experiment requires a settled, scenario-compatible checkpoint");
    for (const auto& candidate : request.candidates) validate_candidate_at(request.checkpoint, candidate);
}
ComparisonRequest prepare_request(const scenario::ScenarioDefinition& definition, std::uint64_t tick,
    Objective objective, std::vector<ScenarioCandidate> candidates)
{
    (void)objective_name(objective); validate_candidates(candidates);
    warehouse::WarehouseSimulation runtime{definition}; runtime.advance_to(tick);
    ComparisonRequest request{1, scenario::serialize_scenario(definition), runtime.capture_snapshot(), objective, std::move(candidates)};
    validate_request(request); return request;
}
warehouse::WarehouseMetrics execute_candidate(const warehouse::WarehouseSnapshot& checkpoint, ResourceCounts counts)
{
    if (total(counts) > 100000) throw std::invalid_argument("Candidate total resources exceeds 100000");
    warehouse::validate_snapshot(checkpoint);
    if (!checkpoint.settled) throw std::invalid_argument("Candidate requires a settled checkpoint");
    validate_candidate_at(checkpoint, {"candidate", counts});
    auto branch = warehouse::WarehouseSimulation::create_from(checkpoint);
    // Reductions first prevent temporary over-allocation/starts that would make
    // later reductions invalid. Within each phase, use bay/worker/forklift order.
    for (bool reductions : {true, false})
        for (auto resource : resources)
        {
            const auto before = warehouse::capacity_for(checkpoint.capacity, resource), after = warehouse::capacity_for(counts, resource);
            if (after != before && ((after < before) == reductions)) branch.set_resource_capacity(resource, after);
        }
    branch.run(); return branch.metrics();
}
MetricDelta compare_metrics(const warehouse::WarehouseMetrics& baseline, const warehouse::WarehouseMetrics& candidate,
    ResourceCounts baseline_resources, ResourceCounts candidate_resources)
{
    for (const auto* m : {&baseline, &candidate})
    {
        if (m->arrived > 100000 || m->started > 100000 || m->completed > 100000 || m->waiting > 100000 || m->in_progress > 100000 ||
            m->elapsed_ticks > scenario::max_execution_ticks)
            throw std::invalid_argument("Comparison metrics exceed warehouse bounds");
        for (auto value : {m->throughput_per_hour, m->average_wait_seconds, m->maximum_wait_seconds, m->average_processing_seconds,
            m->loading_bay_utilization, m->worker_utilization, m->forklift_utilization})
            if (value && (!std::isfinite(*value) || *value < 0)) throw std::invalid_argument("Comparison metrics require finite nonnegative values");
        if (!std::isfinite(m->completed_processing_seconds) || m->completed_processing_seconds < 0)
            throw std::invalid_argument("Comparison processing metric is invalid");
    }
    if (total(baseline_resources) > 100000 || total(candidate_resources) > 100000)
        throw std::invalid_argument("Comparison resources exceed warehouse bounds");
    if (baseline.elapsed_ticks != candidate.elapsed_ticks) throw std::invalid_argument("Comparison intervals differ");
    MetricDelta delta;
    delta.completed = static_cast<std::int64_t>(candidate.completed) - static_cast<std::int64_t>(baseline.completed);
    delta.waiting = static_cast<std::int64_t>(candidate.waiting) - static_cast<std::int64_t>(baseline.waiting);
    if (baseline.completed != 0) delta.completed_percent = static_cast<double>(delta.completed) * 100 / static_cast<double>(baseline.completed);
    delta.throughput_per_hour = difference(baseline.throughput_per_hour, candidate.throughput_per_hour);
    delta.average_wait_seconds = difference(baseline.average_wait_seconds, candidate.average_wait_seconds);
    if (delta.average_wait_seconds && *baseline.average_wait_seconds != 0)
        delta.average_wait_percent = *delta.average_wait_seconds * 100 / *baseline.average_wait_seconds;
    delta.loading_bay_utilization = difference(baseline.loading_bay_utilization, candidate.loading_bay_utilization);
    delta.worker_utilization = difference(baseline.worker_utilization, candidate.worker_utilization);
    delta.forklift_utilization = difference(baseline.forklift_utilization, candidate.forklift_utilization);
    auto& r = delta.resources;
    r.loading_bays = static_cast<std::int64_t>(candidate_resources.loading_bays) - baseline_resources.loading_bays;
    r.workers = static_cast<std::int64_t>(candidate_resources.workers) - baseline_resources.workers;
    r.forklifts = static_cast<std::int64_t>(candidate_resources.forklifts) - baseline_resources.forklifts;
    for (auto value : {r.loading_bays, r.workers, r.forklifts})
    { r.additions += static_cast<std::uint64_t>(std::max<std::int64_t>(0, value)); r.changes += static_cast<std::uint64_t>(std::abs(value)); }
    return delta;
}
void rank_candidates(std::vector<CandidateResult>& candidates, Objective objective)
{
    (void)objective_name(objective);
    for (auto& result : candidates)
    {
        if (result.metrics.completed > 100000 || result.metrics.waiting > 100000 || result.metrics.started > 100000 ||
            result.metrics.total_wait_ticks > 1000000000000ULL)
            throw std::invalid_argument("Ranking metrics exceed warehouse bounds");
        result.rank.reset(); result.eligible = objective_value(result.metrics, objective).has_value();
        const auto value = objective_value(result.metrics, objective);
        if (value && (!std::isfinite(*value) || *value < 0)) throw std::invalid_argument("Invalid ranking metric");
        result.eligibility_reason = result.eligible ? "" : "Average started queue wait is unavailable (no started shipments).";
    }
    std::vector<CandidateResult*> sorted;
    for (auto& result : candidates) if (result.eligible) sorted.push_back(&result);
    std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b)
    {
        const auto av = *objective_value(a->metrics, objective), bv = *objective_value(b->metrics, objective);
        if (objective == Objective::MinimizeAverageWait)
        {
            const auto left = a->metrics.total_wait_ticks * b->metrics.started;
            const auto right = b->metrics.total_wait_ticks * a->metrics.started;
            if (left != right) return left < right; // Exact rational comparison; products <=1e17.
        }
        else if (av != bv) return objective == Objective::MaximizeCompleted ? av > bv : av < bv;
        if (a->delta.resources.additions != b->delta.resources.additions) return a->delta.resources.additions < b->delta.resources.additions;
        if (a->delta.resources.changes != b->delta.resources.changes) return a->delta.resources.changes < b->delta.resources.changes;
        return a->candidate.name < b->candidate.name;
    });
    for (std::size_t i = 0; i < sorted.size(); ++i) sorted[i]->rank = static_cast<std::uint32_t>(i + 1);
}
AnalysisReport run_comparison(const ComparisonRequest& request)
{
    validate_request(request); // Preflight every requested candidate before any branch runs.
    AnalysisReport report; report.request = request;
    std::sort(report.request.candidates.begin(), report.request.candidates.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    const auto baseline_counts = request.checkpoint.capacity;
    const auto baseline_metrics = execute_candidate(request.checkpoint, baseline_counts);
    report.baseline = result_for({"Baseline", baseline_counts}, baseline_metrics, baseline_counts, baseline_metrics);
    report.baseline.eligible = objective_value(baseline_metrics, request.objective).has_value();
    if (!report.baseline.eligible) report.baseline.eligibility_reason = "Average started queue wait is unavailable (no started shipments).";
    for (const auto& candidate : report.request.candidates)
        report.candidates.push_back(result_for(candidate, baseline_metrics, baseline_counts, execute_candidate(request.checkpoint, candidate.resources)));
    rank_candidates(report.candidates, request.objective);
    auto ranked = report.candidates;
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.rank.value_or(1000) < b.rank.value_or(1000); });
    for (const auto& candidate : ranked) if (candidate.rank) report.ranking.push_back(candidate.candidate.name);
    if (!report.baseline.eligible)
        report.warnings.push_back("Baseline objective metric is unavailable; improvements relative to baseline are inconclusive.");
    else if (!report.ranking.empty() && !improves(baseline_metrics, ranked.front().metrics, request.objective))
        report.warnings.push_back("The best-ranked candidate does not improve on baseline for the selected objective.");
    for (auto resource : resources)
    {
        BottleneckEvidence evidence; evidence.resource = resource;
        evidence.baseline_capacity = warehouse::capacity_for(baseline_counts, resource);
        evidence.tested_capacity = evidence.baseline_capacity + 1; // Counts <=100000; uint32 addition is safe.
        auto counts = baseline_counts; warehouse::set_capacity(counts, resource, evidence.tested_capacity);
        if (total(counts) > 100000 || request.checkpoint.capacity_changes.size() == warehouse::max_capacity_changes)
        {
            evidence.conclusion = BottleneckConclusion::Unavailable;
            evidence.summary = "Add-one intervention cannot be applied within resource/history limits.";
            report.warnings.push_back(std::string{warehouse::resource_name(resource)} + ": " + evidence.summary);
        }
        else
        {
            // Reuse an already measured identical candidate; inputs and boundary are equal.
            const auto found = std::find_if(report.candidates.begin(), report.candidates.end(), [&](const auto& c) { return c.candidate.resources == counts; });
            const auto metrics = found == report.candidates.end() ? execute_candidate(request.checkpoint, counts) : found->metrics;
            evidence.result = result_for({"Add one " + std::string{warehouse::resource_name(resource)}, counts}, baseline_metrics, baseline_counts, metrics);
            const auto before = objective_value(baseline_metrics, request.objective), after = objective_value(metrics, request.objective);
            evidence.conclusion = !before || !after ? BottleneckConclusion::Inconclusive :
                improves(baseline_metrics, metrics, request.objective) ? BottleneckConclusion::LikelyBottleneck : BottleneckConclusion::NoPositiveEffect;
            evidence.summary = "Adding one " + std::string{warehouse::resource_name(resource)} + " at tick " + std::to_string(request.checkpoint.current_tick) +
                " changed completed shipments from " + std::to_string(baseline_metrics.completed) + " to " + std::to_string(metrics.completed) +
                " and final waiting shipments from " + std::to_string(baseline_metrics.waiting) + " to " + std::to_string(metrics.waiting) + ".";
        }
        report.bottlenecks.push_back(std::move(evidence));
    }
    if (std::none_of(report.bottlenecks.begin(), report.bottlenecks.end(), [](const auto& b) { return b.conclusion == BottleneckConclusion::LikelyBottleneck; }))
        report.warnings.push_back("No positive objective effect was observed in add-one probes; joint constraints or the observation horizon may hide benefits.");
    report.limitations = {"Fixed-duration FIFO model; no calibration or financial costs.",
        "Likely bottlenecks apply only to the chosen objective and tested checkpoint/horizon.",
        "Single-resource probes cannot prove or exclude joint constraints.",
        "Started queue-wait averages exclude still-waiting censored shipments; lower averages can hide unfinished work.",
        "Rankings identify the best tested candidates, not a global or financial optimum."};
    return report;
}
} // namespace simforge::analysis
