#pragma once
#include <simforge/warehouse/warehouse_simulation.hpp>

namespace simforge::analysis::detail {
// Shared compact evaluation for sweeps and grids. Histories and snapshots are not retained.
inline warehouse::WarehouseMetrics evaluate(const scenario::ScenarioDefinition& definition,
    const simulation::ExecutionControl* control,std::uint64_t& executions,std::uint64_t& events) {
    warehouse::WarehouseSimulation simulation(definition,control);simulation.run(control);
    ++executions;events+=simulation.processed_events();return simulation.metrics();
}
}
