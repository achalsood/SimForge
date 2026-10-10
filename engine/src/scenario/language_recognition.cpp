#include "language_internal.hpp"
#include <algorithm>

namespace simforge::scenario::language
{
namespace
{
using detail::Quantity;
bool is(std::span<const Token> t,std::size_t i,std::string_view word) {return i<t.size() && t[i].text==word;}
std::size_t articles(std::span<const Token> t,std::size_t i)
{while(is(t,i,"the")||is(t,i,"a")||is(t,i,"an")) ++i;return i;}
struct Entity {Field field;std::size_t end;};
std::optional<Entity> entity(std::span<const Token> t,std::size_t at)
{
    auto i=articles(t,at);
    if(is(t,i,"number")&&is(t,i+1,"of")) i=articles(t,i+2);
    if(i>=t.size()) return {};
    const auto word=t[i].text; Field field;auto end=i+1;
    if(word=="loading" && (is(t,end,"bay")||is(t,end,"bays"))) {field=Field::Bays;++end;}
    else if(word=="dock"||word=="docks"||word=="bay"||word=="bays") field=Field::Bays;
    else if(word=="worker"||word=="workers"||word=="staff") field=Field::Workers;
    else if(word=="forklift"||word=="forklifts") field=Field::Forklifts;
    else if(word=="lift"&&(is(t,end,"truck")||is(t,end,"trucks"))) {field=Field::Forklifts;++end;}
    else if(word=="arrival"||word=="arrivals") {
        if(is(t,end,"rate")||is(t,end,"frequency")) {field=Field::ArrivalRate;++end;}
        else {field=Field::ArrivalInterval;if(is(t,end,"interval")) ++end;}
    } else if(word=="interval"&&is(t,end,"between")&&is(t,end+1,"shipments")) {field=Field::ArrivalInterval;end+=2;}
    else if(word=="processing") {field=Field::Processing;if(is(t,end,"time")||is(t,end,"duration")) ++end;}
    else if(word=="simulation") {field=Field::Horizon;if(is(t,end,"time")||is(t,end,"duration")||is(t,end,"horizon")) ++end;}
    else if(word=="duration"||word=="horizon"||word=="shift") field=Field::Horizon;
    else if(word=="tick"&&is(t,end,"rate")) {field=Field::TickRate;++end;}
    else if(word=="seed") field=Field::Seed;
    else if(word=="random"&&is(t,end,"seed")) {field=Field::Seed;++end;}
    else return {};
    if(field<=Field::Forklifts && (is(t,end,"capacity")||is(t,end,"count"))) ++end;
    return Entity{field,end};
}
struct Parsed {Operation operation;std::size_t end;};
Span span(std::span<const Token> t,std::size_t first,std::size_t end)
{return {t[first].source.begin,t[end-1].source.begin+t[end-1].source.size-t[first].source.begin};}
Parsed operation(std::span<const Token> t,std::size_t start,std::size_t end,Field field,Action action,Rational n,Unit unit,Pattern pattern)
{return {{field,action,n,unit,span(t,start,end),Origin::Rule,pattern},end};}
bool resource(Field field) {return field==Field::Bays||field==Field::Workers||field==Field::Forklifts;}
struct Amount {Rational number;Unit unit;std::size_t end;};
std::optional<Amount> amount(std::span<const Token> t,std::size_t at,Field field,bool factor)
{
    const auto q=detail::quantity(t,at);if(!q) return {};
    auto end=q->end;
    if(is(t,end,"%")||is(t,end,"percent")||is(t,end,"percentage")) return Amount{q->number,Unit::Percent,end+1};
    if(factor) return Amount{q->number,Unit::Factor,end};
    if(field==Field::ArrivalRate) {
        if(is(t,end,"shipment")||is(t,end,"shipments")) ++end;
        if(is(t,end,"per")||is(t,end,"a")||is(t,end,"an")) {
            ++end;
            if(end<t.size()) if(const auto unit=detail::duration_unit(t[end].text)) {
                const auto rate=*unit==Unit::Seconds?Unit::PerSecond:*unit==Unit::Minutes?Unit::PerMinute:Unit::PerHour;
                return Amount{q->number,rate,end+1};
            }
        }
        return Amount{q->number,Unit::Count,end};
    }
    if(is(t,end,"-") && end+1<t.size() && detail::duration_unit(t[end+1].text)) ++end;
    if(end<t.size()) if(const auto unit=detail::duration_unit(t[end].text)) return Amount{q->number,*unit,end+1};
    if(is(t,end,"hz")||is(t,end,"hertz")) return Amount{q->number,Unit::Hertz,end+1};
    return Amount{q->number,field==Field::Seed?Unit::Integer:resource(field)?Unit::Count:Unit::Count,end};
}
std::optional<Parsed> arrival(std::span<const Token> t,std::size_t start)
{
    auto i=articles(t,start);
    // Rate assignments precede the one-shipment/every interval grammar.
    if(const auto q=detail::quantity(t,i);q && (is(t,q->end,"shipment")||is(t,q->end,"shipments"))) {
        auto end=q->end+1;
        if(is(t,end,"per")||is(t,end,"an")||is(t,end,"a")) {
            const auto a=amount(t,i,Field::ArrivalRate,false);
            return operation(t,start,a->end,Field::ArrivalRate,Action::Set,a->number,a->unit,Pattern::Rate);
        }
        if(is(t,end,"every")||((is(t,end,"arrives")||is(t,end,"arrive"))&&is(t,end+1,"every"))) {
            if(q->number!=Rational{1,1}) detail::fail(Code::UnsupportedInstruction,"Batch arrivals are unsupported; use an explicit shipments-per-time rate");
            if(!is(t,end,"every")) ++end;
            const auto a=amount(t,end+1,Field::ArrivalInterval,false);if(!a) return {};
            return operation(t,start,a->end,Field::ArrivalInterval,Action::Set,a->number,a->unit,Pattern::Interval);
        }
    }
    if(is(t,i,"shipment")||is(t,i,"shipments")||is(t,i,"arrivals")) {
        ++i;if(is(t,i,"arrive")||is(t,i,"arrives")) ++i;
        if(is(t,i,"every")) {
            const auto a=amount(t,i+1,Field::ArrivalInterval,false);if(!a) return {};
            return operation(t,start,a->end,Field::ArrivalInterval,Action::Set,a->number,a->unit,Pattern::Interval);
        }
    }
    if(is(t,start,"make")&&is(t,start+1,"shipments")&&is(t,start+2,"arrive")&&is(t,start+3,"twice")&&is(t,start+4,"as")&&
        (is(t,start+5,"frequently")||is(t,start+5,"often")))
        return operation(t,start,start+6,Field::ArrivalRate,Action::Multiply,{2,1},Unit::Factor,Pattern::Relative);
    return {};
}
std::optional<Parsed> processing(std::span<const Token> t,std::size_t start)
{
    if(is(t,start,"take")||is(t,start,"takes")) {
        if(const auto a=amount(t,start+1,Field::Processing,false);a && is(t,a->end,"to")&&is(t,a->end+1,"process"))
            return operation(t,start,a->end+2,Field::Processing,Action::Set,a->number,a->unit,Pattern::Processing);
    }
    auto i=start;if(is(t,i,"each")||is(t,i,"a")) ++i;
    if(!is(t,i,"shipment")) return {};
    ++i;if(!is(t,i,"takes")&&!is(t,i,"requires")&&!is(t,i,"needs")) return {};
    const auto a=amount(t,i+1,Field::Processing,false);if(!a) return {};
    auto end=a->end;
    if(is(t,end,"to")&&is(t,end+1,"process")) end+=2;
    return operation(t,start,end,Field::Processing,Action::Set,a->number,a->unit,Pattern::Processing);
}
std::optional<Parsed> horizon(std::span<const Token> t,std::size_t start)
{
    auto i=start;
    if(is(t,i,"run")||is(t,i,"simulate")) {
        i=articles(t,i+1);if(is(t,i,"simulation")) ++i;if(is(t,i,"for")) ++i;
        if(const auto a=amount(t,i,Field::Horizon,false)) return operation(t,start,a->end,Field::Horizon,Action::Set,a->number,a->unit,Pattern::Horizon);
    }
    i=articles(t,start);
    const auto a=amount(t,i,Field::Horizon,false);if(!a) return {};
    auto end=a->end;if(is(t,end,"simulation")) ++end;
    if(is(t,end,"horizon")||is(t,end,"shift")) return operation(t,start,end+1,Field::Horizon,Action::Set,a->number,a->unit,Pattern::Horizon);
    return {};
}
Pattern pattern(Field f)
{
    if(resource(f)) return Pattern::Capacity;
    switch(f) {case Field::ArrivalInterval:return Pattern::Interval;case Field::ArrivalRate:return Pattern::Rate;
    case Field::Processing:return Pattern::Processing;case Field::Horizon:return Pattern::Horizon;case Field::TickRate:return Pattern::TickRate;
    case Field::Seed:return Pattern::Seed;default:return Pattern::Structured;}
}
std::optional<Parsed> generic(std::span<const Token> t,std::size_t start)
{
    if(start>=t.size()) return {};
    auto at=start;Action action=Action::Set;bool verb=false,factor=false;
    const auto word=t[at].text;
    if(word=="double"||word=="halve") {
        if(const auto f=entity(t,at+1)) return operation(t,start,f->end,f->field,word=="double"?Action::Multiply:Action::Divide,{2,1},Unit::Factor,Pattern::Relative);
        return {};
    }
    if(word=="set"||word=="use"||word=="have"||word=="allocate") {verb=true;++at;}
    else if(word=="add"||word=="increase"||word=="raise"||word=="extend") {action=Action::Add;verb=true;++at;}
    else if(word=="remove"||word=="subtract"||word=="decrease"||word=="reduce"||word=="lower"||word=="cut") {action=Action::Subtract;verb=true;++at;}
    else if(word=="multiply"||word=="divide") {action=word=="multiply"?Action::Multiply:Action::Divide;verb=factor=true;++at;}
    at=articles(t,at);
    if(const auto f=entity(t,at)) {
        if(word=="extend"&&f->field!=Field::Horizon) return {};
        at=f->end;
        if(action==Action::Set) {
            if(is(t,at,"to")||is(t,at,"of")||is(t,at,"for")||is(t,at,":")) ++at;
            else if(f->field==Field::Processing && (is(t,at,"takes")||is(t,at,"requires"))) ++at;
            else if(f->field==Field::ArrivalInterval && is(t,at,"every")) ++at;
        } else {
            if(is(t,at,"by")) ++at;
            else return {};
        }
        if(const auto a=amount(t,at,f->field,factor)) {
            auto kind=action;
            if(a->unit==Unit::Percent && action==Action::Add) kind=Action::IncreasePercent;
            if(a->unit==Unit::Percent && action==Action::Subtract) kind=Action::DecreasePercent;
            return operation(t,start,a->end,f->field,kind,a->number,a->unit,action==Action::Set?pattern(f->field):Pattern::Relative);
        }
        return {};
    }
    if(factor) return {};
    if(const auto q=detail::quantity(t,at)) {
        if(const auto f=entity(t,q->end);f && resource(f->field))
            return operation(t,start,f->end,f->field,action,q->number,Unit::Count,action==Action::Set?Pattern::Capacity:Pattern::Relative);
        if(action==Action::Set && (is(t,q->end,"hz")||is(t,q->end,"hertz")))
            return operation(t,start,q->end+1,Field::TickRate,Action::Set,q->number,Unit::Hertz,Pattern::TickRate);
    }
    if(verb && action==Action::Set) {
        if(const auto parsed=arrival(t,at)) {auto p=*parsed;p.operation.source=span(t,start,p.end);return p;}
        if(const auto parsed=horizon(t,at)) {auto p=*parsed;p.operation.source=span(t,start,p.end);return p;}
    }
    return {};
}
std::size_t prefix(std::span<const Token> t)
{
    std::size_t at=0;if(is(t,at,"please")) ++at;
    if((is(t,at,"i")&&(is(t,at+1,"need")||is(t,at+1,"want")))||(is(t,at,"we")&&is(t,at+1,"need"))) at+=2;
    if(is(t,at,"create")||is(t,at,"build")) {
        const auto next=articles(t,at+1);
        if(is(t,next,"warehouse")&&is(t,next+1,"with")) at=next+2;
    }
    return at;
}
bool unchanged(std::span<const Token> t)
{
    return t.size()==4&&is(t,0,"keep")&&is(t,1,"everything")&&is(t,2,"else")&&is(t,3,"unchanged");
}
bool uncertain(std::span<const Token> t)
{
    constexpr std::array words{"not","no","never","without","except","if","unless","or","maybe","approximately","about","roughly","optimal","faster","more","less","pm","am"};
    for(const auto& token:t) for(const auto* word:words) if(token.text==word) return true;
    for(std::size_t i=0;i<t.size();++i) if(is(t,i,"at")&&(is(t,i+1,"least")||is(t,i+1,"most"))) return true;
    for(std::size_t i=0;i<t.size();++i) if(is(t,i,"between") && !(i && is(t,i-1,"interval") && is(t,i+1,"shipments"))) return true;
    return false;
}
void clause(std::string_view text,std::span<const Token> t,Recognition& result)
{
    if(t.empty()||unchanged(t)) return;
    const auto whole=span(t,0,t.size());
    if(uncertain(t)) {
        detail::diagnose(result.diagnostics,Code::AmbiguousInstruction,Stage::Recognition,whole,
            "Specify one unconditional numeric change to a supported parameter; goals, alternatives, estimates and clock times are not constraints");return;
    }
    const auto start=prefix(t);
    try {
        std::optional<Parsed> parsed=arrival(t,start);
        if(!parsed) parsed=processing(t,start);
        if(!parsed) parsed=horizon(t,start);
        if(!parsed) parsed=generic(t,start);
        if(parsed) {
            if(result.operations.size()==max_operations) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,whole,"At most 64 operations are supported");return;}
            result.operations.push_back(parsed->operation);
            if(parsed->end<t.size()) detail::diagnose(result.diagnostics,Code::UnsupportedInstruction,Stage::Recognition,span(t,parsed->end,t.size()),
                "Unrecognized material after this instruction. Remove it or express it as a separate supported clause");
            return;
        }
    } catch(const detail::Failure& e) {detail::diagnose(result.diagnostics,e.code,Stage::Recognition,whole,e.what());return;}
    bool known=false;
    for(std::size_t i=0;i<t.size();++i) if(entity(t,i)||is(t,i,"process")||is(t,i,"waiting")||is(t,i,"throughput")||is(t,i,"capacity")) known=true;
    detail::diagnose(result.diagnostics,known?Code::AmbiguousInstruction:Code::UnsupportedInstruction,Stage::Recognition,whole,
        known?"Specify a supported field, an exact quantity and appropriate units; no numeric change can be inferred from this request":
            "This instruction cannot be represented by aggregate bays, workers, forklifts, arrival interval, processing time or simulation duration");
    (void)text;
}
}
Recognition recognize(std::string_view text,std::span<const Token> tokens)
{
    Recognition result;
    if(text.size()>max_input_bytes || tokens.size()>max_tokens) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,{},"Input exceeds parser limits");return result;}
    for(const auto& token:tokens) if(token.source.begin>text.size()||token.source.size>text.size()-token.source.begin) {
        detail::diagnose(result.diagnostics,Code::UnsupportedCharacter,Stage::Recognition,{},"Token source span is invalid");return result;
    }
    std::size_t begin=0,clauses=0;
    for(std::size_t i=0;i<=tokens.size();++i) {
        if(i==tokens.size()||tokens[i].kind==TokenKind::Boundary||tokens[i].text=="and"||tokens[i].text=="then"||tokens[i].text=="but") {
            if(i>begin) {
                if(++clauses>128) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,{},"At most 128 clauses are supported");break;}
                clause(text,tokens.subspan(begin,i-begin),result);
            }
            begin=i+1;
        }
    }
    return result;
}
}
