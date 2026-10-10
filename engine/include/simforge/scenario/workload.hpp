#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace simforge::scenario
{
enum class ArrivalMode : std::uint8_t { Constant, Profile, Uniform };
enum class ProcessingMode : std::uint8_t { Constant, Uniform };
struct DemandSegment
{
    std::uint32_t start_seconds{0}, end_seconds{0}, interval_seconds{300};
    bool operator==(const DemandSegment&) const = default;
};
struct WorkloadConfig
{
    ArrivalMode arrivals{ArrivalMode::Constant};
    ProcessingMode processing{ProcessingMode::Constant};
    std::uint32_t arrival_min_seconds{0}, arrival_max_seconds{0};
    std::uint32_t processing_min_seconds{0}, processing_max_seconds{0};
    std::vector<DemandSegment> segments;
    bool operator==(const WorkloadConfig&) const = default;
};
inline constexpr std::uint32_t workload_generator_version = 1;
inline constexpr std::size_t max_demand_segments = 64;
struct WarehouseConfig;
struct ScenarioDefinition;
namespace workload
{
[[nodiscard]] bool advanced(const WarehouseConfig& config) noexcept;
[[nodiscard]] bool stochastic(const WarehouseConfig& config) noexcept;
void validate(const WarehouseConfig& config, std::uint32_t duration_seconds, std::uint32_t tick_rate);
[[nodiscard]] std::uint32_t arrival_bound(const WarehouseConfig& config, std::uint32_t duration_seconds, std::uint32_t tick_rate);
[[nodiscard]] std::optional<std::uint64_t> first_arrival(const WarehouseConfig& config, std::uint32_t tick_rate);
[[nodiscard]] std::optional<std::uint64_t> next_arrival(const WarehouseConfig& config, std::uint32_t tick_rate,
    std::uint64_t horizon, std::uint64_t seed, std::uint64_t shipment_id, std::uint64_t previous_tick);
[[nodiscard]] std::uint64_t processing_ticks(const WarehouseConfig& config, std::uint32_t tick_rate,
    std::uint64_t seed, std::uint64_t shipment_id);
[[nodiscard]] std::uint32_t arrival_count(const ScenarioDefinition& definition);
// SplitMix64 reference transition, with defined unsigned wraparound; no global state.
[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) noexcept;
[[nodiscard]] std::uint64_t uniform_ticks(std::uint64_t seed, std::uint64_t domain,
    std::uint64_t index, std::uint64_t minimum, std::uint64_t maximum);
inline constexpr std::uint64_t arrival_domain = 0xa0761d6478bd642fULL;
inline constexpr std::uint64_t processing_domain = 0xe7037ed1a0b428dbULL;
[[nodiscard]] const char* arrival_name(ArrivalMode mode);
[[nodiscard]] const char* processing_name(ProcessingMode mode);
}
}
