#pragma once

#include <simforge/simulation/simulation_snapshot.hpp>

#include <filesystem>
#include <optional>
#include <variant>
#include <string_view>
#include <string>
#include <vector>

namespace simforge::scenario
{
inline constexpr std::size_t max_scenario_bytes = 4 * 1024 * 1024;
inline constexpr std::uint64_t max_execution_ticks = 10'000'000;
inline constexpr std::uint32_t max_warehouse_shipments = 100000;

struct SimulationConfig
{
    std::uint32_t tick_rate{60};
    std::uint32_t duration_seconds{10};
    std::uint64_t random_seed{0};
};
struct WarehouseConfig
{
    std::uint32_t loading_bays{0}, workers{0}, forklifts{0}, shipment_interval_seconds{1};
    std::uint32_t processing_duration_seconds{600};
};
struct InitialEntity
{
    std::optional<world::Transform> transform;
    std::optional<world::Velocity> velocity;
};
struct MovementConfig { std::vector<InitialEntity> entities; };
struct ScenarioDefinition
{
    std::uint32_t schema_version{1};
    std::string name;
    SimulationConfig simulation;
    std::variant<WarehouseConfig, MovementConfig> domain;
};

void validate(const ScenarioDefinition& definition);
[[nodiscard]] ScenarioDefinition parse_scenario(std::string_view json);
[[nodiscard]] std::string serialize_scenario(const ScenarioDefinition& definition);
[[nodiscard]] ScenarioDefinition load_scenario(const std::filesystem::path& path);
void save_scenario(const std::filesystem::path& path, const ScenarioDefinition& definition);
[[nodiscard]] std::uint64_t total_ticks(const ScenarioDefinition& definition);
// Constructs movement ECS/clock state only. WarehouseSimulation owns warehouse initialization.
[[nodiscard]] simulation::SimulationSnapshot initial_state(const ScenarioDefinition& definition);
} // namespace simforge::scenario
