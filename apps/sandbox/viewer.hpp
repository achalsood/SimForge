#pragma once
#include <filesystem>
#include <optional>
#include <cstdint>
int run_viewer(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output,
    const std::filesystem::path& scenario_path, double speed);
