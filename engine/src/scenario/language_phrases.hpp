#pragma once
#include "language_internal.hpp"
#include <initializer_list>

namespace simforge::scenario::language::detail::grammar
{
struct Entity { Field field; std::size_t end; };
struct Parsed { Operation operation; std::size_t end; };
struct Amount { Rational number; Unit unit; std::size_t end; };
bool is(std::span<const Token> tokens, std::size_t at, std::string_view word);
std::size_t articles(std::span<const Token> tokens, std::size_t at);
std::optional<Entity> entity(std::span<const Token> tokens, std::size_t at);
Span span(std::span<const Token> tokens, std::size_t first, std::size_t end);
Parsed operation(std::span<const Token> tokens, std::size_t first, std::size_t end,
    Field field, Action action, Rational number, Unit unit, Pattern pattern);
bool resource(Field field);
std::optional<Amount> amount(std::span<const Token> tokens, std::size_t at, Field field, bool factor);
Pattern pattern(Field field);
const char* entity_name(Field field);
std::optional<Parsed> conversational(std::span<const Token> tokens, std::size_t start);
bool topic(std::span<const Token> tokens);
}
