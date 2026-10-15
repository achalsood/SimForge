#pragma once
#include <simforge/analysis/statistical_experiment.hpp>
#include <simforge/warehouse/warehouse_input.hpp>

namespace simforge::analysis::checkpoint {
inline constexpr std::uint32_t method_version=1;
inline constexpr std::size_t metric_count=8, max_source_commands=32, max_report_bytes=4*1024*1024;
enum class Action : std::uint8_t { Set, Increase, Decrease };
struct Intervention {
    warehouse::ResourceType resource{warehouse::ResourceType::Forklift};
    Action action{Action::Increase};
    std::uint32_t value{1};
    bool operator==(const Intervention&) const = default;
};
struct Request {
    std::uint32_t version{1}; std::uint64_t id{0};
    scenario::ScenarioDefinition source;
    std::uint64_t checkpoint_tick{0};
    Intervention intervention;
    std::vector<warehouse::WarehouseCommand> source_commands;
    ReplicationPlan plan;
    Objective objective{Objective::MaximizeCompleted};
};
struct Timings {
    double prefix_ms{0},snapshot_ms{0},branch_ms{0},control_ms{0},intervention_ms{0},aggregation_ms{0},total_ms{0};
};
struct Outcome {
    std::uint64_t seed{0};
    std::string checkpoint_digest,workload_digest;
    std::uint64_t checkpoint_events{0};
    warehouse::ResourceCounts checkpoint_capacity;
    warehouse::WarehouseCommand applied;
    warehouse::WarehouseMetrics checkpoint,control,intervention;
    // Cumulative available resource ticks are sufficient statistics for valid interval utilization.
    std::array<std::uint64_t,3> checkpoint_available{},control_available{},intervention_available{};
    bool operator==(const Outcome&) const = default;
};
struct Report {
    Request request;
    std::vector<Outcome> outcomes;
    std::array<PairedSummary,metric_count> end_metrics,post_metrics;
    std::vector<std::string> limitations;
    std::uint64_t prefix_executions{0},control_executions{0},intervention_executions{0};
    Timings timings; // Execution measurements only; excluded from immutable evidence/identity.
};
struct Branches {
    warehouse::WarehouseSnapshot checkpoint,control,intervention;
    Outcome outcome;
};
void validate(const Request&);
std::string_view metric_name(std::size_t);
std::string_view metric_unit(std::size_t);
std::optional<double> metric_value(const Outcome&,bool intervention,std::size_t metric,bool post,const Request&);
bool higher_is_better(std::size_t); // Utilization is descriptive: no preferred direction.
Branches reconstruct(const Request&,std::uint64_t seed,const simulation::ExecutionControl* =nullptr,Timings* =nullptr);
void aggregate(Report&);
Report run(const Request&,const simulation::ExecutionControl* =nullptr);
std::string request_json(const Request&);
Request parse_request(std::string_view);
std::string report_json(const Report&);
Report parse_report(std::string_view);
std::string report_csv(const Report&,bool seeds=false);
std::string identity(const Request&);
void save_report(const std::filesystem::path&,const Report&);
void save_csv(const std::filesystem::path&,const Report&,bool seeds=false);
}
