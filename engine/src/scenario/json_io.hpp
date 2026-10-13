#pragma once

#include <nlohmann/json.hpp>
#include <simforge/world/components.hpp>
#include <filesystem>
#include <string_view>

namespace simforge::scenario::detail
{
using Json = nlohmann::json;
Json parse_json(std::string_view text, std::size_t limit,std::size_t max_events=4'000'000);
void fields(const Json& value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional, const std::string& path);
std::uint64_t integer(const Json& value, std::uint64_t maximum, const std::string& path);
std::string string(const Json& value, const std::string& path);
world::Vec3 vector(const Json& value, const std::string& path);
Json vector_json(world::Vec3 value);
void vector_valid(world::Vec3 value, const std::string& path);
std::string load_text(const std::filesystem::path& path, std::size_t limit);
void save_text(const std::filesystem::path& path, std::string_view text);
[[noreturn]] void invalid(const std::string& path, const std::string& reason);
} // namespace simforge::scenario::detail
