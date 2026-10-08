#include "comparison_json.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace simforge::analysis
{
namespace detail
{
using namespace scenario::detail;
Json candidate_json(const ScenarioCandidate& candidate)
{
    return {{"name", candidate.name}, {"loading_bays", candidate.resources.loading_bays},
        {"workers", candidate.resources.workers}, {"forklifts", candidate.resources.forklifts}};
}
std::vector<ScenarioCandidate> parse_candidates(const Json& json)
{
    if (!json.is_array() || json.empty() || json.size() > max_candidates) invalid("candidates", "must contain 1..50 candidates");
    std::vector<ScenarioCandidate> result; result.reserve(json.size());
    for (const auto& item : json)
    {
        fields(item, {"name", "loading_bays", "workers", "forklifts"}, {}, "candidate");
        result.push_back({string(item.at("name"), "candidate.name"), {
            static_cast<std::uint32_t>(integer(item.at("loading_bays"), 100000, "candidate.loading_bays")),
            static_cast<std::uint32_t>(integer(item.at("workers"), 100000, "candidate.workers")),
            static_cast<std::uint32_t>(integer(item.at("forklifts"), 100000, "candidate.forklifts"))}});
    }
    return result;
}
Json request_json(const ComparisonRequest& request)
{
    const auto bytes = warehouse::serialize_snapshot(request.checkpoint);
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex; hex.reserve(bytes.size() * 2);
    for (auto byte : bytes)
    { const auto value = std::to_integer<unsigned>(byte); hex += digits[value >> 4]; hex += digits[value & 15]; }
    Json result{{"schema_version", request.schema_version}, {"scenario_identity", request.scenario_identity},
        {"checkpoint", std::move(hex)}, {"objective", objective_name(request.objective)}, {"candidates", Json::array()}};
    for (const auto& candidate : request.candidates) result["candidates"].push_back(candidate_json(candidate));
    return result;
}
ComparisonRequest parse_request(const Json& json)
{
    fields(json, {"schema_version", "scenario_identity", "checkpoint", "objective", "candidates"}, {}, "request");
    ComparisonRequest result;
    result.schema_version = static_cast<std::uint32_t>(integer(json.at("schema_version"), 1, "request.schema_version"));
    if (result.schema_version != 1) invalid("request.schema_version", "unsupported version");
    result.scenario_identity = string(json.at("scenario_identity"), "scenario_identity");
    const auto hex = string(json.at("checkpoint"), "checkpoint");
    if (hex.size() % 2 != 0 || hex.size() / 2 > warehouse::warehouse_snapshot_max_bytes) invalid("checkpoint", "invalid hex length");
    const auto digit = [](char c) -> unsigned
    {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        invalid("checkpoint", "expected lowercase hexadecimal");
    };
    std::vector<std::byte> bytes; bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) bytes.push_back(static_cast<std::byte>((digit(hex[i]) << 4) | digit(hex[i + 1])));
    result.checkpoint = warehouse::deserialize_snapshot(bytes);
    result.objective = parse_objective(string(json.at("objective"), "objective"));
    result.candidates = parse_candidates(json.at("candidates")); validate_request(result); return result;
}
void save_output(const std::filesystem::path& path, std::string_view bytes)
{
    if (path.empty() || path.filename().empty()) throw std::invalid_argument("Output path requires a filename");
    const auto name = path.generic_u8string();
    if (std::find(name.begin(), name.end(), char8_t{0}) != name.end()) throw std::invalid_argument("Output path contains NUL");
    std::ofstream file{path, std::ios::binary | std::ios::out | std::ios::noreplace};
    if (!file) throw std::runtime_error("Cannot create new output: " + path.string());
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); file.close();
    if (!file)
    {
        std::error_code error; std::filesystem::remove(path, error);
        throw std::runtime_error("Output write failed: " + path.string());
    }
}
}
ExperimentDefinition parse_experiment(std::string_view text)
{
    using namespace scenario::detail;
    const auto json = parse_json(text, max_experiment_bytes);
    fields(json, {"schema_version", "scenario", "objective", "candidates"}, {"checkpoint_seconds"}, "experiment");
    ExperimentDefinition result;
    result.schema_version = static_cast<std::uint32_t>(integer(json.at("schema_version"), 1, "schema_version"));
    const auto scenario_path = string(json.at("scenario"), "scenario");
    result.scenario = std::u8string{scenario_path.begin(), scenario_path.end()};
    result.checkpoint_seconds = json.contains("checkpoint_seconds") ? integer(json.at("checkpoint_seconds"), 604800, "checkpoint_seconds") : 0;
    result.objective = parse_objective(string(json.at("objective"), "objective"));
    result.candidates = detail::parse_candidates(json.at("candidates")); validate_experiment(result); return result;
}
std::string serialize_experiment(const ExperimentDefinition& experiment)
{
    validate_experiment(experiment);
    const auto path = experiment.scenario.generic_u8string();
    detail::Json json{{"schema_version", experiment.schema_version}, {"scenario", std::string{path.begin(), path.end()}},
        {"checkpoint_seconds", experiment.checkpoint_seconds}, {"objective", objective_name(experiment.objective)}, {"candidates", detail::Json::array()}};
    for (const auto& candidate : experiment.candidates) json["candidates"].push_back(detail::candidate_json(candidate));
    const auto text = json.dump();
    if (text.size() > max_experiment_bytes) throw std::length_error("Experiment byte limit"); return text;
}
ComparisonRequest load_experiment(const std::filesystem::path& path)
{
    const auto experiment = parse_experiment(scenario::detail::load_text(path, max_experiment_bytes));
    const auto directory = std::filesystem::canonical(path).parent_path();
    const auto input = std::filesystem::canonical(directory / experiment.scenario);
    const auto relative = input.lexically_relative(directory);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || !std::filesystem::is_regular_file(input))
        throw std::invalid_argument("Experiment scenario must resolve to a regular file inside its directory");
    const auto definition = scenario::load_scenario(input);
    if (experiment.checkpoint_seconds > definition.simulation.duration_seconds) throw std::invalid_argument("Checkpoint exceeds scenario horizon");
    return prepare_request(definition, experiment.checkpoint_seconds * definition.simulation.tick_rate, experiment.objective, experiment.candidates);
}
} // namespace simforge::analysis
