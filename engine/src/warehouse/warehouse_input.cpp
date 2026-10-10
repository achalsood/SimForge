#include <simforge/warehouse/warehouse_input.hpp>

#include <algorithm>
#include <stdexcept>

namespace simforge::warehouse
{
void validate_command(const WarehouseCommand& command)
{
    if ((command.type != WarehouseCommandType::SetForkliftCapacity && command.type != WarehouseCommandType::SetResourceCapacity) || command.capacity > 100000 ||
        command.tick > scenario::max_execution_ticks || command.sequence >= max_capacity_changes)
        throw std::invalid_argument("Invalid warehouse capacity command");
    (void)resource_name(command.resource);
    if (command.type == WarehouseCommandType::SetForkliftCapacity && command.resource != ResourceType::Forklift)
        throw std::invalid_argument("Legacy forklift command must target forklifts");
}
void validate_recording(const WarehouseRecording& recording)
{
    if ((recording.schema_version != 1 && recording.schema_version != 2 && recording.schema_version != 3) || recording.commands.size() > max_capacity_changes)
        throw std::invalid_argument("Invalid warehouse recording version/command count");
    validate_snapshot(recording.initial_snapshot); validate_snapshot(recording.expected_final);
    if (recording.schema_version == 1 && (recording.initial_snapshot.version != 1 || recording.expected_final.version != 1))
        throw std::invalid_argument("v1 recording requires v1 warehouse snapshots");
    if((recording.initial_snapshot.version==3 || recording.expected_final.version==3) != (recording.schema_version==3) ||
        (recording.schema_version==3 && (recording.initial_snapshot.version!=3 || recording.expected_final.version!=3)))
        throw std::invalid_argument("Workload recordings require v3 snapshots and recording schema");
    if (recording.scenario_identity != recording.initial_snapshot.scenario_identity ||
        recording.scenario_identity != recording.expected_final.scenario_identity ||
        recording.initial_tick != recording.initial_snapshot.current_tick ||
        recording.end_tick != recording.expected_final.current_tick || recording.end_tick < recording.initial_tick)
        throw std::invalid_argument("Warehouse recording scenario/time mismatch");
    const auto& initial = recording.initial_snapshot.capacity_changes;
    const auto& final = recording.expected_final.capacity_changes;
    if (final.size() != initial.size() + recording.commands.size() ||
        !std::equal(initial.begin(), initial.end(), final.begin()))
        throw std::invalid_argument("Warehouse recording capacity history mismatch");
    auto previous_tick = recording.initial_tick;
    for (std::size_t i = 0; i < recording.commands.size(); ++i)
    {
        const auto& command = recording.commands[i]; validate_command(command);
        if (command.sequence != i || command.tick < previous_tick || command.tick > recording.end_tick ||
            final[initial.size() + i].tick != command.tick || final[initial.size() + i].capacity != command.capacity ||
            final[initial.size() + i].resource != command.resource ||
            (recording.schema_version == 1 && command.type != WarehouseCommandType::SetForkliftCapacity))
            throw std::invalid_argument("Warehouse recording command ordering/history mismatch");
        previous_tick = command.tick;
    }
}
WarehouseInputRecorder::WarehouseInputRecorder(WarehouseSimulation& runtime) : runtime_{runtime}
{
    recording_.initial_snapshot = runtime.capture_snapshot(); validate_snapshot(recording_.initial_snapshot);
    recording_.scenario_identity = recording_.initial_snapshot.scenario_identity;
    recording_.initial_tick = runtime.clock().tick_count();
    recording_.schema_version = recording_.initial_snapshot.version;
}
void WarehouseInputRecorder::submit(const WarehouseCommand& command)
{
    validate_command(command);
    if (command.tick != runtime_.clock().tick_count() || command.sequence != recording_.commands.size() ||
        recording_.commands.size() == max_capacity_changes)
        throw std::invalid_argument("Warehouse command must use current tick and next sequence");
    recording_.commands.push_back(command);
    try { runtime_.set_resource_capacity(command.resource, command.capacity); }
    catch (...) { recording_.commands.pop_back(); throw; }
    if (command.type == WarehouseCommandType::SetResourceCapacity && recording_.schema_version!=3) recording_.schema_version = 2;
}
void WarehouseInputRecorder::set_resource_capacity(ResourceType resource, std::uint32_t capacity)
{
    submit({runtime_.clock().tick_count(), recording_.commands.size(), WarehouseCommandType::SetResourceCapacity, capacity, resource});
}
void WarehouseInputRecorder::set_forklift_capacity(std::uint32_t forklifts)
{
    submit({runtime_.clock().tick_count(), recording_.commands.size(), WarehouseCommandType::SetForkliftCapacity, forklifts});
}
void WarehouseInputRecorder::advance_to(std::uint64_t tick) { runtime_.advance_to(tick); }
WarehouseRecording WarehouseInputRecorder::capture_recording() const
{
    auto result = recording_; result.end_tick = runtime_.clock().tick_count();
    result.expected_final = runtime_.capture_snapshot(); validate_recording(result); return result;
}
void replay_recording(const WarehouseRecording& recording, WarehouseSimulation& destination)
{
    validate_recording(recording);
    if (auto difference = first_difference(recording.initial_snapshot, destination.capture_snapshot()))
        throw std::invalid_argument("Incompatible warehouse replay initial state: " + *difference);
    auto replacement = WarehouseSimulation::create_from(recording.initial_snapshot);
    for (const auto& command : recording.commands)
    {
        replacement.advance_to(command.tick); // Settle all due events before commands, including tick zero.
        replacement.set_resource_capacity(command.resource, command.capacity);
    }
    // A no-op recording may preserve the untouched constructor boundary.
    if (recording.expected_final.settled) replacement.advance_to(recording.end_tick);
    if (auto difference = first_difference(recording.expected_final, replacement.capture_snapshot()))
        throw std::invalid_argument("Warehouse replay failed: " + *difference);
    destination.restore_snapshot(replacement.capture_snapshot());
}
} // namespace simforge::warehouse
