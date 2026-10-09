#pragma once
#include <filesystem>
#include <optional>
#include <cstdint>
namespace simforge::scenario { struct ScenarioDefinition; }
// Optional prepared input is copied immediately, before graphics initialization.
int run_viewer(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output,
    const std::filesystem::path& scenario_path, double speed, bool show_workspace = false,
    const simforge::scenario::ScenarioDefinition* prepared_definition = nullptr);
