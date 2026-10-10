#include <simforge/scenario/scenario.hpp>
#include <simforge/world/world.hpp>
#include "json_io.hpp"
#include "workload_json.hpp"

#include <algorithm>
#include <limits>

namespace simforge::scenario
{
using namespace detail;
void validate(const ScenarioDefinition& definition)
{
    if (definition.schema_version != 1) invalid("schema_version", "unsupported version");
    if (definition.domain.valueless_by_exception()) invalid("domain", "invalid domain variant");
    if (definition.name.empty() || definition.name.size() > 256 ||
        std::any_of(definition.name.begin(), definition.name.end(), [](unsigned char c) { return c < 32; }))
        invalid("name", "must contain 1..256 UTF-8 bytes without control characters");
    try { (void)Json(definition.name).dump(); }
    catch (const Json::exception&) { invalid("name", "invalid UTF-8"); }
    const auto& config = definition.simulation;
    if (config.tick_rate == 0 || config.tick_rate > 1000) invalid("simulation.tick_rate", "must be 1..1000");
    if (config.duration_seconds == 0 || config.duration_seconds > 604800)
        invalid("simulation.duration_seconds", "must be 1..604800 integer seconds");
    if (static_cast<std::uint64_t>(config.tick_rate) * config.duration_seconds > max_execution_ticks)
        invalid("simulation.duration_seconds", "tick budget exceeds 10000000");
    if (const auto* warehouse = std::get_if<WarehouseConfig>(&definition.domain))
    {
        if (static_cast<std::uint64_t>(warehouse->loading_bays) + warehouse->workers + warehouse->forklifts > 100000)
            invalid("warehouse", "total resources must not exceed 100000");
        if (warehouse->workload.arrivals==ArrivalMode::Constant && (warehouse->shipment_interval_seconds == 0 || warehouse->shipment_interval_seconds > 604800))
            invalid("warehouse.shipment_interval_seconds", "must be 1..604800 integer seconds");
        if (warehouse->workload.processing==ProcessingMode::Constant && (warehouse->processing_duration_seconds == 0 || warehouse->processing_duration_seconds > 604800))
            invalid("warehouse.processing_duration_seconds", "must be 1..604800 integer seconds");
        workload::validate(*warehouse,config.duration_seconds,config.tick_rate);
    }
    else
    {
        const auto& entities = std::get<MovementConfig>(definition.domain).entities;
        if (entities.size() > 10000) invalid("initial_world.entities", "maximum 10000 entities");
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const auto path = "initial_world.entities[" + std::to_string(i) + "]";
            const auto& entity = entities[i];
            if (!entity.transform && !entity.velocity) invalid(path, "at least one component required");
            if (entity.transform)
            {
                vector_valid(entity.transform->position, path + ".transform.position");
                vector_valid(entity.transform->rotation, path + ".transform.rotation");
                vector_valid(entity.transform->scale, path + ".transform.scale");
            }
            if (entity.velocity) vector_valid(entity.velocity->linear, path + ".velocity.linear");
        }
    }
}

ScenarioDefinition parse_scenario(std::string_view text)
{
    const auto json = parse_json(text, max_scenario_bytes);
    fields(json, {"schema_version", "name", "domain", "simulation"}, {"warehouse", "initial_world"}, "scenario");
    ScenarioDefinition definition;
    definition.schema_version = static_cast<std::uint32_t>(integer(json.at("schema_version"), 1, "schema_version"));
    definition.name = string(json.at("name"), "name");
    const auto& config = json.at("simulation");
    fields(config, {"tick_rate", "duration_seconds", "random_seed"}, {}, "simulation");
    definition.simulation = {
        static_cast<std::uint32_t>(integer(config.at("tick_rate"), 1000, "simulation.tick_rate")),
        static_cast<std::uint32_t>(integer(config.at("duration_seconds"), 604800, "simulation.duration_seconds")),
        integer(config.at("random_seed"), std::numeric_limits<std::uint64_t>::max(), "simulation.random_seed")};
    const auto domain = string(json.at("domain"), "domain");
    if (domain == "warehouse")
    {
        if (!json.contains("warehouse") || json.contains("initial_world")) invalid("warehouse", "required; initial_world is unsupported in warehouse v1");
        const auto& warehouse = json.at("warehouse");
        fields(warehouse, {"loading_bays", "workers", "forklifts"},
               {"shipment_interval_seconds","processing_duration_seconds","workload"}, "warehouse");
        if(warehouse.contains("workload") && (warehouse.contains("shipment_interval_seconds") || warehouse.contains("processing_duration_seconds")))
            invalid("warehouse.workload","cannot coexist with legacy timing fields");
        if(!warehouse.contains("workload") && !warehouse.contains("shipment_interval_seconds")) invalid("warehouse.shipment_interval_seconds","required field missing");
        definition.domain = WarehouseConfig{
            static_cast<std::uint32_t>(integer(warehouse.at("loading_bays"), 100000, "warehouse.loading_bays")),
            static_cast<std::uint32_t>(integer(warehouse.at("workers"), 100000, "warehouse.workers")),
            static_cast<std::uint32_t>(integer(warehouse.at("forklifts"), 100000, "warehouse.forklifts")),
            warehouse.contains("shipment_interval_seconds") ? static_cast<std::uint32_t>(integer(warehouse.at("shipment_interval_seconds"), 604800, "warehouse.shipment_interval_seconds")) : 0,
            warehouse.contains("processing_duration_seconds")
                ? static_cast<std::uint32_t>(integer(warehouse.at("processing_duration_seconds"), 604800, "warehouse.processing_duration_seconds")) : 600};
        if(warehouse.contains("workload")) parse_workload(warehouse.at("workload"),std::get<WarehouseConfig>(definition.domain));
    }
    else if (domain == "movement")
    {
        if (!json.contains("initial_world") || json.contains("warehouse")) invalid("initial_world", "required; warehouse fields conflict with movement");
        const auto& initial = json.at("initial_world");
        fields(initial, {"entities"}, {}, "initial_world");
        const auto& entities = initial.at("entities");
        if (!entities.is_array() || entities.size() > 10000) invalid("initial_world.entities", "must be an array of at most 10000 entities");
        MovementConfig movement;
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const auto path = "initial_world.entities[" + std::to_string(i) + "]";
            const auto& record = entities[i];
            fields(record, {}, {"transform", "velocity"}, path);
            InitialEntity entity;
            if (record.contains("transform"))
            {
                const auto& transform = record.at("transform");
                fields(transform, {"position"}, {"rotation", "scale"}, path + ".transform");
                entity.transform = world::Transform{vector(transform.at("position"), path + ".transform.position"),
                    transform.contains("rotation") ? vector(transform.at("rotation"), path + ".transform.rotation") : world::Vec3{},
                    transform.contains("scale") ? vector(transform.at("scale"), path + ".transform.scale") : world::Vec3{1,1,1}};
            }
            if (record.contains("velocity"))
            {
                const auto& velocity = record.at("velocity");
                fields(velocity, {"linear"}, {}, path + ".velocity");
                entity.velocity = world::Velocity{vector(velocity.at("linear"), path + ".velocity.linear")};
            }
            movement.entities.push_back(entity);
        }
        definition.domain = std::move(movement);
    }
    else invalid("domain", "unsupported domain '" + domain + "'");
    validate(definition);
    return definition;
}

std::string serialize_scenario(const ScenarioDefinition& definition)
{
    validate(definition);
    const auto& config = definition.simulation;
    Json json{{"schema_version", 1}, {"name", definition.name},
        {"simulation", {{"tick_rate", config.tick_rate}, {"duration_seconds", config.duration_seconds}, {"random_seed", config.random_seed}}}};
    if (const auto* warehouse = std::get_if<WarehouseConfig>(&definition.domain))
    {
        json["domain"] = "warehouse";
        json["warehouse"] = {{"loading_bays", warehouse->loading_bays}, {"workers", warehouse->workers},
            {"forklifts", warehouse->forklifts}, {"shipment_interval_seconds", warehouse->shipment_interval_seconds},
            {"processing_duration_seconds", warehouse->processing_duration_seconds}};
        if(workload::advanced(*warehouse)) {
            json["warehouse"].erase("shipment_interval_seconds");json["warehouse"].erase("processing_duration_seconds");
            json["warehouse"]["workload"]=workload_json(*warehouse);
        }
    }
    else
    {
        json["domain"] = "movement";
        auto& entities = json["initial_world"]["entities"] = Json::array();
        for (const auto& entity : std::get<MovementConfig>(definition.domain).entities)
        {
            Json record = Json::object();
            if (entity.transform) record["transform"] = {{"position", vector_json(entity.transform->position)},
                {"rotation", vector_json(entity.transform->rotation)}, {"scale", vector_json(entity.transform->scale)}};
            if (entity.velocity) record["velocity"] = {{"linear", vector_json(entity.velocity->linear)}};
            entities.push_back(std::move(record));
        }
    }
    const auto result = json.dump();
    if (result.size() > max_scenario_bytes) throw std::length_error("Canonical scenario exceeds byte limit");
    return result;
}
ScenarioDefinition load_scenario(const std::filesystem::path& path) { return parse_scenario(load_text(path, max_scenario_bytes)); }
void save_scenario(const std::filesystem::path& path, const ScenarioDefinition& definition) { save_text(path, serialize_scenario(definition)); }
std::uint64_t total_ticks(const ScenarioDefinition& definition)
{
    validate(definition);
    return static_cast<std::uint64_t>(definition.simulation.tick_rate) * definition.simulation.duration_seconds;
}
simulation::SimulationSnapshot initial_state(const ScenarioDefinition& definition)
{
    validate(definition);
    const auto* movement = std::get_if<MovementConfig>(&definition.domain);
    if (!movement) invalid("domain", "initial_state constructs only movement ECS snapshots; use WarehouseSimulation for warehouse execution");
    world::World world;
    world.reserve(movement->entities.size());
    for (const auto& record : movement->entities)
    {
        const auto entity = world.create_entity();
        if (record.transform) world.emplace<world::Transform>(entity, *record.transform);
        if (record.velocity) world.emplace<world::Velocity>(entity, *record.velocity);
    }
    simulation::SimulationClock clock{definition.simulation.tick_rate};
    return simulation::capture_snapshot(world, clock);
}
} // namespace simforge::scenario
