#include <simforge/visualization/live_warehouse.hpp>
#include <simforge/visualization/visual_palette.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace simforge::visualization
{
namespace
{
void validate(const warehouse::WarehouseObservation& s)
{
    const auto& m = s.metrics;
    const auto arrived = m.arrived, active = m.in_progress;
    if (s.tick_rate == 0 || s.tick > s.horizon || s.horizon > scenario::max_execution_ticks ||
        s.at_horizon != (s.tick == s.horizon) || m.elapsed_ticks != s.tick || arrived > scenario::max_warehouse_shipments ||
        m.completed > arrived || m.waiting > arrived || active > arrived || m.started > arrived ||
        arrived != m.waiting+active+m.completed || m.started != active+m.completed ||
        s.waiting.size() > max_visible_waiting || s.processing.size() > max_visual_capacity ||
        s.waiting.size() > m.waiting || s.processing.size() > active ||
        s.hidden_waiting != m.waiting-s.waiting.size() || s.hidden_processing != active-s.processing.size() ||
        s.usage != warehouse::ResourceCounts{static_cast<std::uint32_t>(active),static_cast<std::uint32_t>(active),static_cast<std::uint32_t>(active)} ||
        s.usage.loading_bays > s.capacity.loading_bays || s.usage.workers > s.capacity.workers || s.usage.forklifts > s.capacity.forklifts)
        throw std::invalid_argument("Inconsistent or oversized warehouse observation");
    std::optional<warehouse::ShipmentId> previous;
    const auto records = [&](const auto& records, warehouse::ShipmentStatus status)
    {
        for (const auto& record : records)
        {
            if (record.id >= arrived || (previous && record.id <= *previous) || record.status != status ||
                record.arrival_tick > s.tick || record.arrival_tick >= s.horizon || record.completion_tick ||
                (status == warehouse::ShipmentStatus::Waiting && record.start_tick) ||
                (status == warehouse::ShipmentStatus::InProgress && (!record.start_tick || *record.start_tick < record.arrival_tick || *record.start_tick > s.tick)))
                throw std::invalid_argument("Invalid observable shipment identity/lifecycle/order");
            previous = record.id;
        }
    };
    records(s.processing,warehouse::ShipmentStatus::InProgress); records(s.waiting,warehouse::ShipmentStatus::Waiting);
}
}
LiveWarehouseScene::LiveWarehouseScene(const scenario::ScenarioDefinition& definition) : definition_{definition}
{
    scenario::validate(definition_);
    const auto* c = std::get_if<scenario::WarehouseConfig>(&definition_.domain);
    if (!c) throw std::invalid_argument("Live visualization requires a warehouse scenario");
    rebuild({c->loading_bays,c->workers,c->forklifts});
}
void LiveWarehouseScene::rebuild(warehouse::ResourceCounts capacity)
{
    auto layout_definition = definition_;
    auto& c = std::get<scenario::WarehouseConfig>(layout_definition.domain);
    c.loading_bays = capacity.loading_bays; c.workers = capacity.workers; c.forklifts = capacity.forklifts;
    auto layout = build_warehouse_layout(layout_definition);
    renderer::RenderFrameData frame; frame.framing_bounds = layout.bounds;
    frame.camera = renderer::frame_bounds(layout.bounds,16.0f/9.0f);
    std::vector<glm::vec3> queue_slots, bay_slots;
    std::vector<CapacityVisual> resources;
    static_selectables_.clear(); highlight_.reset();
    for (const auto& o : layout.objects)
    {
        if (o.kind == WarehouseVisualKind::Pallet)
        { if (queue_slots.size() < max_visible_waiting) queue_slots.push_back(o.render.transform.position); }
        else if (o.kind == WarehouseVisualKind::LoadingBay) bay_slots.push_back(o.render.transform.position+glm::vec3{0,0.33f,0.3f});
        else if (o.kind != WarehouseVisualKind::Forklift && o.kind != WarehouseVisualKind::Worker)
        {
            if (o.kind == WarehouseVisualKind::StorageRack)
                static_selectables_.push_back({{VisualCategory::Rack,o.id},o.bounds,frame.objects.size()});
            frame.objects.push_back(o.render);
        }
        if (o.kind == WarehouseVisualKind::LoadingBay || o.kind == WarehouseVisualKind::Forklift || o.kind == WarehouseVisualKind::Worker)
        {
            auto bounds = o.bounds;
            // Pick the door, rather than the dock's empty loading volume that contains cargo.
            if (o.kind == WarehouseVisualKind::LoadingBay) bounds.maximum.z = bounds.minimum.z+0.45f;
            resources.push_back({o.kind,o.id,false,o.render,bounds});
        }
    }
    layout_ = std::move(layout); frame_ = std::move(frame); queue_slots_ = std::move(queue_slots); bay_slots_ = std::move(bay_slots);
    static_count_ = frame_.objects.size(); capacity_ = capacity; ++layout_generations_;
    resources_ = std::move(resources);
}
void LiveWarehouseScene::set_state(const warehouse::WarehouseObservation& state, bool reset)
{
    clear_highlight();
    validate(state);
    if (state.tick_rate != definition_.simulation.tick_rate || state.horizon != scenario::total_ticks(definition_))
        throw std::invalid_argument("Observation scenario timing mismatch");
    if (state.capacity != capacity_) { rebuild(state.capacity); reset = true; }
    const bool snap = reset || !last_tick_ || state.tick < *last_tick_ || state.tick-*last_tick_ > 120ULL*state.tick_rate;
    for (auto& o : resources_)
    {
        std::uint32_t usage = 0;
        if (o.kind == WarehouseVisualKind::LoadingBay) usage = state.usage.loading_bays;
        else if (o.kind == WarehouseVisualKind::Forklift) usage = state.usage.forklifts;
        else if (o.kind == WarehouseVisualKind::Worker) usage = state.usage.workers;
        else continue;
        const bool busy = o.id <= usage;
        o.render.color = busy ? palette::busy : palette::idle;
        o.render.neutral_material = true;
        o.busy = busy;
    }
    std::vector<ShipmentVisual> next;
    const auto add = [&](const auto& records, const auto& slots, bool waiting)
    {
        for (std::size_t i = 0; i < std::min(records.size(),slots.size()); ++i)
        {
            const auto& record = records[i];
            ShipmentVisual visual{record.id,record.status,{renderer::MeshId::Pallet,{slots[i],{0,0,0},{1,1,1}},
                waiting ? (i == 0 ? palette::queue_head : palette::waiting) : palette::processing}};
            visual.target.neutral_material = true;
            // A small stable tonal variation makes identity changes visible,
            // without claiming a domain material or random shipment property.
            visual.target.color *= 0.82f+0.036f*static_cast<float>(record.id%6);
            const auto old = std::lower_bound(shipments_.begin(),shipments_.end(),record.id,[](const auto& o, auto id) { return o.id < id; });
            if (!snap) visual.age = old != shipments_.end() && old->id == record.id && old->status == record.status ? old->age : std::chrono::nanoseconds{0};
            next.push_back(visual);
        }
    };
    add(state.processing,bay_slots_,false); add(state.waiting,queue_slots_,true);
    shipments_ = std::move(next); last_tick_ = state.tick;
    visibility_ = {std::min(state.waiting.size(),queue_slots_.size()),state.metrics.waiting-std::min(state.waiting.size(),queue_slots_.size()),
        std::min(state.processing.size(),bay_slots_.size()),state.metrics.in_progress-std::min(state.processing.size(),bay_slots_.size())};
    frame_.objects.resize(static_count_);
    for (const auto& o : resources_) frame_.objects.push_back(o.render);
    for (const auto& o : shipments_) frame_.objects.push_back(o.target);
    animate(std::chrono::nanoseconds{0});
}
void LiveWarehouseScene::animate(std::chrono::nanoseconds elapsed)
{
    if (elapsed.count() < 0) throw std::invalid_argument("Animation elapsed time cannot be negative");
    clear_highlight();
    for (std::size_t i = 0; i < shipments_.size(); ++i)
    {
        auto& visual = shipments_[i];
        visual.age += std::min(elapsed,std::chrono::duration_cast<std::chrono::nanoseconds>(shipment_settle_duration)-visual.age);
        const float fraction = static_cast<float>(visual.age.count())/static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(shipment_settle_duration).count());
        auto render = visual.target; render.transform.position.y += 0.30f*(1-fraction)*(1-fraction);
        frame_.objects[static_count_+resources_.size()+i] = render;
    }
    rebuild_selectables();
}
void LiveWarehouseScene::clear_highlight()
{
    if (highlight_) frame_.objects.at(highlight_->first).color = highlight_->second;
    highlight_.reset();
}
void LiveWarehouseScene::rebuild_selectables()
{
    selectables_ = static_selectables_;
    for (std::size_t i = 0; i < resources_.size(); ++i)
    {
        const auto& o = resources_[i];
        const auto kind = o.kind == WarehouseVisualKind::LoadingBay ? VisualCategory::LoadingBay :
            o.kind == WarehouseVisualKind::Worker ? VisualCategory::Worker : VisualCategory::Forklift;
        selectables_.push_back({{kind,o.id},o.bounds,static_count_+i});
    }
    for (std::size_t i = 0; i < shipments_.size(); ++i)
    {
        const auto index = static_count_+resources_.size()+i;
        const auto p = frame_.objects[index].transform.position;
        selectables_.push_back({{VisualCategory::Shipment,shipments_[i].id},{p+glm::vec3{-0.65f,0,-0.60f},p+glm::vec3{0.65f,1.34f,0.60f}},index});
    }
}
void LiveWarehouseScene::present(const renderer::Camera& camera, renderer::RenderViewport viewport, std::optional<VisualKey> selection)
{
    clear_highlight(); frame_.camera = camera; frame_.framing_bounds.reset(); frame_.viewport = viewport;
    if (!selection) return;
    for (const auto& o : selectables_) if (o.key == *selection)
    {
        highlight_ = std::pair{o.render_index,frame_.objects[o.render_index].color};
        frame_.objects[o.render_index].color = palette::selected; break;
    }
}
std::string format_live_metrics(const warehouse::WarehouseObservation& state, LiveVisibility visibility)
{
    std::ostringstream text; text << std::fixed << std::setprecision(2);
    text << "tick=" << state.tick << '/' << state.horizon << " time_s=" << static_cast<double>(state.tick)/state.tick_rate
         << " arrivals=" << state.metrics.arrived << " started=" << state.metrics.started << " completed=" << state.metrics.completed
         << " waiting=" << state.metrics.waiting << " in_progress=" << state.metrics.in_progress;
    const auto number = [&](const char* name, std::optional<double> value, double scale = 1)
    { text << ' ' << name << '='; if (value) text << *value*scale; else text << "unavailable"; };
    number("throughput_per_hour",state.metrics.throughput_per_hour); number("average_wait_s",state.metrics.average_wait_seconds);
    number("bay_util_pct",state.metrics.loading_bay_utilization,100); number("worker_util_pct",state.metrics.worker_utilization,100);
    number("forklift_util_pct",state.metrics.forklift_utilization,100);
    text << " usage_bay_worker_forklift=" << state.usage.loading_bays << '/' << state.usage.workers << '/' << state.usage.forklifts
         << " capacity=" << state.capacity.loading_bays << '/' << state.capacity.workers << '/' << state.capacity.forklifts
         << " visible_waiting=" << visibility.visible_waiting << " hidden_waiting=" << visibility.hidden_waiting
         << " visible_processing=" << visibility.visible_processing << " hidden_processing=" << visibility.hidden_processing;
    if (!state.waiting.empty()) text << " next_shipment=" << state.waiting.front().id;
    if (state.at_horizon) text << " horizon_reached";
    return text.str();
}
}
