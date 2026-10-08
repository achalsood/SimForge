#include <simforge/warehouse/warehouse_input.hpp>
#include "json_io.hpp"

namespace simforge::warehouse
{
namespace
{
using namespace scenario::detail;
std::string encode(const WarehouseSnapshot& snapshot)
{
    constexpr std::string_view digits = "0123456789abcdef";
    const auto bytes = serialize_snapshot(snapshot);
    std::string text; text.reserve(2 * bytes.size());
    for (auto byte : bytes)
    {
        const auto value = std::to_integer<unsigned>(byte);
        text.push_back(digits[value >> 4]); text.push_back(digits[value & 15]);
    }
    return text;
}
WarehouseSnapshot decode(const Json& json, const std::string& path)
{
    const auto text = string(json, path);
    if (text.size() % 2 != 0 || text.size() / 2 > warehouse_snapshot_max_bytes) invalid(path, "invalid snapshot hex length");
    const auto digit = [&](char c) -> unsigned
    {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        invalid(path, "snapshot must use lowercase hexadecimal");
    };
    std::vector<std::byte> bytes; bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::byte>((digit(text[i]) << 4) | digit(text[i + 1])));
    return deserialize_snapshot(bytes);
}
}
std::string serialize_recording(const WarehouseRecording& recording)
{
    validate_recording(recording);
    Json json{{"schema_version", recording.schema_version}, {"domain", "warehouse"},
        {"scenario_identity", recording.scenario_identity}, {"initial_tick", recording.initial_tick},
        {"end_tick", recording.end_tick}, {"initial_snapshot", encode(recording.initial_snapshot)},
        {"expected_final", encode(recording.expected_final)}, {"commands", Json::array()}};
    for (const auto& command : recording.commands)
        json["commands"].push_back({{"tick", command.tick}, {"sequence", command.sequence},
            {"type", "set_forklift_capacity"}, {"forklifts", command.forklifts}});
    auto result = json.dump();
    if (result.size() > warehouse_recording_max_bytes) throw std::length_error("Warehouse recording byte limit");
    return result;
}
WarehouseRecording parse_recording(std::string_view text)
{
    const auto json = parse_json(text, warehouse_recording_max_bytes);
    fields(json, {"schema_version", "domain", "scenario_identity", "initial_tick", "end_tick", "initial_snapshot", "expected_final", "commands"}, {}, "recording");
    if (string(json.at("domain"), "domain") != "warehouse") invalid("domain", "warehouse recording required");
    WarehouseRecording result;
    result.schema_version = static_cast<std::uint32_t>(integer(json.at("schema_version"), 1, "schema_version"));
    result.scenario_identity = string(json.at("scenario_identity"), "scenario_identity");
    result.initial_tick = integer(json.at("initial_tick"), scenario::max_execution_ticks, "initial_tick");
    result.end_tick = integer(json.at("end_tick"), scenario::max_execution_ticks, "end_tick");
    result.initial_snapshot = decode(json.at("initial_snapshot"), "initial_snapshot");
    result.expected_final = decode(json.at("expected_final"), "expected_final");
    const auto& commands = json.at("commands");
    if (!commands.is_array() || commands.size() > max_capacity_changes) invalid("commands", "expected array, maximum 100000");
    result.commands.reserve(commands.size());
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        const auto& command = commands[i]; const auto path = "commands[" + std::to_string(i) + "]";
        fields(command, {"tick", "sequence", "type", "forklifts"}, {}, path);
        if (string(command.at("type"), path + ".type") != "set_forklift_capacity") invalid(path + ".type", "unsupported command");
        result.commands.push_back({integer(command.at("tick"), scenario::max_execution_ticks, path + ".tick"),
            integer(command.at("sequence"), max_capacity_changes - 1, path + ".sequence"), WarehouseCommandType::SetForkliftCapacity,
            static_cast<std::uint32_t>(integer(command.at("forklifts"), 100000, path + ".forklifts"))});
    }
    validate_recording(result); return result;
}
WarehouseRecording load_recording(const std::filesystem::path& path)
{ return parse_recording(scenario::detail::load_text(path, warehouse_recording_max_bytes)); }
void save_recording(const std::filesystem::path& path, const WarehouseRecording& recording)
{ scenario::detail::save_text(path, serialize_recording(recording)); }
} // namespace simforge::warehouse
