#include <simforge/scenario/scenario_language.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace simforge::scenario;
namespace language=simforge::scenario::language;
using Json=nlohmann::json;
Json fields(const ScenarioDefinition& d)
{
    Json j{{"name",d.name},{"schema_version",d.schema_version}};
    for(const auto f:language::physical_fields) j[language::field_name(f)]=language::value(d,f);return j;
}
Json operation(const language::Operation& op,std::string_view text)
{
    return {{"field",language::field_name(op.field)},{"action",language::action_name(op.action)},
        {"value",Json::array({op.value.numerator,op.value.denominator})},{"unit",language::unit_name(op.unit)},
        {"source_text",std::string(text.substr(op.source.begin,op.source.size))},{"pattern",language::pattern_name(op.pattern)}};
}
int expanded(const std::filesystem::path& path,bool holdout)
{
    std::ifstream file(path);Json corpus;file>>corpus;
    if(corpus.at("corpus_version")!=2||corpus.at("default_source")!=Json::parse(serialize_scenario(language::default_source())))
        throw std::runtime_error("Expanded corpus defaults/version drifted");
    std::size_t positives=0,negatives=0,op_matches=0,value_matches=0,rejections=0,unsafe=0,omissions=0;
    double total_us=0,worst_us=0;std::set<std::string> ids;
    for(const auto& row:corpus.at("examples")) {
        const auto id=row.at("id").get<std::string>();if(!ids.insert(id).second) throw std::runtime_error("Duplicate expanded case ID");
        const auto text=row.at("text").get<std::string>();const auto source=parse_scenario(row.at("source").dump());
        const auto mode=row.at("mode")=="create"?language::Mode::Create:language::Mode::Modify;
        const auto p=language::interpret(text,mode,source,{1,1});
        total_us+=p.timings().total_us;worst_us=std::max(worst_us,p.timings().total_us);
        Json actual=Json::array();std::size_t previous_end=0;
        for(const auto& op:p.operations()) {
            if(op.origin!=language::Origin::Rule || op.source.begin<previous_end ||
                op.source.begin>text.size() || op.source.size>text.size()-op.source.begin || !op.source.size)
                throw std::runtime_error("Invalid/overlapping operation provenance");
            previous_end=op.source.begin+op.source.size;
            auto j=operation(op,text);j.erase("pattern");j.erase("source_text");actual.push_back(j);
        }
        if(row.at("classification")=="valid") {
            ++positives;const bool ops=p.valid()&&actual==row.at("expected_operations");op_matches+=ops;
            auto expected=fields(mode==language::Mode::Create?language::default_source():source);
            if(mode==language::Mode::Create) expected["name"]="Described warehouse";
            for(const auto& [key,value]:row.at("expected_fields").items()) expected[key]=value;
            const bool values=p.valid()&&fields(p.proposed())==expected;value_matches+=values;
            if(p.valid()&&(!ops||!values)) {
                ++unsafe;
                if(actual.size()<row.at("expected_operations").size()) ++omissions;
            }
            if(!ops||!values) std::cout<<id<<": "<<(p.valid()?"unsafe semantic mismatch":"safe coverage miss")
                <<" | "<<text<<" | actual="<<actual.dump()<<'\n';
        } else {
            ++negatives;rejections+=!p.valid();
            if(p.valid()) {++unsafe;if(row.value("partial",false)) ++omissions;std::cout<<id<<": unsafe acceptance | "<<text<<'\n';}
        }
    }
    std::cout<<(holdout?"Holdout":"Development")<<" examples: "<<positives+negatives<<" | Supported exact operations: "<<op_matches<<'/'<<positives
        <<" | Exact final scenarios: "<<value_matches<<'/'<<positives<<" | Correct rejections: "<<rejections<<'/'<<negatives
        <<" | Unsafe false acceptances: "<<unsafe<<" | Accepted silent omissions: "<<omissions
        <<" | Mean/worst interpretation us: "<<total_us/(positives+negatives)<<'/'<<worst_us<<'\n';
    // The holdout coverage gate was declared before evaluation; safe misses are
    // measured, never turned into expected rejections or used to tune grammar.
    return unsafe||omissions||rejections!=negatives|| (holdout ? op_matches*2<positives||value_matches*2<positives : op_matches!=positives||value_matches!=positives);
}
int main(int argc,char** argv)
{
    try {
        const std::filesystem::path directory=argc>1?argv[1]:SIMFORGE_LANGUAGE_CORPUS;
        if(std::filesystem::is_regular_file(directory)) return expanded(directory,argc>2&&std::string_view(argv[2])=="--holdout");
        std::size_t examples=0,operation_matches=0,value_matches=0,unsupported=0,ambiguous=0,unsafe=0,failures=0;
        double total_us=0,worst_us=0;
        std::set<std::string> identities;
        for(const auto* filename:{"supported.json","rejected.json"}) {
            std::ifstream file(directory/filename);Json corpus;file>>corpus;
            if(corpus.at("corpus_version")!=1||corpus.at("default_source")!=Json::parse(serialize_scenario(language::default_source()))) throw std::runtime_error("Corpus defaults/version drifted");
            for(const auto& row:corpus.at("examples")) {
                const auto name=row.at("id").get<std::string>();if(!identities.insert(name).second) throw std::runtime_error("Duplicate corpus ID");
                ++examples;const auto text=row.at("text").get<std::string>();
                const auto source=parse_scenario(row.at("source").dump());
                const auto mode=row.at("mode")=="create"?language::Mode::Create:language::Mode::Modify;
                const auto result=language::interpret(text,mode,source,{1,1});
                total_us+=result.timings().total_us;worst_us=std::max(worst_us,result.timings().total_us);
                Json actual_operations=Json::array();
                for(const auto& op:result.operations()) {
                    if(op.origin!=language::Origin::Rule) throw std::runtime_error("Parser operation provenance drift");
                    actual_operations.push_back(operation(op,text));
                }
                const bool ops=actual_operations==row.at("expected_operations");operation_matches+=ops;
                auto expected=fields(mode==language::Mode::Create?language::default_source():source);
                if(mode==language::Mode::Create) expected["name"]="Described warehouse";
                for(const auto& [key,value]:row.at("expected_fields").items()) expected[key]=value;
                const bool values=fields(result.proposed())==expected;value_matches+=values;
                std::set<std::string> actual_diagnostics,expected_diagnostics;
                for(const auto& d:result.diagnostics()) if(d.severity==language::Severity::Error) actual_diagnostics.insert(language::code_name(d.code));
                for(const auto& code:row.at("expected_diagnostics")) expected_diagnostics.insert(code.get<std::string>());
                const bool diagnostics=actual_diagnostics==expected_diagnostics;
                const bool accepted=row.at("classification")=="valid";
                if(!accepted&&result.valid()) ++unsafe;
                const bool status=result.valid()==accepted;
                if(expected_diagnostics.contains("unsupported") && diagnostics && !result.valid()) ++unsupported;
                if(expected_diagnostics.contains("ambiguous") && diagnostics && !result.valid()) ++ambiguous;
                if(!ops||!values||!diagnostics||!status) {
                    ++failures;std::cerr<<name<<": ops="<<ops<<" values="<<values<<" diagnostics="<<diagnostics<<" status="<<status<<'\n';
                    if(!ops) std::cerr<<"  actual operations: "<<actual_operations.dump()<<"\n  expected: "<<row.at("expected_operations").dump()<<'\n';
                    if(!values) std::cerr<<"  actual fields: "<<fields(result.proposed()).dump()<<"\n  expected: "<<expected.dump()<<'\n';
                    if(!diagnostics) {std::cerr<<"  diagnostic codes:";for(const auto& d:actual_diagnostics) std::cerr<<' '<<d;std::cerr<<'\n';}
                }
            }
        }
        std::cout<<"Corpus examples: "<<examples<<" | Exact typed-operation matches: "<<operation_matches<<'/'<<examples
            <<" | Exact full value matches: "<<value_matches<<'/'<<examples<<" | Unsupported detections: "<<unsupported
            <<" | Ambiguity detections: "<<ambiguous<<" | Unsafe false acceptances: "<<unsafe
            <<" | Mean/worst interpretation us: "<<total_us/examples<<'/'<<worst_us<<" | Failures: "<<failures<<'\n';
        return failures||unsafe?1:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
