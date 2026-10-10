#pragma once
#include <simforge/scenario/scenario_language.hpp>
#include <stdexcept>
namespace simforge::scenario::language::detail
{
struct Failure : std::runtime_error { Code code; Failure(Code code,std::string message):std::runtime_error(std::move(message)),code(code){} };
[[noreturn]] inline void fail(Code code,const char* message) {throw Failure(code,message);}
Rational add(Rational a,Rational b);
Rational subtract(Rational a,Rational b);
Rational multiply(Rational a,Rational b);
Rational divide(Rational a,Rational b);
void diagnose(std::vector<Diagnostic>& diagnostics,Code code,Stage stage,Span span,std::string message);
bool errors(std::span<const Diagnostic> diagnostics);
struct Quantity {Rational number;std::size_t end;};
std::optional<Quantity> quantity(std::span<const Token> tokens,std::size_t begin);
std::optional<Unit> duration_unit(std::string_view text);
}
