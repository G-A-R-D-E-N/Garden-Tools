#include "fo4/as3/parser.hpp"

#include "fo4/as3/lexer.hpp"
#include "parser_state.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3 {
namespace detail {
StmtPtr Parser::parse_statement() {
  NestingScope nesting{*this};
  if (!nesting) return nullptr;
  const auto& token = peek();
  if (token.kind == TokenKind::end_of_file) {
    fail("unexpected end of input");
    return nullptr;
  }
  if (check_punct(";")) {
    auto statement = make_stmt(StmtKind::empty, token.offset, token.line);
    advance();
    return statement;
  }
  if (check_punct("{")) {
    auto block = make_stmt(StmtKind::block, token.offset, token.line);
    advance();
    while (!check_punct("}") && !at_end()) {
      auto statement = parse_statement();
      if (!statement) return nullptr;
      block->body.push_back(std::move(statement));
    }
    if (!match_punct("}")) {
      fail("expected '}' after the block");
      return nullptr;
    }
    return block;
  }
  if (check_keyword("var") || check_keyword("const")) return parse_var_declaration(true);
  if (check_keyword("if")) {
    auto statement = make_stmt(StmtKind::if_statement, token.offset, token.line);
    advance();
    if (!expect_punct("(", "after 'if'")) return nullptr;
    statement->expression = parse_assignment();
    if (!statement->expression) return nullptr;
    if (!expect_punct(")", "after the if condition")) return nullptr;
    auto body = parse_statement();
    if (!body) return nullptr;
    statement->body.push_back(std::move(body));
    if (match_keyword("else")) {
      auto alternate = parse_statement();
      if (!alternate) return nullptr;
      statement->alternate.push_back(std::move(alternate));
    }
    return statement;
  }
  if (check_keyword("while")) {
    auto statement = make_stmt(StmtKind::while_statement, token.offset, token.line);
    advance();
    if (!expect_punct("(", "after 'while'")) return nullptr;
    statement->expression = parse_assignment();
    if (!statement->expression) return nullptr;
    if (!expect_punct(")", "after the while condition")) return nullptr;
    auto body = parse_statement();
    if (!body) return nullptr;
    statement->body.push_back(std::move(body));
    return statement;
  }
  if (check_keyword("do")) {
    auto statement = make_stmt(StmtKind::do_while_statement, token.offset, token.line);
    advance();
    auto body = parse_statement();
    if (!body) return nullptr;
    statement->body.push_back(std::move(body));
    if (!match_keyword("while")) {
      fail("expected 'while' after the do body");
      return nullptr;
    }
    if (!expect_punct("(", "after 'do ... while'")) return nullptr;
    statement->expression = parse_assignment();
    if (!statement->expression) return nullptr;
    if (!expect_punct(")", "after the do-while condition")) return nullptr;
    if (!expect_punct(";", "after the do-while statement")) return nullptr;
    return statement;
  }
  if (is_keyword_modifier(token)) {
    const auto offset = token.offset;
    const auto line = token.line;
    auto modifiers = parse_modifiers(false);
    if (check_keyword("class")) {
      return parse_class_declaration(std::move(modifiers), offset, line);
    }
    if (check_keyword("function")) {
      auto member = parse_function_declaration();
      if (member) member->modifiers = std::move(modifiers);
      return member;
    }
    if (check_keyword("var") || check_keyword("const")) {
      auto member = parse_var_declaration(true);
      if (member) member->modifiers = std::move(modifiers);
      return member;
    }
    fail("expected a declaration after the modifiers");
    return nullptr;
  }
  if (check_keyword("function")) return parse_function_declaration();
  if (check_keyword("package")) return parse_package_declaration();
  if (check_keyword("import")) return parse_import_directive();
  if (check_keyword("class")) return parse_class_declaration({}, token.offset, token.line);
  if (check_keyword("switch")) return parse_switch_statement();
  if (check_keyword("for")) return parse_for_statement();
  if (check_keyword("throw")) {
    auto statement = make_stmt(StmtKind::throw_statement, token.offset, token.line);
    advance();
    statement->expression = parse_assignment();
    if (!statement->expression) return nullptr;
    if (!expect_punct(";", "after the throw statement")) return nullptr;
    return statement;
  }
  if (check_keyword("return")) {
    auto statement = make_stmt(StmtKind::return_statement, token.offset, token.line);
    advance();
    if (!check_punct(";")) {
      statement->expression = parse_assignment();
      if (!statement->expression) return nullptr;
    }
    if (!expect_punct(";", "after the return statement")) return nullptr;
    return statement;
  }
  if (check_keyword("break") || check_keyword("continue")) {
    const auto kind =
        token.text == "break" ? StmtKind::break_statement : StmtKind::continue_statement;
    auto statement = make_stmt(kind, token.offset, token.line);
    advance();
    if (!expect_punct(";", "after the jump statement")) return nullptr;
    return statement;
  }
  if (token.kind == TokenKind::keyword) {
    fail("unsupported statement: " + token.text);
    return nullptr;
  }
  auto statement = make_stmt(StmtKind::expression, token.offset, token.line);
  statement->expression = parse_assignment();
  if (!statement->expression) return nullptr;
  if (!expect_punct(";", "after the expression statement")) return nullptr;
  return statement;
}

StmtPtr Parser::parse_var_declaration(bool expects_semicolon) {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  auto declaration = make_stmt(StmtKind::var_declaration, offset, line);
  while (true) {
    if (peek().kind != TokenKind::identifier) {
      fail("expected a binding name");
      return nullptr;
    }
    VarBinding binding;
    binding.name = peek().text;
    advance();
    if (match_punct(":")) {
      if (peek().kind != TokenKind::identifier && !check_keyword("void")) {
        fail("expected a type name after ':'");
        return nullptr;
      }
      binding.type = peek().text;
      advance();
      while (match_punct("[")) {
        if (!match_punct("]")) {
          fail("expected ']' in the array type");
          return nullptr;
        }
        binding.type += "[]";
      }
      if (check_punct(".")) {
        fail("qualified type names are not supported");
        return nullptr;
      }
    }
    if (match_punct("=")) {
      binding.initializer = parse_assignment();
      if (!binding.initializer) return nullptr;
    }
    declaration->bindings.push_back(std::move(binding));
    if (match_punct(",")) continue;
    break;
  }
  if (expects_semicolon && !expect_punct(";", "after the variable declaration")) return nullptr;
  return declaration;
}

StmtPtr Parser::parse_switch_statement() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  auto statement = make_stmt(StmtKind::switch_statement, offset, line);
  if (!expect_punct("(", "after 'switch'")) return nullptr;
  statement->expression = parse_assignment();
  if (!statement->expression) return nullptr;
  if (!expect_punct(")", "after the switch condition")) return nullptr;
  if (!expect_punct("{", "before the switch body")) return nullptr;
  while (!check_punct("}") && !at_end()) {
    SwitchCase entry;
    if (match_keyword("case")) {
      entry.label = parse_assignment();
      if (!entry.label) return nullptr;
      if (!expect_punct(":", "after the case label")) return nullptr;
    } else if (match_keyword("default")) {
      entry.is_default = true;
      for (const auto& existing : statement->cases) {
        if (existing.is_default) {
          fail("a switch may have only one default case");
          return nullptr;
        }
      }
      if (!expect_punct(":", "after 'default'")) return nullptr;
    } else {
      fail("expected 'case' or 'default' in the switch body");
      return nullptr;
    }
    while (!check_punct("}") && !at_end() && !check_keyword("case") &&
           !check_keyword("default")) {
      auto child = parse_statement();
      if (!child) return nullptr;
      entry.statements.push_back(std::move(child));
    }
    statement->cases.push_back(std::move(entry));
  }
  if (!match_punct("}")) {
    fail("expected '}' after the switch body");
    return nullptr;
  }
  return statement;
}

StmtPtr Parser::parse_for_statement() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  const bool each = peek().kind == TokenKind::identifier && peek().text == "each";
  if (each) advance();
  auto statement =
      make_stmt(each ? StmtKind::for_in_statement : StmtKind::for_statement, offset, line);
  statement->each = each;
  if (!expect_punct("(", "after 'for'")) return nullptr;
  if (check_keyword("var") || check_keyword("const")) {
    statement->initializer = parse_var_declaration(false);
    if (!statement->initializer) return nullptr;
    if (match_keyword("in")) {
      statement->kind = StmtKind::for_in_statement;
      if (statement->initializer->bindings.size() != 1 ||
          statement->initializer->bindings.front().initializer) {
        fail("a for-in binding must be a single name without an initialiser");
        return nullptr;
      }
      statement->expression = parse_assignment();
      if (!statement->expression) return nullptr;
      if (!expect_punct(")", "after the for-in collection")) return nullptr;
      auto body = parse_statement();
      if (!body) return nullptr;
      statement->body.push_back(std::move(body));
      return statement;
    }
    if (each) {
      fail("'for each' requires an 'in' collection");
      return nullptr;
    }
    if (!expect_punct(";", "after the for initialiser")) return nullptr;
  } else if (match_punct(";")) {
  } else {
    auto initialiser = make_stmt(StmtKind::expression, peek().offset, peek().line);
    allow_in = false;
    initialiser->expression = parse_assignment();
    allow_in = true;
    if (!initialiser->expression) return nullptr;
    statement->initializer = std::move(initialiser);
    if (match_keyword("in")) {
      statement->kind = StmtKind::for_in_statement;
      statement->expression = parse_assignment();
      if (!statement->expression) return nullptr;
      if (!expect_punct(")", "after the for-in collection")) return nullptr;
      auto body = parse_statement();
      if (!body) return nullptr;
      statement->body.push_back(std::move(body));
      return statement;
    }
    if (each) {
      fail("'for each' requires an 'in' collection");
      return nullptr;
    }
    if (!expect_punct(";", "after the for initialiser")) return nullptr;
  }
  if (!check_punct(";")) {
    statement->expression = parse_assignment();
    if (!statement->expression) return nullptr;
  }
  if (!expect_punct(";", "after the for condition")) return nullptr;
  if (!check_punct(")")) {
    statement->second_expression = parse_assignment();
    if (!statement->second_expression) return nullptr;
  }
  if (!expect_punct(")", "after the for header")) return nullptr;
  auto body = parse_statement();
  if (!body) return nullptr;
  statement->body.push_back(std::move(body));
  return statement;
}

bool Parser::parse_type_text(std::string& type) {
  if (peek().kind != TokenKind::identifier && !check_keyword("void")) {
    fail("expected a type name");
    return false;
  }
  type = peek().text;
  advance();
  while (match_punct("[")) {
    if (!match_punct("]")) {
      fail("expected ']' in the array type");
      return false;
    }
    type += "[]";
  }
  if (check_punct(".")) {
    fail("qualified type names are not supported");
    return false;
  }
  return true;
}

StmtPtr Parser::parse_function_declaration() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  if (peek().kind != TokenKind::identifier) {
    fail("expected a function name");
    return nullptr;
  }
  auto statement = make_stmt(StmtKind::function_declaration, offset, line);
  statement->name = peek().text;
  advance();
  if (!expect_punct("(", "after the function name")) return nullptr;
  while (!check_punct(")")) {
    Parameter parameter;
    parameter.rest = match_punct("...");
    if (peek().kind != TokenKind::identifier) {
      fail("expected a parameter name");
      return nullptr;
    }
    parameter.name = peek().text;
    advance();
    if (match_punct(":")) {
      if (!parse_type_text(parameter.type)) return nullptr;
    }
    if (match_punct("=")) {
      parameter.default_value = parse_assignment();
      if (!parameter.default_value) return nullptr;
    }
    const bool rest = parameter.rest;
    statement->parameters.push_back(std::move(parameter));
    if (!match_punct(",")) break;
    if (rest) {
      fail("a rest parameter must be last");
      return nullptr;
    }
    if (check_punct(")")) break;
  }
  if (!expect_punct(")", "after the parameter list")) return nullptr;
  if (match_punct(":")) {
    if (!parse_type_text(statement->return_type)) return nullptr;
  }
  if (!check_punct("{")) {
    fail("expected '{' before the function body");
    return nullptr;
  }
  auto body = parse_statement();
  if (!body) return nullptr;
  statement->body.push_back(std::move(body));
  return statement;
}

std::vector<std::string> Parser::parse_modifiers(bool allow_contextual) {
  std::vector<std::string> modifiers;
  while (is_keyword_modifier(peek()) ||
         (allow_contextual && is_contextual_modifier(peek()))) {
    modifiers.push_back(peek().text);
    advance();
  }
  return modifiers;
}

std::string Parser::parse_dotted_name() {
  if (peek().kind != TokenKind::identifier) {
    fail("expected a name");
    return {};
  }
  std::string name = peek().text;
  advance();
  while (check_punct(".")) {
    advance();
    if (peek().kind != TokenKind::identifier) {
      fail("expected a name after '.'");
      return {};
    }
    name += "." + peek().text;
    advance();
  }
  return name;
}

StmtPtr Parser::parse_import_directive() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  auto statement = make_stmt(StmtKind::import_directive, offset, line);
  if (peek().kind != TokenKind::identifier) {
    fail("expected an import path");
    return nullptr;
  }
  std::string name = peek().text;
  advance();
  while (check_punct(".")) {
    advance();
    if (match_punct("*")) {
      name += ".*";
      break;
    }
    if (peek().kind != TokenKind::identifier) {
      fail("expected a name or '*' after '.'");
      return nullptr;
    }
    name += "." + peek().text;
    advance();
  }
  statement->text = std::move(name);
  if (!expect_punct(";", "after the import directive")) return nullptr;
  return statement;
}

StmtPtr Parser::parse_package_declaration() {
  const auto offset = peek().offset;
  const auto line = peek().line;
  advance();
  auto statement = make_stmt(StmtKind::package_declaration, offset, line);
  if (peek().kind == TokenKind::identifier) {
    statement->name = parse_dotted_name();
    if (statement->name.empty()) return nullptr;
  }
  if (!expect_punct("{", "before the package body")) return nullptr;
  while (!check_punct("}") && !at_end()) {
    auto child = parse_statement();
    if (!child) return nullptr;
    statement->body.push_back(std::move(child));
  }
  if (!match_punct("}")) {
    fail("expected '}' after the package body");
    return nullptr;
  }
  return statement;
}

StmtPtr Parser::parse_class_declaration(std::vector<std::string> modifiers,
                                        std::size_t offset,
                                        std::size_t line) {
  advance();
  if (peek().kind != TokenKind::identifier) {
    fail("expected a class name");
    return nullptr;
  }
  auto statement = make_stmt(StmtKind::class_declaration, offset, line);
  statement->modifiers = std::move(modifiers);
  statement->name = peek().text;
  advance();
  if (match_keyword("extends")) {
    if (peek().kind != TokenKind::identifier) {
      fail("expected a base class name");
      return nullptr;
    }
    statement->base = peek().text;
    advance();
  }
  if (match_keyword("implements")) {
    while (true) {
      if (peek().kind != TokenKind::identifier) {
        fail("expected an interface name");
        return nullptr;
      }
      statement->interfaces.push_back(peek().text);
      advance();
      if (!match_punct(",")) break;
    }
  }
  if (!expect_punct("{", "before the class body")) return nullptr;
  while (!check_punct("}") && !at_end()) {
    if (check_punct(";")) {
      auto empty = make_stmt(StmtKind::empty, peek().offset, peek().line);
      advance();
      statement->body.push_back(std::move(empty));
      continue;
    }
    auto member_modifiers = parse_modifiers(true);
    if (check_keyword("class") || check_keyword("interface")) {
      fail("nested class declarations are not supported");
      return nullptr;
    }
    if (check_keyword("var") || check_keyword("const")) {
      auto member = parse_var_declaration(true);
      if (!member) return nullptr;
      member->modifiers = std::move(member_modifiers);
      statement->body.push_back(std::move(member));
      continue;
    }
    if (check_keyword("function")) {
      auto member = parse_function_declaration();
      if (!member) return nullptr;
      member->modifiers = std::move(member_modifiers);
      statement->body.push_back(std::move(member));
      continue;
    }
    fail("expected a class member declaration");
    return nullptr;
  }
  if (!match_punct("}")) {
    fail("expected '}' after the class body");
    return nullptr;
  }
  return statement;
}
}  // namespace detail

ProgramResult parse_program(std::string_view source) {
  detail::Parser parser;
  parser.tokens = tokenize(source);
  for (const auto& token : parser.tokens) {
    if (token.kind != TokenKind::error) continue;
    ProgramResult result;
    result.error = "lexer error: " + token.error;
    result.offset = token.offset;
    result.line = token.line;
    return result;
  }
  ProgramResult result;
  while (!parser.at_end()) {
    auto statement = parser.parse_statement();
    if (!statement) {
      result.error = parser.error.empty() ? "parse error" : parser.error;
      result.offset = parser.error_offset;
      result.line = parser.error_line;
      return result;
    }
    result.statements.push_back(std::move(statement));
  }
  return result;
}
}  // namespace fo4::as3
