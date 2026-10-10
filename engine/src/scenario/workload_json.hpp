#pragma once
#include "json_io.hpp"
#include <simforge/scenario/scenario.hpp>

namespace simforge::scenario::detail
{
inline void parse_workload(const Json& json, WarehouseConfig& c)
{
    fields(json,{"generator_version","arrivals","processing"},{},"warehouse.workload");
    if(integer(json.at("generator_version"),workload_generator_version,"workload.generator_version")!=workload_generator_version)
        invalid("workload.generator_version","unsupported generator version");
    const auto& a=json.at("arrivals");const auto& p=json.at("processing");
    if(!a.is_object() || !a.contains("mode")) invalid("workload.arrivals.mode","required mode missing");
    if(!p.is_object() || !p.contains("mode")) invalid("workload.processing.mode","required mode missing");
    const auto arrival=string(a.at("mode"),"workload.arrivals.mode");
    const auto processing=string(p.at("mode"),"workload.processing.mode");
    c.shipment_interval_seconds=0;c.processing_duration_seconds=0;c.workload={};
    const auto seconds=[](const Json& j,const char* key,const char* path) {return static_cast<std::uint32_t>(integer(j.at(key),604800,path));};
    if(arrival=="constant") {
        fields(a,{"mode","interval_seconds"},{},"workload.arrivals");
        c.shipment_interval_seconds=seconds(a,"interval_seconds","workload.arrivals.interval_seconds");
    } else if(arrival=="uniform") {
        fields(a,{"mode","minimum_seconds","maximum_seconds"},{},"workload.arrivals");c.workload.arrivals=ArrivalMode::Uniform;
        c.workload.arrival_min_seconds=seconds(a,"minimum_seconds","workload.arrivals.minimum_seconds");
        c.workload.arrival_max_seconds=seconds(a,"maximum_seconds","workload.arrivals.maximum_seconds");
    } else if(arrival=="profile") {
        fields(a,{"mode","segments"},{},"workload.arrivals");c.workload.arrivals=ArrivalMode::Profile;
        const auto& segments=a.at("segments");
        if(!segments.is_array() || segments.size()>max_demand_segments) invalid("workload.arrivals.segments","must be an array of at most 64 segments");
        for(const auto& s:segments) {
            fields(s,{"start_seconds","end_seconds","interval_seconds"},{},"workload.arrivals.segment");
            c.workload.segments.push_back({seconds(s,"start_seconds","segment.start_seconds"),seconds(s,"end_seconds","segment.end_seconds"),seconds(s,"interval_seconds","segment.interval_seconds")});
        }
    } else invalid("workload.arrivals.mode","supported modes are constant, profile, uniform");
    if(processing=="constant") {
        fields(p,{"mode","duration_seconds"},{},"workload.processing");
        c.processing_duration_seconds=seconds(p,"duration_seconds","workload.processing.duration_seconds");
    } else if(processing=="uniform") {
        fields(p,{"mode","minimum_seconds","maximum_seconds"},{},"workload.processing");c.workload.processing=ProcessingMode::Uniform;
        c.workload.processing_min_seconds=seconds(p,"minimum_seconds","workload.processing.minimum_seconds");
        c.workload.processing_max_seconds=seconds(p,"maximum_seconds","workload.processing.maximum_seconds");
    } else invalid("workload.processing.mode","supported modes are constant, uniform");
}
inline Json workload_json(const WarehouseConfig& c)
{
    Json a{{"mode",workload::arrival_name(c.workload.arrivals)}},p{{"mode",workload::processing_name(c.workload.processing)}};
    if(c.workload.arrivals==ArrivalMode::Constant) a["interval_seconds"]=c.shipment_interval_seconds;
    else if(c.workload.arrivals==ArrivalMode::Uniform) {a["minimum_seconds"]=c.workload.arrival_min_seconds;a["maximum_seconds"]=c.workload.arrival_max_seconds;}
    else {a["segments"]=Json::array();for(const auto& s:c.workload.segments) a["segments"].push_back({{"start_seconds",s.start_seconds},{"end_seconds",s.end_seconds},{"interval_seconds",s.interval_seconds}});}
    if(c.workload.processing==ProcessingMode::Constant) p["duration_seconds"]=c.processing_duration_seconds;
    else {p["minimum_seconds"]=c.workload.processing_min_seconds;p["maximum_seconds"]=c.workload.processing_max_seconds;}
    return {{"generator_version",workload_generator_version},{"arrivals",std::move(a)},{"processing",std::move(p)}};
}
}
