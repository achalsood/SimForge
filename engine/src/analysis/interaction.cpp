#include <simforge/analysis/interaction.hpp>
#include "evaluation.hpp"
#include "parameter_transform.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>

namespace simforge::analysis {
namespace {
bool resource(SweepParameter p) {return p==SweepParameter::Forklifts||p==SweepParameter::Workers||p==SweepParameter::LoadingBays;}
bool baseline_present(const InteractionRequest& r) {
    return std::find(r.x_values.begin(),r.x_values.end(),parameter_value(r.source,r.x))!=r.x_values.end()&&
        std::find(r.y_values.begin(),r.y_values.end(),parameter_value(r.source,r.y))!=r.y_values.end();
}
std::uint64_t multiply(std::uint64_t a,std::uint64_t b) {
    if(b&&a>UINT64_MAX/b)throw std::overflow_error("Interaction work arithmetic overflow");return a*b;
}
}
std::size_t interaction_index(std::size_t x,std::size_t y,std::size_t width) {
    if(!width||x>=width||y>(SIZE_MAX-x)/width)throw std::invalid_argument("Invalid interaction coordinate");return y*width+x;
}
std::uint64_t interaction_cell_id(const InteractionRequest& r,std::uint32_t x,std::uint32_t y) {
    // Ordered factor/value tuple; experiment identity is carried separately by the report/action.
    (void)r;return (std::uint64_t{x}<<32)|y;
}
scenario::ScenarioDefinition tested_definition(const InteractionRequest& r,std::uint32_t x,std::uint32_t y,std::uint64_t seed) {
    auto d=r.source;auto& c=std::get<scenario::WarehouseConfig>(d.domain);
    detail::parameter_field(c,r.x)=x;detail::parameter_field(c,r.y)=y;
    d.simulation.random_seed=seed;scenario::validate(d);return d;
}
std::uint64_t interaction_execution_count(const InteractionRequest& r) {
    const auto cells=multiply(r.x_values.size(),r.y_values.size());
    if(cells==UINT64_MAX)throw std::overflow_error("Interaction execution overflow");
    return multiply(cells+(baseline_present(r)?0:1),r.plan.count);
}
void validate_interaction_request(const InteractionRequest& r) {
    if(r.schema_version!=1)throw std::invalid_argument("Unsupported interaction request version");
    scenario::validate(r.source);if(!std::holds_alternative<scenario::WarehouseConfig>(r.source.domain))throw std::invalid_argument("Warehouse source required");
    (void)parameter_name(r.x);(void)parameter_name(r.y);(void)metric_name(r.objective);(void)replication_seeds(r.plan);
    if(r.x==r.y)throw std::invalid_argument("Choose two distinct factors");
    if(!r.multi_seed&&r.plan.count!=1)throw std::invalid_argument("Single-seed interaction requires one seed");
    if(r.multi_seed&&(!resource(r.x)||!resource(r.y)))throw std::invalid_argument("Workload factors support single-seed exploratory studies only");
    if(!resource(r.x)&&!resource(r.y))throw std::invalid_argument("At least one resource factor is required");
    if(!r.limits.axis||r.limits.axis>max_interaction_axis||!r.limits.cells||r.limits.cells>max_interaction_cells||
        !r.limits.executions||r.limits.executions>max_interaction_executions||!r.limits.arrival_work||r.limits.arrival_work>max_statistical_arrival_work)
        throw std::invalid_argument("Interaction limits may reduce but never exceed supported bounds");
    const auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);
    for(auto p:{r.x,r.y}) {
        if(p==SweepParameter::ArrivalInterval&&c.workload.arrivals!=scenario::ArrivalMode::Constant)throw std::invalid_argument("Arrival factor requires constant arrivals");
        if(p==SweepParameter::ProcessingDuration&&c.workload.processing!=scenario::ProcessingMode::Constant)throw std::invalid_argument("Processing factor requires constant processing");
    }
    for(const auto* axis:{&r.x_values,&r.y_values}) {
        if(axis->empty()||axis->size()>r.limits.axis)throw std::length_error("Interaction axes require 1..10 values");
        if(std::set<std::uint32_t>(axis->begin(),axis->end()).size()!=axis->size())throw std::invalid_argument("Duplicate interaction axis value");
    }
    if(multiply(r.x_values.size(),r.y_values.size())>r.limits.cells||interaction_execution_count(r)>r.limits.executions)throw std::length_error("Interaction grid or execution budget exceeded");
    std::uint64_t work=baseline_present(r)?0:scenario::workload::arrival_bound(c,r.source.simulation.duration_seconds,r.source.simulation.tick_rate);
    for(auto y:r.y_values)for(auto x:r.x_values) {
        auto d=tested_definition(r,x,y,r.source.simulation.random_seed);
        const auto arrivals=scenario::workload::arrival_bound(std::get<scenario::WarehouseConfig>(d.domain),d.simulation.duration_seconds,d.simulation.tick_rate);
        if(arrivals>UINT64_MAX-work)throw std::overflow_error("Interaction arrival arithmetic overflow");work+=arrivals;
    }
    if(multiply(work,r.plan.count)>r.limits.arrival_work)throw std::length_error("Interaction workload exceeds conservative arrival budget");
}
std::optional<double> interaction_contrast(std::optional<double> a,std::optional<double> b,std::optional<double> c,std::optional<double> d) {
    for(auto v:{a,b,c,d})if(v&&!std::isfinite(*v))throw std::invalid_argument("Nonfinite interaction input");
    if(!a||!b||!c||!d)return std::nullopt;
    // Difference of marginal changes reduces cancellation for large common offsets.
    const double first=*d-*c,second=*b-*a,result=first-second;
    if(!std::isfinite(first)||!std::isfinite(second)||!std::isfinite(result))throw std::overflow_error("Interaction contrast overflow");return result;
}
void aggregate_interaction_report(InteractionReport& r) {
    validate_interaction_request(r.request);const auto& q=r.request;const auto seeds=replication_seeds(q.plan);
    if(!std::is_sorted(q.x_values.begin(),q.x_values.end())||!std::is_sorted(q.y_values.begin(),q.y_values.end()))throw std::invalid_argument("Completed grids require ascending axes");
    if(r.replications.size()!=seeds.size())throw std::invalid_argument("Incomplete interaction report");
    const auto width=q.x_values.size(),height=q.y_values.size();
    for(std::size_t i=0;i<seeds.size();++i)if(r.replications[i].seed!=seeds[i]||r.replications[i].cells.size()!=width*height)throw std::invalid_argument("Interaction seed/cell identity mismatch");
    const bool stochastic=q.multi_seed&&workload_varies(std::get<scenario::WarehouseConfig>(q.source.domain));
    const auto summary=[&](auto before,auto after,std::size_t m) {
        std::vector<std::optional<double>> a,b;for(const auto& row:r.replications){a.push_back(before(row,static_cast<StatisticalMetric>(m)));b.push_back(after(row,static_cast<StatisticalMetric>(m)));}
        auto result=summarize_pairs(a,b,higher_is_better(static_cast<StatisticalMetric>(m)),stochastic);
        if(!q.multi_seed&&result.defined==result.count)result.diagnostic="Single realization; no confidence interval.";return result;
    };
    const auto baseline=[](const auto& row,auto m){return metric_value(row.baseline,m);};
    for(std::size_t m=0;m<statistical_metric_count;++m)r.baseline_means[m]=summary(baseline,baseline,m).baseline_mean;
    r.cells.clear();r.interactions.clear();r.best_cells.clear();const auto objective=static_cast<std::size_t>(q.objective);std::optional<double> best;
    for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width;++x) {
        const auto index=interaction_index(x,y,width);InteractionCell cell;cell.x_index=x;cell.y_index=y;cell.x=q.x_values[x];cell.y=q.y_values[y];cell.id=interaction_cell_id(q,cell.x,cell.y);
        cell.baseline=cell.x==parameter_value(q.source,q.x)&&cell.y==parameter_value(q.source,q.y);
        for(const auto& row:r.replications)if(cell.baseline&&row.cells[index]!=row.baseline)throw std::invalid_argument("Baseline cell measurement mismatch");
        for(std::size_t m=0;m<statistical_metric_count;++m) {const auto after=[index](const auto& row,auto metric){return metric_value(row.cells[index],metric);};
            cell.means[m]=summary([](const auto&,auto)->std::optional<double>{return 0.;},after,m).candidate_mean;cell.effects[m]=summary(baseline,after,m);}
        if(auto v=cell.means[objective]) {if(!best||(higher_is_better(q.objective)?*v>*best:*v<*best)){best=v;r.best_cells.clear();}if(*v==*best)r.best_cells.push_back(cell.id);}
        r.cells.push_back(std::move(cell));
    }
    for(std::size_t y=0;y+1<height;++y)for(std::size_t x=0;x+1<width;++x) {
        InteractionRectangle rectangle;rectangle.x_index=x;rectangle.y_index=y;rectangle.x0=q.x_values[x];rectangle.x1=q.x_values[x+1];rectangle.y0=q.y_values[y];rectangle.y1=q.y_values[y+1];
        rectangle.cells={interaction_index(x,y,width),interaction_index(x+1,y,width),interaction_index(x,y+1,width),interaction_index(x+1,y+1,width)};
        for(const auto& row:r.replications) {std::array<std::optional<double>,statistical_metric_count> contrasts;
            for(std::size_t m=0;m<statistical_metric_count;++m){auto metric=static_cast<StatisticalMetric>(m);contrasts[m]=interaction_contrast(metric_value(row.cells[rectangle.cells[0]],metric),metric_value(row.cells[rectangle.cells[1]],metric),metric_value(row.cells[rectangle.cells[2]],metric),metric_value(row.cells[rectangle.cells[3]],metric));}rectangle.per_seed.push_back(contrasts);}
        for(std::size_t m=0;m<statistical_metric_count;++m) {std::vector<std::optional<double>> zero(seeds.size(),0.),contrasts;for(const auto& row:rectangle.per_seed)contrasts.push_back(row[m]);rectangle.effects[m]=summarize_pairs(zero,contrasts,higher_is_better(static_cast<StatisticalMetric>(m)),stochastic);
            auto& s=rectangle.effects[m];if(s.lower&&s.upper&&s.standard_deviation&&*s.standard_deviation>0&&(*s.lower>0||*s.upper<0)){
                s.diagnostic=*s.lower>0?"Nominal interval excludes zero; positive local additive interaction.":"Nominal interval excludes zero; negative local additive interaction.";
                if(s.count<20)s.diagnostic+=" Small sample: skew/discreteness may make the approximation unreliable.";}
            if(!q.multi_seed&&rectangle.effects[m].defined==rectangle.effects[m].count)rectangle.effects[m].diagnostic="Single realization; no confidence interval.";}
        const auto& effect=rectangle.effects[objective];
        if(!effect.mean_difference)rectangle.interpretation="Incomplete four-cell observations; interaction is undefined.";
        else {rectangle.interpretation=*effect.mean_difference>0?"Positive local additive interaction":*effect.mean_difference<0?"Negative local additive interaction":"Zero observed local additive interaction";
            if(*effect.mean_difference!=0)rectangle.interpretation+=(higher_is_better(q.objective)?*effect.mean_difference>0:*effect.mean_difference<0)?"; favorable contrast on this metric scale.":"; unfavorable contrast on this metric scale.";
            if(stochastic&&(!effect.lower||(*effect.lower<=0&&*effect.upper>=0)))rectangle.interpretation+=" Evidence inconclusive.";
            rectangle.interpretation+=" X marginal change differs between the two measured Y factor values by "+std::to_string(*effect.mean_difference)+" "+std::string(metric_unit(q.objective))+".";}
        r.interactions.push_back(std::move(rectangle));
    }
    r.limitations={"Fresh-start bounded two-factor study; best observed among tested configurations is not a global optimum. Exact best-mean ties are retained.",
        "Raw cell effects are tested minus original baseline; favorable direction is separate from sign.",
        "Local additive interaction is Y11 - Y10 - Y01 + Y00 on the selected metric scale, not causal evidence outside the simulation or on a logarithmic/percentage scale.",
        "Each contrast is computed from four matched observations within one seed before aggregation; irregular step lengths describe distinct actual rectangles and are not normalized.",
        "Nominal 95% paired Student-t intervals are not simultaneous across the grid; best-cell selection introduces selection bias. Small samples can be unreliable for skewed/discrete outcomes.",
        "Resource-resource cells retain identical arrivals and per-shipment processing requirements per seed. Workload factors change external demand/service and support single-seed exploration only.",
        "Undefined observations suppress complete aggregates and intervals; no zero substitution or complete-case selection. Fewer than three seeds and deterministic workloads have no uncertainty intervals.",
        "Positive interaction is favorable only for higher-is-better metrics. Zero observed variance is not a guarantee. Utilization is diagnostic; no costs or ROI are modeled.",
        "Started wait metrics exclude still-waiting shipments and may compare changed populations. Playback requires one measured seed; no average snapshot exists."};
}
InteractionReport run_interaction(const InteractionRequest& request,const simulation::ExecutionControl* control) {
    validate_interaction_request(request);InteractionReport r;r.request=request;std::sort(r.request.x_values.begin(),r.request.x_values.end());std::sort(r.request.y_values.begin(),r.request.y_values.end());
    const auto seeds=replication_seeds(request.plan);
    for(std::size_t s=0;s<seeds.size();++s) {
        if(control)control->update("Replication",std::to_string(seeds[s]),s+1,seeds.size());InteractionReplication row;row.seed=seeds[s];
        auto baseline=request.source;baseline.simulation.random_seed=seeds[s];if(control)control->update("Simulating baseline");row.baseline=detail::evaluate(baseline,control,r.simulation_executions,r.processed_events);
        row.cells.reserve(r.request.x_values.size()*r.request.y_values.size());
        for(auto y:r.request.y_values)for(auto x:r.request.x_values) {
            const auto name=std::to_string(x)+", "+std::to_string(y);if(control)control->update("Evaluating candidate",name,x,y);
            const bool original=x==parameter_value(request.source,request.x)&&y==parameter_value(request.source,request.y);
            row.cells.push_back(original?row.baseline:detail::evaluate(tested_definition(request,x,y,seeds[s]),control,r.simulation_executions,r.processed_events));
            if(control)control->update("Candidate complete",name,x,y);
        }
        r.replications.push_back(std::move(row));if(control)control->update("Replication complete",{},s+1,seeds.size());
    }
    if(control)control->update("Aggregating interaction results");const auto start=std::chrono::steady_clock::now();aggregate_interaction_report(r);
    r.aggregation_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(control)control->update("Interaction results ready");return r;
}
}
