#pragma once

#include "fo4/as3/ast.hpp"
#include "fo4/as3/lexer.hpp"
#include "parser_helpers.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3::detail {
struct Parser {
  std::vector<Token> tokens;
  std::size_t index{0};
  std::string error;
  std::size_t error_offset{0};
  std::size_t error_line{1};
  bool allow_in{true};

  const Token& peek(std::size_t ahead = 0) const {
    const auto position = index + ahead;
    return position < tokens.size() ? tokens[position] : tokens.back();
  }

  void advance() {
    if (index + 1 < tokens.size()) ++index;
  }

  bool at_end() const { return peek().kind == TokenKind::end_of_file; }

  bool check_punct(std::string_view text) const {
    return peek().kind == TokenKind::punct && peek().text == text;
  }

  bool check_keyword(std::string_view text) const {
    return peek().kind == TokenKind::keyword && peek().text == text;
  }

  bool match_punct(std::string_view text) {
    if (!check_punct(text)) return false;
    advance();
    return true;
  }

  bool match_keyword(std::string_view text) {
    if (!check_keyword(text)) return false;
    advance();
    return true;
  }

  bool fail(std::string message) {
    if (error.empty()) {
      error = std::move(message);
      error_offset = peek().offset;
      error_line = peek().line;
    }
    return false;
  }

  ExprPtr make(ExprKind kind, std::size_t offset, std::size_t line) const {
    auto expression = std::make_unique<Expr>();
    expression->kind = kind;
    expression->offset = offset;
    expression->line = line;
    return expression;
  }

  StmtPtr make_stmt(StmtKind kind, std::size_t offset, std::size_t line) const {
    auto statement = std::make_unique<Stmt>();
    statement->kind = kind;
    statement->offset = offset;
    statement->line = line;
    return statement;
  }

  bool expect_punct(std::string_view text, const char* context) {
    if (match_punct(text)) return true;
    fail(std::string{"expected '"} + std::string{text} + "' " + context);
    return false;
  }

  ExprPtr parse_root();
  ExprPtr parse_sequence();
  ExprPtr parse_assignment();
  ExprPtr parse_conditional();
  ExprPtr parse_binary(int minimum_precedence);
  ExprPtr parse_unary();
  std::vector<ExprPtr> parse_arguments();
  ExprPtr parse_postfix();
  ExprPtr parse_member_chain();
  ExprPtr parse_array_literal();
  ExprPtr parse_object_literal();
  ExprPtr parse_primary();
  StmtPtr parse_statement();
  StmtPtr parse_var_declaration(bool expects_semicolon);
  StmtPtr parse_switch_statement();
  StmtPtr parse_for_statement();
  bool parse_type_text(std::string& type);
  StmtPtr parse_function_declaration();
  std::vector<std::string> parse_modifiers(bool allow_contextual);
  std::string parse_dotted_name();
  StmtPtr parse_import_directive();
  StmtPtr parse_package_declaration();
  StmtPtr parse_class_declaration(std::vector<std::string> modifiers,
                                  std::size_t offset,
                                  std::size_t line);
};
}  // namespace fo4::as3::detail
