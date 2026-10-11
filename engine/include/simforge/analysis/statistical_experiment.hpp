#pragma once
#include <simforge/analysis/scenario_comparison.hpp>
#include <array>

namespace simforge::analysis {
inline constexpr std::uint32_t statistical_method_version=1;
inline constexpr std::size_t max_replications=100, max_statistical_candidates=20;
inline constexpr std::uint64_t max_statistical_arrival_work=20'000'000;
enum class SeedPolicy : std::uint8_t { Sequential, Explicit };
struct ReplicationPlan {
    std::uint32_t count{20};
    SeedPolicy policy{SeedPolicy::Sequential};
    std::uint64_t base_seed{1001};
    std::vector<std::uint64_t> explicit_seeds;
    std::uint32_t generator_version{scenario::workload_generator_version}, method_version{statistical_method_version};
    double confidence{0.95};
    bool operator==(const ReplicationPlan&) const = default;
};
enum class StatisticalMetric : std::uint8_t { Completed, Waiting, Throughput, AverageWait, MaximumWait };
inline constexpr std::size_t statistical_metric_count=5;
std::string_view metric_name(StatisticalMetric metric);
std::string_view metric_unit(StatisticalMetric metric);
bool higher_is_better(StatisticalMetric metric);
StatisticalMetric objective_metric(Objective objective);
std::optional<double> metric_value(const warehouse::WarehouseMetrics& metrics,StatisticalMetric metric);
bool workload_varies(const scenario::WarehouseConfig& config) noexcept;
std::vector<std::uint64_t> replication_seeds(const ReplicationPlan& plan);
double student_t_critical_95(std::size_t degrees_of_freedom);
struct PairedSummary {
    std::uint32_t count{0},defined{0},positive{0},negative{0},zero{0};
    std::optional<double> baseline_mean,candidate_mean,mean_difference,standard_deviation,standard_error,
        lower,upper,minimum,maximum,median,favorable_fraction,relative_change_percent;
    std::string diagnostic;
    bool operator==(const PairedSummary&) const = default;
};
PairedSummary summarize_pairs(std::span<const std::optional<double>> baseline,
    std::span<const std::optional<double>> candidate,bool higher,bool stochastic=true);
struct StatisticalCandidate {
    std::uint64_t id{0};scenario::ScenarioDefinition definition;
};
struct StatisticalRequest {
    std::uint32_t schema_version{1};std::uint64_t id{0};
    scenario::ScenarioDefinition baseline;
    std::vector<StatisticalCandidate> candidates;
    Objective objective{Objective::MaximizeCompleted};
    ReplicationPlan plan;
};
struct ReplicationOutcome {
    std::uint64_t seed{0};warehouse::WarehouseMetrics baseline;
    std::vector<warehouse::WarehouseMetrics> candidates;
    std::array<std::optional<warehouse::WarehouseMetrics>,3> probes;
    bool operator==(const ReplicationOutcome&) const = default;
};
struct StatisticalResult {
    std::uint64_t candidate_id{0};std::string name;
    std::array<PairedSummary,statistical_metric_count> metrics;
    std::optional<std::uint32_t> rank;
    bool operator==(const StatisticalResult&) const = default;
};
struct StatisticalReport {
    StatisticalRequest request;
    std::vector<ReplicationOutcome> replications;
    std::vector<StatisticalResult> candidates;
    std::array<std::array<PairedSummary,statistical_metric_count>,3> probes;
    std::vector<std::string> limitations;
    std::uint64_t simulation_executions{0};
};
void validate_statistical_request(const StatisticalRequest& request);
void aggregate_statistical_report(StatisticalReport& report);
StatisticalReport run_statistical_experiment(const StatisticalRequest& request,const simulation::ExecutionControl* control=nullptr);
std::string serialize_statistical_report(const StatisticalReport& report);
StatisticalReport parse_statistical_report(std::string_view json);
StatisticalReport replay_statistical_report(std::string_view json);
std::string serialize_statistical_csv(const StatisticalReport& report,bool per_seed=false);
void save_statistical_report(const std::filesystem::path& path,const StatisticalReport& report);
void save_statistical_csv(const std::filesystem::path& path,const StatisticalReport& report,bool per_seed=false);
std::string serialize_statistical_request(const StatisticalRequest& request);
StatisticalRequest parse_statistical_request(std::string_view json);
StatisticalRequest load_statistical_experiment(const std::filesystem::path& path);
}
