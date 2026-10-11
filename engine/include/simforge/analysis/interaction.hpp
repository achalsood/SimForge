#pragma once
#include <simforge/analysis/sensitivity.hpp>

namespace simforge::analysis {
inline constexpr std::size_t max_interaction_axis=10, max_interaction_cells=100;
inline constexpr std::uint64_t max_interaction_executions=10'100;
struct InteractionLimits {
    std::uint32_t axis{10},cells{100};
    std::uint64_t executions{max_interaction_executions},arrival_work{max_statistical_arrival_work};
};
// Value object copied into the worker; no editor or viewer references survive submission.
struct InteractionRequest {
    std::uint32_t schema_version{1};std::uint64_t id{0},source_id{0};
    scenario::ScenarioDefinition source;
    SweepParameter x{SweepParameter::Forklifts},y{SweepParameter::Workers};
    std::vector<std::uint32_t> x_values,y_values;
    StatisticalMetric objective{StatisticalMetric::Completed};
    bool multi_seed{false};ReplicationPlan plan;
    InteractionLimits limits;
};
scenario::ScenarioDefinition tested_definition(const InteractionRequest&,std::uint32_t x,std::uint32_t y,std::uint64_t seed);
void validate_interaction_request(const InteractionRequest&);
std::uint64_t interaction_execution_count(const InteractionRequest&);
std::size_t interaction_index(std::size_t x,std::size_t y,std::size_t width);
std::uint64_t interaction_cell_id(const InteractionRequest&,std::uint32_t x,std::uint32_t y);
std::optional<double> interaction_contrast(std::optional<double> v00,std::optional<double> v10,std::optional<double> v01,std::optional<double> v11);
struct InteractionReplication {
    std::uint64_t seed{0};warehouse::WarehouseMetrics baseline;
    std::vector<warehouse::WarehouseMetrics> cells; // y-major, x advances first
};
struct InteractionCell {
    std::uint64_t id{0};std::size_t x_index{0},y_index{0};std::uint32_t x{0},y{0};bool baseline{false};
    std::array<std::optional<double>,statistical_metric_count> means;
    std::array<PairedSummary,statistical_metric_count> effects;
};
struct InteractionRectangle {
    std::size_t x_index{0},y_index{0};std::uint32_t x0{0},x1{0},y0{0},y1{0};
    std::array<std::size_t,4> cells; // 00,10,01,11
    std::array<PairedSummary,statistical_metric_count> effects;
    std::vector<std::array<std::optional<double>,statistical_metric_count>> per_seed;
    std::string interpretation;
};
struct InteractionReport {
    InteractionRequest request;
    std::vector<InteractionReplication> replications;
    std::vector<InteractionCell> cells;
    std::vector<InteractionRectangle> interactions;
    std::array<std::optional<double>,statistical_metric_count> baseline_means;
    std::vector<std::uint64_t> best_cells; // all exact ties, never a global optimum
    std::uint64_t simulation_executions{0},processed_events{0};double aggregation_ms{0};
    std::vector<std::string> limitations;
};
void aggregate_interaction_report(InteractionReport&);
InteractionReport run_interaction(const InteractionRequest&,const simulation::ExecutionControl* control=nullptr);
std::string serialize_interaction_request(const InteractionRequest&);
InteractionRequest parse_interaction_request(std::string_view);
InteractionRequest load_interaction_experiment(const std::filesystem::path&);
std::string serialize_interaction_report(const InteractionReport&);
InteractionReport parse_interaction_report(std::string_view);
enum class InteractionCsv { Summary, Seeds, Interactions, InteractionSeeds };
std::string serialize_interaction_csv(const InteractionReport&,InteractionCsv kind=InteractionCsv::Summary);
void save_interaction_report(const std::filesystem::path&,const InteractionReport&);
void save_interaction_csv(const std::filesystem::path&,const InteractionReport&,InteractionCsv kind=InteractionCsv::Summary);
}
