#pragma once
#include <simforge/scenario/scenario.hpp>
#include <simforge/simulation/execution_control.hpp>
#include <memory>
#include <array>
#include <span>

namespace simforge::analysis::demand
{
inline constexpr std::uint32_t schema_version=1, algorithm_version=1;
inline constexpr std::size_t max_csv_bytes=32*1024*1024, max_records=1'000'000,
    max_bins=1024, max_report_bytes=1024*1024;
inline constexpr std::int64_t max_exposure_seconds=366LL*86400;
inline constexpr const char* software_version="SimForge-demand-1";
enum class Model { Constant, DailyProfile };
// UTC integer seconds; every exposure and partition uses [start,end).
struct ImportSettings {
    std::int64_t start{0},end{0};
    bool continuous{false}; // Must be explicitly asserted. Gaps/closed shifts unsupported.
    bool operator==(const ImportSettings&) const = default;
};
struct Provenance {
    std::string dataset_id,source_filename,sha256;
    std::size_t accepted{0},duplicates{0};
    bool reordered{false};
    ImportSettings exposure;
};
struct Dataset {
    Provenance provenance;
    std::vector<std::int64_t> timestamps;
    double import_ms{0};
    std::array<std::uint64_t,64> overview_counts{}; // Bounded worker-built import chart
};
struct FitSettings {
    std::int64_t split{0};
    Model model{Model::Constant};
    std::uint32_t daily_bin_seconds{3600}, diagnostic_bin_seconds{3600};
};
struct FileRequest {
    std::filesystem::path csv;
    ImportSettings exposure;
    FitSettings fit;
};
struct Rate {
    std::uint64_t count{0},exposure_seconds{0};
    double per_second{0},lower_per_second{0},upper_per_second{0};
};
struct Metrics {
    std::uint64_t observed{0},exposure_seconds{0};
    double expected{0},signed_error{0},absolute_error{0},error_per_hour{0};
    std::optional<double> deviance,log_likelihood; // null when zero predicted rate meets positive count
};
struct Bin {
    std::int64_t start{0},end{0};
    bool holdout{false};
    std::uint64_t observed{0};
    double expected{0},baseline_expected{0},residual{0};
};
struct Timings { double import_ms{0},fit_ms{0},validation_ms{0}; };
struct Conversion {
    scenario::ScenarioDefinition scenario,source;
    std::string calibration_id,semantics;
    std::int64_t utc_start{0};
    std::uint64_t seed{0};
    std::vector<double> fitted_per_second,represented_per_second;
};
struct Report {
    std::string id;
    Provenance provenance;
    FitSettings settings;
    Rate baseline;
    std::vector<Rate> rates; // constant: one; profile: UTC bins from midnight, predefined width
    std::vector<Bin> bins; // chronological diagnostics, split boundary inserted explicitly
    Metrics training,holdout,baseline_training,baseline_holdout;
    std::vector<std::string> warnings,limitations;
    Timings timings;
    std::vector<Conversion> conversions; // At most 8 explicit, reviewed scenario conversions.
};
[[nodiscard]] std::int64_t parse_timestamp(std::string_view);
[[nodiscard]] std::string timestamp(std::int64_t);
[[nodiscard]] std::string sha256(std::string_view,const simulation::ExecutionControl* =nullptr);
void validate_exposure(const ImportSettings&);
[[nodiscard]] std::shared_ptr<const Dataset> import_csv(std::string_view,std::string source_filename,
    const ImportSettings&,const simulation::ExecutionControl* =nullptr);
[[nodiscard]] std::shared_ptr<const Dataset> load_csv(const std::filesystem::path&,const ImportSettings&,const simulation::ExecutionControl* =nullptr);
[[nodiscard]] Rate estimate_rate(std::uint64_t count,std::uint64_t exposure_seconds);
[[nodiscard]] std::shared_ptr<const Report> fit(std::shared_ptr<const Dataset>,const FitSettings&,
    const simulation::ExecutionControl* =nullptr);
[[nodiscard]] std::shared_ptr<const Report> calibrate(const FileRequest&,const simulation::ExecutionControl* =nullptr);
// Explicitly accepted periodic surrogate, nearest integer SECOND (half up), no Poisson claim.
[[nodiscard]] Conversion convert(const Report&,const scenario::ScenarioDefinition& source,
    std::string name,std::uint64_t seed,std::int64_t utc_start,bool accept_periodic_approximation);
[[nodiscard]] std::string report_json(const Report&);
[[nodiscard]] std::string report_csv(const Report&);
// Validates derived statistics by recomputing from retained bounded count/exposure evidence.
[[nodiscard]] std::shared_ptr<const Report> parse_report(std::string_view);
[[nodiscard]] FileRequest load_request(const std::filesystem::path&);
[[nodiscard]] std::string conversion_json(const Conversion&);
void save_export(const std::filesystem::path&,std::string_view);
}
