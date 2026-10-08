#pragma once
#include <cstdint>
#include <optional>
#include <filesystem>
int run_graphics(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output);
