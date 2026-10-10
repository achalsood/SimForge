#include "language_internal.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace simforge::scenario::language
{
namespace
{
template<class E> bool enum_valid(E value,E last) {return static_cast<int>(value)>=0&&static_cast<int>(value)<=static_cast<int>(last);}
template<std::size_t N> const char* label(int i,const std::array<const char*,N>& names)
{if(i<0||static_cast<std::size_t>(i)>=N) throw std::invalid_argument("Unsupported scenario-language enum");return names[i];}
using Timer=std::chrono::steady_clock;
double us(Timer::time_point start) {return std::chrono::duration<double,std::micro>(Timer::now()-start).count();}
bool resource(Field f) {return f==Field::Bays||f==Field::Workers||f==Field::Forklifts;}
Rational converted(const Operation& op)
{
    const auto q=rational(op.value.numerator,op.value.denominator);
    if(op.action==Action::Multiply||op.action==Action::Divide) {
        if(op.unit!=Unit::Factor) detail::fail(Code::InvalidUnit,"Multiplication/division require a dimensionless factor");return q;
    }
    if(op.action==Action::IncreasePercent||op.action==Action::DecreasePercent) {
        if(op.unit!=Unit::Percent) detail::fail(Code::InvalidUnit,"Percentage changes require percent units");return detail::divide(q,{100,1});
    }
    if(resource(op.field)) {
        if(op.unit!=Unit::Count) detail::fail(Code::InvalidUnit,"Resource capacities require counts");return q;
    }
    if(op.field==Field::Seed) {
        if(op.unit!=Unit::Integer||op.action!=Action::Set) detail::fail(Code::InvalidUnit,"Random seed supports absolute integer assignment only");return q;
    }
    if(op.field==Field::TickRate) {
        if(op.unit!=Unit::Hertz||op.action!=Action::Set) detail::fail(Code::InvalidUnit,"Tick rate supports absolute Hz assignment only");return q;
    }
    if(op.field==Field::ArrivalRate) {
        if(op.unit==Unit::PerSecond) return q;
        if(op.unit==Unit::PerMinute) return detail::divide(q,{60,1});
        if(op.unit==Unit::PerHour) return detail::divide(q,{3600,1});
        detail::fail(Code::InvalidUnit,"Arrival rate requires shipments per second, minute or hour");
    }
    if(op.unit==Unit::Seconds) return q;
    if(op.unit==Unit::Minutes) return detail::multiply(q,{60,1});
    if(op.unit==Unit::Hours) return detail::multiply(q,{3600,1});
    detail::fail(Code::InvalidUnit,"Durations require explicit seconds, minutes or hours");
}
void assign(ScenarioDefinition& d,Field field,std::uint64_t n)
{
    field=physical_field(field);
    auto& w=std::get<WarehouseConfig>(d.domain);
    const auto maximum=resource(field)?100000ULL:field==Field::TickRate?1000ULL:field==Field::Seed?std::numeric_limits<std::uint64_t>::max():604800ULL;
    if(n>maximum) detail::fail(Code::Range,"Proposed value exceeds the authoritative field range");
    switch(field) {
    case Field::Bays:w.loading_bays=static_cast<std::uint32_t>(n);break;
    case Field::Workers:w.workers=static_cast<std::uint32_t>(n);break;
    case Field::Forklifts:w.forklifts=static_cast<std::uint32_t>(n);break;
    case Field::ArrivalInterval:w.shipment_interval_seconds=static_cast<std::uint32_t>(n);break;
    case Field::Processing:w.processing_duration_seconds=static_cast<std::uint32_t>(n);break;
    case Field::Horizon:d.simulation.duration_seconds=static_cast<std::uint32_t>(n);break;
    case Field::TickRate:d.simulation.tick_rate=static_cast<std::uint32_t>(n);break;
    case Field::Seed:d.simulation.random_seed=n;break;
    default:detail::fail(Code::UnsupportedInstruction,"Unsupported physical field");
    }
}
}
Field physical_field(Field f)
{
    if(!enum_valid(f,Field::Seed)) throw std::invalid_argument("Unsupported language field");
    return f==Field::ArrivalRate?Field::ArrivalInterval:f;
}
std::size_t field_index(Field field)
{
    const auto f=physical_field(field);
    const auto it=std::find(physical_fields.begin(),physical_fields.end(),f);
    if(it==physical_fields.end()) throw std::invalid_argument("Unsupported physical language field");
    return static_cast<std::size_t>(it-physical_fields.begin());
}
const char* field_name(Field f)
{return label(static_cast<int>(f),std::array{"loading_bays","workers","forklifts","arrival_interval","arrival_rate","processing_duration","simulation_duration","tick_rate","random_seed"});}
const char* action_name(Action a)
{return label(static_cast<int>(a),std::array{"set","add","subtract","multiply","divide","increase_percent","decrease_percent"});}
const char* unit_name(Unit u)
{return label(static_cast<int>(u),std::array{"count","seconds","minutes","hours","factor","percent","hz","integer","per_second","per_minute","per_hour"});}
const char* code_name(Code c)
{return label(static_cast<int>(c),std::array{"input_limit","unsupported_character","invalid_number","invalid_unit","unsupported","ambiguous","conflict","precision","range","overflow","invalid_source","no_operations","inherited_defaults"});}
const char* pattern_name(Pattern p)
{return label(static_cast<int>(p),std::array{"capacity_phrase","arrival_interval_phrase","arrival_rate_phrase","processing_phrase","horizon_phrase","tick_rate_phrase","seed_phrase","relative_change","review_edit","structured_input"});}
std::uint64_t value(const ScenarioDefinition& d,Field field)
{
    const auto& w=std::get<WarehouseConfig>(d.domain);
    switch(physical_field(field)) {
    case Field::Bays:return w.loading_bays;case Field::Workers:return w.workers;case Field::Forklifts:return w.forklifts;
    case Field::ArrivalInterval:return w.shipment_interval_seconds;case Field::Processing:return w.processing_duration_seconds;
    case Field::Horizon:return d.simulation.duration_seconds;case Field::TickRate:return d.simulation.tick_rate;case Field::Seed:return d.simulation.random_seed;
    default:throw std::invalid_argument("Unsupported scenario field");
    }
}
ScenarioDefinition default_source()
{return {1,"Warehouse Baseline",{60,28800,12345},WarehouseConfig{3,10,2,300,900}};}
Application apply_operations(const ScenarioDefinition& source,std::span<const Operation> operations)
{
    Application result;
    try {validate(source);if(!std::holds_alternative<WarehouseConfig>(source.domain)) throw std::invalid_argument("Warehouse scenario required");}
    catch(const std::exception& e) {detail::diagnose(result.diagnostics,Code::InvalidSource,Stage::Source,{},e.what());return result;}
    result.definition=source;
    for(std::size_t i=0;i<physical_fields.size();++i) result.computed[i]=Rational{value(source,physical_fields[i]),1};
    if(operations.size()>max_operations) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Application,{},"At most 64 typed operations are supported");return result;}
    std::array<unsigned,8> counts{};
    for(const auto& op:operations) {
        if(!enum_valid(op.field,Field::Seed)||!enum_valid(op.action,Action::DecreasePercent)||!enum_valid(op.unit,Unit::PerHour)||
            !enum_valid(op.origin,Origin::Structured)||!enum_valid(op.pattern,Pattern::Structured)) {
            detail::diagnose(result.diagnostics,Code::UnsupportedInstruction,Stage::Application,op.source,"Unsupported typed operation enum");continue;
        }
        ++counts[field_index(op.field)];
    }
    for(const auto& op:operations) {
        if(!enum_valid(op.field,Field::Seed)||!enum_valid(op.action,Action::DecreasePercent)||!enum_valid(op.unit,Unit::PerHour)||
            !enum_valid(op.origin,Origin::Structured)||!enum_valid(op.pattern,Pattern::Structured)) continue;
        const auto index=field_index(op.field);
        if(counts[index]>1) {detail::diagnose(result.diagnostics,Code::Conflict,Stage::Application,op.source,"Multiple instructions target the same field. Choose one explicit instruction; order is not inferred");continue;}
        try {
            const auto& workload=std::get<WarehouseConfig>(source.domain).workload;
            if((physical_field(op.field)==Field::ArrivalInterval && workload.arrivals!=ArrivalMode::Constant) ||
                (op.field==Field::Processing && workload.processing!=ProcessingMode::Constant))
                detail::fail(Code::UnsupportedInstruction,"Advanced workload timing must be edited in the Scenario Editor; fixed timing commands require constant mode");
            if((op.field==Field::TickRate||op.field==Field::Seed)&&op.action!=Action::Set)
                detail::fail(Code::UnsupportedInstruction,"Tick rate and seed allow absolute assignment only");
            auto current=Rational{value(source,op.field),1};
            if(op.field==Field::ArrivalRate) current=detail::divide({1,1},current);
            const auto amount=converted(op);Rational next;
            switch(op.action) {
            case Action::Set:next=amount;break;
            case Action::Add:next=detail::add(current,amount);break;
            case Action::Subtract:next=detail::subtract(current,amount);break;
            case Action::Multiply:next=detail::multiply(current,amount);break;
            case Action::Divide:next=detail::divide(current,amount);break;
            case Action::IncreasePercent:next=detail::multiply(current,detail::add({1,1},amount));break;
            case Action::DecreasePercent:next=detail::multiply(current,detail::subtract({1,1},amount));break;
            default:detail::fail(Code::UnsupportedInstruction,"Unsupported operation");
            }
            if(op.field==Field::ArrivalRate) next=detail::divide({1,1},next);
            result.computed[index]=next;
            if(next.denominator!=1) detail::fail(Code::Precision,"This produces a fractional capacity or second. No rounding is performed; choose an exact integer in Edit Proposal");
            assign(result.definition,op.field,next.numerator);
        } catch(const detail::Failure& e) {detail::diagnose(result.diagnostics,e.code,Stage::Application,op.source,e.what());}
    }
    return result;
}
Proposal compile(const ScenarioDefinition& source,Mode mode,Stamp stamp,std::string_view description,const Recognition& recognized)
{
    const auto began=Timer::now();Proposal result;result.mode_=mode;result.stamp_=stamp;
    result.description_=std::string(description.substr(0,max_input_bytes));
    if(description.size()>max_input_bytes) detail::diagnose(result.recognition_diagnostics_,Code::InputLimit,Stage::Tokenization,{},"Description exceeds 8192 bytes");
    if(!enum_valid(mode,Mode::Modify)) detail::diagnose(result.recognition_diagnostics_,Code::InvalidSource,Stage::Source,{},"Unsupported source mode");
    if(recognized.operations.size()>max_operations||recognized.diagnostics.size()>max_diagnostics) {
        detail::diagnose(result.recognition_diagnostics_,Code::InputLimit,Stage::Recognition,{},"Typed interpretation exceeds operation/diagnostic limits");
    } else {
        result.operations_=recognized.operations;
        for(const auto& d:recognized.diagnostics) {
            if(!enum_valid(d.severity,Severity::Error)||!enum_valid(d.code,Code::InheritedDefaults)||!enum_valid(d.stage,Stage::Source) ||
                (d.code!=Code::InheritedDefaults && d.severity!=Severity::Error))
                detail::diagnose(result.recognition_diagnostics_,Code::UnsupportedInstruction,Stage::Recognition,{},"Invalid diagnostic severity or code");
            else if(d.message.size()>4096||d.source.begin>result.description_.size()||d.source.size>result.description_.size()-d.source.begin)
                detail::diagnose(result.recognition_diagnostics_,Code::InputLimit,Stage::Recognition,{},"Invalid or oversized diagnostic provenance");
            else if(result.recognition_diagnostics_.size()<max_diagnostics) result.recognition_diagnostics_.push_back(d);
        }
        for(const auto& op:result.operations_) if(op.source.begin>result.description_.size()||op.source.size>result.description_.size()-op.source.begin)
            detail::diagnose(result.recognition_diagnostics_,Code::UnsupportedInstruction,Stage::Recognition,{},"Typed operation source span is outside the description");
    }
    result.diagnostics_=result.recognition_diagnostics_;
    const auto defaults=default_source();const auto& actual=mode==Mode::Create?defaults:source;
    try {
        validate(actual);if(!std::holds_alternative<WarehouseConfig>(actual.domain)) throw std::invalid_argument("Warehouse source required");
        result.source_=actual;result.source_identity_=serialize_scenario(actual);
    } catch(const std::exception& e) {
        detail::diagnose(result.diagnostics_,Code::InvalidSource,Stage::Source,{},e.what());result.timings_.total_us=us(began);return result;
    }
    if(mode==Mode::Create && result.diagnostics_.size()<max_diagnostics)
        result.diagnostics_.push_back({Severity::Information,Code::InheritedDefaults,Stage::Source,{},
            "Create inherits bundled warehouse-baseline parameters for every unspecified field. The suggested name is Described warehouse"});
    if(result.operations_.empty()&&!detail::errors(result.diagnostics_))
        detail::diagnose(result.diagnostics_,Code::NoOperations,Stage::Recognition,{},"No explicit supported numeric changes were recognized");
    const auto apply_began=Timer::now();auto applied=apply_operations(actual,result.operations_);result.timings_.apply_us=us(apply_began);
    result.proposed_=std::move(applied.definition);result.computed_=applied.computed;
    for(const auto& d:applied.diagnostics) detail::diagnose(result.diagnostics_,d.code,d.stage,d.source,d.message);
    if(mode==Mode::Create) result.proposed_.name="Described warehouse";
    const auto validate_began=Timer::now();
    try {validate(result.proposed_);} catch(const std::exception& e) {detail::diagnose(result.diagnostics_,Code::Range,Stage::Validation,{},e.what());}
    result.timings_.validate_us=us(validate_began);result.valid_=!detail::errors(result.diagnostics_);result.timings_.total_us=us(began);return result;
}
Proposal interpret(std::string_view description,Mode mode,const ScenarioDefinition& source,Stamp stamp)
{
    const auto began=Timer::now();const auto tokens_began=Timer::now();auto tokens=tokenize(description);const auto token_us=us(tokens_began);
    const auto recognition_began=Timer::now();auto recognized=recognize(description,tokens.tokens);const auto recognition_us=us(recognition_began);
    for(auto& d:tokens.diagnostics) if(recognized.diagnostics.size()<max_diagnostics) recognized.diagnostics.push_back(std::move(d));
    auto proposal=compile(source,mode,stamp,description,recognized);
    proposal.timings_.tokenize_us=token_us;proposal.timings_.recognize_us=recognition_us;proposal.timings_.total_us=us(began);return proposal;
}
Proposal revise(const Proposal& proposal,std::span<const Operation> corrections,std::optional<std::string> name)
{
    if(name && name->size()>256) throw std::length_error("Reviewed name exceeds 256 bytes");
    auto edits=proposal.corrections_;std::array<bool,8> supplied{};
    if(corrections.size()>physical_fields.size()) throw std::invalid_argument("At most one correction per physical field");
    for(auto edit:corrections) {
        if(edit.field==Field::ArrivalRate||edit.action!=Action::Set||edit.origin!=Origin::Review||edit.pattern!=Pattern::Review)
            throw std::invalid_argument("Review corrections must be explicit absolute physical-field assignments");
        const auto index=field_index(edit.field);if(supplied[index]) throw std::invalid_argument("Duplicate review correction");supplied[index]=true;
        std::erase_if(edits,[&](const auto& e){return physical_field(e.field)==edit.field;});edits.push_back(edit);
    }
    Recognition input{proposal.operations_,proposal.recognition_diagnostics_};
    for(const auto& d:proposal.diagnostics_) if(d.code==Code::Conflict && input.diagnostics.size()<max_diagnostics) {
        auto conflict=d;conflict.stage=Stage::Recognition;input.diagnostics.push_back(std::move(conflict));
    }
    for(const auto& edit:edits) {
        std::erase_if(input.operations,[&](const auto& op){return physical_field(op.field)==edit.field;});input.operations.push_back(edit);
    }
    auto result=compile(proposal.source_,proposal.mode_,proposal.stamp_,proposal.description_,input);
    result.operations_=proposal.operations_;result.corrections_=std::move(edits);result.recognition_diagnostics_=proposal.recognition_diagnostics_;
    result.reviewed_name_=name?std::move(name):proposal.reviewed_name_;
    if(result.reviewed_name_) {
        result.proposed_.name=*result.reviewed_name_;
        try {validate(result.proposed_);}catch(const std::exception& e) {detail::diagnose(result.diagnostics_,Code::Range,Stage::Validation,{},e.what());}
    }
    std::array<unsigned,8> original_counts{};
    for(const auto& op:proposal.operations_) ++original_counts[field_index(op.field)];
    if(std::any_of(original_counts.begin(),original_counts.end(),[](unsigned count){return count>1;}))
        detail::diagnose(result.diagnostics_,Code::Conflict,Stage::Recognition,{},"Conflicting original instructions require revised text and interpretation");
    result.valid_=!detail::errors(result.diagnostics_);return result;
}
}
