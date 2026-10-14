#pragma once
#include <simforge/analysis/demand_calibration.hpp>
namespace simforge::analysis::demand::detail {
void validate_settings(const ImportSettings&,const FitSettings&);
void derive(Report&,const simulation::ExecutionControl* =nullptr);
std::string dataset_identity(std::string_view,const ImportSettings&);
}
