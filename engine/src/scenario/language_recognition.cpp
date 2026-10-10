#include "language_phrases.hpp"
#include <algorithm>

namespace simforge::scenario::language
{
namespace
{
using namespace detail::grammar;
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
    std::size_t at=0;
    if(is(t,at,"please")) ++at;
    if((is(t,at,"could")||is(t,at,"can")) && (is(t,at+1,"you")||is(t,at+1,"we"))) at+=2;
    if(is(t,at,"i'd")&&is(t,at+1,"like")&&is(t,at+2,"to")) at+=3;
    if(is(t,at,"let's")) ++at;
    if(is(t,at,"we")&&is(t,at+1,"could")&&is(t,at+2,"use")) at+=3;
    if((is(t,at,"i")&&(is(t,at+1,"need")||is(t,at+1,"want")))||(is(t,at,"we")&&is(t,at+1,"need"))) {
        at+=2;if(is(t,at,"to")) ++at;
    }
    if(is(t,at,"please")) ++at;
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
    constexpr std::array words{"not","no","never","without","except","if","unless","or","maybe","approximately","about","roughly","optimal","faster","less","pm","am","considered","discussed","decided","against","would","might"};
    for(const auto& token:t) {
        if(token.text.ends_with("n't")) return true;
        for(const auto* word:words) if(token.text==word) return true;
    }
    for(std::size_t i=0;i<t.size();++i) if(is(t,i,"more")) {
        bool quantified=false;
        try {
            for(std::size_t n=i>5?i-5:0;n<i;++n) if(const auto q=detail::quantity(t,n);q&&q->end==i) {
                const auto f=entity(t,i+1);quantified=f&&resource(f->field);break;
            }
        } catch(const detail::Failure&) {return true;}
        if(!quantified) return true;
    }
    for(std::size_t i=0;i<t.size();++i) if(is(t,i,"at")&&(is(t,i+1,"least")||is(t,i+1,"most"))) return true;
    for(std::size_t i=0;i<t.size();++i) if(is(t,i,"between") && !(i && is(t,i-1,"interval") && is(t,i+1,"shipments"))) return true;
    return false;
}
void clause(std::string_view text,std::span<const Token> t,Recognition& result)
{
    if(t.empty()||unchanged(t)||(t.size()==1&&is(t,0,"please"))) return;
    if(is(t,t.size()-1,"please")) t=t.first(t.size()-1);
    if(t.empty()) return;
    const auto whole=span(t,0,t.size());
    if(uncertain(t)) {
        std::string message="Specify one unconditional numeric change to a supported parameter; negation, goals, alternatives, estimates and clock times are not constraints";
        if(std::any_of(t.begin(),t.end(),[](const auto& token){return token.text=="more";})) {
            bool numeric=false;std::optional<Field> field;
            for(std::size_t i=0;i<t.size();++i) {
                if(const auto f=entity(t,i)) field=f->field;
                try {if(detail::quantity(t,i)) numeric=true;} catch(const detail::Failure&) {numeric=true;}
            }
            if(field&&!numeric) message=std::string{"I found a request to change "}+entity_name(*field)+", but no quantity was specified. State an exact count or factor";
        }
        detail::diagnose(result.diagnostics,Code::AmbiguousInstruction,Stage::Recognition,whole,
            std::move(message));return;
    }
    const auto start=prefix(t);
    try {
        std::optional<Parsed> parsed=arrival(t,start);
        if(!parsed) parsed=processing(t,start);
        if(!parsed) parsed=horizon(t,start);
        if(!parsed) parsed=conversational(t,start);
        if(!parsed) parsed=generic(t,start);
        if(parsed) {
            if(result.operations.size()==max_operations) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,whole,"At most 64 operations are supported");return;}
            result.operations.push_back(parsed->operation);
            if(parsed->end<t.size()) detail::diagnose(result.diagnostics,Code::UnsupportedInstruction,Stage::Recognition,span(t,parsed->end,t.size()),
                "Unrecognized material after this instruction. Remove it or express it as a separate supported clause");
            return;
        }
    } catch(const detail::Failure& e) {detail::diagnose(result.diagnostics,e.code,Stage::Recognition,whole,e.what());return;}
    bool known=false;std::optional<Field> known_field;

    for(std::size_t i=0;i<t.size();++i) {
        if(const auto f=entity(t,i)) {known=true;known_field=f->field;}
        if(is(t,i,"process")||is(t,i,"waiting")||is(t,i,"throughput")||is(t,i,"capacity")) known=true;
    }
    detail::diagnose(result.diagnostics,known?Code::AmbiguousInstruction:Code::UnsupportedInstruction,Stage::Recognition,whole,
        known_field?std::string{"I found a request involving "}+entity_name(*known_field)+", but could not associate an exact quantity and operation. Specify the count, factor, percentage or time units":
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
    std::size_t begin=0,clauses=0;bool alternative=false;
    for(std::size_t i=0;i<=tokens.size();++i) {
        if(i==tokens.size()||tokens[i].kind==TokenKind::Boundary||tokens[i].text=="and"||tokens[i].text=="then"||tokens[i].text=="but") {
            if(i>begin) {
                if(i<tokens.size() && tokens[i].text=="," && topic(tokens.subspan(begin,i-begin))) continue;
                if(++clauses>128) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,{},"At most 128 clauses are supported");break;}
                const auto local=tokens.subspan(begin,i-begin);
                // A bare numeric alternative after "and" inherits only the explicit
                // preceding absolute capacity field; it is always a conflict.
                bool numeric_alternative=false;
                if(alternative && !result.operations.empty()) {
                    try {
                        if(const auto q=detail::quantity(local,0);q&&q->end==local.size()) {
                            const auto& previous=result.operations.back();
                            if(previous.action==Action::Set&&resource(previous.field)) {
                                if(result.operations.size()==max_operations) detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Recognition,span(local,0,local.size()),"At most 64 operations are supported");
                                else result.operations.push_back(operation(local,0,local.size(),previous.field,Action::Set,q->number,Unit::Count,Pattern::Capacity).operation);
                                detail::diagnose(result.diagnostics,Code::Conflict,Stage::Recognition,span(local,0,local.size()),"Conflicting resource counts. Choose one absolute count");
                                numeric_alternative=true;
                            }
                        }
                    } catch(const detail::Failure&) {} // Ordinary clause path reports malformed numbers.
                }
                if(!numeric_alternative) clause(text,local,result);
            }
            alternative=i<tokens.size()&&tokens[i].text=="and";begin=i+1;
        }
    }
    return result;
}
}
