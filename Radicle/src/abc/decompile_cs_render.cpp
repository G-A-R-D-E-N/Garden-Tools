#include "decompile_write.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

namespace {

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

bool is_comparison(const std::string& op) {
  return op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" || op == "<=" ||
         op == ">" || op == ">=" || op == "is";
}

bool is_logical(const AbcExpr& expr) {
  return expr.kind == AbcExprKind::Binary && (expr.text == "&&" || expr.text == "||");
}

bool is_boolean(const AbcExpr& expr) {
  switch (expr.kind) {
    case AbcExprKind::Literal:
      return expr.text == "true" || expr.text == "false";
    case AbcExprKind::Unary:
      return expr.text == "!";
    case AbcExprKind::Binary:
      return is_comparison(expr.text);
    default:
      return false;
  }
}

int cs_precedence(const AbcExpr& expr) {
  if (expr.kind == AbcExprKind::Cast) {
    return expr.text != "Object" && cs_cast_helper(expr.text) != nullptr ? kPrimaryPrec : kUnaryPrec;
  }
  return precedence(expr);
}

bool is_terminal(const AbcStmt& stmt) {
  return stmt.kind == AbcStmtKind::Break || stmt.kind == AbcStmtKind::Return ||
         stmt.kind == AbcStmtKind::Throw || stmt.kind == AbcStmtKind::Continue;
}

}  // namespace

void CsRenderer::fail(AbcDecompileStatus status, const std::string& message) {
  if (failed_) {
    return;
  }
  failed_ = true;
  status_ = status;
  message_ = message;
}

std::string CsRenderer::wrap(const AbcExpr& value, int minimum) {
  const std::string text = expr(value);
  return cs_precedence(value) < minimum ? "(" + text + ")" : text;
}

std::string CsRenderer::type_name(const std::string& as3) {
  std::vector<std::string> unmapped;
  const std::string text = cs_type_text(as3, packages_, unmapped);
  if (!unmapped.empty()) {
    fail(AbcDecompileStatus::UnmappedName, "'" + unmapped[0] + "' has no C# mapping");
  }
  return text;
}

std::string CsRenderer::catch_type(const std::string& type) {
  return type == "*" ? std::string{"System.Exception"} : type_name(type);
}

std::string CsRenderer::name_text(const AbcExpr& value) {
  if (value.attribute) {
    fail(AbcDecompileStatus::UnsupportedExpression, "an E4X attribute name has no C# form");
    return {};
  }
  const CsName resolved = cs_resolve_name(value.text, packages_);
  if (resolved.status == CsNameStatus::Unmapped) {
    fail(AbcDecompileStatus::UnmappedName, "'" + value.text + "' has no C# mapping");
  } else if (resolved.status == CsNameStatus::Unsupported) {
    fail(AbcDecompileStatus::UnsupportedExpression, "'" + value.text + "' has no C# form");
  }
  return resolved.text;
}

std::string CsRenderer::args_text(const std::vector<AbcExpr>& args, std::size_t from) {
  std::string out;
  for (std::size_t i = from; i < args.size(); ++i) {
    out += (i == from ? "" : ", ") + wrap(args[i], kAssignPrec);
  }
  return out;
}

bool CsRenderer::plain_callee(const AbcExpr& value) const {
  switch (value.kind) {
    case AbcExprKind::Name:
      return true;
    case AbcExprKind::Member:
    case AbcExprKind::ApplyType:
      return !value.args.empty() && plain_callee(value.args[0]);
    default:
      return false;
  }
}

std::string CsRenderer::cast_text(const AbcExpr& value) {
  const AbcExpr& operand = value.args[0];
  if (value.text == "Object") {
    return "(object)(" + expr(operand) + ")";
  }
  if (const char* helper = cs_cast_helper(value.text)) {
    return std::string(helper) + "(" + expr(operand) + ")";
  }
  const std::string type = type_name(value.text);
  return "(" + type + ")(" + expr(operand) + ")";
}

std::string CsRenderer::call_text(const AbcExpr& value) {
  const AbcExpr& callee = value.args[0];
  if (callee.kind == AbcExprKind::Super) {
    fail(AbcDecompileStatus::UnsupportedExpression,
         "a super call outside the first statement of a constructor");
    return {};
  }
  if (callee.kind == AbcExprKind::Name && !callee.attribute && value.args.size() == 2 &&
      cs_resolve_name(callee.text, packages_).type) {
    AbcExpr as_cast = make_expr(AbcExprKind::Cast, callee.text, {value.args[1]});
    return cast_text(as_cast);
  }
  return wrap(callee, kMemberPrec) + "(" + args_text(value.args, 1) + ")";
}

std::string CsRenderer::construct_text(const AbcExpr& value) {
  if (!plain_callee(value.args[0])) {
    fail(AbcDecompileStatus::UnsupportedExpression, "new is applied to a computed class");
    return {};
  }
  return "new " + expr(value.args[0]) + "(" + args_text(value.args, 1) + ")";
}

std::string CsRenderer::binary_text(const AbcExpr& value) {
  const std::string& op = value.text;
  if (op == "&&" || op == "||") {
    fail(AbcDecompileStatus::UnsupportedExpression,
         "a short-circuit operator is used as a value, which C# types as bool");
    return {};
  }
  if (op == "instanceof" || op == "in") {
    fail(AbcDecompileStatus::UnsupportedExpression, op + " has no C# form");
    return {};
  }
  if (op == "as") {
    const std::string target = cs_resolve_name(value.args[1].text, packages_).text;
    if (target == "int" || target == "uint" || target == "double" || target == "bool") {
      fail(AbcDecompileStatus::UnsupportedExpression, "as to the value type " + target + " has no C# form");
      return {};
    }
  }
  const int level = binary_precedence(op);
  if (op == "/") {
    return "(double)" + wrap(value.args[0], kUnaryPrec) + " / " + wrap(value.args[1], level + 1);
  }
  const std::string cs_op = op == "===" ? "==" : op == "!==" ? "!=" : op;
  return wrap(value.args[0], level) + " " + cs_op + " " + wrap(value.args[1], level + 1);
}

std::string CsRenderer::cond(const AbcExpr& value, int minimum) {
  if (is_logical(value)) {
    const int level = binary_precedence(value.text);
    const std::string text =
        cond(value.args[0], level) + " " + value.text + " " + cond(value.args[1], level + 1);
    return level < minimum ? "(" + text + ")" : text;
  }
  if (is_boolean(value)) {
    return wrap(value, minimum);
  }
  return "Flash.Ops.ToBool(" + expr(value) + ")";
}

std::string CsRenderer::expr(const AbcExpr& value) {
  const std::vector<AbcExpr>& args = value.args;
  switch (value.kind) {
    case AbcExprKind::Literal:
      return cs_literal(value.text);
    case AbcExprKind::Local: {
      if (value.text == "arguments") {
        fail(AbcDecompileStatus::UnsupportedExpression, "the arguments object has no C# form");
        return {};
      }
      const auto renamed = renames_.find(value.text);
      return renamed != renames_.end() ? renamed->second : cs_identifier(value.text);
    }
    case AbcExprKind::This:
      return "this";
    case AbcExprKind::Super:
      return "base";
    case AbcExprKind::Name:
      return name_text(value);
    case AbcExprKind::Member: {
      if (args.size() > 1 || value.attribute) {
        fail(AbcDecompileStatus::UnsupportedExpression,
             "a namespace-qualified or attribute member has no C# form");
        return {};
      }
      const CsName member = cs_resolve_member(value.text);
      if (member.status == CsNameStatus::Unmapped) {
        fail(AbcDecompileStatus::UnmappedName, "'" + value.text + "' has no C# mapping");
      }
      return wrap(args[0], kMemberPrec) + "." + member.text;
    }
    case AbcExprKind::Index:
      if (args.size() > 2 || value.attribute) {
        fail(AbcDecompileStatus::UnsupportedExpression,
             "a namespace-qualified or attribute index has no C# form");
        return {};
      }
      return wrap(args[0], kMemberPrec) + "[" + expr(args[1]) + "]";
    case AbcExprKind::Descendants:
      fail(AbcDecompileStatus::UnsupportedExpression, "an E4X descendants query has no C# form");
      return {};
    case AbcExprKind::Call:
      return call_text(value);
    case AbcExprKind::Construct:
      return construct_text(value);
    case AbcExprKind::ApplyType: {
      std::string out = wrap(args[0], kMemberPrec) + "<";
      for (std::size_t i = 1; i < args.size(); ++i) {
        out += (i == 1 ? "" : ", ") + expr(args[i]);
      }
      return out + ">";
    }
    case AbcExprKind::Unary: {
      if (value.text == "!") {
        return "!" + cond(args[0], kUnaryPrec);
      }
      if (value.text != "-" && value.text != "~") {
        fail(AbcDecompileStatus::UnsupportedExpression, "'" + value.text + "' has no C# form");
        return {};
      }
      std::string operand = wrap(args[0], kUnaryPrec);
      if (value.text == "-" && !operand.empty() && operand[0] == '-') {
        operand = "(" + operand + ")";
      }
      return value.text + operand;
    }
    case AbcExprKind::Postfix:
      return wrap(args[0], kMemberPrec) + value.text;
    case AbcExprKind::Binary:
      return binary_text(value);
    case AbcExprKind::Conditional:
      return cond(args[0], 4) + " ? " + wrap(args[1], kAssignPrec) + " : " + wrap(args[2], kConditionalPrec);
    case AbcExprKind::Cast:
      return cast_text(value);
    case AbcExprKind::ArrayLiteral:
      return args.empty() ? "new Flash.AsArray()" : "new Flash.AsArray { " + args_text(args, 0) + " }";
    case AbcExprKind::ObjectLiteral: {
      if (args.empty()) {
        return "new Flash.AsObject()";
      }
      std::string out = "new Flash.AsObject { ";
      for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
        out += (i == 0 ? "" : ", ") + std::string("[") + cs_literal(args[i].text) + "] = " +
               wrap(args[i + 1], kAssignPrec);
      }
      return out + " }";
    }
    case AbcExprKind::Assign:
      return expr(args[0]) + " = " + wrap(args[1], kAssignPrec);
    case AbcExprKind::ScopeRef:
    case AbcExprKind::GlobalScope:
    case AbcExprKind::Activation:
    case AbcExprKind::CatchScope:
      fail(AbcDecompileStatus::UnsupportedExpression, "a scope object is used as a value");
      return {};
  }
  return {};
}

void CsRenderer::statements(std::string& out, const std::vector<AbcStmt>& list, std::size_t depth) {
  for (const AbcStmt& stmt : list) {
    statement(out, stmt, depth);
  }
}

void CsRenderer::if_statement(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  const AbcStmt* current = &stmt;
  indent(out, depth);
  out += "if (" + cond(current->expr, 0) + ") {\n";
  statements(out, current->body, depth + 1);
  while (!current->other.empty()) {
    indent(out, depth);
    if (current->other.size() == 1 && current->other[0].kind == AbcStmtKind::If) {
      current = &current->other[0];
      out += "} else if (" + cond(current->expr, 0) + ") {\n";
      statements(out, current->body, depth + 1);
    } else {
      out += "} else {\n";
      statements(out, current->other, depth + 1);
      break;
    }
  }
  indent(out, depth);
  out += "}\n";
}

void CsRenderer::switch_statement(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  indent(out, depth);
  out += "switch (" + expr(stmt.expr) + ") {\n";
  for (std::size_t i = 0; i < stmt.cases.size(); ++i) {
    const AbcStmtCase& one = stmt.cases[i];
    for (const std::uint32_t value : one.values) {
      indent(out, depth + 1);
      out += "case " + std::to_string(value) + ":\n";
    }
    if (one.is_default) {
      indent(out, depth + 1);
      out += "default:\n";
    }
    statements(out, one.body, depth + 2);
    const bool last = i + 1 == stmt.cases.size();
    const bool terminal = !one.body.empty() && is_terminal(one.body.back());
    if (!terminal && (last || !one.body.empty())) {
      if (!last) {
        fail(AbcDecompileStatus::UnsupportedSwitch, "a case falls through to the next one");
        return;
      }
      indent(out, depth + 2);
      out += "break;\n";
    }
  }
  indent(out, depth);
  out += "}\n";
}

void CsRenderer::try_statement(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  if (stmt.catches.empty()) {
    fail(AbcDecompileStatus::UnsupportedTry, "a try without a catch has no C# form here");
    return;
  }
  indent(out, depth);
  out += "try {\n";
  statements(out, stmt.body, depth + 1);
  for (const AbcStmtCatch& one : stmt.catches) {
    indent(out, depth);
    out += "} catch (" + catch_type(one.type) + " " + cs_identifier(one.variable) + ") {\n";
    statements(out, one.body, depth + 1);
  }
  indent(out, depth);
  out += "}\n";
}

void CsRenderer::statement(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  switch (stmt.kind) {
    case AbcStmtKind::Expr: {
      const AbcExpr& value = stmt.expr;
      const bool cast_call = value.kind == AbcExprKind::Call && value.args[0].kind == AbcExprKind::Name &&
                             value.args.size() == 2 && cs_resolve_name(value.args[0].text, packages_).type;
      const bool allowed = !cast_call && (value.kind == AbcExprKind::Call || value.kind == AbcExprKind::Construct ||
                                          value.kind == AbcExprKind::Assign || value.kind == AbcExprKind::Postfix);
      indent(out, depth);
      out += (allowed ? "" : "_ = ") + expr(value) + ";\n";
      return;
    }
    case AbcStmtKind::Return:
      indent(out, depth);
      out += stmt.has_expr ? "return " + expr(stmt.expr) + ";\n" : "return;\n";
      return;
    case AbcStmtKind::Throw:
      indent(out, depth);
      out += "throw " + expr(stmt.expr) + ";\n";
      return;
    case AbcStmtKind::Break:
      indent(out, depth);
      out += "break;\n";
      return;
    case AbcStmtKind::Continue:
      indent(out, depth);
      out += "continue;\n";
      return;
    case AbcStmtKind::If:
      if_statement(out, stmt, depth);
      return;
    case AbcStmtKind::While:
      indent(out, depth);
      out += "while (" + cond(stmt.expr, 0) + ") {\n";
      statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "}\n";
      return;
    case AbcStmtKind::With:
      fail(AbcDecompileStatus::UnsupportedExpression, "a with statement has no C# form");
      return;
    case AbcStmtKind::Loop:
      indent(out, depth);
      out += "while (true) {\n";
      statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "}\n";
      return;
    case AbcStmtKind::DoWhile:
      indent(out, depth);
      out += "do {\n";
      statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "} while (" + cond(stmt.expr, 0) + ");\n";
      return;
    case AbcStmtKind::Switch:
      switch_statement(out, stmt, depth);
      return;
    case AbcStmtKind::Try:
      try_statement(out, stmt, depth);
      return;
  }
}

}  // namespace fo4::abc::detail
