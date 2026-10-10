#include "language_phrases.hpp"
#include <algorithm>

namespace simforge::scenario::language::detail::grammar
{
bool is(std::span<const Token> t,std::size_t i,std::string_view word) {return i<t.size() && t[i].text==word;}
std::size_t articles(std::span<const Token> t,std::size_t i)
{while(is(t,i,"the")||is(t,i,"a")||is(t,i,"an")||is(t,i,"our")||is(t,i,"my")) ++i;return i;}
std::optional<Entity> entity(std::span<const Token> t,std::size_t at)
{
    auto i=articles(t,at);
    if(is(t,i,"number")&&is(t,i+1,"of")) i=articles(t,i+2);
    if(i>=t.size()) return {};
    const auto word=t[i].text; Field field;auto end=i+1;
    if(word=="loading" && (is(t,end,"bay")||is(t,end,"bays"))) {field=Field::Bays;++end;}
    else if(word=="dock"||word=="docks"||word=="bay"||word=="bays") field=Field::Bays;
    else if(word=="worker"||word=="workers"||word=="staff"||word=="staffing") field=Field::Workers;
    else if(word=="forklift"||word=="forklifts") field=Field::Forklifts;
    else if(word=="lift"&&(is(t,end,"truck")||is(t,end,"trucks"))) {field=Field::Forklifts;++end;}
    else if((word=="shipment"||word=="delivery")&&is(t,end,"arrival")&&(is(t,end+1,"rate")||is(t,end+1,"frequency"))) {field=Field::ArrivalRate;end+=2;}
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
Span span(std::span<const Token> t,std::size_t first,std::size_t end)
{return {t[first].source.begin,t[end-1].source.begin+t[end-1].source.size-t[first].source.begin};}
Parsed operation(std::span<const Token> t,std::size_t start,std::size_t end,Field field,Action action,Rational n,Unit unit,Pattern pattern)
{return {{field,action,n,unit,span(t,start,end),Origin::Rule,pattern},end};}
bool resource(Field field) {return field==Field::Bays||field==Field::Workers||field==Field::Forklifts;}
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
Pattern pattern(Field f)
{
    if(resource(f)) return Pattern::Capacity;
    switch(f) {case Field::ArrivalInterval:return Pattern::Interval;case Field::ArrivalRate:return Pattern::Rate;
    case Field::Processing:return Pattern::Processing;case Field::Horizon:return Pattern::Horizon;case Field::TickRate:return Pattern::TickRate;
    case Field::Seed:return Pattern::Seed;default:return Pattern::Structured;}
}
const char* entity_name(Field f)
{
    switch(f) {
    case Field::Bays:return "loading bay capacity";case Field::Workers:return "worker capacity";
    case Field::Forklifts:return "forklift capacity";case Field::ArrivalInterval:return "arrival interval";
    case Field::ArrivalRate:return "arrival frequency";case Field::Processing:return "processing duration";
    case Field::Horizon:return "simulation duration";case Field::TickRate:return "tick rate";case Field::Seed:return "random seed";
    }return "a supported parameter";
}

namespace
{
bool eat(std::span<const Token> t,std::size_t& at,std::initializer_list<std::string_view> words)
{
    auto next=at;for(const auto word:words) {if(!is(t,next,word)) return false;++next;}
    at=next;return true;
}
struct Factor {Rational value;std::size_t end;};
std::optional<Factor> multiplier(std::span<const Token> t,std::size_t at)
{
    if(is(t,at,"twice")) return Factor{{2,1},at+1};
    if(is(t,at,"triple")||is(t,at,"thrice")) return Factor{{3,1},at+1};
    if(is(t,at,"half")) return Factor{{1,2},at+1};
    if(const auto q=quantity(t,at);q && (is(t,q->end,"times")||is(t,q->end,"x")))
        return Factor{q->number,q->end+1};
    return {};
}
bool reference(std::span<const Token> t,std::size_t& at)
{
    return eat(t,at,{"what","it","is","now"})||eat(t,at,{"what","it","currently","is"})||
        eat(t,at,{"its","current","value"})||eat(t,at,{"of","its","current","value"})||
        eat(t,at,{"of","the","current","count"})||eat(t,at,{"of","the","current","number"})||
        eat(t,at,{"the","current","count"})||eat(t,at,{"the","current","number"})||
        eat(t,at,{"the","current","value"});
}
std::optional<Parsed> factor_before_entity(std::span<const Token> t,std::size_t first,std::size_t at)
{
    const auto factor=multiplier(t,at);if(!factor) return {};
    auto next=factor->end;
    const bool relation=eat(t,next,{"as","many"})||eat(t,next,{"the","current","number","of"})||
        eat(t,next,{"the","existing","number","of"});
    // Compact 2x/3x notation is an explicit factor even without "as many".
    if(!relation && !is(t,factor->end-1,"x")) return {};
    if(const auto f=entity(t,next);f && resource(f->field))
        return operation(t,first,f->end,f->field,Action::Multiply,factor->value,Unit::Factor,Pattern::Relative);
    return {};
}
std::optional<Parsed> factor_after_entity(std::span<const Token> t,std::size_t first,Entity f,std::size_t at)
{
    const auto factor=multiplier(t,at);if(!factor) return {};
    auto end=factor->end;Action action=Action::Multiply;
    if(f.field==Field::Processing && eat(t,end,{"as","fast"})) action=Action::Divide;
    else if(f.field==Field::Processing && eat(t,end,{"as","long"})) {}
    else if(!reference(t,end)) return {};
    if(f.field==Field::TickRate||f.field==Field::Seed) return {};
    return operation(t,first,end,f.field,action,factor->value,Unit::Factor,Pattern::Relative);
}
enum class Verb {Assignment,Addition,Subtraction};
struct Request {Verb kind;std::size_t end;};
std::optional<Request> request(std::span<const Token> t,std::size_t at)
{
    auto end=at;
    if(eat(t,end,{"get","rid","of"})||eat(t,end,{"take","away"})) return Request{Verb::Subtraction,end};
    if(eat(t,end,{"put","in"})||eat(t,end,{"bring","in"})||eat(t,end,{"give","us"})) return Request{Verb::Assignment,end};
    if(is(t,at,"get")||is(t,at,"hire")||is(t,at,"install")) return Request{Verb::Assignment,at+1};
    if(is(t,at,"add")||is(t,at,"increase")||is(t,at,"raise")) return Request{Verb::Addition,at+1};
    if(is(t,at,"remove")||is(t,at,"reduce")||is(t,at,"decrease")||is(t,at,"drop")||is(t,at,"cut")) return Request{Verb::Subtraction,at+1};
    if(is(t,at,"use")||is(t,at,"set")||is(t,at,"have")||is(t,at,"allocate")) return Request{Verb::Assignment,at+1};
    return {};
}
Action action(Verb verb)
{return verb==Verb::Addition?Action::Add:verb==Verb::Subtraction?Action::Subtract:Action::Set;}
bool extra(std::span<const Token> t,std::size_t at)
{return is(t,at,"more")||is(t,at,"additional")||is(t,at,"extra")||is(t,at,"another");}
std::optional<Parsed> quantity_before_entity(std::span<const Token> t,std::size_t first,std::size_t at,Verb verb)
{
    auto kind=action(verb);bool relative=false;
    if(is(t,at,"another")) {relative=true;kind=Action::Add;++at;}
    const auto q=quantity(t,at);if(!q) return {};
    auto next=q->end;
    if(extra(t,next)) {relative=true;kind=Action::Add;++next;}
    else if(is(t,next,"fewer")) {relative=true;kind=Action::Subtract;++next;}
    const auto f=entity(t,next);if(!f||!resource(f->field)) return {};
    auto end=f->end;
    if(eat(t,end,{"on","top","of","the","existing","number"})) {relative=true;kind=Action::Add;}
    if(verb==Verb::Subtraction) {
        if(kind==Action::Add) fail(Code::AmbiguousInstruction,"Removal conflicts with an additional-capacity modifier");
        (void)eat(t,end,{"from","the","warehouse"});
    }
    if(verb==Verb::Addition && kind==Action::Subtract) fail(Code::AmbiguousInstruction,"Addition conflicts with fewer resources");
    // Legacy numeric commands retain their exact pattern/span through generic().
    const bool conversational_verb=is(t,first,"get")||is(t,first,"hire")||is(t,first,"install")||is(t,first,"put")||is(t,first,"bring")||is(t,first,"give");
    if(!relative && !conversational_verb && end==f->end && !is(t,first,"drop") && !is(t,first,"take")) return {};
    return operation(t,first,end,f->field,kind,q->number,Unit::Count,kind==Action::Set?Pattern::Capacity:Pattern::Relative);
}
std::optional<Parsed> entity_request(std::span<const Token> t,std::size_t first,std::size_t at,Verb verb)
{
    const auto f=entity(t,at);if(!f) return {};
    auto next=f->end;
    if(is(t,first,"cut") && eat(t,next,{"in","half"}))
        return operation(t,first,next,f->field,Action::Multiply,{1,2},Unit::Factor,Pattern::Relative);
    if(is(t,next,"to")) {
        ++next;
        if(const auto parsed=factor_after_entity(t,first,*f,next)) return parsed;
        const auto q=quantity(t,next);if(!q) return {};
        auto end=q->end;
        if(is(t,end,"%")||is(t,end,"percent")) {
            ++end;if(!reference(t,end)) return {};
            return operation(t,first,end,f->field,Action::Multiply,divide(q->number,{100,1}),Unit::Factor,Pattern::Relative);
        }
        if(verb==Verb::Addition||verb==Verb::Subtraction) {
            const auto a=amount(t,next,f->field,false);if(!a) return {};
            return operation(t,first,a->end,f->field,Action::Set,a->number,a->unit,pattern(f->field));
        }
    }
    next=f->end;
    if(eat(t,next,{"by","a","factor","of"})) {
        const auto q=quantity(t,next);if(!q) return {};
        return operation(t,first,q->end,f->field,Action::Multiply,q->number,Unit::Factor,Pattern::Relative);
    }
    if(is(t,first,"drop") && eat(t,next,{"by"})) {
        const auto a=amount(t,next,f->field,false);if(!a) return {};
        return operation(t,first,a->end,f->field,a->unit==Unit::Percent?Action::DecreasePercent:Action::Subtract,a->number,a->unit,Pattern::Relative);
    }
    return {};
}
std::optional<Parsed> predicate(std::span<const Token> t,std::size_t first,std::size_t at)
{
    auto f=entity(t,at);if(!f) return {};
    auto next=f->end;
    if(eat(t,next,{"should","be"})) {
        if(is(t,next,"doubled")||is(t,next,"tripled")||is(t,next,"halved")) {
            const auto n=is(t,next,"doubled")?Rational{2,1}:is(t,next,"tripled")?Rational{3,1}:Rational{1,2};
            return operation(t,first,next+1,f->field,Action::Multiply,n,Unit::Factor,Pattern::Relative);
        }
        return {};
    }
    if(f->field==Field::Processing) (void)eat(t,next,{"should","take"});
    return factor_after_entity(t,first,*f,next);
}
std::optional<Parsed> processing_change(std::span<const Token> t,std::size_t first)
{
    auto next=first;
    if(eat(t,next,{"make","each","shipment","take"}))
        return factor_after_entity(t,first,{Field::Processing,next},next);
    next=first;
    if(eat(t,next,{"shave"})) {
        const auto a=amount(t,next,Field::Processing,false);if(!a) return {};
        next=a->end;if(!eat(t,next,{"off"})) return {};
        if(const auto f=entity(t,next);f&&f->field==Field::Processing)
            return operation(t,first,f->end,f->field,Action::Subtract,a->number,a->unit,Pattern::Relative);
    }
    next=first;
    if(eat(t,next,{"add"})) {
        const auto a=amount(t,next,Field::Processing,false);if(!a) return {};
        next=a->end;if(!eat(t,next,{"to"})) return {};
        if(const auto f=entity(t,next);f&&f->field==Field::Processing)
            return operation(t,first,f->end,f->field,Action::Add,a->number,a->unit,Pattern::Relative);
    }
    return {};
}
bool shipments(std::span<const Token> t,std::size_t at)
{return is(t,at,"shipments")||is(t,at,"deliveries");}
std::optional<Parsed> arrival_change(std::span<const Token> t,std::size_t first)
{
    auto next=first;
    if(is(t,next,"receive")) ++next;
    else if(is(t,next,"make")||is(t,next,"have")) ++next;
    else return {};
    // "receive three times as many shipments per hour" is relative frequency.
    if(const auto factor=multiplier(t,next)) {
        next=factor->end;
        if(!eat(t,next,{"as","many"})||!shipments(t,next)) return {};
        ++next;if(!eat(t,next,{"per"})||next>=t.size()||!duration_unit(t[next].text)) return {};
        return operation(t,first,next+1,Field::ArrivalRate,Action::Multiply,factor->value,Unit::Factor,Pattern::Relative);
    }
    if(!shipments(t,next)) return {};
    ++next;(void)eat(t,next,{"arrive"});
    if(eat(t,next,{"every"})) {
        const auto a=amount(t,next,Field::ArrivalInterval,false);if(!a) return {};
        return operation(t,first,a->end,Field::ArrivalInterval,Action::Set,a->number,a->unit,Pattern::Interval);
    }
    const auto factor=multiplier(t,next);if(!factor) return {};
    next=factor->end;
    if(!eat(t,next,{"as","often"})&&!eat(t,next,{"as","frequently"})) return {};
    return operation(t,first,next,Field::ArrivalRate,Action::Multiply,factor->value,Unit::Factor,Pattern::Relative);
}
}
bool topic(std::span<const Token> t)
{const auto f=is(t,0,"for")?entity(t,1):std::nullopt;return f&&f->end==t.size()&&resource(f->field);}
std::optional<Parsed> conversational(std::span<const Token> t,std::size_t start)
{
    if(start>=t.size()) return {};
    if(is(t,start,"for")) {
        const auto f=entity(t,start+1);if(!f||!is(t,f->end,",")) return {};
        const auto verb=request(t,f->end+1);if(!verb) return {};
        const auto q=quantity(t,verb->end);if(!q) return {};
        return operation(t,start,q->end,f->field,action(verb->kind),q->number,Unit::Count,Pattern::Relative);
    }
    if(const auto p=arrival_change(t,start)) return p;
    if(const auto p=processing_change(t,start)) return p;
    if(is(t,start,"triple")) {
        if(const auto f=entity(t,start+1)) return operation(t,start,f->end,f->field,Action::Multiply,{3,1},Unit::Factor,Pattern::Relative);
    }
    if(is(t,start,"make")) return predicate(t,start,start+1);
    if(const auto p=predicate(t,start,start)) return p;
    const auto verb=request(t,start);
    const auto at=verb?verb->end:start;
    if(const auto p=factor_before_entity(t,start,at)) return p;
    if(verb) if(const auto p=entity_request(t,start,at,verb->kind)) return p;
    return quantity_before_entity(t,start,at,verb?verb->kind:Verb::Assignment);
}
}
