#include "fo4/as3/ast.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3 {

Expr::~Expr() {
  std::vector<ExprPtr> pending;
  const auto detach = [&pending](Expr& node) {
    for (auto& child : node.children) {
      if (child) pending.push_back(std::move(child));
    }
    for (auto& member : node.members) {
      if (member.second) pending.push_back(std::move(member.second));
    }
  };
  detach(*this);
  while (!pending.empty()) {
    auto node = std::move(pending.back());
    pending.pop_back();
    detach(*node);
  }
}

std::string_view expr_kind_name(ExprKind kind) {
  switch (kind) {
    case ExprKind::number_literal: return "number_literal";
    case ExprKind::string_literal: return "string_literal";
    case ExprKind::boolean_literal: return "boolean_literal";
    case ExprKind::null_literal: return "null_literal";
    case ExprKind::identifier: return "identifier";
    case ExprKind::this_expression: return "this_expression";
    case ExprKind::member: return "member";
    case ExprKind::index: return "index";
    case ExprKind::call: return "call";
    case ExprKind::construct: return "construct";
    case ExprKind::unary: return "unary";
    case ExprKind::binary: return "binary";
    case ExprKind::logical: return "logical";
    case ExprKind::conditional: return "conditional";
    case ExprKind::assignment: return "assignment";
    case ExprKind::sequence: return "sequence";
    case ExprKind::array_literal: return "array_literal";
    case ExprKind::object_literal: return "object_literal";
  }
  return "unknown";
}

std::string_view stmt_kind_name(StmtKind kind) {
  switch (kind) {
    case StmtKind::empty: return "empty";
    case StmtKind::expression: return "expression";
    case StmtKind::block: return "block";
    case StmtKind::var_declaration: return "var_declaration";
    case StmtKind::if_statement: return "if_statement";
    case StmtKind::while_statement: return "while_statement";
    case StmtKind::do_while_statement: return "do_while_statement";
    case StmtKind::for_statement: return "for_statement";
    case StmtKind::for_in_statement: return "for_in_statement";
    case StmtKind::function_declaration: return "function_declaration";
    case StmtKind::throw_statement: return "throw_statement";
    case StmtKind::switch_statement: return "switch_statement";
    case StmtKind::package_declaration: return "package_declaration";
    case StmtKind::class_declaration: return "class_declaration";
    case StmtKind::import_directive: return "import_directive";
    case StmtKind::return_statement: return "return_statement";
    case StmtKind::break_statement: return "break_statement";
    case StmtKind::continue_statement: return "continue_statement";
  }
  return "unknown";
}

}  // namespace fo4::as3
