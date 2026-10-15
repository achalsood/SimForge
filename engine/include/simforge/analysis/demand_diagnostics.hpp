#pragma once
#include <simforge/analysis/demand_calibration.hpp>

namespace simforge::analysis::diagnostics {
inline constexpr std::uint32_t method_version=1;
inline constexpr std::size_t max_bins=1024,max_pieces=4096,max_replications=1000,max_report_bytes=8*1024*1024;
inline constexpr double max_expected_count=80000,max_expected_work=5'000'000;
enum class Partition { Training, Holdout };
struct Settings {
    Partition partition{Partition::Holdout};
    std::uint32_t bin_seconds{3600},replications{100},tail_threshold{10};
    std::uint64_t seed{42};
    double level{0.95};
    std::int64_t prediction_start{0};
    std::uint32_t prediction_seconds{3600};
    bool exploratory{true}; // Repeated/manual holdout inspection is never independent confirmation.
    bool operator==(const Settings&) const = default;
};
struct Request {
    std::shared_ptr<const demand::Report> source;
    Settings settings;
    std::filesystem::path csv; // Optional verified reimport; paths are not retained in evidence.
};
struct Interval { std::uint64_t lower{0},upper{0}; bool operator==(const Interval&) const = default; };
struct Prediction {
    std::optional<double> mean,lower_mean,upper_mean;
    std::optional<Interval> process,parameter_aware;
    bool operator==(const Prediction&) const = default;
};
struct Piece { std::int64_t start{0},end{0}; std::uint32_t rate_index{0}; bool operator==(const Piece&) const = default; };
struct Bin {
    std::int64_t start{0},end{0};
    std::uint64_t exposure_seconds{0},count{0};
    bool available{false},fully_observed{false};
    std::vector<Piece> pieces;
    Prediction prediction;
    std::optional<double> residual;
};
struct Statistics {
    std::size_t valid_bins{0},positive_bins{0},adjacent_pairs{0};
    std::uint64_t zeros{0},tail_bins{0},maximum{0},zero_mean_positive{0};
    double expected_zeros{0},expected_tail_bins{0};
    std::optional<double> pearson,dispersion,lag_one;
    std::int64_t residual_degrees_of_freedom{0};
    bool operator==(const Statistics&) const = default;
};
struct Coverage {
    std::uint64_t evaluated{0},inside{0},below{0},above{0};
    double mean_width{0};
    std::optional<double> fraction;
    bool operator==(const Coverage&) const = default;
};
struct Replication { std::uint64_t seed{0}; Statistics statistics; };
struct Reference {
    std::optional<double> lower,median,upper,lower_rank,upper_rank,mc_standard_error;
    std::uint32_t evaluated_replications{0};
    std::optional<double> upper_tail_wilson_lower,upper_tail_wilson_upper;
    bool operator==(const Reference&) const = default;
};
struct Report {
    std::string id,title{"Demand model diagnostics"},notes;
    Request request;
    std::string data_availability;
    std::vector<Bin> bins;
    Statistics observed;
    Coverage process_coverage,parameter_coverage;
    Prediction future;
    std::vector<Replication> replications;
    Reference dispersion_reference,zero_reference,tail_reference,lag_reference;
    std::string assessment;
    std::vector<std::string> warnings;
    double preparation_ms{0},simulation_ms{0};
};
[[nodiscard]] std::uint64_t poisson_quantile(double mean,double probability);
[[nodiscard]] double poisson_cdf(double mean,std::int64_t count);
[[nodiscard]] Interval process_interval(double mean,double level);
[[nodiscard]] Prediction predict(const demand::Report&,std::int64_t start,std::int64_t end,double level);
[[nodiscard]] Statistics statistics(std::span<const Bin>,std::span<const std::uint64_t>,const Settings&,std::size_t fitted_parameters);
[[nodiscard]] std::shared_ptr<const Report> run(Request,std::shared_ptr<const demand::Dataset> = {},const simulation::ExecutionControl* = nullptr);
[[nodiscard]] std::string report_json(const Report&);
[[nodiscard]] std::string report_csv(const Report&);
[[nodiscard]] std::shared_ptr<const Report> parse_report(std::string_view);
[[nodiscard]] Request load_request(const std::filesystem::path&);
void save_export(const std::filesystem::path&,std::string_view);
namespace detail { void validate_retained_report(const Report&); }
}
