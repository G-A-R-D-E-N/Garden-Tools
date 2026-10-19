#include "parser_internal.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fo4::psc {

namespace {

bool comparison_operator(TokenKind kind, BinaryOperator& out) {
  switch (kind) {
    case TokenKind::Equal:
      out = BinaryOperator::Equal;
      return true;
    case TokenKind::NotEqual:
      out = BinaryOperator::NotEqual;
      return true;
    case TokenKind::Less:
      out = BinaryOperator::Less;
      return true;
    case TokenKind::LessEqual:
      out = BinaryOperator::LessEqual;
      return true;
    case TokenKind::Greater:
      out = BinaryOperator::Greater;
      return true;
    case TokenKind::GreaterEqual:
      out = BinaryOperator::GreaterEqual;
      return true;
    default:
      return false;
  }
}

ExpressionPtr make_binary(BinaryOperator binary, ExpressionPtr left, ExpressionPtr right) {
  auto expression = std::make_unique<Expression>();
  expression->kind = ExpressionKind::Binary;
  expression->binary = binary;
  expression->position = left->position;
  expression->left = std::move(left);
  expression->right = std::move(right);
  return expression;
}

}  // namespace

ExpressionPtr Parser::parse_expression() { return parse_or(); }

ExpressionPtr Parser::parse_or() {
  ExpressionPtr left = parse_and();
  if (failed()) return nullptr;
  while (peek().kind == TokenKind::OrOr) {
    advance();
    if (!chain_within(++links_)) return nullptr;
    ExpressionPtr right = parse_and();
    if (failed()) return nullptr;
    left = make_binary(BinaryOperator::Or, std::move(left), std::move(right));
  }
  return left;
}

ExpressionPtr Parser::parse_and() {
  ExpressionPtr left = parse_comparison();
  if (failed()) return nullptr;
  while (peek().kind == TokenKind::AndAnd) {
    advance();
    if (!chain_within(++links_)) return nullptr;
    ExpressionPtr right = parse_comparison();
    if (failed()) return nullptr;
    left = make_binary(BinaryOperator::And, std::move(left), std::move(right));
  }
  return left;
}

ExpressionPtr Parser::parse_comparison() {
  ExpressionPtr left = parse_additive();
  if (failed()) return nullptr;
  for (;;) {
    BinaryOperator binary = BinaryOperator::Equal;
    if (!comparison_operator(peek().kind, binary)) return left;
    advance();
    if (!chain_within(++links_)) return nullptr;
    ExpressionPtr right = parse_additive();
    if (failed()) return nullptr;
    left = make_binary(binary, std::move(left), std::move(right));
  }
}

ExpressionPtr Parser::parse_additive() {
  ExpressionPtr left = parse_multiplicative();
  if (failed()) return nullptr;
  for (;;) {
    const TokenKind kind = peek().kind;
    BinaryOperator binary = BinaryOperator::Add;
    if (kind == TokenKind::Plus) {
      binary = BinaryOperator::Add;
    } else if (kind == TokenKind::Minus) {
      binary = BinaryOperator::Subtract;
    } else {
      return left;
    }
    advance();
    if (!chain_within(++links_)) return nullptr;
    ExpressionPtr right = parse_multiplicative();
    if (failed()) return nullptr;
    left = make_binary(binary, std::move(left), std::move(right));
  }
}

ExpressionPtr Parser::parse_multiplicative() {
  ExpressionPtr left = parse_unary();
  if (failed()) return nullptr;
  for (;;) {
    const TokenKind kind = peek().kind;
    BinaryOperator binary = BinaryOperator::Multiply;
    if (kind == TokenKind::Star) {
      binary = BinaryOperator::Multiply;
    } else if (kind == TokenKind::Slash) {
      binary = BinaryOperator::Divide;
    } else if (kind == TokenKind::Percent) {
      binary = BinaryOperator::Modulo;
    } else {
      return left;
    }
    advance();
    if (!chain_within(++links_)) return nullptr;
    ExpressionPtr right = parse_unary();
    if (failed()) return nullptr;
    left = make_binary(binary, std::move(left), std::move(right));
  }
}

ExpressionPtr Parser::parse_unary() {
  DepthGuard guard(depth_, kMaxNestingDepth);
  if (!guard.within()) {
    fail(ParseErrorKind::DepthLimit, peek(), "shallower nesting");
    return nullptr;
  }
  const TokenKind kind = peek().kind;
  UnaryOperator unary = UnaryOperator::Negate;
  if (kind == TokenKind::Minus) {
    unary = UnaryOperator::Negate;
  } else if (kind == TokenKind::Plus) {
    unary = UnaryOperator::Plus;
  } else if (kind == TokenKind::Not) {
    unary = UnaryOperator::Not;
  } else {
    return parse_cast();
  }
  const SourcePosition position = peek().position;
  advance();
  ExpressionPtr operand = parse_unary();
  if (failed()) return nullptr;
  auto expression = std::make_unique<Expression>();
  expression->kind = ExpressionKind::Unary;
  expression->unary = unary;
  expression->position = position;
  expression->left = std::move(operand);
  return expression;
}

ExpressionPtr Parser::parse_cast() {
  ExpressionPtr operand = parse_postfix();
  if (failed()) return nullptr;
  while (peek().kind == TokenKind::KwAs || peek().kind == TokenKind::KwIs) {
    const bool isType = peek().kind == TokenKind::KwIs;
    const SourcePosition position = operand->position;
    advance();
    if (!chain_within(++links_)) return nullptr;
    std::string type;
    if (!expect_qualified_name(type)) return nullptr;
    auto expression = std::make_unique<Expression>();
    expression->kind = isType ? ExpressionKind::Is : ExpressionKind::Cast;
    expression->position = position;
    expression->text = std::move(type);
    expression->left = std::move(operand);
    operand = std::move(expression);
  }
  return operand;
}

ExpressionPtr Parser::parse_postfix() {
  ExpressionPtr expression = parse_primary();
  if (failed()) return nullptr;
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind != TokenKind::Dot && kind != TokenKind::LBracket && kind != TokenKind::LParen) return expression;
    if (!chain_within(++links_)) return nullptr;
    if (kind == TokenKind::Dot) {
      advance();
      std::string member;
      if (!expect_name(member)) return nullptr;
      const SourcePosition position = expression->position;
      auto access = std::make_unique<Expression>();
      access->kind = ExpressionKind::Member;
      access->position = position;
      access->text = std::move(member);
      access->left = std::move(expression);
      if (peek().kind == TokenKind::LParen) {
        auto call = std::make_unique<Expression>();
        call->kind = ExpressionKind::Call;
        call->position = position;
        call->left = std::move(access);
        if (!parse_arguments(call->arguments, call->argumentNames)) return nullptr;
        expression = std::move(call);
        continue;
      }
      expression = std::move(access);
      continue;
    }
    if (kind == TokenKind::LBracket) {
      advance();
      const SourcePosition position = expression->position;
      auto index = std::make_unique<Expression>();
      index->kind = ExpressionKind::Index;
      index->position = position;
      index->left = std::move(expression);
      index->right = parse_expression();
      if (failed()) return nullptr;
      if (!expect(TokenKind::RBracket)) return nullptr;
      expression = std::move(index);
      continue;
    }
    if (kind == TokenKind::LParen) {
      const SourcePosition position = expression->position;
      auto call = std::make_unique<Expression>();
      call->kind = ExpressionKind::Call;
      call->position = position;
      call->left = std::move(expression);
      if (!parse_arguments(call->arguments, call->argumentNames)) return nullptr;
      expression = std::move(call);
      continue;
    }
  }
}

ExpressionPtr Parser::parse_new() {
  const SourcePosition position = peek().position;
  advance();
  auto expression = std::make_unique<Expression>();
  expression->kind = ExpressionKind::New;
  expression->position = position;
  if (!expect_qualified_name(expression->text)) return nullptr;
  if (peek().kind == TokenKind::LBracket) {
    advance();
    expression->arrayNew = true;
    expression->left = parse_expression();
    if (failed()) return nullptr;
    if (!expect(TokenKind::RBracket)) return nullptr;
  }
  return expression;
}

ExpressionPtr Parser::parse_primary() {
  const Token& token = peek();
  switch (token.kind) {
    case TokenKind::Integer: {
      auto expression = std::make_unique<Expression>();
      expression->kind = ExpressionKind::IntegerLiteral;
      expression->position = token.position;
      expression->text = token.text;
      expression->intValue = token.intValue;
      advance();
      return expression;
    }
    case TokenKind::Float: {
      auto expression = std::make_unique<Expression>();
      expression->kind = ExpressionKind::FloatLiteral;
      expression->position = token.position;
      expression->text = token.text;
      expression->floatValue = token.floatValue;
      advance();
      return expression;
    }
    case TokenKind::String: {
      auto expression = std::make_unique<Expression>();
      expression->kind = ExpressionKind::StringLiteral;
      expression->position = token.position;
      expression->text = token.text;
      expression->stringValue = token.stringValue;
      advance();
      return expression;
    }
    case TokenKind::KwTrue:
    case TokenKind::KwFalse: {
      auto expression = std::make_unique<Expression>();
      expression->kind = ExpressionKind::BoolLiteral;
      expression->position = token.position;
      expression->text = token.text;
      expression->boolValue = token.kind == TokenKind::KwTrue;
      advance();
      return expression;
    }
    case TokenKind::KwNone:
    case TokenKind::Identifier: {
      auto expression = std::make_unique<Expression>();
      expression->kind = ExpressionKind::Identifier;
      expression->position = token.position;
      if (token.kind == TokenKind::Identifier) {
        if (!expect_qualified_name(expression->text)) return nullptr;
      } else {
        expression->text = token.text;
        advance();
      }
      return expression;
    }
    case TokenKind::KwNew:
      return parse_new();
    case TokenKind::LParen: {
      advance();
      ExpressionPtr inner = parse_expression();
      if (failed()) return nullptr;
      if (!expect(TokenKind::RParen)) return nullptr;
      return inner;
    }
    default:
      fail(ParseErrorKind::UnexpectedToken, token, "expression");
      return nullptr;
  }
}

bool Parser::parse_arguments(std::vector<ExpressionPtr>& arguments, std::vector<std::string>& names) {
  if (!expect(TokenKind::LParen)) return false;
  if (match(TokenKind::RParen)) return true;
  for (;;) {
    std::string name;
    if (peek().kind == TokenKind::Identifier && peek(1).kind == TokenKind::Assign) {
      name = peek().text;
      advance();
      advance();
    }
    ExpressionPtr value = parse_expression();
    if (failed()) return false;
    arguments.push_back(std::move(value));
    names.push_back(std::move(name));
    if (match(TokenKind::Comma)) continue;
    break;
  }
  return expect(TokenKind::RParen);
}

bool Parser::chain_within(std::size_t links) {
  if (links <= kMaxChainLength) return true;
  fail(ParseErrorKind::DepthLimit, peek(), "shallower nesting");
  return false;
}

}  // namespace fo4::psc
