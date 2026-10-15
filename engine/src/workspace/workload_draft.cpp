#include <simforge/workspace/scenario_workspace.hpp>
#include <charconv>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace simforge::workspace
{
WorkloadDraft workload_draft(const scenario::WarehouseConfig& c)
{
    WorkloadDraft d;d.arrivals=c.workload.arrivals;d.processing=c.workload.processing;
    if(d.arrivals==scenario::ArrivalMode::Uniform) {d.bounds[0]=std::to_string(c.workload.arrival_min_seconds);d.bounds[1]=std::to_string(c.workload.arrival_max_seconds);}
    if(d.processing==scenario::ProcessingMode::Uniform) {d.bounds[2]=std::to_string(c.workload.processing_min_seconds);d.bounds[3]=std::to_string(c.workload.processing_max_seconds);}
    for(const auto& s:c.workload.segments) d.segments.push_back({std::to_string(s.start_seconds),std::to_string(s.end_seconds),std::to_string(s.interval_seconds)});
    const auto decimal=[](double value){std::ostringstream o;o<<std::setprecision(17)<<value;return o.str();};
    if(scenario::workload::poisson(c))d.rate_per_second=decimal(c.workload.rate_per_second);
    for(const auto& s:c.workload.intensities)d.intensities.push_back({std::to_string(s.start_seconds),std::to_string(s.end_seconds),decimal(s.rate_per_second)});
    return d;
}
void apply_workload_draft(scenario::WarehouseConfig& c,const WorkloadDraft& d)
{
    const auto number=[](const std::string& text,const char* field) {
        std::uint32_t n=0;const auto p=std::from_chars(text.data(),text.data()+text.size(),n);
        if(p.ec!=std::errc{} || p.ptr!=text.data()+text.size() || n>604800)
            throw std::invalid_argument(std::string{"warehouse.workload."}+field+": enter an integer in 0..604800 seconds");
        return n;
    };
    c.workload={};c.workload.arrivals=d.arrivals;c.workload.processing=d.processing;
    const auto rate=[](const std::string& text){double value=0;const auto p=std::from_chars(text.data(),text.data()+text.size(),value);if(p.ec!=std::errc{} || p.ptr!=text.data()+text.size() || !std::isfinite(value) || value<0 || value>scenario::max_poisson_rate)throw std::invalid_argument("Enter a finite rate in 0..1000 arrivals/second");return value;};
    if(d.arrivals!=scenario::ArrivalMode::Constant) c.shipment_interval_seconds=0;
    if(d.arrivals==scenario::ArrivalMode::Poisson)c.workload.rate_per_second=rate(d.rate_per_second);
    else if(d.arrivals==scenario::ArrivalMode::PiecewisePoisson) {
        if(d.intensities.size()>scenario::max_demand_segments)throw std::length_error("At most 64 intensity segments");
        for(const auto& s:d.intensities)c.workload.intensities.push_back({number(s[0],"segment.start_seconds"),number(s[1],"segment.end_seconds"),rate(s[2])});
    } else if(d.arrivals==scenario::ArrivalMode::Uniform) {
        c.workload.arrival_min_seconds=number(d.bounds[0],"arrivals.minimum_seconds");
        c.workload.arrival_max_seconds=number(d.bounds[1],"arrivals.maximum_seconds");
    } else if(d.arrivals==scenario::ArrivalMode::Profile) {
        if(d.segments.size()>scenario::max_demand_segments) throw std::length_error("At most 64 demand segments");
        for(const auto& s:d.segments) c.workload.segments.push_back({number(s[0],"segment.start_seconds"),number(s[1],"segment.end_seconds"),number(s[2],"segment.interval_seconds")});
    }
    if(d.processing==scenario::ProcessingMode::Uniform) {
        c.processing_duration_seconds=0;
        c.workload.processing_min_seconds=number(d.bounds[2],"processing.minimum_seconds");
        c.workload.processing_max_seconds=number(d.bounds[3],"processing.maximum_seconds");
    }
}
}
