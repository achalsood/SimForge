#pragma once
#include <simforge/analysis/statistical_experiment.hpp>

namespace simforge::analysis {
inline constexpr std::size_t max_sweep_points=50;
enum class SweepParameter : std::uint8_t { Forklifts, Workers, LoadingBays, ArrivalInterval, ProcessingDuration };
std::string_view parameter_name(SweepParameter parameter);
SweepParameter parse_parameter(std::string_view name);
std::uint32_t parameter_value(const scenario::ScenarioDefinition&,SweepParameter);
std::vector<std::uint32_t> sweep_range(std::uint32_t minimum,std::uint32_t maximum,std::uint32_t step);
struct SensitivityRequest {
    std::uint32_t schema_version{1};std::uint64_t id{0},source_id{0};
    scenario::ScenarioDefinition source;
    SweepParameter parameter{SweepParameter::Forklifts};
    std::vector<std::uint32_t> values;
    StatisticalMetric objective{StatisticalMetric::Completed};
    bool multi_seed{false};ReplicationPlan plan;
    double practical_threshold{1.0}; // favorable objective units per parameter unit
};
scenario::ScenarioDefinition tested_definition(const SensitivityRequest&,std::uint32_t value,std::uint64_t seed);
void validate_sensitivity_request(const SensitivityRequest&);
struct SensitivityReplication {
    std::uint64_t seed{0};warehouse::WarehouseMetrics baseline;
    std::vector<warehouse::WarehouseMetrics> points;
    std::array<std::optional<warehouse::WarehouseMetrics>,3> probes;
};
struct SensitivityPoint {
    std::uint64_t id{0};std::uint32_t value{0};bool baseline{false};
    std::array<PairedSummary,statistical_metric_count> against_baseline,adjacent;
    std::array<std::optional<double>,statistical_metric_count> means;
    std::array<std::optional<double>,statistical_metric_count> mean_lower,mean_upper;
    std::optional<double> marginal_per_unit;
    std::string marginal_interpretation;
};
struct SensitivityReport {
    SensitivityRequest request;
    std::vector<SensitivityReplication> replications;
    std::vector<SensitivityPoint> points;
    std::array<std::array<PairedSummary,statistical_metric_count>,3> probes;
    std::array<std::optional<double>,statistical_metric_count> baseline_means;
    std::optional<std::uint64_t> best_point;
    std::uint64_t simulation_executions{0},processed_events{0};
    double aggregation_ms{0};
    std::vector<std::string> limitations;
};
void aggregate_sensitivity_report(SensitivityReport&);
SensitivityReport run_sensitivity(const SensitivityRequest&,const simulation::ExecutionControl* control=nullptr);
std::string serialize_sensitivity_request(const SensitivityRequest&);
SensitivityRequest parse_sensitivity_request(std::string_view);
SensitivityRequest load_sensitivity_experiment(const std::filesystem::path&);
std::string serialize_sensitivity_report(const SensitivityReport&);
SensitivityReport parse_sensitivity_report(std::string_view);
enum class SensitivityCsv { Summary, Seeds, Marginal };
std::string serialize_sensitivity_csv(const SensitivityReport&,SensitivityCsv kind=SensitivityCsv::Summary);
void save_sensitivity_report(const std::filesystem::path&,const SensitivityReport&);
void save_sensitivity_csv(const std::filesystem::path&,const SensitivityReport&,SensitivityCsv kind=SensitivityCsv::Summary);
}
