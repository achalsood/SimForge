#include <simforge/world/components.hpp>
#include "json_io.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace simforge::scenario::detail
{
void invalid(const std::string& path, const std::string& reason)
{
    throw std::invalid_argument(path + ": " + reason);
}

Json parse_json(std::string_view text, std::size_t limit)
{
    if (text.size() > limit) throw std::length_error("JSON input exceeds byte limit");
    if (text.find('\0') != std::string_view::npos) invalid("JSON", "raw NUL bytes are not permitted");
    std::vector<std::set<std::string>> objects;
    std::size_t events = 0;
    const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed)
    {
        if (depth > 32 || ++events > 4'000'000) throw std::length_error("JSON nesting/event limit exceeded");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        else if (event == Json::parse_event_t::object_end) objects.pop_back();
        else if (event == Json::parse_event_t::key)
        {
            const auto key = parsed.get<std::string>();
            if (!objects.back().insert(key).second) invalid(key, "duplicate JSON field");
        }
        return true;
    };
    try { return Json::parse(text.begin(), text.end(), callback); }
    catch (const Json::exception& error) { invalid("JSON", error.what()); }
}

void fields(const Json& value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional, const std::string& path)
{
    if (!value.is_object()) invalid(path, "must be an object");
    for (auto key : required) if (!value.contains(key)) invalid(path + "." + std::string{key}, "required field missing");
    for (const auto& [key, unused] : value.items())
    {
        (void)unused;
        if (std::find(required.begin(), required.end(), key) == required.end() &&
            std::find(optional.begin(), optional.end(), key) == optional.end())
            invalid(path + "." + key, "unexpected field");
    }
}

std::uint64_t integer(const Json& value, std::uint64_t maximum, const std::string& path)
{
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        invalid(path, "must be an unsigned integer, received " + value.dump());
    const auto result = value.get<std::uint64_t>();
    if (result > maximum) invalid(path, "exceeds limit " + std::to_string(maximum));
    return result;
}
std::string string(const Json& value, const std::string& path)
{
    if (!value.is_string()) invalid(path, "must be a string");
    return value.get<std::string>();
}
void vector_valid(world::Vec3 value, const std::string& path)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
        std::abs(value.x) > 1e12 || std::abs(value.y) > 1e12 || std::abs(value.z) > 1e12)
        invalid(path, "components must be finite and within +/-1e12");
}
world::Vec3 vector(const Json& value, const std::string& path)
{
    if (!value.is_array() || value.size() != 3) invalid(path, "must be an array of three numbers");
    for (const auto& number : value) if (!number.is_number()) invalid(path, "must contain numbers");
    world::Vec3 result{value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
    vector_valid(result, path);
    // Normalize signed zero so logically identical configuration has one encoding.
    if (result.x == 0) result.x = 0;
    if (result.y == 0) result.y = 0;
    if (result.z == 0) result.z = 0;
    return result;
}
Json vector_json(world::Vec3 value)
{
    return Json::array({value.x == 0 ? 0.0 : value.x, value.y == 0 ? 0.0 : value.y, value.z == 0 ? 0.0 : value.z});
}
std::string load_text(const std::filesystem::path& path, std::size_t limit)
{
    std::ifstream file{path, std::ios::binary};
    if (!file) throw std::runtime_error("Cannot open input: " + path.string());
    std::string result;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0)
    {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > limit - result.size()) throw std::length_error("Input file exceeds byte limit");
        result.append(buffer, count);
    }
    if (!file.eof()) throw std::runtime_error("Input read failed: " + path.string());
    return result;
}
void save_text(const std::filesystem::path& path, std::string_view text)
{
    // Never overwrite an existing scenario/recording implicitly.
    if (std::filesystem::exists(path)) throw std::runtime_error("Output already exists: " + path.string());
    std::ofstream file{path, std::ios::binary | std::ios::out | std::ios::noreplace};
    if (!file) throw std::runtime_error("Cannot open output: " + path.string());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) throw std::runtime_error("Output write failed: " + path.string());
}
} // namespace simforge::scenario::detail
