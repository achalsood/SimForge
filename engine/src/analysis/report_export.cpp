#include "comparison_json.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simforge::analysis
{
namespace
{
using detail::Json;
Json optional(std::optional<double> value) { return value ? Json(*value) : Json(nullptr); }
Json metrics_json(const warehouse::WarehouseMetrics& m)
{
    return {{"elapsed_ticks", m.elapsed_ticks}, {"arrived", m.arrived}, {"started", m.started}, {"completed", m.completed},
        {"waiting", m.waiting}, {"in_progress", m.in_progress}, {"total_wait_ticks", m.total_wait_ticks}, {"max_wait_ticks", m.max_wait_ticks},
        {"completed_processing_ticks", m.completed_processing_ticks}, {"occupied_resource_ticks", m.occupied_resource_ticks},
        {"throughput_per_hour", optional(m.throughput_per_hour)}, {"average_wait_seconds", optional(m.average_wait_seconds)},
        {"maximum_wait_seconds", optional(m.maximum_wait_seconds)}, {"average_processing_seconds", optional(m.average_processing_seconds)},
        {"completed_processing_seconds", m.completed_processing_seconds}, {"loading_bay_utilization", optional(m.loading_bay_utilization)},
        {"worker_utilization", optional(m.worker_utilization)}, {"forklift_utilization", optional(m.forklift_utilization)}};
}
Json delta_json(const MetricDelta& d)
{
    return {{"completed", d.completed}, {"completed_percent", optional(d.completed_percent)}, {"waiting", d.waiting},
        {"throughput_per_hour", optional(d.throughput_per_hour)}, {"average_wait_seconds", optional(d.average_wait_seconds)},
        {"average_wait_percent", optional(d.average_wait_percent)}, {"loading_bay_utilization", optional(d.loading_bay_utilization)},
        {"worker_utilization", optional(d.worker_utilization)}, {"forklift_utilization", optional(d.forklift_utilization)},
        {"resources", {{"loading_bays", d.resources.loading_bays}, {"workers", d.resources.workers}, {"forklifts", d.resources.forklifts},
            {"additional_resources", d.resources.additions}, {"absolute_resource_changes", d.resources.changes}}}};
}
Json result_json(const CandidateResult& result)
{
    return {{"candidate", detail::candidate_json(result.candidate)}, {"metrics", metrics_json(result.metrics)}, {"delta", delta_json(result.delta)},
        {"eligible", result.eligible}, {"rank", result.rank ? Json(*result.rank) : Json(nullptr)}, {"eligibility_reason", result.eligibility_reason}};
}
std::string_view conclusion_name(BottleneckConclusion conclusion)
{
    switch (conclusion)
    {
    case BottleneckConclusion::LikelyBottleneck: return "likely_bottleneck_under_tested_conditions";
    case BottleneckConclusion::NoPositiveEffect: return "no_positive_objective_effect";
    case BottleneckConclusion::Unavailable: return "intervention_unavailable";
    case BottleneckConclusion::Inconclusive: return "inconclusive";
    }
    throw std::invalid_argument("Unknown bottleneck conclusion");
}
void finite_json(const Json& value)
{
    if (value.is_number_float() && !std::isfinite(value.get<double>())) throw std::invalid_argument("Report contains nonfinite numeric value");
    if (value.is_structured()) for (const auto& child : value) finite_json(child);
}
void validate_summary(const AnalysisReport& report)
{
    if (report.schema_version != 1) throw std::invalid_argument("Unsupported analysis report version");
    validate_request(report.request);
    const auto definition = scenario::parse_scenario(report.request.scenario_identity);
    const auto& config = std::get<scenario::WarehouseConfig>(definition.domain);
    const auto expected_arrivals = 1 + (definition.simulation.duration_seconds - 1) / config.shipment_interval_seconds;
    if (report.candidates.size() != report.request.candidates.size() || report.bottlenecks.size() != 3 ||
        report.ranking.size() > max_candidates || report.warnings.size() > 100 || report.limitations.size() > 100 ||
        report.baseline.candidate != ScenarioCandidate{"Baseline", report.request.checkpoint.capacity})
        throw std::invalid_argument("Invalid analysis report shape");
    const auto check_result = [&](const CandidateResult& result)
    {
        const auto& m = result.metrics;
        if (m.elapsed_ticks != report.request.checkpoint.horizon_ticks || m.arrived > scenario::max_warehouse_shipments ||
            m.waiting > 100000 || m.in_progress > 100000 ||
            m.started > m.arrived || m.completed > m.started || m.arrived != m.waiting + m.in_progress + m.completed ||
            m.started != m.completed + m.in_progress || m.total_wait_ticks > 1000000000000ULL ||
            m.completed_processing_ticks > 1000000000000ULL || m.occupied_resource_ticks > 1000000000000ULL ||
            m.max_wait_ticks > m.elapsed_ticks || m.in_progress > std::min({result.candidate.resources.loading_bays,
                result.candidate.resources.workers, result.candidate.resources.forklifts}))
            throw std::invalid_argument("Invalid analysis operational metrics");
        const auto& checkpoint = report.request.checkpoint;
        if (m.arrived != expected_arrivals || m.started < checkpoint.totals.started || m.completed < checkpoint.totals.completed ||
            m.total_wait_ticks < checkpoint.totals.total_wait_ticks || m.max_wait_ticks < checkpoint.totals.max_wait_ticks ||
            m.occupied_resource_ticks < checkpoint.totals.occupied_resource_ticks ||
            m.completed_processing_ticks != m.completed * checkpoint.processing_ticks)
            throw std::invalid_argument("Analysis metrics conflict with shared history");
        const double rate = checkpoint.tick_rate;
        const auto expected_average = m.started ? std::optional<double>{static_cast<double>(m.total_wait_ticks) / static_cast<double>(m.started) / rate} : std::nullopt;
        const auto expected_maximum = m.started ? std::optional<double>{static_cast<double>(m.max_wait_ticks) / rate} : std::nullopt;
        const double processing_seconds = static_cast<double>(m.completed_processing_ticks) / rate;
        const auto processing_average = m.completed ? std::optional<double>{processing_seconds / static_cast<double>(m.completed)} : std::nullopt;
        const auto throughput = std::optional<double>{static_cast<double>(m.completed) * 3600.0 * rate / static_cast<double>(m.elapsed_ticks)};
        if (m.average_wait_seconds != expected_average || m.maximum_wait_seconds != expected_maximum ||
            m.completed_processing_seconds != processing_seconds || m.average_processing_seconds != processing_average || m.throughput_per_hour != throughput)
            throw std::invalid_argument("Analysis ratios conflict with raw metrics");
        const auto delta = checkpoint.horizon_ticks - checkpoint.current_tick;
        const auto utilization = [&](std::uint64_t prior, std::uint32_t capacity) -> std::optional<double>
        {
            const auto available = prior + delta * capacity;
            if (m.occupied_resource_ticks > available) throw std::invalid_argument("Analysis occupied time exceeds available capacity");
            if (available == 0) return {}; return static_cast<double>(m.occupied_resource_ticks) / static_cast<double>(available);
        };
        if (m.loading_bay_utilization != utilization(checkpoint.totals.available_bay_ticks,result.candidate.resources.loading_bays) ||
            m.worker_utilization != utilization(checkpoint.totals.available_worker_ticks,result.candidate.resources.workers) ||
            m.forklift_utilization != utilization(checkpoint.totals.available_forklift_ticks,result.candidate.resources.forklifts))
            throw std::invalid_argument("Analysis utilization conflicts with capacity history");
        if (result.delta != compare_metrics(report.baseline.metrics, m, report.baseline.candidate.resources, result.candidate.resources))
            throw std::invalid_argument("Invalid analysis baseline deltas");
        finite_json(result_json(result));
    };
    check_result(report.baseline);
    for (std::size_t i = 0; i < report.candidates.size(); ++i)
    {
        if (report.candidates[i].candidate != report.request.candidates[i] ||
            (i != 0 && report.candidates[i - 1].candidate.name >= report.candidates[i].candidate.name))
            throw std::invalid_argument("Report candidate identity/order mismatch");
        check_result(report.candidates[i]);
    }
    auto ranked = report.candidates; rank_candidates(ranked, report.request.objective);
    if (ranked != report.candidates) throw std::invalid_argument("Invalid analysis ranks/eligibility");
    for (const auto& name : report.ranking)
        if (name.size() > 256) throw std::invalid_argument("Invalid rank name");
    for (std::size_t i = 0; i < report.ranking.size(); ++i)
        if (std::none_of(ranked.begin(), ranked.end(), [&](const auto& c) { return c.candidate.name == report.ranking[i] && c.rank == i + 1; }))
            throw std::invalid_argument("Invalid analysis ranking order");
    const auto ranked_count = std::count_if(ranked.begin(), ranked.end(), [](const auto& r) { return r.rank.has_value(); });
    if (report.ranking.size() != static_cast<std::size_t>(ranked_count)) throw std::invalid_argument("Missing ranked candidate");
    for (const auto& evidence : report.bottlenecks)
    {
        (void)warehouse::resource_name(evidence.resource); (void)conclusion_name(evidence.conclusion);
        if (evidence.result) check_result(*evidence.result);
        if (evidence.summary.size() > 4096) throw std::length_error("Evidence text limit");
    }
    for (const auto* list : {&report.warnings, &report.limitations})
        for (const auto& text : *list) if (text.size() > 4096) throw std::length_error("Report text limit");
}
Json report_json(const AnalysisReport& report)
{
    validate_summary(report);
    const auto& checkpoint = report.request.checkpoint;
    Json json{{"schema_version", report.schema_version}, {"request", detail::request_json(report.request)},
        {"checkpoint", {{"tick", checkpoint.current_tick}, {"horizon_ticks", checkpoint.horizon_ticks},
            {"tick_rate", checkpoint.tick_rate}, {"snapshot_schema_version", checkpoint.version}}},
        {"units", {{"wait_time", "seconds"}, {"throughput", "shipments/hour"}, {"utilization", "fraction_of_historical_available_capacity"},
            {"percentage_change", "percent"}}}, {"baseline", result_json(report.baseline)}, {"candidates", Json::array()},
        {"ranking", report.ranking}, {"bottlenecks", Json::array()}, {"warnings", report.warnings}, {"model_limitations", report.limitations}};
    for (const auto& result : report.candidates) json["candidates"].push_back(result_json(result));
    for (const auto& evidence : report.bottlenecks)
        json["bottlenecks"].push_back({{"resource", warehouse::resource_name(evidence.resource)}, {"baseline_capacity", evidence.baseline_capacity},
            {"tested_capacity", evidence.tested_capacity}, {"classification", conclusion_name(evidence.conclusion)},
            {"result", evidence.result ? result_json(*evidence.result) : Json(nullptr)}, {"summary", evidence.summary}});
    finite_json(json); return json;
}
std::string number(std::optional<double> value)
{
    if (!value) return "";
    if (!std::isfinite(*value)) throw std::invalid_argument("CSV contains nonfinite value");
    char buffer[64]; const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), *value, std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{}) throw std::runtime_error("CSV numeric conversion failed");
    return {buffer, converted.ptr};
}
std::string csv_escape(std::string_view text)
{
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) return std::string{text};
    std::string result{"\""};
    for (auto c : text) { if (c == '"') result += '"'; result += c; } result += '"'; return result;
}
}
std::string serialize_report(const AnalysisReport& report)
{
    const auto text = report_json(report).dump();
    if (text.size() > max_report_bytes) throw std::length_error("Analysis report byte limit"); return text;
}
std::string serialize_csv(const AnalysisReport& report)
{
    validate_summary(report);
    std::string text = "candidate,baseline,loading_bays,workers,forklifts,arrived,started,completed,waiting,in_progress,throughput_per_hour,average_wait_seconds,maximum_wait_seconds,loading_bay_utilization,worker_utilization,forklift_utilization,completed_processing_seconds,delta_completed,delta_completed_percent,delta_waiting,delta_throughput_per_hour,delta_average_wait_seconds,delta_average_wait_percent,delta_loading_bays,delta_workers,delta_forklifts,additional_resources,absolute_resource_changes,rank\r\n";
    const auto row = [&](const CandidateResult& r, bool baseline)
    {
        const auto& c = r.candidate.resources; const auto& m = r.metrics; const auto& d = r.delta;
        std::vector<std::string> values{csv_escape(r.candidate.name), baseline ? "true" : "false", std::to_string(c.loading_bays),
            std::to_string(c.workers), std::to_string(c.forklifts), std::to_string(m.arrived), std::to_string(m.started),
            std::to_string(m.completed), std::to_string(m.waiting), std::to_string(m.in_progress), number(m.throughput_per_hour),
            number(m.average_wait_seconds), number(m.maximum_wait_seconds), number(m.loading_bay_utilization), number(m.worker_utilization),
            number(m.forklift_utilization), number(m.completed_processing_seconds), std::to_string(d.completed), number(d.completed_percent),
            std::to_string(d.waiting), number(d.throughput_per_hour), number(d.average_wait_seconds), number(d.average_wait_percent),
            std::to_string(d.resources.loading_bays), std::to_string(d.resources.workers), std::to_string(d.resources.forklifts),
            std::to_string(d.resources.additions), std::to_string(d.resources.changes), r.rank ? std::to_string(*r.rank) : ""};
        for (std::size_t i = 0; i < values.size(); ++i) { if (i != 0) text += ','; text += values[i]; } text += "\r\n";
    };
    row(report.baseline, true); for (const auto& result : report.candidates) row(result, false);
    if (text.size() > max_report_bytes) throw std::length_error("CSV byte limit"); return text;
}
AnalysisReport replay_report(std::string_view text)
{
    using namespace scenario::detail;
    const auto json = parse_json(text, max_report_bytes);
    fields(json, {"schema_version", "request", "checkpoint", "units", "baseline", "candidates", "ranking", "bottlenecks", "warnings", "model_limitations"}, {}, "report");
    if (integer(json.at("schema_version"), 1, "schema_version") != 1) invalid("schema_version", "unsupported version");
    auto report = run_comparison(detail::parse_request(json.at("request")));
    if (report_json(report) != json) throw std::invalid_argument("Analysis report replay diverged from recorded results");
    return report;
}
void save_report(const std::filesystem::path& path, const AnalysisReport& report) { detail::save_output(path, serialize_report(report)); }
void save_csv(const std::filesystem::path& path, const AnalysisReport& report) { detail::save_output(path, serialize_csv(report)); }
} // namespace simforge::analysis
