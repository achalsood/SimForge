#pragma once

#include <simforge/warehouse/warehouse_simulation.hpp>

namespace simforge::warehouse
{
enum class WarehouseCommandType : std::uint8_t { SetForkliftCapacity = 0, SetResourceCapacity = 1 };
struct WarehouseCommand
{
    std::uint64_t tick{0}, sequence{0};
    WarehouseCommandType type{WarehouseCommandType::SetForkliftCapacity};
    std::uint32_t capacity{0};
    ResourceType resource{ResourceType::Forklift};
    bool operator==(const WarehouseCommand&) const = default;
};
struct WarehouseRecording
{
    std::uint32_t schema_version{1};
    std::string scenario_identity;
    std::uint64_t initial_tick{0}, end_tick{0};
    WarehouseSnapshot initial_snapshot, expected_final;
    std::vector<WarehouseCommand> commands;
};
inline constexpr std::size_t warehouse_recording_max_bytes = 64 * 1024 * 1024;
void validate_command(const WarehouseCommand& command);
void validate_recording(const WarehouseRecording& recording);
class WarehouseInputRecorder
{
public:
    // Caller exclusively mutates runtime through the recorder until capture.
    explicit WarehouseInputRecorder(WarehouseSimulation& runtime);
    void submit(const WarehouseCommand& command);
    void set_forklift_capacity(std::uint32_t forklifts);
    void set_resource_capacity(ResourceType resource, std::uint32_t capacity);
    void advance_to(std::uint64_t tick);
    [[nodiscard]] WarehouseRecording capture_recording() const;
private:
    WarehouseSimulation& runtime_;
    WarehouseRecording recording_;
};
// Destination must match the embedded initial state exactly. All execution and
// final comparison occur in isolation; any failure leaves destination unchanged.
void replay_recording(const WarehouseRecording& recording, WarehouseSimulation& destination);
[[nodiscard]] std::string serialize_recording(const WarehouseRecording& recording);
[[nodiscard]] WarehouseRecording parse_recording(std::string_view json);
[[nodiscard]] WarehouseRecording load_recording(const std::filesystem::path& path);
void save_recording(const std::filesystem::path& path, const WarehouseRecording& recording);
} // namespace simforge::warehouse
