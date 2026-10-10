#pragma once
#include <simforge/scenario/scenario.hpp>
#include <array>
#include <span>

namespace simforge::scenario::language
{
inline constexpr std::size_t max_input_bytes=8192, max_tokens=1024, max_operations=64, max_diagnostics=128;
enum class Field { Bays, Workers, Forklifts, ArrivalInterval, ArrivalRate, Processing, Horizon, TickRate, Seed };
enum class Action { Set, Add, Subtract, Multiply, Divide, IncreasePercent, DecreasePercent };
enum class Unit { Count, Seconds, Minutes, Hours, Factor, Percent, Hertz, Integer, PerSecond, PerMinute, PerHour };
enum class Origin { Rule, Review, Structured };
enum class Pattern { Capacity, Interval, Rate, Processing, Horizon, TickRate, Seed, Relative, Review, Structured };
enum class Mode { Create, Modify };
enum class Severity { Information, Warning, Error };
enum class Stage { Tokenization, Recognition, Application, Validation, Source };
enum class Code { InputLimit, UnsupportedCharacter, InvalidNumber, InvalidUnit, UnsupportedInstruction, AmbiguousInstruction,
    Conflict, Precision, Range, Overflow, InvalidSource, NoOperations, InheritedDefaults };
struct Span { std::size_t begin{0}, size{0}; bool operator==(const Span&) const = default; };
struct Rational { std::uint64_t numerator{0}, denominator{1}; bool operator==(const Rational&) const = default; };
struct Operation
{
    Field field{Field::Forklifts};
    Action action{Action::Set};
    Rational value;
    Unit unit{Unit::Count};
    Span source;
    Origin origin{Origin::Structured};
    Pattern pattern{Pattern::Structured};
    bool operator==(const Operation&) const = default;
};
struct Diagnostic
{
    Severity severity{Severity::Error};
    Code code{Code::UnsupportedInstruction};
    Stage stage{Stage::Recognition};
    Span source;
    std::string message;
    bool operator==(const Diagnostic&) const = default;
};
enum class TokenKind { Word, Number, Percent, Hyphen, Boundary, Colon };
struct Token { TokenKind kind; std::string text; Span source; bool operator==(const Token&) const = default; };
struct Tokenization { std::vector<Token> tokens; std::vector<Diagnostic> diagnostics; };
struct Recognition { std::vector<Operation> operations; std::vector<Diagnostic> diagnostics; };
struct Application
{
    ScenarioDefinition definition;
    std::array<std::optional<Rational>,8> computed;
    std::vector<Diagnostic> diagnostics;
};
struct Stamp { std::uint64_t workspace{0}, revision{0}; bool operator==(const Stamp&) const = default; };
struct Timings { double tokenize_us{0}, recognize_us{0}, apply_us{0}, validate_us{0}, total_us{0}; };
inline constexpr std::array<Field,8> physical_fields{Field::Bays,Field::Workers,Field::Forklifts,Field::ArrivalInterval,
    Field::Processing,Field::Horizon,Field::TickRate,Field::Seed};
[[nodiscard]] Field physical_field(Field field);
[[nodiscard]] std::size_t field_index(Field field);
[[nodiscard]] const char* field_name(Field field);
[[nodiscard]] const char* action_name(Action action);
[[nodiscard]] const char* unit_name(Unit unit);
[[nodiscard]] const char* code_name(Code code);
[[nodiscard]] const char* pattern_name(Pattern pattern);
[[nodiscard]] Rational rational(std::uint64_t numerator,std::uint64_t denominator=1);
[[nodiscard]] Rational parse_number(std::string_view text);
[[nodiscard]] std::string format(Rational value);
[[nodiscard]] std::uint64_t value(const ScenarioDefinition& definition,Field field);
[[nodiscard]] ScenarioDefinition default_source(); // Parameters of examples/warehouse-baseline.json.
[[nodiscard]] Tokenization tokenize(std::string_view text);
[[nodiscard]] Recognition recognize(std::string_view text,std::span<const Token> tokens);
[[nodiscard]] Application apply_operations(const ScenarioDefinition& source,std::span<const Operation> operations);
class Proposal
{
public:
    [[nodiscard]] const std::string& description() const noexcept {return description_;}
    [[nodiscard]] Mode mode() const noexcept {return mode_;}
    [[nodiscard]] Stamp stamp() const noexcept {return stamp_;}
    [[nodiscard]] const std::string& source_identity() const noexcept {return source_identity_;}
    [[nodiscard]] const ScenarioDefinition& source() const noexcept {return source_;}
    [[nodiscard]] const ScenarioDefinition& proposed() const noexcept {return proposed_;}
    [[nodiscard]] const std::vector<Operation>& operations() const noexcept {return operations_;}
    [[nodiscard]] const std::vector<Operation>& corrections() const noexcept {return corrections_;}
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept {return diagnostics_;}
    [[nodiscard]] const std::array<std::optional<Rational>,8>& computed() const noexcept {return computed_;}
    [[nodiscard]] bool valid() const noexcept {return valid_;}
    [[nodiscard]] const Timings& timings() const noexcept {return timings_;}
private:
    friend Proposal compile(const ScenarioDefinition&,Mode,Stamp,std::string_view,const Recognition&);
    friend Proposal interpret(std::string_view,Mode,const ScenarioDefinition&,Stamp);
    friend Proposal revise(const Proposal&,std::span<const Operation>,std::optional<std::string>);
    std::string description_, source_identity_;
    Mode mode_{Mode::Modify}; Stamp stamp_;
    ScenarioDefinition source_, proposed_;
    std::vector<Operation> operations_, corrections_;
    std::vector<Diagnostic> recognition_diagnostics_, diagnostics_;
    std::array<std::optional<Rational>,8> computed_;
    std::optional<std::string> reviewed_name_;
    Timings timings_;
    bool valid_{false};
};
[[nodiscard]] Proposal compile(const ScenarioDefinition& source,Mode mode,Stamp stamp,std::string_view description,
    const Recognition& recognized);
[[nodiscard]] Proposal interpret(std::string_view description,Mode mode,const ScenarioDefinition& source,Stamp stamp={});
// Explicit review corrections replace one physical field each. Recognition errors remain blocking.
[[nodiscard]] Proposal revise(const Proposal& proposal,std::span<const Operation> corrections,
    std::optional<std::string> name={});
}
