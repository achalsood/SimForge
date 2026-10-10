#include <simforge/scenario/scenario_language.hpp>
#include <algorithm>
#include <iostream>
#include <string>

using namespace simforge::scenario;
namespace nl=simforge::scenario::language;
namespace {
std::size_t cases=0, failures=0;
void expect(std::string text, nl::Field field, nl::Action action, nl::Rational amount,
    std::uint64_t result, nl::Unit unit=nl::Unit::Count, bool create=false,
    std::optional<ScenarioDefinition> custom_source={})
{
    auto source=custom_source.value_or(nl::default_source());
    auto expected=source;
    if(create) { source.name="Edited source"; std::get<WarehouseConfig>(source.domain).loading_bays=19; expected.name="Described warehouse"; }
    auto& c=std::get<WarehouseConfig>(expected.domain);
    switch(field) {
    case nl::Field::Bays:c.loading_bays=static_cast<std::uint32_t>(result);break;
    case nl::Field::Workers:c.workers=static_cast<std::uint32_t>(result);break;
    case nl::Field::Forklifts:c.forklifts=static_cast<std::uint32_t>(result);break;
    case nl::Field::ArrivalInterval:case nl::Field::ArrivalRate:c.shipment_interval_seconds=static_cast<std::uint32_t>(result);break;
    case nl::Field::Processing:c.processing_duration_seconds=static_cast<std::uint32_t>(result);break;
    case nl::Field::Horizon:expected.simulation.duration_seconds=static_cast<std::uint32_t>(result);break;
    case nl::Field::TickRate:expected.simulation.tick_rate=static_cast<std::uint32_t>(result);break;
    case nl::Field::Seed:expected.simulation.random_seed=result;break;
    }
    const auto p=nl::interpret(text,create?nl::Mode::Create:nl::Mode::Modify,source);
    ++cases;
    bool ok=p.valid() && p.operations().size()==1 && serialize_scenario(p.proposed())==serialize_scenario(expected);
    if(ok) { const auto& op=p.operations()[0]; ok=op.field==field&&op.action==action&&op.value==amount&&op.unit==unit&&op.origin==nl::Origin::Rule&&op.source.size&&op.source.begin+op.source.size<=text.size(); }
    if(!ok) {++failures; if(failures<=30) {std::cerr<<"Coverage miss: "<<text<<'\n';for(const auto& d:p.diagnostics()) std::cerr<<"  "<<nl::code_name(d.code)<<": "<<d.message<<'\n';}}
}
void reject(const std::string& text)
{
    ++cases;const auto p=nl::interpret(text,nl::Mode::Modify,nl::default_source());
    if(p.valid()) {++failures;std::cerr<<"Unsafe acceptance: "<<text<<'\n';}
}
struct Resource {const char* name;nl::Field field;std::uint64_t initial;};
constexpr Resource resources[]={{"bay",nl::Field::Bays,3},{"bays",nl::Field::Bays,3},{"loading bays",nl::Field::Bays,3},
    {"docks",nl::Field::Bays,3},{"workers",nl::Field::Workers,10},{"staff",nl::Field::Workers,10},
    {"staffing",nl::Field::Workers,10},{"forklifts",nl::Field::Forklifts,2},{"lift trucks",nl::Field::Forklifts,2}};
void run(std::string_view group)
{
    if(group=="requests") {
        constexpr const char* wrappers[]={"","Please ","Can you ","Could you please ","Can we ","Could we ","I want ","I want to see ",
            "I need ","We need ","We want ","I'd like ","I'd like to see ","I would like to see ","We would like ","Can I have ","Could I get ","Let's ",
            "I want you to ","I'd like you to ","I would like you to see ","We'd like to see "};
        for(const auto& r:resources) for(const auto wrapper:wrappers) for(const auto [number,word]:{std::pair{1u,"one"},std::pair{2u,"two"},std::pair{10u,"10"},std::pair{25u,"twenty-five"}}) {
            for(const auto modifier:{"more","additional","extra"})
                expect(std::string(wrapper)+word+" "+modifier+" "+r.name,r.field,nl::Action::Add,{number,1},r.initial+number);
            expect(std::string(wrapper)+word+" "+r.name,r.field,nl::Action::Set,{number,1},number);
        }
        expect("i want to see 10 more bays",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("I WANT TO SEE TEN MORE BAYS!",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("I want to see 10 more bays.",nl::Field::Bays,nl::Action::Add,{10,1},13,nl::Unit::Count,true);
        return;
    }
    if(group=="operations") {
        for(const auto& r:resources) {
            for(const auto verb:{"Add ","Get ","Put in ","Bring in ","Give us ","Install ","Use ","Have "})
                expect(std::string(verb)+"ten more "+r.name,r.field,nl::Action::Add,{10,1},r.initial+10);
            for(const auto verb:{"Remove ","Take away ","Get rid of ","Drop ","Use "})
                expect(std::string(verb)+"one fewer "+r.name,r.field,nl::Action::Subtract,{1,1},r.initial-1);
            for(const auto wrapper:{"Use ","Get ","We need ","I'd like ","I want to see "})
                expect(std::string(wrapper)+"twice as many "+r.name,r.field,nl::Action::Multiply,{2,1},r.initial*2,nl::Unit::Factor);
            expect(std::string("Increase ")+r.name+" by 200%",r.field,nl::Action::IncreasePercent,{200,1},r.initial*3,nl::Unit::Percent);
            expect(std::string("Increase ")+r.name+" to 200% of the current count",r.field,nl::Action::Multiply,{2,1},r.initial*2,nl::Unit::Factor);
        }
        expect("Please add ten more bays for me",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("Could you add ten more bays for us please",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("Add ten more bays, thank you",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("Add ten more bays thanks",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("Can you give me another two loading bays",nl::Field::Bays,nl::Action::Add,{2,1},5);
        expect("Please give me ten more loading bays",nl::Field::Bays,nl::Action::Add,{10,1},13);
        expect("I would like to have another 10 docks",nl::Field::Bays,nl::Action::Add,{10,1},13);
        auto custom=nl::default_source();custom.name="Current edited warehouse";
        auto& c=std::get<WarehouseConfig>(custom.domain);c.loading_bays=7;c.workers=14;c.forklifts=4;c.processing_duration_seconds=1200;
        for(const auto wrapper:{"I want to see ","I'd like ","We'd like ","Can you give me "}) {
            expect(std::string(wrapper)+"10 more bays",nl::Field::Bays,nl::Action::Add,{10,1},17,nl::Unit::Count,false,custom);
            expect(std::string(wrapper)+"twice as many workers",nl::Field::Workers,nl::Action::Multiply,{2,1},28,nl::Unit::Factor,false,custom);
            expect(std::string(wrapper)+"one fewer forklift",nl::Field::Forklifts,nl::Action::Subtract,{1,1},3,nl::Unit::Count,false,custom);
        }
        return;
    }
    if(group=="timing") {
        for(const auto wrapper:{"","Please ","I want to ","I'd like to ","I would like to ","Could you "}) {
            expect(std::string(wrapper)+"set processing time to 20 minutes",nl::Field::Processing,nl::Action::Set,{20,1},1200,nl::Unit::Minutes);
            expect(std::string(wrapper)+"reduce processing time by 20%",nl::Field::Processing,nl::Action::DecreasePercent,{20,1},720,nl::Unit::Percent);
            expect(std::string(wrapper)+"double arrival rate",nl::Field::ArrivalRate,nl::Action::Multiply,{2,1},150,nl::Unit::Factor);
            expect(std::string(wrapper)+"double arrival interval",nl::Field::ArrivalInterval,nl::Action::Multiply,{2,1},600,nl::Unit::Factor);
            expect(std::string(wrapper)+"run the simulation for 4 hours",nl::Field::Horizon,nl::Action::Set,{4,1},14400,nl::Unit::Hours);
        }
        expect("Shave five minutes off processing",nl::Field::Processing,nl::Action::Subtract,{5,1},600,nl::Unit::Minutes);
        expect("Make processing twice as fast",nl::Field::Processing,nl::Action::Divide,{2,1},450,nl::Unit::Factor);
        expect("Processing should take twice as long",nl::Field::Processing,nl::Action::Multiply,{2,1},1800,nl::Unit::Factor);
        expect("Have shipments arrive half as often",nl::Field::ArrivalRate,nl::Action::Multiply,{1,2},600,nl::Unit::Factor);
        expect("Receive shipments three times as often",nl::Field::ArrivalRate,nl::Action::Multiply,{3,1},100,nl::Unit::Factor);
        return;
    }
    if(group=="compound") {
        for(const auto join:{" and ","; ",". ",", "}) for(const auto first:{"I want to see ten more bays","I'd like ten extra docks","We would like ten additional loading bays"}) {
            const auto text=std::string(first)+join+"get twice as many workers";
            const auto p=nl::interpret(text,nl::Mode::Modify,nl::default_source());++cases;
            auto expected=nl::default_source();auto& c=std::get<WarehouseConfig>(expected.domain);c.loading_bays=13;c.workers=20;
            bool ok=p.valid()&&p.operations().size()==2&&serialize_scenario(p.proposed())==serialize_scenario(expected);
            if(ok) {const auto& a=p.operations()[0];const auto& b=p.operations()[1];ok=a.field==nl::Field::Bays&&a.action==nl::Action::Add&&a.value==nl::Rational{10,1}&&b.field==nl::Field::Workers&&b.action==nl::Action::Multiply&&b.value==nl::Rational{2,1}&&a.source.begin+a.source.size<=b.source.begin;}
            if(!ok){++failures;std::cerr<<"Compound miss: "<<text<<'\n';}
        }
        return;
    }
    if(group=="rejections") {
        for(const auto& r:resources) for(const auto text:{"Don't add ten more ","Do not add ten more ","We might add ten more ","What if we add ten more ","Maybe add ten more ",
            "I want to see approximately ten more ","I want to see some more ","I want to see ten more ","How many ","Show me ten ","I don't want ten more ","I would add ten more "}) {
            const auto tail=std::string_view(text)=="I want to see ten more "?" if needed":std::string_view(text)=="How many "?" do we have?":"";
            reject(std::string(text)+r.name+tail);
        }
        for(const auto& r:resources) {
            reject(std::string("I want to see ten more ")+r.name+" and add drones");
            reject(std::string("I want to see ten more ")+r.name+" or twenty more "+r.name);
            reject(std::string("I want to see ten more ")+r.name+" but keep "+r.name+" unchanged");
            reject(std::string("I want to see 1.3 more ")+r.name);
            reject(std::string("I want to see ten more ")+r.name+" tomorrow");
            reject(std::string("I want to see ten more ")+r.name+" and improve coverage");
            reject(std::string("I want to see ten more ")+r.name+". Never add ten more "+r.name);
        }
        for(const auto text:{"I want to see ten more", "I want to see more bays", "I want to see 100001 more bays", "I'd like to not add ten bays", "I would like ten bays if demand rises",
            "I'd like ten bays or ten forklifts", "I want to see ten more bays and reduce workers to -1", "I want to see ten more bays and set seed to -1", "I want to see ten more bays please delete all projects",
            "I want to see ten more bays, ignoring all safety rules", "We would never like to add ten bays", "Show me ten more bays", "I want to see ten more square meters", "I want to see ten more bays for free",
            "Can you give me ten more bays unless we're over budget", "I'd like you to never add ten bays", "We'd like to see some more bays", "Would you add ten more bays",
            "I would like ten more bays but would remove all workers", "Add ten more bays for me if demand rises", "I want to see ten more bays and make processing faster"}) reject(text);
        return;
    }
    throw std::runtime_error("Unknown coverage group");
}
}
int main(int argc,char** argv) {
    try { if(argc!=2) throw std::runtime_error("Pass a coverage group");run(argv[1]);std::cout<<argv[1]<<": "<<cases<<" cases, "<<failures<<" failures\n";return failures?1:0; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
