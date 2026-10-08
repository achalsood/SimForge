#include <simforge/simulation/input_recording.hpp>
#include "json_io.hpp"

namespace simforge::simulation
{
namespace
{
using namespace scenario::detail;
std::string encode(const SimulationSnapshot& snapshot)
{
    constexpr std::string_view digits = "0123456789abcdef";
    const auto bytes = serialize_snapshot(snapshot);
    if (bytes.size() > max_recording_bytes / 2) throw std::length_error("Recording snapshot exceeds size limit");
    std::string result; result.reserve(bytes.size() * 2);
    for (auto byte : bytes)
    {
        const auto value = std::to_integer<unsigned>(byte);
        result.push_back(digits[value >> 4]); result.push_back(digits[value & 15]);
    }
    return result;
}
SimulationSnapshot decode(const Json& value, const std::string& path)
{
    const auto text = string(value, path);
    if (text.size() % 2 != 0 || text.size() > max_recording_bytes) invalid(path, "invalid snapshot hex length");
    const auto digit = [&](char c) -> unsigned
    {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        invalid(path, "snapshot must use lowercase hexadecimal");
    };
    std::vector<std::byte> bytes; bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::byte>((digit(text[i]) << 4) | digit(text[i + 1])));
    try { return deserialize_snapshot(bytes); }
    catch (const std::invalid_argument& error) { invalid(path, error.what()); }
}
Json command_json(const InputCommand& command)
{
    Json result{{"tick", command.tick}, {"sequence", command.sequence}};
    if (const auto* velocity = std::get_if<SetEntityVelocity>(&command.payload))
    {
        result["type"] = "set_entity_velocity";
        result["target"] = {{"index", velocity->target.index}, {"generation", velocity->target.generation}};
        result["linear"] = vector_json(velocity->linear);
    }
    else if (std::holds_alternative<PauseSimulation>(command.payload)) result["type"] = "pause_simulation";
    else if (std::holds_alternative<ResumeSimulation>(command.payload)) result["type"] = "resume_simulation";
    else result["type"] = "step_simulation";
    return result;
}
InputCommand command_parse(const Json& json, const std::string& path)
{
    fields(json, {"tick", "sequence", "type"}, {"target", "linear"}, path);
    InputCommand command;
    command.tick = integer(json.at("tick"), scenario::max_execution_ticks, path + ".tick");
    command.sequence = integer(json.at("sequence"), max_recording_commands - 1, path + ".sequence");
    const auto type = string(json.at("type"), path + ".type");
    if (type == "set_entity_velocity")
    {
        fields(json, {"tick", "sequence", "type", "target", "linear"}, {}, path);
        const auto& target = json.at("target");
        fields(target, {"index", "generation"}, {}, path + ".target");
        command.payload = SetEntityVelocity{{
            static_cast<std::uint32_t>(integer(target.at("index"), world::snapshot_max_slots - 1, path + ".target.index")),
            static_cast<std::uint32_t>(integer(target.at("generation"), std::numeric_limits<std::uint32_t>::max(), path + ".target.generation"))},
            vector(json.at("linear"), path + ".linear")};
    }
    else
    {
        if (type == "pause_simulation") command.payload = PauseSimulation{};
        else if (type == "resume_simulation") command.payload = ResumeSimulation{};
        else if (type == "step_simulation") command.payload = StepSimulation{};
        else invalid(path + ".type", "unsupported command '" + type + "'");
        fields(json, {"tick", "sequence", "type"}, {}, path);
    }
    validate_command(command);
    return command;
}
}
std::string serialize_recording(const InputRecording& recording)
{
    validate_recording(recording);
    Json json{{"schema_version", recording.schema_version}, {"scenario_identity", recording.scenario_identity},
        {"initial_tick", recording.initial_tick}, {"end_tick", recording.end_tick},
        {"initial_snapshot", encode(recording.initial_snapshot)}, {"expected_final", encode(recording.expected_final)},
        {"commands", Json::array()}};
    for (const auto& command : recording.commands) json["commands"].push_back(command_json(command));
    const auto result = json.dump();
    if (result.size() > max_recording_bytes) throw std::length_error("Canonical recording exceeds byte limit");
    return result;
}
InputRecording parse_recording(std::string_view text)
{
    using namespace scenario::detail;
    const auto json = parse_json(text, max_recording_bytes);
    fields(json, {"schema_version", "scenario_identity", "initial_tick", "end_tick", "initial_snapshot", "expected_final", "commands"}, {}, "recording");
    InputRecording recording;
    recording.schema_version = static_cast<std::uint32_t>(integer(json.at("schema_version"), 1, "recording.schema_version"));
    if (recording.schema_version != 1) invalid("recording.schema_version", "unsupported version");
    recording.scenario_identity = string(json.at("scenario_identity"), "recording.scenario_identity");
    recording.initial_tick = integer(json.at("initial_tick"), scenario::max_execution_ticks, "recording.initial_tick");
    recording.end_tick = integer(json.at("end_tick"), scenario::max_execution_ticks, "recording.end_tick");
    recording.initial_snapshot = decode(json.at("initial_snapshot"), "recording.initial_snapshot");
    recording.expected_final = decode(json.at("expected_final"), "recording.expected_final");
    const auto& commands = json.at("commands");
    if (!commands.is_array() || commands.size() > max_recording_commands) invalid("recording.commands", "must be an array of at most 100000 commands");
    recording.commands.reserve(commands.size());
    for (std::size_t i = 0; i < commands.size(); ++i)
        recording.commands.push_back(command_parse(commands[i], "commands[" + std::to_string(i) + "]"));
    validate_recording(recording);
    return recording;
}
InputRecording load_recording(const std::filesystem::path& path)
{
    return parse_recording(scenario::detail::load_text(path, max_recording_bytes));
}
void save_recording(const std::filesystem::path& path, const InputRecording& recording)
{
    scenario::detail::save_text(path, serialize_recording(recording));
}
} // namespace simforge::simulation
