#pragma once

#include <simforge/scenario/scenario.hpp>
#include <simforge/warehouse/resource_manager.hpp>

#include <span>

namespace simforge::warehouse
{
enum class ShipmentStatus : std::uint8_t { Waiting, InProgress, Completed };
struct ShipmentRecord
{
    ShipmentId id;
    ShipmentStatus status;
    std::uint64_t arrival_tick;
    std::optional<std::uint64_t> start_tick, completion_tick;
    bool operator==(const ShipmentRecord&) const = default;
};
struct WarehouseMetrics
{
    std::uint64_t elapsed_ticks{0}, arrived{0}, started{0}, completed{0}, waiting{0}, in_progress{0};
    std::uint64_t total_wait_ticks{0}, max_wait_ticks{0}, completed_processing_ticks{0}, occupied_resource_ticks{0};
    std::optional<double> throughput_per_hour, average_wait_seconds, maximum_wait_seconds, average_processing_seconds;
    double completed_processing_seconds{0};
    std::optional<double> loading_bay_utilization, worker_utilization, forklift_utilization;
    bool operator==(const WarehouseMetrics&) const = default;
};

class WarehouseSimulation
{
public:
    explicit WarehouseSimulation(const scenario::ScenarioDefinition& definition);
    WarehouseSimulation(const WarehouseSimulation&) = delete;
    WarehouseSimulation& operator=(const WarehouseSimulation&) = delete;
    WarehouseSimulation(WarehouseSimulation&&) = delete;
    WarehouseSimulation& operator=(WarehouseSimulation&&) = delete;

    void advance_to(std::uint64_t tick);
    void run() { advance_to(horizon_ticks_); }
    [[nodiscard]] WarehouseMetrics metrics() const;
    [[nodiscard]] std::span<const ShipmentRecord> shipments() const noexcept { return shipments_; }
    [[nodiscard]] const simulation::SimulationClock& clock() const noexcept { return clock_; }
    [[nodiscard]] std::uint64_t horizon_ticks() const noexcept { return horizon_ticks_; }
    [[nodiscard]] std::uint64_t processed_events() const noexcept { return processed_events_; }
    [[nodiscard]] std::size_t pending_events() const noexcept { return scheduler_.pending_count(); }
    [[nodiscard]] std::size_t peak_pending_events() const noexcept { return scheduler_.peak_pending(); }
    [[nodiscard]] ResourceCounts resource_usage() const noexcept { return resources_.usage(); }
    // Owned payload capacities only; excludes allocator/container-object overhead.
    [[nodiscard]] std::size_t approximate_storage_bytes() const noexcept;
private:
    struct Parameters
    {
        scenario::WarehouseConfig config;
        std::uint32_t rate, arrivals;
        std::uint64_t horizon;
    };
    static Parameters parameters(const scenario::ScenarioDefinition& definition);
    explicit WarehouseSimulation(Parameters parameters);
    void move_clock_to(std::uint64_t tick);
    void process(const ShipmentEvent& event);
    void start_waiting();

    scenario::WarehouseConfig config_;
    simulation::SimulationClock clock_;
    std::uint64_t horizon_ticks_, arrival_interval_ticks_, processing_ticks_;
    std::uint32_t arrival_limit_;
    ResourceManager resources_;
    EventScheduler scheduler_;
    std::vector<ShipmentRecord> shipments_;
    std::vector<ShipmentId> waiting_queue_;
    std::size_t queue_head_{0};
    std::uint64_t started_{0}, completed_{0}, total_wait_ticks_{0}, max_wait_ticks_{0};
    std::uint64_t completed_processing_ticks_{0}, occupied_resource_ticks_{0}, processed_events_{0};
};
} // namespace simforge::warehouse
