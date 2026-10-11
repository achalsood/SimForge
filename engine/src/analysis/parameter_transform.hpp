#pragma once
#include <simforge/analysis/sensitivity.hpp>

namespace simforge::analysis::detail {
// Apply all requested fields before validating the final scenario. A valid joint
// resource transfer may exceed the total-resource bound at an intermediate step.
template<class Config> auto& parameter_field(Config& c,SweepParameter p) {
    switch(p) {
    case SweepParameter::Forklifts:return c.forklifts;
    case SweepParameter::Workers:return c.workers;
    case SweepParameter::LoadingBays:return c.loading_bays;
    case SweepParameter::ArrivalInterval:return c.shipment_interval_seconds;
    case SweepParameter::ProcessingDuration:return c.processing_duration_seconds;
    }throw std::invalid_argument("Unsupported sweep parameter");
}
}
