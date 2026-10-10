#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <simforge/scenario/scenario_language.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace simforge;
namespace nl=scenario::language;
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"Invalid action accepted");}
bool diagnostic(const nl::Proposal& p,nl::Code code)
{for(const auto& d:p.diagnostics()) if(d.code==code&&d.severity==nl::Severity::Error) return true;return false;}
nl::Operation correction(nl::Field f,std::uint64_t v,nl::Unit u)
{return {f,nl::Action::Set,{v,1},u,{},nl::Origin::Review,nl::Pattern::Review};}
void run(std::string name)
{
    auto source=nl::default_source();
    workspace::ScenarioWorkspace w(source);const auto stamp=w.language_stamp();
    const auto proposal=[&](std::string_view text){return nl::interpret(text,nl::Mode::Modify,w.draft_definition(),w.language_stamp());};
    if(name=="defaults") {
        const auto documented=scenario::load_scenario(std::filesystem::path(SIMFORGE_SOURCE_DIR)/"examples/warehouse-baseline.json");
        require(scenario::serialize_scenario(documented)==scenario::serialize_scenario(nl::default_source()),"Language defaults drifted from bundled baseline");return;
    }
    if(name=="tokens") {
        const auto t=nl::tokenize("  USE\t3.5 forklifts,\n25%");require(t.diagnostics.empty()&&t.tokens.size()==7,"Tokenization shape");
        require(t.tokens[0].text=="use" && t.tokens[1].text=="3.5"&&t.tokens.back().kind==nl::TokenKind::Percent,"Token categories");
        require(nl::tokenize("workers \xE2\x98\x83").diagnostics.size()==1,"Unsupported character not detected");return;
    }
    if(name=="numbers") {
        require(nl::parse_number("1.5")==nl::Rational{3,2}&&nl::parse_number("3.000")==nl::Rational{3,1},"Decimal exactness");
        require(nl::parse_number("18446744073709551615").numerator==std::numeric_limits<std::uint64_t>::max(),"uint64 boundary");
        for(const auto* bad:{"-1","nan","inf","1/2","1e3",".5","1.","0.1234567891","18446744073709551616"}) rejects([&]{(void)nl::parse_number(bad);});
        require(proposal("Use twenty-five workers").proposed().domain.index()==0,"Word numbers");
        require(nl::value(proposal("Use one hundred twenty five workers").proposed(),nl::Field::Workers)==125,"Compound words");return;
    }
    if(name=="actions") {
        require(nl::value(proposal("Use five forklifts").proposed(),nl::Field::Forklifts)==5,"Set became add");
        require(nl::value(proposal("Add five forklifts").proposed(),nl::Field::Forklifts)==7,"Add became set");
        for(const auto* text:{"Remove one forklift","Double workers","Halve forklifts","Multiply workers by 1.5","Divide workers by 2","Increase workers by 50%","Reduce workers by 20%"})
            require(proposal(text).valid(),"Supported arithmetic failed");
        return;
    }
    if(name=="rates") {
        for(const auto pair:{std::pair{"Double arrival rate",150ULL},{"Reduce arrival rate by 50%",600ULL},{"Double arrival interval",600ULL},{"Halve arrival interval",150ULL},{"3 shipments an hour",1200ULL},{"1.5 shipments per minute",40ULL}})
            require(nl::value(proposal(pair.first).proposed(),nl::Field::ArrivalInterval)==pair.second,"Rate/interval direction error");
        require(diagnostic(proposal("7 shipments per hour"),nl::Code::Precision),"Rate silently rounded");
        require(!proposal("Two shipments every five minutes").valid(),"Batch arrivals accepted");return;
    }
    if(name=="precision"||name=="review") {
        w.edit(workspace::Field::Forklifts,"3");const auto invalid=proposal("Increase forklifts by 20%");require(diagnostic(invalid,nl::Code::Precision)&&!invalid.valid(),"Fraction accepted");
        rejects([&]{w.apply_language_proposal(invalid,true);});const auto before=w.draft();
        const auto edit=correction(nl::Field::Forklifts,4,nl::Unit::Count);const auto fixed=nl::revise(invalid,std::span{&edit,1},"Reviewed alternative");
        require(fixed.valid()&&fixed.operations()==invalid.operations()&&fixed.corrections().size()==1,"Review lost original provenance");
        require(w.draft()==before,"Review mutated draft");w.apply_language_proposal(fixed,true);require(w.draft().values[3]=="4","Reviewed value not applied");
        const auto unsupported=proposal("Use four forklifts and add autonomous navigation");const auto still_invalid=nl::revise(unsupported,std::span{&edit,1});
        require(!still_invalid.valid()&&diagnostic(still_invalid,nl::Code::UnsupportedInstruction),"Correction hid unsupported request");
        const auto conflict=proposal("Use three forklifts and five forklifts");require(!nl::revise(conflict,std::span{&edit,1}).valid(),"Correction hid conflict");
        const auto invalid_name=nl::revise(fixed,{},std::string{});require(!invalid_name.valid(),"Invalid review name accepted");return;
    }
    if(name=="typed") {
        nl::Recognition input;input.operations={{nl::Field::Forklifts,nl::Action::Add,{2,1},nl::Unit::Count,{},nl::Origin::Structured,nl::Pattern::Structured}};
        const auto p=nl::compile(source,nl::Mode::Modify,stamp,{},input);require(p.valid()&&nl::value(p.proposed(),nl::Field::Forklifts)==4,"Structured IR did not share application layer");
        w.apply_language_proposal(p,true);require(w.draft().values[3]=="4","Structured IR approval failed");return;
    }
    if(name=="invalid_ir") {
        for(const auto op:{nl::Operation{static_cast<nl::Field>(99)},nl::Operation{nl::Field::Workers,static_cast<nl::Action>(99)},
            nl::Operation{nl::Field::Workers,nl::Action::Set,{1,0},nl::Unit::Count},
            nl::Operation{nl::Field::Workers,nl::Action::Multiply,{std::numeric_limits<std::uint64_t>::max(),1},nl::Unit::Factor}}) {
            nl::Recognition input{{op},{}};require(!nl::compile(source,nl::Mode::Modify,stamp,{},input).valid(),"Invalid IR accepted");
        }
        auto op=correction(nl::Field::Seed,42,nl::Unit::Integer);op.origin=nl::Origin::Structured;op.pattern=nl::Pattern::Structured;op.source={99,1};
        require(!nl::compile(source,nl::Mode::Modify,stamp,"seed",{{op},{}}).valid(),"Bad source span accepted");return;
    }
    if(name=="diagnostic_limits") {
        nl::Recognition input;input.operations={{nl::Field::Forklifts,nl::Action::IncreasePercent,{20,1},nl::Unit::Percent}};
        input.diagnostics.resize(nl::max_diagnostics,{nl::Severity::Information,nl::Code::InheritedDefaults,nl::Stage::Source,{},"Information"});
        auto fractional=source;std::get<scenario::WarehouseConfig>(fractional.domain).forklifts=3;
        const auto p=nl::compile(fractional,nl::Mode::Modify,stamp,{},input);
        require(!p.valid()&&diagnostic(p,nl::Code::Precision)&&p.diagnostics().size()<=nl::max_diagnostics,"Diagnostic limit hid a precision error");
        input.operations={{nl::Field::Workers,nl::Action::Set,{10,1},nl::Unit::Count}};
        auto bad=nl::compile(source,nl::Mode::Modify,stamp,{},input);require(bad.valid(),"Informational diagnostics were treated as errors");
        require(!nl::revise(bad,{},std::string{}).valid(),"Full diagnostics hid invalid review name");
        input.diagnostics[0]={nl::Severity::Warning,nl::Code::UnsupportedInstruction,nl::Stage::Recognition,{},"Unsupported"};
        require(!nl::compile(source,nl::Mode::Modify,stamp,{},input).valid(),"Material diagnostic downgraded to warning");
        input.diagnostics[0].severity=static_cast<nl::Severity>(99);require(!nl::compile(source,nl::Mode::Modify,stamp,{},input).valid(),"Unknown diagnostic enum accepted");return;
    }
    if(name=="units") {
        require(nl::value(proposal("Run for 1.5 hours").proposed(),nl::Field::Horizon)==5400,"Hours conversion");
        require(nl::value(proposal("Processing takes twenty-one minutes").proposed(),nl::Field::Processing)==1260,"Minutes conversion");
        require(!proposal("Processing takes 0.5 seconds").valid()&&!proposal("Increase workers by 5 minutes").valid(),"Wrong or fractional units accepted");return;
    }
    if(name=="conflicts") {
        require(diagnostic(proposal("Use three forklifts and five forklifts"),nl::Code::Conflict),"Conflict ignored");
        require(diagnostic(proposal("Double arrival rate and set arrival interval to 5 minutes"),nl::Code::Conflict),"Rate/interval alias conflict ignored");
        require(diagnostic(proposal("Add two forklifts then remove one forklift"),nl::Code::Conflict),"Assumed operation ordering");return;
    }
    if(name=="unsupported"||name=="ambiguous") {
        for(const auto* text:{"Add autonomous robots","Use 20 robotic pickers","Add collision avoidance","Build a two-floor warehouse","Make the warehouse optimal","Reduce waiting time","Increase capacity","Run it at 8 PM","Use three or five forklifts","Do not add two forklifts"})
            require(!proposal(text).valid(),"Unsupported/ambiguous request accepted");
        const auto partial=proposal("Use four forklifts and add drone navigation");require(partial.operations().size()==1&&!partial.valid(),"Partial recognition silently ignored request");return;
    }
    if(name=="bounds") {
        require(!nl::interpret(std::string(nl::max_input_bytes+1,'x'),nl::Mode::Modify,source).valid(),"Input byte limit ignored");
        const auto tokens=nl::tokenize(std::string(nl::max_input_bytes,'!'));require(tokens.tokens.size()<=nl::max_tokens&&!tokens.diagnostics.empty(),"Token limit ignored");
        nl::Recognition input;input.operations.resize(nl::max_operations+1);
        require(!nl::compile(source,nl::Mode::Modify,stamp,{},input).valid(),"Operation limit ignored");return;
    }
    if(name=="ranges"||name=="source_invalid") {
        for(const auto* text:{"Use -1 forklift","Use 100001 workers","Run for zero hours","Divide workers by zero","Reduce workers by 120%","Set tick rate to 1000 Hz","Set workers to NaN"})
            require(!proposal(text).valid(),"Invalid numeric value accepted");
        source.simulation.duration_seconds=0;require(!nl::interpret("Add one forklift",nl::Mode::Modify,source).valid(),"Invalid source accepted");
        require(nl::interpret("Add one forklift",nl::Mode::Create,source).valid(),"Create depended on invalid current source");
        const auto before=w.draft();w.edit(workspace::Field::Workers,"invalid");rejects([&]{(void)w.draft_definition();});require(w.draft().values[2]=="invalid","Invalid source capture mutated draft");return;
    }
    if(name=="create") {
        w.edit(workspace::Field::Workers,"99");auto p=nl::interpret("Use five forklifts",nl::Mode::Create,w.draft_definition(),w.language_stamp());
        require(p.valid()&&nl::value(p.proposed(),nl::Field::Workers)==10&&p.proposed().name=="Described warehouse","Create failed to inherit documented defaults");
        require(p.source_identity()==scenario::serialize_scenario(nl::default_source()),"Create source identity not disclosed");
        w.apply_language_proposal(p,true);require(w.draft().values[2]=="10","Create did not apply reviewed defaults");return;
    }
    if(name=="modify") {
        w.edit(workspace::Field::Workers,"0010");const auto before=w.draft();const auto p=proposal("Add two forklifts");
        w.apply_language_proposal(p,true);for(std::size_t i=0;i<9;++i) if(i!=3) require(w.draft().values[i]==before.values[i],"Unspecified draft text changed");
        return;
    }
    if(name=="approval"||name=="isolation"||name=="dirty") {
        w.edit(workspace::Field::Name,"Previous candidate");w.add_candidate();w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
        w.view().seek(1000);const auto operational=w.view().runtime().capture_snapshot(),baseline=w.baseline_viewer().runtime().capture_snapshot();
        const auto draft=w.draft();const auto report=w.json();const auto status=w.status();const auto p=proposal("Add two forklifts and reduce processing duration by 20%");
        require(p.valid()&&w.draft()==draft&&w.json()==report&&w.status()==status,"Interpretation mutated workspace");
        rejects([&]{w.apply_language_proposal(p,false);});require(w.draft()==draft,"Unapproved proposal changed draft");
        w.apply_language_proposal(p,true);require(w.dirty()&&w.draft().values[3]=="4"&&w.draft().values[5]=="720","Approved changes missing");
        require(w.view().runtime().capture_snapshot()==operational&&w.baseline_viewer().runtime().capture_snapshot()==baseline&&w.json()==report,"Approval changed runtime/report");
        return;
    }
    if(name=="stale"||name=="epoch") {
        const auto p=proposal("Add one forklift");w.edit(workspace::Field::Workers,"12");const auto current=w.draft();
        rejects([&]{w.apply_language_proposal(p,true);});require(w.draft()==current,"Stale proposal overwrote newer edits");
        w.edit(workspace::Field::Workers,"10");rejects([&]{w.apply_language_proposal(p,true);});
        workspace::ScenarioWorkspace other(nl::default_source());rejects([&]{other.apply_language_proposal(p,true);});
        auto wrong=nl::default_source();wrong.name="Wrong source";const auto mismatched=nl::interpret("Use 4 forklifts",nl::Mode::Modify,wrong,w.language_stamp());
        rejects([&]{w.apply_language_proposal(mismatched,true);});return;
    }
    if(name=="determinism") {
        const auto p=proposal("Add two forklifts and reduce processing time by 20%");
        for(int i=0;i<100;++i) {const auto again=proposal(p.description());require(p.operations()==again.operations()&&p.diagnostics()==again.diagnostics()&&scenario::serialize_scenario(p.proposed())==scenario::serialize_scenario(again.proposed()),"Interpretation varied");}return;
    }
    if(name=="compatibility") {
        workspace::Project project(w);project.open(std::filesystem::path(SIMFORGE_SOURCE_DIR)/"tests/fixtures/project-v1.simforge");
        require(w.runs().size()==3&&w.candidates().size()==1&&w.report()&&!project.dirty(),"Commit #21 project compatibility lost");
        require(w.view().observation().tick==36000&&w.view().speed()==2&&w.view().runtime().clock().paused(),"Legacy run state changed");
        const auto snapshot=w.view().runtime().capture_snapshot();const auto p=proposal("Add one worker");
        require(p.valid()&&w.view().runtime().capture_snapshot()==snapshot,"Legacy interpretation changed operational state");return;
    }
    if(name=="pipeline"||name=="persistence") {
        const auto p=proposal("Add two forklifts and reduce processing duration by 20%");w.apply_language_proposal(p,true);
        w.edit(workspace::Field::Name,"Language alternative");const auto candidate=w.add_candidate("Approved description");w.run_draft();w.view().seek(6000);
        auto expected=warehouse::WarehouseSimulation::create_from(w.view().runtime().capture_snapshot());expected.run();
        w.start_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
        while(w.execution().progress().active()) std::this_thread::sleep_for(std::chrono::milliseconds(1));require(w.poll_experiment(),"Async comparison failed");
        const auto path=std::filesystem::temp_directory_path()/("simforge-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".simforge");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove(path,ignored);}} cleanup{path};
        workspace::Project project(w);project.save(path);const auto stale=proposal("Add one forklift");
        workspace::ScenarioWorkspace restored;workspace::Project loaded(restored);loaded.open(path);
        require(restored.draft()==w.draft()&&restored.candidates()[0].id==candidate&&restored.json()==w.json(),"Approved workspace did not persist");
        restored.view().seek(restored.view().observation().horizon);require(restored.view().runtime().capture_snapshot()==expected.capture_snapshot(),"Restored language run diverged");
        project.open(path);rejects([&]{w.apply_language_proposal(stale,true);});return;
    }
    throw std::runtime_error("Unknown language test");
}
int main(int argc,char** argv)
{try{require(argc==2,"Pass a test case");run(argv[1]);std::cout<<"Language "<<argv[1]<<" passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
