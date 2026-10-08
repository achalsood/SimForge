#pragma once

#include <simforge/scenario/scenario.hpp>

namespace simforge::simulation
{
struct SetEntityVelocity { world::EntityId target; world::Vec3 linear; };
struct PauseSimulation {};
struct ResumeSimulation {};
struct StepSimulation {};
using CommandPayload = std::variant<SetEntityVelocity, PauseSimulation, ResumeSimulation, StepSimulation>;
struct InputCommand
{
    std::uint64_t tick{0}, sequence{0};
    CommandPayload payload;
};
struct InputRecording
{
    std::uint32_t schema_version{1};
    std::string scenario_identity; // Exact normalized scenario, not a hash.
    std::uint64_t initial_tick{0}, end_tick{0};
    SimulationSnapshot initial_snapshot;
    SimulationSnapshot expected_final;
    std::vector<InputCommand> commands;
};
inline constexpr std::size_t max_recording_bytes = 32 * 1024 * 1024;
inline constexpr std::size_t max_recording_commands = 100000;
void validate_command(const InputCommand& command);
void validate_recording(const InputRecording& recording);

// Caller owns World/clock and applies no unrecorded mutations during a session.
class InputRecorder
{
public:
    InputRecorder(const scenario::ScenarioDefinition& definition, world::World& world, SimulationClock& clock);
    void submit(const InputCommand& command); // Exactly current tick and next sequence.
    void submit(CommandPayload payload);
    void advance_to(std::uint64_t tick); // Explicit ticks; honors pause, bypasses scale.
    [[nodiscard]] InputRecording capture_recording() const;
    [[nodiscard]] std::size_t command_count() const noexcept { return recording_.commands.size(); }
private:
    world::World& world_;
    SimulationClock& clock_;
    InputRecording recording_;
    std::uint64_t tick_limit_;
};

// Runs from embedded initial state in isolation, verifies expected final state,
// then restores destination atomically. All old destination handles invalidate.
void replay_recording(const scenario::ScenarioDefinition& definition, const InputRecording& recording,
                      world::World& destination, SimulationClock& clock);
[[nodiscard]] std::string serialize_recording(const InputRecording& recording);
[[nodiscard]] InputRecording parse_recording(std::string_view json);
[[nodiscard]] InputRecording load_recording(const std::filesystem::path& path);
void save_recording(const std::filesystem::path& path, const InputRecording& recording);
} // namespace simforge::simulation
