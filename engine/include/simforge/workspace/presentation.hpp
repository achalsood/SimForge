#pragma once
#include <simforge/workspace/scenario_workspace.hpp>

namespace simforge::workspace
{
// Presentation only. The authoritative rank is computed by simforge_analysis.
[[nodiscard]] const analysis::CandidateResult* best_tested(const Comparison& report);
struct MetricBars { double baseline{0}, candidate{0}; bool available{false}; };
// Fractions share a zero origin and common maximum. Undefined values stay N/A.
[[nodiscard]] MetricBars metric_bars(std::optional<double> baseline, std::optional<double> candidate);
struct FieldPresentation { const char* label; const char* unit; const char* description; };
inline constexpr std::array<FieldPresentation,9> field_presentation{{
    {"Scenario name","","A distinct name for this tested configuration."},
    {"Loading bays","bays","Available loading positions."},
    {"Workers","people","Aggregate service capacity."},
    {"Forklifts","vehicles","Aggregate handling capacity."},
    {"Shipment arrival interval","seconds","Time between incoming shipments."},
    {"Shipment processing time","seconds","Service time for each shipment."},
    {"Run duration","seconds","The shared comparison horizon."},
    {"Tick rate","Hz","Fixed simulation ticks per second."},
    {"Random seed","integer","Retained for reproducibility; no randomness in this model."}
}};
}
