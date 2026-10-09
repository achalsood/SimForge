#pragma once

#include <simforge/warehouse/warehouse_simulation.hpp>

namespace simforge::analysis
{
using warehouse::ResourceCounts;
using warehouse::ResourceType;
enum class Objective : std::uint8_t { MaximizeCompleted, MinimizeAverageWait, MinimizeWaiting };
enum class BottleneckConclusion : std::uint8_t { LikelyBottleneck, NoPositiveEffect, Unavailable, Inconclusive };
inline constexpr std::size_t max_candidates = 50;
inline constexpr std::size_t max_experiment_bytes = 64 * 1024;
inline constexpr std::size_t max_report_bytes = 64 * 1024 * 1024;
struct ScenarioCandidate
{
    std::string name;
    ResourceCounts resources{};
    bool operator==(const ScenarioCandidate&) const = default;
};
struct ExperimentDefinition
{
    std::uint32_t schema_version{1};
    std::filesystem::path scenario;
    std::uint64_t checkpoint_seconds{0};
    Objective objective{Objective::MaximizeCompleted};
    std::vector<ScenarioCandidate> candidates;
};
struct ComparisonRequest
{
    std::uint32_t schema_version{1};
    std::string scenario_identity;
    warehouse::WarehouseSnapshot checkpoint;
    Objective objective{Objective::MaximizeCompleted};
    std::vector<ScenarioCandidate> candidates;
    bool operator==(const ComparisonRequest&) const = default;
};
struct ResourceDelta
{
    std::int64_t loading_bays{0}, workers{0}, forklifts{0};
    std::uint64_t additions{0}, changes{0}; // Positive additions; total absolute changes.
    bool operator==(const ResourceDelta&) const = default;
};
struct MetricDelta
{
    std::int64_t completed{0}, waiting{0};
    std::optional<double> completed_percent, throughput_per_hour, average_wait_seconds, average_wait_percent;
    std::optional<double> loading_bay_utilization, worker_utilization, forklift_utilization;
    ResourceDelta resources;
    bool operator==(const MetricDelta&) const = default;
};
struct CandidateResult
{
    ScenarioCandidate candidate;
    warehouse::WarehouseMetrics metrics;
    MetricDelta delta;
    bool eligible{true};
    std::optional<std::uint32_t> rank;
    std::string eligibility_reason;
    bool operator==(const CandidateResult&) const = default;
};
struct BottleneckEvidence
{
    ResourceType resource{ResourceType::Forklift};
    std::uint32_t baseline_capacity{0}, tested_capacity{0};
    BottleneckConclusion conclusion{BottleneckConclusion::Inconclusive};
    std::optional<CandidateResult> result;
    std::string summary;
    bool operator==(const BottleneckEvidence&) const = default;
};
struct AnalysisReport
{
    std::uint32_t schema_version{1};
    ComparisonRequest request; // Shared checkpoint once; no per-branch shipment copies.
    CandidateResult baseline;
    std::vector<CandidateResult> candidates; // Canonical name order.
    std::vector<std::string> ranking; // Eligible candidate names, best tested first.
    std::vector<BottleneckEvidence> bottlenecks; // Bay, worker, forklift.
    std::vector<std::string> warnings, limitations;
    bool operator==(const AnalysisReport&) const = default;
};
[[nodiscard]] std::string_view objective_name(Objective objective);
[[nodiscard]] Objective parse_objective(std::string_view name);
void validate_experiment(const ExperimentDefinition& experiment);
void validate_request(const ComparisonRequest& request);
// Preflight owned checkpoints without copying their shipment/history payloads.
void validate_checkpoint_candidates(const warehouse::WarehouseSnapshot& checkpoint, std::span<const ScenarioCandidate> candidates);
[[nodiscard]] ComparisonRequest prepare_request(const scenario::ScenarioDefinition& scenario,
    std::uint64_t checkpoint_tick, Objective objective, std::vector<ScenarioCandidate> candidates, const simulation::ExecutionControl* control = nullptr);
[[nodiscard]] ComparisonRequest load_experiment(const std::filesystem::path& path);
[[nodiscard]] ExperimentDefinition parse_experiment(std::string_view json);
[[nodiscard]] std::string serialize_experiment(const ExperimentDefinition& experiment);
// Public phases allow benchmarks/tools to separate simulation from derived analysis.
[[nodiscard]] warehouse::WarehouseMetrics execute_candidate(const warehouse::WarehouseSnapshot& checkpoint,
    ResourceCounts resources, const simulation::ExecutionControl* control = nullptr,
    warehouse::WarehouseSnapshot* final_snapshot = nullptr);
[[nodiscard]] MetricDelta compare_metrics(const warehouse::WarehouseMetrics& baseline,
    const warehouse::WarehouseMetrics& candidate, ResourceCounts baseline_resources, ResourceCounts candidate_resources);
void rank_candidates(std::vector<CandidateResult>& candidates, Objective objective);
[[nodiscard]] AnalysisReport run_comparison(const ComparisonRequest& request, const simulation::ExecutionControl* control = nullptr,
    std::vector<warehouse::WarehouseSnapshot>* final_snapshots = nullptr);
[[nodiscard]] std::string serialize_report(const AnalysisReport& report);
[[nodiscard]] std::string serialize_csv(const AnalysisReport& report);
// Same CSV columns/escaping for independently executed scenarios with matching intervals.
[[nodiscard]] std::string serialize_comparison_csv(const CandidateResult& baseline, std::span<const CandidateResult> candidates);
// Validates schema, reruns the embedded inputs, and checks every exported field.
[[nodiscard]] AnalysisReport replay_report(std::string_view json);
void save_report(const std::filesystem::path& path, const AnalysisReport& report);
void save_csv(const std::filesystem::path& path, const AnalysisReport& report);
} // namespace simforge::analysis
