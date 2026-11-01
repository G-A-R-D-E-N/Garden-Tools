#include "fo4/as3/parser.hpp"

#include "fo4/as3/lexer.hpp"
#include "parser_state.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3 {
ParseResult parse_expression(std::string_view source) {
  detail::Parser parser;
  parser.tokens = tokenize(source);
  for (const auto& token : parser.tokens) {
    if (token.kind != TokenKind::error) continue;
    ParseResult result;
    result.error = "lexer error: " + token.error;
    result.offset = token.offset;
    result.line = token.line;
    return result;
  }
  auto expression = parser.parse_root();
  ParseResult result;
  if (!expression) {
    result.error = parser.error.empty() ? "parse error" : parser.error;
    result.offset = parser.error_offset;
    result.line = parser.error_line;
    return result;
  }
  result.expression = std::move(expression);
  return result;
}

namespace detail {
ExprPtr Parser::parse_root() {
  if (peek().kind == TokenKind::error) {
    fail("lexer error: " + peek().error);
    return nullptr;
  }
  auto expression = parse_sequence();
  if (!expression) return nullptr;
  if (!at_end()) {
    fail("unexpected token after the expression");
    return nullptr;
  }
  return expression;
}

ExprPtr Parser::parse_sequence() {
  auto first = parse_assignment();
  if (!first) return nullptr;
  if (!check_punct(",")) return first;
  auto sequence = make(ExprKind::sequence, first->offset, first->line);
  sequence->children.push_back(std::move(first));
  while (match_punct(",")) {
    auto next = parse_assignment();
    if (!next) return nullptr;
    sequence->children.push_back(std::move(next));
  }
  return sequence;
}

ExprPtr Parser::parse_assignment() {
  NestingScope nesting{*this};
  if (!nesting) return nullptr;
  auto target = parse_conditional();
  if (!target) return nullptr;
  if (peek().kind != TokenKind::punct || !is_assignment_operator(peek().text)) return target;
  const auto offset = peek().offset;
  const auto line = peek().line;
  auto operation = peek().text;
  advance();
  auto value = parse_assignment();
  if (!value) return nullptr;
  auto assignment = make(ExprKind::assignment, offset, line);
  assignment->text = std::move(operation);
  assignment->children.push_back(std::move(target));
  assignment->children.push_back(std::move(value));
  return assignment;
}

ExprPtr Parser::parse_conditional() {
  auto test = parse_binary(1);
  if (!test) return nullptr;
  if (!match_punct("?")) return test;
  const auto offset = test->offset;
  const auto line = test->line;
  auto consequent = parse_assignment();
  if (!consequent) return nullptr;
  if (!match_punct(":")) {
    fail("expected ':' in a conditional expression");
    return nullptr;
  }
  auto alternate = parse_assignment();
  if (!alternate) return nullptr;
  auto conditional = make(ExprKind::conditional, offset, line);
  conditional->children.push_back(std::move(test));
  conditional->children.push_back(std::move(consequent));
  conditional->children.push_back(std::move(alternate));
  return conditional;
}

ExprPtr Parser::parse_binary(int minimum_precedence) {
  auto left = parse_unary();
  if (!left) return nullptr;
  while (true) {
    const auto precedence = binary_precedence(peek());
    if (precedence == 0 || precedence < minimum_precedence) break;
    if (!allow_in && check_keyword("in")) break;
    const auto offset = peek().offset;
    const auto line = peek().line;
    auto operation = peek().text;
    advance();
    auto right = parse_binary(precedence + 1);
    if (!right) return nullptr;
    auto combined = make(precedence <= 2 ? ExprKind::logical : ExprKind::binary, offset, line);
    combined->text = std::move(operation);
    combined->children.push_back(std::move(left));
    combined->children.push_back(std::move(right));
    left = std::move(combined);
  }
  return left;
}

ExprPtr Parser::parse_unary() {
  NestingScope nesting{*this};
  if (!nesting) return nullptr;
  if (!is_unary_operator(peek())) return parse_postfix();
  const auto offset = peek().offset;
  const auto line = peek().line;
  auto operation = peek().text;
  advance();
  auto operand = parse_unary();
  if (!operand) return nullptr;
  auto unary = make(ExprKind::unary, offset, line);
  unary->text = std::move(operation);
  unary->children.push_back(std::move(operand));
  return unary;
}

std::vector<ExprPtr> Parser::parse_arguments() {
  std::vector<ExprPtr> arguments;
  if (!match_punct("(")) {
    fail("expected '('");
    return {};
  }
  if (match_punct(")")) return arguments;
  while (true) {
    auto argument = parse_assignment();
    if (!argument) return {};
    arguments.push_back(std::move(argument));
    if (!match_punct(",")) break;
    if (check_punct(")")) break;
  }
  if (!match_punct(")")) {
    fail("expected ')' after the arguments");
    return {};
  }
  return arguments;
}

ExprPtr Parser::parse_postfix() {
  auto expression = parse_primary();
  if (!expression) return nullptr;
  while (true) {
    if (match_punct(".")) {
      if (!is_property_name(peek())) {
        fail("expected a property name after '.'");
        return nullptr;
      }
      auto member = make(ExprKind::member, expression->offset, expression->line);
      member->text = peek().text;
      advance();
      member->children.push_back(std::move(expression));
      expression = std::move(member);
    } else if (match_punct("[")) {
      auto index_expression = parse_assignment();
      if (!index_expression) return nullptr;
      if (!match_punct("]")) {
        fail("expected ']' after the index expression");
        return nullptr;
      }
      auto indexed = make(ExprKind::index, expression->offset, expression->line);
      indexed->children.push_back(std::move(expression));
      indexed->children.push_back(std::move(index_expression));
      expression = std::move(indexed);
    } else if (check_punct("(")) {
      auto arguments = parse_arguments();
      if (arguments.empty() && !error.empty()) return nullptr;
      auto call = make(ExprKind::call, expression->offset, expression->line);
      call->children.push_back(std::move(expression));
      for (auto& argument : arguments) call->children.push_back(std::move(argument));
      expression = std::move(call);
    } else {
      break;
    }
  }
  return expression;
}

ExprPtr Parser::parse_member_chain() {
  auto expression = parse_primary();
  if (!expression) return nullptr;
  while (true) {
    if (match_punct(".")) {
      if (!is_property_name(peek())) {
        fail("expected a property name after '.'");
        return nullptr;
      }
      auto member = make(ExprKind::member, expression->offset, expression->line);
      member->text = peek().text;
      advance();
      member->children.push_back(std::move(expression));
      expression = std::move(member);
    } else if (match_punct("[")) {
      auto index_expression = parse_assignment();
      if (!index_expression) return nullptr;
      if (!match_punct("]")) {
        fail("expected ']' after the index expression");
        return nullptr;
      }
      auto indexed = make(ExprKind::index, expression->offset, expression->line);
      indexed->children.push_back(std::move(expression));
      indexed->children.push_back(std::move(index_expression));
      expression = std::move(indexed);
    } else {
      break;
    }
  }
  return expression;
}

ExprPtr Parser::parse_array_literal() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  auto literal = make(ExprKind::array_literal, offset, line);
  if (!match_punct("[")) {
    fail("expected '['");
    return nullptr;
  }
  while (!check_punct("]")) {
    auto element = parse_assignment();
    if (!element) return nullptr;
    literal->children.push_back(std::move(element));
    if (!match_punct(",")) break;
  }
  if (!match_punct("]")) {
    fail("expected ']' after the array elements");
    return nullptr;
  }
  return literal;
}

ExprPtr Parser::parse_object_literal() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  auto literal = make(ExprKind::object_literal, offset, line);
  if (!match_punct("{")) {
    fail("expected '{'");
    return nullptr;
  }
  while (!check_punct("}")) {
    const auto& key_token = peek();
    if (!is_literal_key(key_token)) {
      fail("expected a property name in the object literal");
      return nullptr;
    }
    auto key = key_token.kind == TokenKind::string ? key_token.value : key_token.text;
    advance();
    if (!match_punct(":")) {
      fail("expected ':' after the property name");
      return nullptr;
    }
    auto value = parse_assignment();
    if (!value) return nullptr;
    literal->members.emplace_back(std::move(key), std::move(value));
    if (!match_punct(",")) break;
  }
  if (!match_punct("}")) {
    fail("expected '}' after the object literal members");
    return nullptr;
  }
  return literal;
}

ExprPtr Parser::parse_primary() {
  NestingScope nesting{*this};
  if (!nesting) return nullptr;
  const auto& token = peek();
  if (token.kind == TokenKind::number) {
    auto literal = make(ExprKind::number_literal, token.offset, token.line);
    literal->text = token.text;
    advance();
    return literal;
  }
  if (token.kind == TokenKind::string) {
    auto literal = make(ExprKind::string_literal, token.offset, token.line);
    literal->text = token.text;
    literal->value = token.value;
    advance();
    return literal;
  }
  if (token.kind == TokenKind::identifier) {
    auto identifier = make(ExprKind::identifier, token.offset, token.line);
    identifier->text = token.text;
    advance();
    return identifier;
  }
  if (token.kind == TokenKind::keyword) {
    if (token.text == "this") {
      auto self = make(ExprKind::this_expression, token.offset, token.line);
      advance();
      return self;
    }
    if (token.text == "null") {
      auto null_literal = make(ExprKind::null_literal, token.offset, token.line);
      advance();
      return null_literal;
    }
    if (token.text == "true" || token.text == "false") {
      auto literal = make(ExprKind::boolean_literal, token.offset, token.line);
      literal->text = token.text;
      advance();
      return literal;
    }
    if (token.text == "new") {
      const auto offset = token.offset;
      const auto line = token.line;
      advance();
      auto target = parse_member_chain();
      if (!target) return nullptr;
      auto construct = make(ExprKind::construct, offset, line);
      construct->children.push_back(std::move(target));
      if (check_punct("(")) {
        auto arguments = parse_arguments();
        if (arguments.empty() && !error.empty()) return nullptr;
        for (auto& argument : arguments) construct->children.push_back(std::move(argument));
      }
      return construct;
    }
  }
  if (token.kind == TokenKind::punct) {
    if (token.text == "(") {
      advance();
      const auto outer_allow_in = allow_in;
      allow_in = true;
      auto inner = parse_sequence();
      allow_in = outer_allow_in;
      if (!inner) return nullptr;
      if (!match_punct(")")) {
        fail("expected ')' after the parenthesized expression");
        return nullptr;
      }
      return inner;
    }
    if (token.text == "[") return parse_array_literal();
    if (token.text == "{") return parse_object_literal();
  }
  fail("expected an expression");
  return nullptr;
}
}  // namespace detail
}  // namespace fo4::as3
