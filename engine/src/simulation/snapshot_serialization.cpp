#include <simforge/simulation/simulation_snapshot.hpp>

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <string_view>
#include <utility>

namespace simforge::simulation
{
namespace
{
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
constexpr std::string_view magic{"SFSTATE\0", 8};

// Integrity check for accidental corruption; not cryptographic authentication.
std::uint64_t checksum(std::span<const std::byte> bytes)
{
    std::uint64_t result = 14695981039346656037ULL;
    for (auto byte : bytes)
    {
        result ^= std::to_integer<std::uint8_t>(byte);
        result *= 1099511628211ULL;
    }
    return result;
}

class Writer
{
public:
    explicit Writer(std::size_t size) { bytes.reserve(size); }
    template<class T> void integer(T value)
    {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xff));
    }
    void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }
    void vector(world::Vec3 value) { real(value.x); real(value.y); real(value.z); }
    void entity(world::EntityId id) { integer(id.index); integer(id.generation); }
    std::vector<std::byte> bytes;
};

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}
    template<class T> T integer()
    {
        static_assert(std::is_unsigned_v<T>);
        require(sizeof(T));
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[offset_++])) << (i * 8);
        return static_cast<T>(value);
    }
    bool boolean()
    {
        const auto value = integer<std::uint8_t>();
        if (value > 1) throw std::invalid_argument("Snapshot boolean must be 0 or 1");
        return value != 0;
    }
    double real() { return std::bit_cast<double>(integer<std::uint64_t>()); }
    world::Vec3 vector() { return {real(), real(), real()}; }
    world::EntityId entity() { return {integer<std::uint32_t>(), integer<std::uint32_t>()}; }
    std::uint32_t count(std::size_t record_size)
    {
        const auto count = integer<std::uint32_t>();
        if (count > world::snapshot_max_slots) throw std::length_error("Snapshot record count exceeds limit");
        require(static_cast<std::size_t>(count) * record_size);
        return count;
    }
    [[nodiscard]] bool finished() const noexcept { return offset_ == bytes_.size(); }
private:
    void require(std::size_t count) const
    {
        if (count > bytes_.size() - offset_) throw std::invalid_argument("Truncated snapshot data");
    }
    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};
}

std::vector<std::byte> serialize_snapshot(const SimulationSnapshot& snapshot)
{
    validate_snapshot(snapshot);
    const auto& world = snapshot.world;
    const auto size = 81 + world.slots.size() * 10 + world.transforms.size() * 80 + world.velocities.size() * 32;
    if (size > snapshot_max_bytes) throw std::length_error("Serialized snapshot exceeds byte limit");
    Writer writer{size};
    for (char byte : magic) writer.integer(static_cast<std::uint8_t>(byte));
    writer.integer(snapshot.version);
    writer.integer(world.version);
    writer.integer(world.generation_limit);
    writer.integer(world.free_head);
    writer.integer(world.live_count);
    writer.integer(static_cast<std::uint32_t>(world.slots.size()));
    for (const auto& slot : world.slots)
    {
        writer.integer(slot.generation);
        writer.integer(slot.next_free);
        writer.integer(static_cast<std::uint8_t>(slot.alive));
        writer.integer(static_cast<std::uint8_t>(slot.retired));
    }
    writer.integer(static_cast<std::uint32_t>(world.transforms.size()));
    for (const auto& record : world.transforms)
    {
        writer.entity(record.entity);
        writer.vector(record.value.position);
        writer.vector(record.value.rotation);
        writer.vector(record.value.scale);
    }
    writer.integer(static_cast<std::uint32_t>(world.velocities.size()));
    for (const auto& record : world.velocities)
    {
        writer.entity(record.entity);
        writer.vector(record.value.linear);
    }
    const auto& clock = snapshot.clock;
    writer.integer(clock.version);
    writer.integer(clock.ticks_per_second);
    writer.integer(clock.ticks);
    writer.integer(clock.remainder);
    writer.integer(static_cast<std::uint8_t>(clock.paused));
    writer.real(clock.time_scale);
    writer.integer(checksum(writer.bytes));
    return std::move(writer.bytes);
}

SimulationSnapshot deserialize_snapshot(std::span<const std::byte> bytes)
{
    if (bytes.size() > snapshot_max_bytes) throw std::length_error("Snapshot input exceeds byte limit");
    if (bytes.size() < 81) throw std::invalid_argument("Truncated snapshot header");
    const auto payload = bytes.first(bytes.size() - 8);
    Reader trailer{bytes.last(8)};
    if (trailer.integer<std::uint64_t>() != checksum(payload))
        throw std::invalid_argument("Snapshot integrity checksum mismatch");
    Reader reader{payload};
    for (char byte : magic)
        if (reader.integer<std::uint8_t>() != static_cast<std::uint8_t>(byte))
            throw std::invalid_argument("Invalid snapshot magic");
    SimulationSnapshot snapshot;
    snapshot.version = reader.integer<std::uint32_t>();
    if (snapshot.version != 1) throw std::invalid_argument("Unsupported simulation snapshot version");
    auto& world = snapshot.world;
    world.version = reader.integer<std::uint32_t>();
    if (world.version != 1) throw std::invalid_argument("Unsupported world snapshot version");
    world.generation_limit = reader.integer<std::uint32_t>();
    world.free_head = reader.integer<std::uint32_t>();
    world.live_count = reader.integer<std::uint32_t>();
    const auto slots = reader.count(10);
    if (world.live_count > slots) throw std::invalid_argument("Living count exceeds slot count");
    world.slots.reserve(slots);
    for (std::uint32_t i = 0; i < slots; ++i)
        world.slots.push_back({reader.integer<std::uint32_t>(), reader.integer<std::uint32_t>(),
                               reader.boolean(), reader.boolean()});
    const auto transforms = reader.count(80);
    if (transforms > world.live_count) throw std::invalid_argument("Too many Transform records");
    world.transforms.reserve(transforms);
    for (std::uint32_t i = 0; i < transforms; ++i)
        world.transforms.push_back({reader.entity(), {reader.vector(), reader.vector(), reader.vector()}});
    const auto velocities = reader.count(32);
    if (velocities > world.live_count) throw std::invalid_argument("Too many Velocity records");
    world.velocities.reserve(velocities);
    for (std::uint32_t i = 0; i < velocities; ++i)
        world.velocities.push_back({reader.entity(), {reader.vector()}});
    auto& clock = snapshot.clock;
    clock.version = reader.integer<std::uint32_t>();
    clock.ticks_per_second = reader.integer<std::uint32_t>();
    clock.ticks = reader.integer<std::uint64_t>();
    clock.remainder = reader.integer<std::uint64_t>();
    clock.paused = reader.boolean();
    clock.time_scale = reader.real();
    if (!reader.finished()) throw std::invalid_argument("Trailing snapshot fields");
    validate_snapshot(snapshot);
    return snapshot;
}

} // namespace simforge::simulation
