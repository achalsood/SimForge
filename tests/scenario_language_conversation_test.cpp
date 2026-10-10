#include <simforge/workspace/scenario_workspace.hpp>
#include <iostream>

using namespace simforge;
namespace nl = scenario::language;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Expected { const char* text; nl::Field field; nl::Action action; std::uint64_t amount, result; };
constexpr Expected reported[] {
    {"Put in 3 more bays.", nl::Field::Bays, nl::Action::Add, 3, 6},
    {"Get 3 more bays.", nl::Field::Bays, nl::Action::Add, 3, 6},
    {"We need 3 additional loading bays.", nl::Field::Bays, nl::Action::Add, 3, 6},
    {"Get twice as many workers.", nl::Field::Workers, nl::Action::Multiply, 2, 20},
    {"We need twice as many workers.", nl::Field::Workers, nl::Action::Multiply, 2, 20},
    {"Use 3 times as many forklifts.", nl::Field::Forklifts, nl::Action::Multiply, 3, 6}
};
void reproduce(std::size_t index)
{
    const auto& expected = reported[index];
    workspace::ScenarioWorkspace workspace(nl::default_source());
    const auto before = workspace.draft();
    const auto tokens = nl::tokenize(expected.text);
    const auto proposal = nl::interpret(expected.text, nl::Mode::Modify,
        workspace.draft_definition(), workspace.language_stamp());
    std::cout << expected.text << " tokens=" << tokens.tokens.size()
        << " operations=" << proposal.operations().size() << " valid=" << proposal.valid() << '\n';
    for (const auto& d : proposal.diagnostics())
        std::cout << nl::code_name(d.code) << ": " << d.message << '\n';
    require(tokens.diagnostics.empty(), "Reported phrase failed tokenization");
    require(workspace.draft() == before, "Interpretation changed draft");
    require(proposal.valid(), "Reported conversational phrase remains invalid");
    require(proposal.operations().size() == 1, "Phrase emitted duplicate/missing operations");
    const auto& op = proposal.operations()[0];
    require(op.field == expected.field && op.action == expected.action &&
        op.value == nl::Rational{expected.amount, 1}, "Wrong semantic operation");
    require(nl::value(proposal.proposed(), expected.field) == expected.result, "Wrong proposed value");
    bool blocked = false;
    try { workspace.apply_language_proposal(proposal, false); } catch (const std::exception&) { blocked = true; }
    require(blocked && workspace.draft() == before, "Approval was bypassed");
    workspace.apply_language_proposal(proposal, true);
    require(nl::value(workspace.draft_definition(), expected.field) == expected.result, "Approved result differs");
}
nl::Proposal interpret(std::string_view text)
{ return nl::interpret(text,nl::Mode::Modify,nl::default_source()); }
bool same_operation(const nl::Operation& a,const nl::Operation& b)
{ return a.field==b.field&&a.action==b.action&&a.value==b.value&&a.unit==b.unit; }
void equivalence(std::initializer_list<const char*> descriptions,nl::Field field,nl::Action action,nl::Rational number,std::uint64_t result)
{
    const auto reference=interpret(*descriptions.begin());
    for(const auto text:descriptions) {
        const auto p=interpret(text);
        require(p.valid()&&p.operations().size()==1,"Equivalent description failed");
        const auto& op=p.operations()[0];
        require(op.field==field&&op.action==action&&op.value==number,"Equivalence emitted wrong typed action");
        require(same_operation(op,reference.operations()[0]),"Equivalent phrases differ semantically");
        require(nl::value(p.proposed(),field)==result&&scenario::serialize_scenario(p.proposed())==scenario::serialize_scenario(reference.proposed()),"Equivalent states differ");
    }
}
void semantic(std::string_view name)
{
    if(name=="equivalence") {
        equivalence({"Add 3 workers","Get 3 more workers","Hire 3 additional workers","Bring in 3 extra staff","Increase staffing by 3"},nl::Field::Workers,nl::Action::Add,{3,1},13);
        equivalence({"Double workers","Get twice as many workers","Use two times as many workers","Workers should be doubled"},nl::Field::Workers,nl::Action::Multiply,{2,1},20);
        equivalence({"Use half as many forklifts","Forklifts should be halved","Reduce forklift capacity to half of its current value"},nl::Field::Forklifts,nl::Action::Multiply,{1,2},1);return;
    }
    if(name=="contrast") {
        for(const auto pair:{std::pair{"Get 3 workers","Get 3 more workers"},
            {"Increase workers by 2","Increase workers to 2"},
            {"Double arrival rate","Double arrival interval"},
            {"Processing twice as fast","Processing twice as long"},
            {"Increase workers by 200%","Increase workers to 200% of the current count"}}) {
            const auto a=interpret(pair.first),b=interpret(pair.second);
            require(a.valid()&&b.valid()&&a.operations().size()==1&&b.operations().size()==1,"Contrast phrase invalid");
            require(!same_operation(a.operations()[0],b.operations()[0]),"Meaningful word did not change typed operation");
            require(scenario::serialize_scenario(a.proposed())!=scenario::serialize_scenario(b.proposed()),"Contrast states collapsed");
        }
        const auto percent=interpret("Increase workers by 200%"),factor=interpret("Increase workers by a factor of 2");
        require(percent.operations()[0].action==nl::Action::IncreasePercent&&nl::value(percent.proposed(),nl::Field::Workers)==30,"Percent increase math");
        require(factor.operations()[0].action==nl::Action::Multiply&&nl::value(factor.proposed(),nl::Field::Workers)==20,"Factor math");return;
    }
    if(name=="association") {
        const auto p=interpret("Get 3 more bays and twice as many workers.");
        require(p.valid()&&p.operations().size()==2,"Compound association failed");
        require(p.operations()[0].field==nl::Field::Bays&&p.operations()[0].action==nl::Action::Add&&p.operations()[0].value==nl::Rational{3,1},"Bay clause association");
        require(p.operations()[1].field==nl::Field::Workers&&p.operations()[1].action==nl::Action::Multiply&&p.operations()[1].value==nl::Rational{2,1},"Worker clause association");
        const auto& a=p.operations()[0].source;const auto& b=p.operations()[1].source;
        require(p.description().substr(a.begin,a.size)=="Get 3 more bays"&&p.description().substr(b.begin,b.size)=="twice as many workers","Source ownership lost");
        require(a.begin+a.size<=b.begin,"Source spans overlap");
        const auto topic=interpret("For forklifts, add 3.");
        require(topic.valid()&&topic.operations().size()==1&&topic.operations()[0].field==nl::Field::Forklifts&&topic.operations()[0].action==nl::Action::Add,"Explicit topic failed");
        require(!interpret("For workers, add 3 forklifts").valid(),"Conflicting topic silently accepted");return;
    }
    if(name=="safety") {
        for(const auto* text:{"Don't add 3 forklifts","We considered adding four bays but decided against it","What if we added 3 forklifts?","We might get three more bays"}) {
            const auto p=interpret(text);require(!p.valid()&&p.operations().empty(),"Negated/hypothetical mutation emitted");
        }
        const auto conflict=interpret("Set forklifts to 3 and 5");
        require(!conflict.valid()&&conflict.operations().size()==2,"Numeric conflict not surfaced");
        bool found=false;for(const auto& d:conflict.diagnostics()) found|=d.code==nl::Code::Conflict;
        require(found,"Conflict code missing");
        require(!interpret("Get more forklifts").valid()&&!interpret("Get 3 more bays and add drone navigation").valid(),"Vague/partial description accepted");
        require(!interpret("Use 1.3 times as many forklifts").valid(),"Fractional resource rounded");
        require(nl::value(interpret("Receive shipments twice as often").proposed(),nl::Field::ArrivalInterval)==150,"Frequency inversion");return;
    }
    if(name=="workflow") {
        workspace::ScenarioWorkspace w(nl::default_source());const auto runtime=w.view().runtime().capture_snapshot();
        auto p=nl::interpret("Get 3 more bays and twice as many workers",nl::Mode::Modify,w.draft_definition(),w.language_stamp());
        require(p.valid()&&!w.dirty(),"Interpretation dirtied workspace");
        w.apply_language_proposal(p,true);
        require(w.dirty()&&w.draft().values[1]=="6"&&w.draft().values[2]=="20"&&w.draft().values[3]=="2","Approved compound draft mismatch");
        require(w.view().runtime().capture_snapshot()==runtime,"Apply changed operational state");
        bool rejected=false;try{w.apply_language_proposal(p,true);}catch(const std::exception&){rejected=true;}
        require(rejected,"Stale conversational proposal accepted");
        w.run_draft();require(w.view().observation().capacity==warehouse::ResourceCounts{6,20,2},"Run does not use approved capacity");return;
    }
    throw std::runtime_error("Unknown semantic test");
}
int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Pass a test case");
        if(std::string_view(argv[1]).size()>1) {semantic(argv[1]);return 0;}
        const auto index = std::stoul(argv[1]);
        require(index < std::size(reported), "Unknown reproduction");
        reproduce(index); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
