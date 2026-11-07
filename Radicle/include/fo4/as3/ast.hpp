#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3 {

enum class ExprKind : std::uint8_t {
  number_literal,
  string_literal,
  boolean_literal,
  null_literal,
  identifier,
  this_expression,
  member,
  index,
  call,
  construct,
  unary,
  binary,
  logical,
  conditional,
  assignment,
  sequence,
  array_literal,
  object_literal,
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  ExprKind kind{ExprKind::identifier};
  std::size_t offset{0};
  std::size_t line{0};
  std::string text;
  std::string value;
  std::vector<ExprPtr> children;
  std::vector<std::pair<std::string, ExprPtr>> members;

  ~Expr();
};

std::string_view expr_kind_name(ExprKind kind);

enum class StmtKind : std::uint8_t {
  empty,
  expression,
  block,
  var_declaration,
  if_statement,
  while_statement,
  do_while_statement,
  for_statement,
  for_in_statement,
  function_declaration,
  return_statement,
  break_statement,
  continue_statement,
  throw_statement,
  switch_statement,
  package_declaration,
  class_declaration,
  import_directive,
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct VarBinding {
  std::string name;
  std::string type;
  ExprPtr initializer;
};

struct Parameter {
  std::string name;
  std::string type;
  ExprPtr default_value;
  bool rest{false};
};

struct SwitchCase {
  ExprPtr label;
  bool is_default{false};
  std::vector<StmtPtr> statements;
};

struct Stmt {
  StmtKind kind{StmtKind::empty};
  std::size_t offset{0};
  std::size_t line{0};
  ExprPtr expression;
  ExprPtr second_expression;
  StmtPtr initializer;
  std::vector<StmtPtr> body;
  std::vector<StmtPtr> alternate;
  std::vector<VarBinding> bindings;
  std::vector<Parameter> parameters;
  std::vector<SwitchCase> cases;
  std::vector<std::string> modifiers;
  std::vector<std::string> interfaces;
  std::string text;
  std::string name;
  std::string base;
  std::string return_type;
  bool each{false};
};

std::string_view stmt_kind_name(StmtKind kind);

}  // namespace fo4::as3
