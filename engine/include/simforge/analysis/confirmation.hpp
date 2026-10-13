#pragma once
#include <simforge/analysis/statistical_experiment.hpp>

namespace simforge::analysis {
inline constexpr const char* confirmation_engine_version="warehouse-2-analysis-1";
struct ConfirmationRequest {
    std::uint32_t version{1};
    std::string source_study_id, source_type, source_title, source_completed;
    std::string engine_version{confirmation_engine_version};
    std::uint32_t generator_version{scenario::workload_generator_version};
    std::uint64_t source_request_id{0}, selection_id{0};
    StatisticalRequest paired;
    StatisticalMetric objective{StatisticalMetric::Completed};
    std::vector<std::uint64_t> exploratory_seeds;
    std::optional<double> exploratory_difference;
    std::string changed_inputs;
    bool additional_analysis{false};
    std::uint32_t confirmation_number{1};
};
struct ConfirmationReport { ConfirmationRequest request; StatisticalReport paired; };
void validate_confirmation_request(const ConfirmationRequest&);
std::string confirmation_changes(const ConfirmationRequest&);
ConfirmationReport run_confirmation(const ConfirmationRequest&,const simulation::ExecutionControl* = nullptr);
std::string serialize_confirmation_request(const ConfirmationRequest&);
ConfirmationRequest parse_confirmation_request(std::string_view);
std::string serialize_confirmation_report(const ConfirmationReport&);
ConfirmationReport parse_confirmation_report(std::string_view);
std::string serialize_confirmation_csv(const ConfirmationReport&,bool per_seed=false,std::string_view study_id={});
void save_confirmation_report(const std::filesystem::path&,const ConfirmationReport&);
void save_confirmation_csv(const std::filesystem::path&,const ConfirmationReport&,bool per_seed=false);
}
