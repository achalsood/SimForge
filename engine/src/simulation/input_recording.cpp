#include <simforge/simulation/input_recording.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>

#include <cmath>
#include <stdexcept>

namespace simforge::simulation
{
namespace
{
[[noreturn]] void invalid(const std::string& field, const std::string& reason)
{
    throw std::invalid_argument(field + ": " + reason);
}
void validate_controls(const CommandPayload& payload, bool& paused)
{
    if (std::holds_alternative<PauseSimulation>(payload))
    {
        if (paused) invalid("PauseSimulation", "clock already paused");
        paused = true;
    }
    if (std::holds_alternative<ResumeSimulation>(payload))
    {
        if (!paused) invalid("ResumeSimulation", "clock is not paused");
        paused = false;
    }
    if (std::holds_alternative<StepSimulation>(payload) && !paused)
        invalid("StepSimulation", "requires pause");
}
void apply(world::World& world, SimulationClock& clock, const CommandPayload& payload)
{
    if (const auto* velocity = std::get_if<SetEntityVelocity>(&payload))
    {
        const auto handle = world.handle_at(velocity->target.index);
        if (handle.generation != velocity->target.generation || !world.has<world::Velocity>(handle))
            invalid("SetEntityVelocity.target", "generation mismatch or missing Velocity");
        world.get<world::Velocity>(handle).linear = velocity->linear;
    }
    else if (std::holds_alternative<PauseSimulation>(payload)) clock.pause();
    else if (std::holds_alternative<ResumeSimulation>(payload)) clock.resume();
    else
    {
        MovementSystem::update(world, clock.fixed_step_seconds());
        clock.single_step();
    }
}
void advance(world::World& world, SimulationClock& clock, std::uint64_t tick)
{
    if (tick < clock.tick_count()) invalid("tick", "cannot advance into the past");
    if (clock.paused() && tick != clock.tick_count()) invalid("tick", "paused clock cannot reach a later command; resume or step at current tick");
    replay_fixed_ticks(world, clock, tick - clock.tick_count());
}
}

void validate_command(const InputCommand& command)
{
    if (command.tick > scenario::max_execution_ticks || command.sequence >= max_recording_commands)
        invalid("command.tick/sequence", "outside recording limits");
    if (command.payload.valueless_by_exception()) invalid("command.type", "invalid variant");
    if (const auto* velocity = std::get_if<SetEntityVelocity>(&command.payload))
    {
        if (velocity->target.index >= world::snapshot_max_slots || velocity->target.generation == 0)
            invalid("SetEntityVelocity.target", "invalid logical entity ID");
        const auto v = velocity->linear;
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            std::abs(v.x) > 1e12 || std::abs(v.y) > 1e12 || std::abs(v.z) > 1e12)
            invalid("SetEntityVelocity.linear", "must be finite and within +/-1e12");
    }
}

void validate_recording(const InputRecording& recording)
{
    if (recording.schema_version != 1) invalid("recording.schema_version", "unsupported version");
    if (recording.commands.size() > max_recording_commands) throw std::length_error("Recording command limit exceeded");
    const auto definition = scenario::parse_scenario(recording.scenario_identity);
    if (scenario::serialize_scenario(definition) != recording.scenario_identity)
        invalid("recording.scenario_identity", "must be canonical scenario JSON");
    if (!std::holds_alternative<scenario::MovementConfig>(definition.domain))
        invalid("recording.domain", "only movement commands are executable");
    validate_snapshot(recording.initial_snapshot); validate_snapshot(recording.expected_final);
    auto clock = recording.initial_snapshot.clock;
    if (clock.ticks != recording.initial_tick || clock.ticks_per_second != definition.simulation.tick_rate ||
        recording.end_tick < recording.initial_tick || recording.end_tick > scenario::total_ticks(definition))
        invalid("recording.initial_tick/end_tick", "incompatible clock or execution budget");
    if (recording.initial_tick == 0)
    {
        if (auto mismatch = first_difference(scenario::initial_state(definition), recording.initial_snapshot))
            invalid("recording.initial_snapshot", *mismatch);
    }
    const auto& initial = recording.initial_snapshot.world;
    std::vector<bool> velocity_present(initial.slots.size(), false);
    for (const auto& record : initial.velocities) velocity_present[record.entity.index] = true;
    for (std::size_t i = 0; i < recording.commands.size(); ++i)
    {
        const auto& command = recording.commands[i];
        validate_command(command);
        if (command.sequence != i || command.tick < clock.ticks || command.tick > recording.end_tick)
            invalid("commands[" + std::to_string(i) + "].tick/sequence", "must follow execution order and tick range");
        if (clock.paused && command.tick > clock.ticks) invalid("commands.tick", "unreachable while paused");
        clock.ticks = command.tick;
        validate_controls(command.payload, clock.paused);
        if (std::holds_alternative<StepSimulation>(command.payload)) ++clock.ticks;
        if (const auto* velocity = std::get_if<SetEntityVelocity>(&command.payload))
        {
            const auto target = velocity->target;
            if (target.index >= initial.slots.size() || !initial.slots[target.index].alive ||
                initial.slots[target.index].generation != target.generation || !velocity_present[target.index])
                invalid("commands[" + std::to_string(i) + "].target", "unavailable logical Velocity target");
        }
    }
    if (clock.ticks > recording.end_tick || (clock.paused && clock.ticks < recording.end_tick))
        invalid("recording.end_tick", "unreachable after commands");
    clock.ticks = recording.end_tick;
    SimulationSnapshot expected_clock{1, {}, clock};
    SimulationSnapshot actual_clock{1, {}, recording.expected_final.clock};
    if (auto mismatch = first_difference(expected_clock, actual_clock)) invalid("recording.expected_final.clock", *mismatch);
}

InputRecorder::InputRecorder(const scenario::ScenarioDefinition& definition, world::World& world, SimulationClock& clock)
    : world_{world}, clock_{clock}, tick_limit_{scenario::total_ticks(definition)}
{
    if (!std::holds_alternative<scenario::MovementConfig>(definition.domain)) invalid("domain", "recording requires movement");
    recording_.scenario_identity = scenario::serialize_scenario(definition);
    recording_.initial_tick = recording_.end_tick = clock.tick_count();
    recording_.initial_snapshot = capture_snapshot(world, clock);
    recording_.expected_final = recording_.initial_snapshot;
    validate_recording(recording_);
}
void InputRecorder::submit(CommandPayload payload)
{
    submit(InputCommand{clock_.tick_count(), recording_.commands.size(), std::move(payload)});
}
void InputRecorder::submit(const InputCommand& command)
{
    validate_command(command);
    if (recording_.commands.size() == max_recording_commands) throw std::length_error("Recording command limit exceeded");
    if (command.tick != clock_.tick_count() || command.sequence != recording_.commands.size())
        invalid("command.tick/sequence", "submit must use current tick and next sequence");
    if (command.tick > tick_limit_ || (std::holds_alternative<StepSimulation>(command.payload) && command.tick == tick_limit_))
        invalid("command.tick", "scenario tick budget exceeded");
    bool paused = clock_.paused(); validate_controls(command.payload, paused);
    if (const auto* velocity = std::get_if<SetEntityVelocity>(&command.payload))
    {
        try
        {
            const auto handle = world_.handle_at(velocity->target.index);
            if (handle.generation != velocity->target.generation || !world_.has<world::Velocity>(handle))
                invalid("command.target", "unavailable generation or Velocity component");
        }
        catch (const std::out_of_range&) { invalid("command.target", "unavailable logical entity"); }
    }
    // Allocate before mutation. Prevalidated commands have no throwing mutation
    // path under the caller's exclusive-access/system-boundary contract.
    recording_.commands.push_back(command);
    try { apply(world_, clock_, command.payload); }
    catch (...) { recording_.commands.pop_back(); throw; }
    recording_.end_tick = clock_.tick_count();
}
void InputRecorder::advance_to(std::uint64_t tick)
{
    if (tick > tick_limit_) invalid("tick", "scenario tick budget exceeded");
    advance(world_, clock_, tick);
    recording_.end_tick = clock_.tick_count();
}
InputRecording InputRecorder::capture_recording() const
{
    auto result = recording_;
    result.end_tick = clock_.tick_count();
    result.expected_final = capture_snapshot(world_, clock_);
    validate_recording(result);
    return result;
}
void replay_recording(const scenario::ScenarioDefinition& definition, const InputRecording& recording,
                      world::World& destination, SimulationClock& destination_clock)
{
    const auto identity = scenario::serialize_scenario(definition);
    if (identity != recording.scenario_identity) invalid("recording.scenario_identity", "scenario mismatch");
    validate_recording(recording);
    world::World world; SimulationClock clock;
    restore_snapshot(world, clock, recording.initial_snapshot);
    // Cursor walks each command once. No JSON parsing or full-record scans per tick.
    for (const auto& command : recording.commands)
    {
        advance(world, clock, command.tick);
        apply(world, clock, command.payload);
    }
    advance(world, clock, recording.end_tick);
    const auto final = capture_snapshot(world, clock);
    if (auto mismatch = first_difference(recording.expected_final, final))
        throw std::runtime_error("Replay divergence: " + *mismatch);
    restore_snapshot(destination, destination_clock, final);
}
} // namespace simforge::simulation
