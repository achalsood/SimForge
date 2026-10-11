#include <simforge/analysis/statistical_experiment.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace simforge::analysis {
std::string_view metric_name(StatisticalMetric m) {
    switch(m) {case StatisticalMetric::Completed:return "completed";case StatisticalMetric::Waiting:return "waiting";
    case StatisticalMetric::Throughput:return "throughput_per_hour";case StatisticalMetric::AverageWait:return "average_wait_seconds";
    case StatisticalMetric::MaximumWait:return "maximum_wait_seconds";}throw std::invalid_argument("Unsupported statistical metric");
}
std::string_view metric_unit(StatisticalMetric m) {(void)metric_name(m);return m==StatisticalMetric::Throughput?"shipments/hour":m==StatisticalMetric::AverageWait || m==StatisticalMetric::MaximumWait?"seconds":"shipments";}
bool higher_is_better(StatisticalMetric m) {(void)metric_name(m);return m==StatisticalMetric::Completed || m==StatisticalMetric::Throughput;}
StatisticalMetric objective_metric(Objective o) {(void)objective_name(o);return o==Objective::MaximizeCompleted?StatisticalMetric::Completed:o==Objective::MinimizeWaiting?StatisticalMetric::Waiting:StatisticalMetric::AverageWait;}
std::optional<double> metric_value(const warehouse::WarehouseMetrics& m,StatisticalMetric metric) {
    switch(metric) {case StatisticalMetric::Completed:return static_cast<double>(m.completed);case StatisticalMetric::Waiting:return static_cast<double>(m.waiting);
    case StatisticalMetric::Throughput:return m.throughput_per_hour;case StatisticalMetric::AverageWait:return m.average_wait_seconds;
    case StatisticalMetric::MaximumWait:return m.maximum_wait_seconds;}throw std::invalid_argument("Unsupported metric");
}
std::vector<std::uint64_t> replication_seeds(const ReplicationPlan& p) {
    if(p.count<1 || p.count>max_replications || p.generator_version!=scenario::workload_generator_version ||
        p.method_version!=statistical_method_version || p.confidence!=0.95) throw std::invalid_argument("Replications: 1..100; workload/method version 1 and confidence 0.95 required");
    std::vector<std::uint64_t> seeds;
    if(p.policy==SeedPolicy::Sequential) {
        if(!p.explicit_seeds.empty() || p.base_seed>std::numeric_limits<std::uint64_t>::max()-(p.count-1)) throw std::invalid_argument("Sequential seed overflow or conflicting explicit list");
        for(std::uint64_t i=0;i<p.count;++i) seeds.push_back(p.base_seed+i);
    } else if(p.policy==SeedPolicy::Explicit) {
        if(p.explicit_seeds.size()!=p.count || p.base_seed!=0) throw std::invalid_argument("Explicit policy requires exactly count seeds and no base seed");seeds=p.explicit_seeds;
    } else throw std::invalid_argument("Unsupported seed policy");
    if(std::set<std::uint64_t>(seeds.begin(),seeds.end()).size()!=seeds.size()) throw std::invalid_argument("Duplicate replication seeds");return seeds;
}
double student_t_critical_95(std::size_t df) {
    // NIST EDA 1.3.6.7.2, 0.975 column, published three-decimal precision.
    // No interpolation, normal approximation, or unvalidated quantile dependency.
    constexpr double table[]{12.706,4.303,3.182,2.776,2.571,2.447,2.365,2.306,2.262,2.228,
        2.201,2.179,2.160,2.145,2.131,2.120,2.110,2.101,2.093,2.086,2.080,2.074,2.069,2.064,2.060,
        2.056,2.052,2.048,2.045,2.042,2.040,2.037,2.035,2.032,2.030,2.028,2.026,2.024,2.023,2.021,
        2.020,2.018,2.017,2.015,2.014,2.013,2.012,2.011,2.010,2.009,2.008,2.007,2.006,2.005,2.004,
        2.003,2.002,2.002,2.001,2.000,2.000,1.999,1.998,1.998,1.997,1.997,1.996,1.995,1.995,1.994,
        1.994,1.993,1.993,1.993,1.992,1.992,1.991,1.991,1.990,1.990,1.990,1.989,1.989,1.989,1.988,
        1.988,1.988,1.987,1.987,1.987,1.986,1.986,1.986,1.986,1.985,1.985,1.985,1.984,1.984};
    static_assert(std::size(table)==99);
    if(df<1 || df>99) throw std::invalid_argument("Student-t degrees of freedom must be 1..99");return table[df-1];
}
namespace {
double sum(std::span<const double> values) {double s=0,c=0;for(double x:values) {const auto y=x-c,t=s+y;c=(t-s)-y;s=t;}return s;}
double mean(const std::vector<double>& v) {std::vector<double> centered;for(double x:v) centered.push_back(x-v.front());return v.front()+sum(centered)/v.size();}
void finite(double x) {if(!std::isfinite(x)) throw std::invalid_argument("Nonfinite statistical value or numerical overflow");}
}
PairedSummary summarize_pairs(std::span<const std::optional<double>> b,std::span<const std::optional<double>> c,bool higher,bool stochastic) {
    if(b.empty() || b.size()!=c.size() || b.size()>max_replications) throw std::invalid_argument("Invalid paired sample sizes");
    PairedSummary r;r.count=static_cast<std::uint32_t>(b.size());std::vector<double> bv,cv,d;
    for(std::size_t i=0;i<b.size();++i) {
        if(b[i]) finite(*b[i]);if(c[i]) finite(*c[i]);if(!b[i] || !c[i]) continue;
        const double delta=*c[i]-*b[i];finite(delta);bv.push_back(*b[i]);cv.push_back(*c[i]);d.push_back(delta);
        ++r.defined;if(delta>0) ++r.positive;else if(delta<0) ++r.negative;else ++r.zero;
    }
    if(r.defined!=r.count) {r.diagnostic="Undefined metric in one or more pairs; aggregate and interval unavailable (no complete-case selection).";return r;}
    r.baseline_mean=mean(bv);r.candidate_mean=mean(cv);r.mean_difference=mean(d);
    finite(*r.baseline_mean);finite(*r.candidate_mean);finite(*r.mean_difference);
    const auto limits=std::minmax_element(d.begin(),d.end());r.minimum=*limits.first;r.maximum=*limits.second;
    std::sort(d.begin(),d.end());r.median=d.size()%2?d[d.size()/2]:d[d.size()/2-1]/2+d[d.size()/2]/2;
    r.favorable_fraction=static_cast<double>(higher?r.positive:r.negative)/r.count;
    if(*r.baseline_mean!=0) {const auto percentage=(*r.candidate_mean-*r.baseline_mean)/ *r.baseline_mean*100;if(std::isfinite(percentage)) r.relative_change_percent=percentage;}
    if(d.size()>=2) {std::vector<double> squared;for(double x:d) {const auto v=x-*r.mean_difference;squared.push_back(v*v);}
        const double variance=sum(squared)/(d.size()-1);finite(variance);if(variance<0) throw std::runtime_error("Negative sample variance");r.standard_deviation=std::sqrt(variance);r.standard_error=*r.standard_deviation/std::sqrt(static_cast<double>(d.size()));}
    if(!stochastic) {r.diagnostic="Deterministic workload: seeds repeat the same model; no stochastic uncertainty interval.";return r;}
    if(d.size()<3) {r.diagnostic="Insufficient replications: fewer than three paired seeds; no confidence interval.";return r;}
    const double width=student_t_critical_95(d.size()-1)* *r.standard_error;
    r.lower=*r.mean_difference-width;r.upper=*r.mean_difference+width;finite(*r.lower);finite(*r.upper);
    r.diagnostic=*r.standard_deviation==0?"Zero observed variance; degenerate model interval is not a guarantee.":
        *r.lower<=0 && *r.upper>=0?"Interval includes zero; evidence is inconclusive.":
        (higher?*r.lower>0:*r.upper<0)?"Interval favors the candidate under the paired-t assumptions.":"Interval favors the baseline under the paired-t assumptions.";
    if(d.size()<20) r.diagnostic+=" Small sample: skew/discreteness may make the approximation unreliable.";
    return r;
}
}
