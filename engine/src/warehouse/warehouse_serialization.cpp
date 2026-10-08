#include <simforge/warehouse/warehouse_snapshot.hpp>
#include <simforge/scenario/scenario.hpp>
#include "../scenario/json_io.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace simforge::warehouse
{
namespace
{
constexpr std::string_view magic{"SFWHSE\0\0", 8};
std::uint64_t checksum(std::span<const std::byte> bytes)
{
    std::uint64_t result = 14695981039346656037ULL;
    for (auto byte : bytes) { result ^= std::to_integer<std::uint8_t>(byte); result *= 1099511628211ULL; }
    return result;
}
class Writer
{
public:
    std::vector<std::byte> bytes;
    template<class T> void integer(T value)
    {
        static_assert(std::is_unsigned_v<T>);
        if (bytes.size() > warehouse_snapshot_max_bytes - sizeof(T)) throw std::length_error("Warehouse binary byte limit");
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 255));
    }
    void count(std::size_t value) { integer(static_cast<std::uint32_t>(value)); }
    void optional(std::optional<std::uint64_t> value)
    { integer<std::uint8_t>(value.has_value()); if (value) integer(*value); }
    void resources(ResourceCounts value) { integer(value.loading_bays); integer(value.workers); integer(value.forklifts); }
};
class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}
    template<class T> T integer()
    {
        static_assert(std::is_unsigned_v<T>);
        if (remaining() < sizeof(T)) throw std::invalid_argument("Truncated warehouse binary");
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[offset_++])) << (8 * i);
        return static_cast<T>(value);
    }
    bool boolean()
    {
        const auto value = integer<std::uint8_t>();
        if (value > 1) throw std::invalid_argument("Invalid warehouse binary boolean");
        return value != 0;
    }
    std::optional<std::uint64_t> optional() { if (!boolean()) return {}; return integer<std::uint64_t>(); }
    std::size_t count(std::size_t maximum, std::size_t minimum_bytes)
    {
        const auto value = integer<std::uint32_t>();
        if (value > maximum || value > remaining() / minimum_bytes)
            throw std::length_error("Warehouse binary container length exceeds bounds/payload");
        return value;
    }
    ResourceCounts resources() { return {integer<std::uint32_t>(), integer<std::uint32_t>(), integer<std::uint32_t>()}; }
    std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};
}
std::vector<std::byte> serialize_snapshot(const WarehouseSnapshot& snapshot)
{
    validate_snapshot(snapshot);
    Writer writer;
    writer.bytes.reserve(256 + snapshot.scenario_identity.size() + snapshot.shipments.size() * 31 + snapshot.events.size() * 21);
    for (auto c : magic) writer.integer(static_cast<std::uint8_t>(c));
    writer.integer(snapshot.version);
    writer.count(snapshot.scenario_identity.size());
    for (auto c : snapshot.scenario_identity) writer.integer(static_cast<std::uint8_t>(c));
    writer.integer(snapshot.tick_rate); writer.integer(snapshot.horizon_ticks); writer.integer(snapshot.processing_ticks);
    writer.integer(snapshot.current_tick); writer.integer<std::uint8_t>(snapshot.settled); writer.integer(snapshot.next_shipment);
    writer.count(snapshot.shipments.size());
    for (const auto& record : snapshot.shipments)
    {
        writer.integer(record.id); writer.integer(static_cast<std::uint8_t>(record.status)); writer.integer(record.arrival_tick);
        writer.optional(record.start_tick); writer.optional(record.completion_tick);
    }
    writer.count(snapshot.waiting_queue.size()); for (auto id : snapshot.waiting_queue) writer.integer(id);
    writer.count(snapshot.reservations.size()); for (auto id : snapshot.reservations) writer.integer(id);
    writer.resources(snapshot.capacity); writer.resources(snapshot.usage);
    writer.count(snapshot.events.size());
    for (const auto& event : snapshot.events)
    {
        writer.integer(event.tick); writer.integer(static_cast<std::uint8_t>(event.type));
        writer.integer(event.sequence); writer.integer(event.shipment);
    }
    writer.integer(snapshot.next_event_sequence); writer.integer(snapshot.peak_pending_events);
    writer.count(snapshot.capacity_changes.size());
    for (const auto& change : snapshot.capacity_changes)
    {
        writer.integer(change.tick); writer.integer(change.started_before);
        if (snapshot.version == 2) writer.integer(static_cast<std::uint8_t>(change.resource));
        writer.integer(change.capacity);
    }
    const auto& t = snapshot.totals;
    for (auto value : {t.started, t.completed, t.total_wait_ticks, t.max_wait_ticks, t.completed_processing_ticks,
        t.occupied_resource_ticks, t.processed_events, t.available_bay_ticks, t.available_worker_ticks, t.available_forklift_ticks})
        writer.integer(value);
    writer.integer(checksum(writer.bytes));
    return std::move(writer.bytes);
}
WarehouseSnapshot deserialize_snapshot(std::span<const std::byte> bytes)
{
    if (bytes.size() > warehouse_snapshot_max_bytes) throw std::length_error("Warehouse binary byte limit");
    if (bytes.size() < 16) throw std::invalid_argument("Truncated warehouse binary header");
    Reader trailer{bytes.last(8)};
    if (checksum(bytes.first(bytes.size() - 8)) != trailer.integer<std::uint64_t>())
        throw std::invalid_argument("Warehouse binary checksum mismatch");
    Reader reader{bytes.first(bytes.size() - 8)};
    for (auto c : magic)
        if (reader.integer<std::uint8_t>() != static_cast<std::uint8_t>(c)) throw std::invalid_argument("Warehouse binary magic mismatch");
    WarehouseSnapshot result;
    result.version = reader.integer<std::uint32_t>();
    if (result.version != 1 && result.version != 2) throw std::invalid_argument("Unsupported warehouse binary version");
    const auto identity_bytes = reader.count(scenario::max_scenario_bytes, 1);
    result.scenario_identity.reserve(identity_bytes);
    for (std::size_t i = 0; i < identity_bytes; ++i) result.scenario_identity.push_back(static_cast<char>(reader.integer<std::uint8_t>()));
    result.tick_rate = reader.integer<std::uint32_t>(); result.horizon_ticks = reader.integer<std::uint64_t>();
    result.processing_ticks = reader.integer<std::uint64_t>(); result.current_tick = reader.integer<std::uint64_t>();
    result.settled = reader.boolean(); result.next_shipment = reader.integer<std::uint32_t>();
    const auto records = reader.count(scenario::max_warehouse_shipments, 15);
    result.shipments.reserve(records);
    for (std::size_t i = 0; i < records; ++i)
    {
        ShipmentRecord record;
        record.id = reader.integer<std::uint32_t>(); record.status = static_cast<ShipmentStatus>(reader.integer<std::uint8_t>());
        record.arrival_tick = reader.integer<std::uint64_t>(); record.start_tick = reader.optional(); record.completion_tick = reader.optional();
        result.shipments.push_back(record);
    }
    const auto queue = reader.count(scenario::max_warehouse_shipments, 4);
    result.waiting_queue.reserve(queue); for (std::size_t i = 0; i < queue; ++i) result.waiting_queue.push_back(reader.integer<std::uint32_t>());
    const auto held = reader.count(scenario::max_warehouse_shipments, 4);
    result.reservations.reserve(held); for (std::size_t i = 0; i < held; ++i) result.reservations.push_back(reader.integer<std::uint32_t>());
    result.capacity = reader.resources(); result.usage = reader.resources();
    const auto events = reader.count(scenario::max_warehouse_shipments + 1ULL, 21);
    result.events.reserve(events);
    for (std::size_t i = 0; i < events; ++i)
    {
        ShipmentEvent event;
        event.tick = reader.integer<std::uint64_t>(); event.type = static_cast<EventType>(reader.integer<std::uint8_t>());
        event.sequence = reader.integer<std::uint64_t>(); event.shipment = reader.integer<std::uint32_t>(); result.events.push_back(event);
    }
    result.next_event_sequence = reader.integer<std::uint64_t>(); result.peak_pending_events = reader.integer<std::uint64_t>();
    const auto changes = reader.count(max_capacity_changes, result.version == 1 ? 20 : 21); result.capacity_changes.reserve(changes);
    for (std::size_t i = 0; i < changes; ++i)
    {
        CapacityChange change{reader.integer<std::uint64_t>(), reader.integer<std::uint64_t>(), 0};
        if (result.version == 2) change.resource = static_cast<ResourceType>(reader.integer<std::uint8_t>());
        change.capacity = reader.integer<std::uint32_t>(); result.capacity_changes.push_back(change);
    }
    auto& t = result.totals;
    for (auto* value : {&t.started, &t.completed, &t.total_wait_ticks, &t.max_wait_ticks, &t.completed_processing_ticks,
        &t.occupied_resource_ticks, &t.processed_events, &t.available_bay_ticks, &t.available_worker_ticks, &t.available_forklift_ticks})
        *value = reader.integer<std::uint64_t>();
    if (reader.remaining() != 0) throw std::invalid_argument("Trailing warehouse binary payload");
    validate_snapshot(result);
    return result;
}
void save_snapshot(const std::filesystem::path& path, const WarehouseSnapshot& snapshot)
{
    const auto bytes = serialize_snapshot(snapshot);
    scenario::detail::save_text(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
WarehouseSnapshot load_snapshot(const std::filesystem::path& path)
{
    const auto bytes = scenario::detail::load_text(path, warehouse_snapshot_max_bytes);
    return deserialize_snapshot({reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
}
} // namespace simforge::warehouse
