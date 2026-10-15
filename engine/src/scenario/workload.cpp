#include <simforge/scenario/scenario.hpp>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace simforge::scenario::workload
{
namespace {
void require(bool ok, const char* field, const char* message)
{ if (!ok) throw std::invalid_argument(std::string{"warehouse.workload."}+field+": "+message); }
void bounds(std::uint32_t minimum, std::uint32_t maximum, const char* field)
{ require(minimum>=1 && minimum<=maximum && maximum<=604800,field,"bounds must be integer seconds: 1 <= minimum <= maximum <= 604800"); }
}
bool advanced(const WarehouseConfig& c) noexcept
{ return c.workload.arrivals!=ArrivalMode::Constant || c.workload.processing!=ProcessingMode::Constant; }
bool stochastic(const WarehouseConfig& c) noexcept
{ return poisson(c) || c.workload.arrivals==ArrivalMode::Uniform || c.workload.processing==ProcessingMode::Uniform; }
bool poisson(const WarehouseConfig& c) noexcept
{ return c.workload.arrivals==ArrivalMode::Poisson || c.workload.arrivals==ArrivalMode::PiecewisePoisson; }
std::uint32_t generator_version(const WarehouseConfig& c) noexcept
{ return poisson(c) ? poisson_generator_version : workload_generator_version; }
double expected_arrivals(const WarehouseConfig& c,std::uint32_t duration) {
    if(c.workload.arrivals==ArrivalMode::Poisson) return c.workload.rate_per_second*duration;
    double n=0;for(const auto& s:c.workload.intensities)n+=s.rate_per_second*(s.end_seconds-s.start_seconds);return n;
}
const char* arrival_name(ArrivalMode mode)
{ switch(mode) {case ArrivalMode::Constant:return "constant";case ArrivalMode::Profile:return "profile";case ArrivalMode::Uniform:return "uniform";case ArrivalMode::Poisson:return "poisson";case ArrivalMode::PiecewisePoisson:return "piecewise_poisson";} throw std::invalid_argument("Unsupported arrival mode"); }
const char* processing_name(ProcessingMode mode)
{ switch(mode) {case ProcessingMode::Constant:return "constant";case ProcessingMode::Uniform:return "uniform";} throw std::invalid_argument("Unsupported processing mode"); }
std::uint32_t arrival_bound(const WarehouseConfig& c, std::uint32_t duration, std::uint32_t)
{
    std::uint64_t count=0;
    if(poisson(c)) {
        const auto mean=expected_arrivals(c,duration);
        require(std::isfinite(mean) && mean>=0 && mean<=max_poisson_expectation,"arrivals","integrated Poisson expectation must be 0..80000; realization is separately limited to 100000");
        return mean==0?0:max_warehouse_shipments;
    }
    if (c.workload.arrivals==ArrivalMode::Profile) {
        for(const auto& s:c.workload.segments) {
            require(s.end_seconds>s.start_seconds && s.interval_seconds>0,"arrivals.segments","invalid segment duration or interval");
            count+=1+(std::uint64_t{s.end_seconds}-s.start_seconds-1)/s.interval_seconds;
        }
    } else {
        const auto minimum=c.workload.arrivals==ArrivalMode::Uniform ? c.workload.arrival_min_seconds : c.shipment_interval_seconds;
        require(minimum>0 && duration>0,"arrivals","interval and horizon must be positive");
        count=1+(std::uint64_t{duration}-1)/minimum;
    }
    require(count<=max_warehouse_shipments,"arrivals","worst-case workload exceeds 100000 arrivals before horizon");
    return static_cast<std::uint32_t>(count);
}
void validate(const WarehouseConfig& c, std::uint32_t duration, std::uint32_t rate)
{
    (void)arrival_name(c.workload.arrivals); (void)processing_name(c.workload.processing);
    const auto& w=c.workload;
    if(poisson(c)) {
        require(c.shipment_interval_seconds==0 && w.arrival_min_seconds==0 && w.arrival_max_seconds==0 && w.segments.empty(),"arrivals","inactive periodic/random interval fields must be absent");
        const auto intensity=[](double x){require(std::isfinite(x) && x>=0 && x<=max_poisson_rate,"arrivals.rate_per_second","finite intensity in 0..1000 arrivals/second required");};
        const auto precision=[](double mean){require(mean==0 || mean>=min_positive_poisson_mean,"arrivals.rate_per_second","nonzero integrated segment expectation must be at least 1e-12 for the binary64 count sampler; smaller intensities are unsupported");};
        intensity(w.rate_per_second);
        if(w.arrivals==ArrivalMode::Poisson) {require(w.intensities.empty(),"arrivals","homogeneous Poisson cannot contain segments");precision(w.rate_per_second*duration);}
        else {
            require(w.rate_per_second==0 && w.intensities.size()<=max_demand_segments,"arrivals.segments","at most 64 piecewise intensities; no homogeneous rate");
            std::uint32_t end=0;
            for(const auto& s:w.intensities) {
                require(s.start_seconds>=end && s.start_seconds<s.end_seconds && s.end_seconds<=duration,"arrivals.segments","ordered non-overlapping [start,end) segments must lie inside horizon");
                intensity(s.rate_per_second);precision(s.rate_per_second*(s.end_seconds-s.start_seconds));end=s.end_seconds;
            }
        }
    } else if(w.arrivals==ArrivalMode::Constant) {
        bounds(c.shipment_interval_seconds,c.shipment_interval_seconds,"arrivals.interval_seconds");
        require(w.arrival_min_seconds==0 && w.arrival_max_seconds==0 && w.segments.empty(),"arrivals","inactive parameters must be absent");
    } else {
        require(c.shipment_interval_seconds==0,"arrivals","legacy interval must be absent for advanced arrivals");
        if(w.arrivals==ArrivalMode::Uniform) {
            bounds(w.arrival_min_seconds,w.arrival_max_seconds,"arrivals.uniform");
            require(w.segments.empty(),"arrivals","uniform arrivals cannot contain a profile");
        } else {
            require(w.arrival_min_seconds==0 && w.arrival_max_seconds==0,"arrivals","profile cannot contain random bounds");
            require(w.segments.size()<=max_demand_segments,"arrivals.segments","at most 64 segments");
            std::uint32_t previous_end=0;
            for(const auto& s:w.segments) {
                require(s.start_seconds>=previous_end && s.start_seconds<s.end_seconds && s.end_seconds<=duration,
                    "arrivals.segments","ordered non-overlapping [start,end) segments must lie inside the simulation horizon");
                bounds(s.interval_seconds,s.interval_seconds,"arrivals.segments.interval_seconds"); previous_end=s.end_seconds;
            }
        }
    }
    if(!poisson(c)) require(w.rate_per_second==0 && w.intensities.empty(),"arrivals","inactive Poisson fields must be absent");
    if(w.processing==ProcessingMode::Constant) {
        bounds(c.processing_duration_seconds,c.processing_duration_seconds,"processing.duration_seconds");
        require(w.processing_min_seconds==0 && w.processing_max_seconds==0,"processing","inactive random bounds must be absent");
    } else {
        require(c.processing_duration_seconds==0,"processing","legacy duration must be absent for random processing");
        bounds(w.processing_min_seconds,w.processing_max_seconds,"processing.uniform");
    }
    (void)arrival_bound(c,duration,rate);
}
std::uint64_t next_random(std::uint64_t& state) noexcept
{
    auto z=(state+=0x9e3779b97f4a7c15ULL);
    z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
    z=(z^(z>>27))*0x94d049bb133111ebULL;
    return z^(z>>31);
}
std::uint64_t uniform_ticks(std::uint64_t seed,std::uint64_t domain,std::uint64_t index,std::uint64_t minimum,std::uint64_t maximum)
{
    if(minimum==0 || maximum<minimum || maximum>604800000ULL) throw std::invalid_argument("Invalid uniform tick bounds");
    auto counter=index;
    auto state=seed^domain^next_random(counter);
    const auto range=maximum-minimum+1, threshold=(std::uint64_t{0}-range)%range;
    // Exact rejection sampling avoids modulo bias. Failure is explicit after 64 draws;
    // it never silently substitutes a biased value or creates an unbounded loop.
    for(unsigned retry=0;retry<64;++retry) {const auto x=next_random(state);if(x>=threshold) return minimum+x%range;}
    throw std::runtime_error("Uniform sampler exhausted its bounded rejection budget");
}
std::optional<std::uint64_t> first_arrival(const WarehouseConfig& c,std::uint32_t rate)
{
    require(!poisson(c),"arrivals","Poisson requires an owned seeded realization");
    require(rate>=1 && rate<=1000,"tick_rate","must be 1..1000");
    if(c.workload.arrivals!=ArrivalMode::Profile) return 0;
    if(c.workload.segments.empty()) return {};
    return std::uint64_t{c.workload.segments.front().start_seconds}*rate;
}
std::optional<std::uint64_t> next_arrival(const WarehouseConfig& c,std::uint32_t rate,std::uint64_t horizon,
    std::uint64_t seed,std::uint64_t id,std::uint64_t previous)
{
    require(rate>=1 && rate<=1000 && horizon<=max_execution_ticks && previous<horizon,"arrivals","invalid tick boundary");
    std::uint64_t next=0;
    if(c.workload.arrivals==ArrivalMode::Profile) {
        const auto& segments=c.workload.segments;
        const auto it=std::upper_bound(segments.begin(),segments.end(),previous,
            [rate](auto tick,const DemandSegment& s){return tick<std::uint64_t{s.start_seconds}*rate;});
        if(it==segments.begin()) throw std::logic_error("Arrival is outside its profile segment");
        const auto current=it-1;
        require(previous<std::uint64_t{current->end_seconds}*rate && current->interval_seconds>0,"arrivals.segments","arrival outside active segment");
        next=previous+std::uint64_t{current->interval_seconds}*rate;
        if(next>=std::uint64_t{current->end_seconds}*rate) {
            if(it==segments.end()) return {};
            next=std::uint64_t{it->start_seconds}*rate;
        }
    } else {
        const auto interval=c.workload.arrivals==ArrivalMode::Constant ? std::uint64_t{c.shipment_interval_seconds}*rate :
            uniform_ticks(seed,arrival_domain,id,std::uint64_t{c.workload.arrival_min_seconds}*rate,std::uint64_t{c.workload.arrival_max_seconds}*rate);
        require(interval>0 && interval<=604800000ULL,"arrivals","invalid interval ticks");
        next=previous+interval; // Validated horizon + maximum duration < 615 million ticks.
    }
    return next<horizon ? std::optional<std::uint64_t>{next} : std::nullopt;
}
std::uint64_t processing_ticks(const WarehouseConfig& c,std::uint32_t rate,std::uint64_t seed,std::uint64_t id)
{
    if(c.workload.processing==ProcessingMode::Constant) return std::uint64_t{c.processing_duration_seconds}*rate;
    return uniform_ticks(seed,processing_domain,id,std::uint64_t{c.workload.processing_min_seconds}*rate,std::uint64_t{c.workload.processing_max_seconds}*rate);
}
std::uint32_t arrival_count(const ScenarioDefinition& d)
{
    scenario::validate(d); const auto& c=std::get<WarehouseConfig>(d.domain);
    if(poisson(c)) return static_cast<std::uint32_t>(poisson_ticks(d).size());
    if(c.workload.arrivals!=ArrivalMode::Uniform) return arrival_bound(c,d.simulation.duration_seconds,d.simulation.tick_rate);
    std::uint32_t count=0;auto next=first_arrival(c,d.simulation.tick_rate);
    while(next) {const auto tick=*next;next=next_arrival(c,d.simulation.tick_rate,std::uint64_t{d.simulation.duration_seconds}*d.simulation.tick_rate,d.simulation.random_seed,count,tick);++count;}
    return count;
}
std::vector<std::uint64_t> poisson_ticks(const ScenarioDefinition& d,const simulation::ExecutionControl* control,std::uint32_t realization_limit) {
    scenario::validate(d);const auto& c=std::get<WarehouseConfig>(d.domain);
    require(poisson(c),"arrivals","Poisson mode required");
    if(control)control->check();
    require(realization_limit<=max_warehouse_shipments,"arrivals","realization limit cannot exceed 100000");
    auto segments=c.workload.intensities;
    if(c.workload.arrivals==ArrivalMode::Poisson)segments={{0,d.simulation.duration_seconds,c.workload.rate_per_second}};
    std::vector<std::uint64_t> ticks;ticks.reserve(static_cast<std::size_t>(std::min(max_poisson_expectation,expected_arrivals(c,d.simulation.duration_seconds)+1024)));
    for(std::size_t i=0;i<segments.size();++i) {
        const auto& s=segments[i];const auto mean=s.rate_per_second*(s.end_seconds-s.start_seconds);
        if(mean==0)continue;
        // Poisson additivity: split large means into equal chunks <=16. Knuth's
        // product sampler has an explicit global draw/realization budget.
        const auto chunks=static_cast<std::uint32_t>(std::ceil(mean/16));
        const auto threshold=std::exp(-mean/chunks);
        auto key=static_cast<std::uint64_t>(i);auto state=d.simulation.random_seed^0x8ebc6af09c88c6e3ULL^next_random(key);
        const auto begin=ticks.size();
        for(std::uint32_t chunk=0;chunk<chunks;++chunk) {
            if(control && chunk%32==0)control->update("Generating Poisson arrivals",{},ticks.size(),max_warehouse_shipments);
            double product=1;
            for(std::uint32_t draws=0;draws<=max_warehouse_shipments;++draws) {
                // Strictly (0,1): midpoint of 52-bit cells, exactly representable.
                const auto u=(static_cast<double>(next_random(state)>>12)+0.5)*0x1p-52;
                product*=u;
                if(product<=threshold)break;
                if(ticks.size()==realization_limit)throw std::length_error("Poisson realization exceeds the configured arrival safety limit (maximum 100000); no truncated workload was published");
                ticks.push_back(0);
                if(control && ticks.size()%256==0)control->check();
            }
        }
        // Isolate location draws from count draws. Conditional discrete uniform
        // placement yields independent Poisson counts on disjoint tick sets.
        for(std::size_t j=begin;j<ticks.size();++j)
            ticks[j]=uniform_ticks(d.simulation.random_seed,0x589965cc75374cc3ULL^next_random(key),j-begin,1,std::uint64_t{s.end_seconds-s.start_seconds}*d.simulation.tick_rate)-1+std::uint64_t{s.start_seconds}*d.simulation.tick_rate;
        std::sort(ticks.begin()+begin,ticks.end());
        if(control)control->check();
    }
    return ticks;
}
}
