#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace simforge::warehouse
{
using ShipmentId = std::uint32_t;
enum class EventType : std::uint8_t { ShipmentCompletion = 0, ShipmentArrival = 1 };
struct ShipmentEvent
{
    std::uint64_t tick;
    EventType type;
    std::uint64_t sequence;
    ShipmentId shipment;
    bool operator==(const ShipmentEvent&) const = default;
};

// Domain-specific, bounded heap. Each shipment/type can be scheduled only once.
class EventScheduler
{
public:
    explicit EventScheduler(std::uint32_t shipment_limit, std::size_t pending_limit);
    [[nodiscard]] std::uint64_t schedule(std::uint64_t tick, EventType type, ShipmentId shipment);
    // Advances scheduling watermark to event tick, or to limit if nothing is due.
    [[nodiscard]] std::optional<ShipmentEvent> pop_due(std::uint64_t limit);
    [[nodiscard]] std::size_t pending_count() const noexcept { return heap_.size(); }
    [[nodiscard]] std::uint64_t current_tick() const noexcept { return current_tick_; }
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    [[nodiscard]] std::size_t peak_pending() const noexcept { return peak_pending_; }
    [[nodiscard]] std::size_t approximate_storage_bytes() const noexcept
    { return heap_.capacity() * sizeof(ShipmentEvent) + scheduled_.capacity(); }
private:
    friend class WarehouseSimulation;
    struct Later { bool operator()(const ShipmentEvent& a, const ShipmentEvent& b) const noexcept; };
    std::vector<ShipmentEvent> heap_;
    std::vector<std::uint8_t> scheduled_;
    std::size_t pending_limit_, peak_pending_{0};
    std::uint64_t current_tick_{0}, next_sequence_{0};
};
} // namespace simforge::warehouse
