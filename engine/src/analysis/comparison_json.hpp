#pragma once

#include <simforge/analysis/scenario_comparison.hpp>
#include "../scenario/json_io.hpp"

namespace simforge::analysis::detail
{
using scenario::detail::Json;
Json candidate_json(const ScenarioCandidate& candidate);
std::vector<ScenarioCandidate> parse_candidates(const Json& json);
Json request_json(const ComparisonRequest& request);
ComparisonRequest parse_request(const Json& json);
// Exclusive creation; removes a newly created partial file if a write fails.
void save_output(const std::filesystem::path& path, std::string_view bytes);
}
