#pragma once
#include "comparison_json.hpp"
#include <simforge/analysis/statistical_experiment.hpp>
namespace simforge::analysis::detail {
Json statistical_plan_json(const ReplicationPlan&);
ReplicationPlan statistical_plan_value(const Json&);
Json statistical_metrics_json(const warehouse::WarehouseMetrics&);
warehouse::WarehouseMetrics statistical_metrics_value(const Json&);
Json statistical_summary_json(const PairedSummary&);
void validate_statistical_metrics(const warehouse::WarehouseMetrics&,const scenario::ScenarioDefinition&);
}
