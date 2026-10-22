#include "parser_internal.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace fo4::psc {

bool is_assignable(const Expression& expression) {
  return expression.kind == ExpressionKind::Identifier || expression.kind == ExpressionKind::Member ||
         expression.kind == ExpressionKind::Index;
}

bool Parser::end_statement() {
  if (peek().kind == TokenKind::NewLine) {
    advance();
    return true;
  }
  if (peek().kind == TokenKind::EndOfFile) return true;
  fail(ParseErrorKind::UnexpectedToken, peek(), "end of line");
  return false;
}

bool Parser::parse_block(std::vector<StatementPtr>& out) {
  for (;;) {
    skip_trivia();
    if (failed()) return false;
    if (peek().kind == TokenKind::EndOfFile) {
      fail(ParseErrorKind::UnexpectedEndOfFile, peek(), "statement");
      return false;
    }
    if (is_block_end(peek().kind)) return true;
    if (!parse_statement(out)) return false;
  }
}

bool Parser::parse_statement(std::vector<StatementPtr>& out) {
  links_ = 0;
  DepthGuard guard(depth_, kMaxNestingDepth);
  if (!guard.within()) {
    fail(ParseErrorKind::DepthLimit, peek(), "shallower nesting");
    return false;
  }
  const TokenKind kind = peek().kind;
  if (kind == TokenKind::KwIf) return parse_if(out);
  if (kind == TokenKind::KwWhile) return parse_while(out);
  if (kind == TokenKind::KwReturn) return parse_return(out);
  return parse_assignment_or_declaration(out);
}

bool Parser::parse_if(std::vector<StatementPtr>& out) {
  auto statement = std::make_unique<Statement>();
  statement->kind = StatementKind::If;
  statement->position = peek().position;
  advance();
  IfBranch first;
  first.position = statement->position;
  first.condition = parse_expression();
  if (failed()) return false;
  if (!parse_block(first.body)) return false;
  if (!is_if_continuation(peek().kind)) {
    fail(ParseErrorKind::UnbalancedIf, peek(), "EndIf");
    return false;
  }
  statement->branches.push_back(std::move(first));
  while (peek().kind == TokenKind::KwElseIf) {
    IfBranch branch;
    branch.position = peek().position;
    advance();
    branch.condition = parse_expression();
    if (failed()) return false;
    if (!parse_block(branch.body)) return false;
    if (!is_if_continuation(peek().kind)) {
      fail(ParseErrorKind::UnbalancedIf, peek(), "EndIf");
      return false;
    }
    statement->branches.push_back(std::move(branch));
  }
  if (peek().kind == TokenKind::KwElse) {
    IfBranch branch;
    branch.position = peek().position;
    advance();
    if (!parse_block(branch.body)) return false;
    if (peek().kind != TokenKind::KwEndIf) {
      fail(ParseErrorKind::UnbalancedIf, peek(), "EndIf");
      return false;
    }
    statement->branches.push_back(std::move(branch));
  }
  advance();
  if (!end_statement()) return false;
  out.push_back(std::move(statement));
  return true;
}

bool Parser::parse_while(std::vector<StatementPtr>& out) {
  auto statement = std::make_unique<Statement>();
  statement->kind = StatementKind::While;
  statement->position = peek().position;
  advance();
  statement->value = parse_expression();
  if (failed()) return false;
  if (!parse_block(statement->body)) return false;
  if (peek().kind != TokenKind::KwEndWhile) {
    fail(ParseErrorKind::UnbalancedWhile, peek(), "EndWhile");
    return false;
  }
  advance();
  if (!end_statement()) return false;
  out.push_back(std::move(statement));
  return true;
}

bool Parser::parse_return(std::vector<StatementPtr>& out) {
  auto statement = std::make_unique<Statement>();
  statement->kind = StatementKind::Return;
  statement->position = peek().position;
  advance();
  const TokenKind kind = peek().kind;
  if (kind != TokenKind::NewLine && kind != TokenKind::EndOfFile && !is_block_end(kind)) {
    statement->value = parse_expression();
    if (failed()) return false;
  }
  if (!end_statement()) return false;
  out.push_back(std::move(statement));
  return true;
}

bool Parser::looks_like_declaration() const {
  if (peek().kind != TokenKind::Identifier) return false;
  std::size_t at = index_ + 1;
  while (at + 1 < tokens_.size() && tokens_[at].kind == TokenKind::Colon &&
         tokens_[at + 1].kind == TokenKind::Identifier) {
    at += 2;
  }
  if (at + 1 < tokens_.size() && tokens_[at].kind == TokenKind::LBracket &&
      tokens_[at + 1].kind == TokenKind::RBracket) {
    at += 2;
  }
  return at < tokens_.size() && tokens_[at].kind == TokenKind::Identifier;
}

bool Parser::take_assignment_operator(BinaryOperator& binary, bool& compound) {
  const TokenKind kind = peek().kind;
  compound = true;
  if (kind == TokenKind::Assign) {
    compound = false;
    binary = BinaryOperator::Add;
  } else if (kind == TokenKind::PlusAssign) {
    binary = BinaryOperator::Add;
  } else if (kind == TokenKind::MinusAssign) {
    binary = BinaryOperator::Subtract;
  } else if (kind == TokenKind::StarAssign) {
    binary = BinaryOperator::Multiply;
  } else if (kind == TokenKind::SlashAssign) {
    binary = BinaryOperator::Divide;
  } else if (kind == TokenKind::PercentAssign) {
    binary = BinaryOperator::Modulo;
  } else {
    return false;
  }
  advance();
  return true;
}

bool Parser::parse_assignment_or_declaration(std::vector<StatementPtr>& out) {
  if (looks_like_declaration()) {
    auto declaration = std::make_unique<Statement>();
    declaration->kind = StatementKind::VariableDeclaration;
    declaration->position = peek().position;
    if (!expect_type(declaration->typeName, declaration->isArray)) return false;
    if (!expect_name(declaration->name)) return false;
    if (match(TokenKind::Assign)) {
      declaration->value = parse_expression();
      if (failed()) return false;
    }
    if (!end_statement()) return false;
    out.push_back(std::move(declaration));
    return true;
  }
  ExpressionPtr target = parse_expression();
  if (failed()) return false;
  BinaryOperator binary = BinaryOperator::Add;
  bool compound = false;
  if (take_assignment_operator(binary, compound)) {
    if (!is_assignable(*target)) {
      Token at;
      at.position = target->position;
      at.text = "expression";
      fail(ParseErrorKind::InvalidAssignmentTarget, at, "an assignable target");
      return false;
    }
    auto assignment = std::make_unique<Statement>();
    assignment->kind = StatementKind::Assignment;
    assignment->position = target->position;
    assignment->binary = binary;
    assignment->compound = compound;
    assignment->target = std::move(target);
    assignment->value = parse_expression();
    if (failed()) return false;
    if (!end_statement()) return false;
    out.push_back(std::move(assignment));
    return true;
  }
  auto statement = std::make_unique<Statement>();
  statement->kind = StatementKind::Expression;
  statement->position = target->position;
  statement->value = std::move(target);
  if (!end_statement()) return false;
  out.push_back(std::move(statement));
  return true;
}

}  // namespace fo4::psc
