#pragma once
#include "../scenario/json_io.hpp"
#include <simforge/analysis/confirmation.hpp>
#include <simforge/analysis/interaction.hpp>
#include <simforge/analysis/sensitivity.hpp>
namespace simforge::analysis::detail {
StatisticalReport statistical_report_value(const scenario::detail::Json&);
SensitivityReport sensitivity_report_value(const scenario::detail::Json&);
InteractionReport interaction_report_value(const scenario::detail::Json&);
ConfirmationReport confirmation_report_value(const scenario::detail::Json&);
}
