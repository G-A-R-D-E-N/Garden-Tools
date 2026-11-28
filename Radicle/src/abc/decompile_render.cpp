#include "decompile_eval.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc {

const char* expr_kind_name(AbcExprKind kind) noexcept {
  switch (kind) {
    case AbcExprKind::Literal:
      return "literal";
    case AbcExprKind::Local:
      return "local";
    case AbcExprKind::This:
      return "this";
    case AbcExprKind::Super:
      return "super";
    case AbcExprKind::Name:
      return "name";
    case AbcExprKind::Member:
      return "member";
    case AbcExprKind::Index:
      return "index";
    case AbcExprKind::Descendants:
      return "descendants";
    case AbcExprKind::Call:
      return "call";
    case AbcExprKind::Construct:
      return "construct";
    case AbcExprKind::ApplyType:
      return "applytype";
    case AbcExprKind::Unary:
      return "unary";
    case AbcExprKind::Postfix:
      return "postfix";
    case AbcExprKind::Binary:
      return "binary";
    case AbcExprKind::Conditional:
      return "conditional";
    case AbcExprKind::Cast:
      return "cast";
    case AbcExprKind::ArrayLiteral:
      return "array";
    case AbcExprKind::ObjectLiteral:
      return "object";
    case AbcExprKind::Assign:
      return "assign";
    case AbcExprKind::ScopeRef:
      return "scoperef";
    case AbcExprKind::GlobalScope:
      return "globalscope";
    case AbcExprKind::Activation:
      return "activation";
    case AbcExprKind::CatchScope:
      return "catchscope";
  }
  return "unknown";
}

std::string dump_function(const AbcFunction& function) {
  std::string out;
  for (const std::string& name : function.locals) {
    out += "var " + name + ";\n";
  }
  detail::render_statements(out, function.statements, 0);
  return out;
}

}  // namespace fo4::abc

namespace fo4::abc::detail {

int binary_precedence(const std::string& op) {
  if (op == "*" || op == "/" || op == "%") {
    return 13;
  }
  if (op == "+" || op == "-") {
    return 12;
  }
  if (op == "<<" || op == ">>" || op == ">>>") {
    return 11;
  }
  if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "instanceof" || op == "in" ||
      op == "is" || op == "as") {
    return 10;
  }
  if (op == "==" || op == "!=" || op == "===" || op == "!==") {
    return 9;
  }
  if (op == "&") {
    return 8;
  }
  if (op == "^") {
    return 7;
  }
  if (op == "|") {
    return 6;
  }
  if (op == "&&") {
    return 5;
  }
  return 4;
}

int precedence(const AbcExpr& expr) {
  switch (expr.kind) {
    case AbcExprKind::Literal:
      return !expr.text.empty() && expr.text[0] == '-' ? kUnaryPrec : kPrimaryPrec;
    case AbcExprKind::Unary:
      return kUnaryPrec;
    case AbcExprKind::Postfix:
      return kPostfixPrec;
    case AbcExprKind::Binary:
      return binary_precedence(expr.text);
    case AbcExprKind::Conditional:
      return kConditionalPrec;
    case AbcExprKind::Assign:
      return kAssignPrec;
    case AbcExprKind::Member:
    case AbcExprKind::Index:
    case AbcExprKind::Descendants:
    case AbcExprKind::Call:
    case AbcExprKind::Construct:
    case AbcExprKind::ApplyType:
    case AbcExprKind::Cast:
      return kMemberPrec;
    default:
      return kPrimaryPrec;
  }
}

namespace {

std::string wrap(const AbcExpr& expr, int minimum) {
  std::string text = render_expr(expr);
  if (precedence(expr) < minimum) {
    return "(" + text + ")";
  }
  return text;
}

std::string join_args(const std::vector<AbcExpr>& args, std::size_t from) {
  std::string out;
  for (std::size_t i = from; i < args.size(); ++i) {
    if (i != from) {
      out += ", ";
    }
    out += wrap(args[i], kAssignPrec);
  }
  return out;
}

bool plain_callee(const AbcExpr& expr) {
  switch (expr.kind) {
    case AbcExprKind::Name:
      return true;
    case AbcExprKind::Member:
      return !expr.args.empty() && plain_callee(expr.args[0]);
    case AbcExprKind::ApplyType:
      return !expr.args.empty() && plain_callee(expr.args[0]);
    default:
      return false;
  }
}

bool is_identifier(const std::string& text) {
  if (text.empty() || (text[0] >= '0' && text[0] <= '9')) {
    return false;
  }
  for (const char c : text) {
    const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '$';
    if (!word) {
      return false;
    }
  }
  return true;
}

std::string object_key(const AbcExpr& key) {
  if (key.text.size() >= 2 && key.text.front() == '"') {
    const std::string inner = key.text.substr(1, key.text.size() - 2);
    if (is_identifier(inner)) {
      return inner;
    }
  }
  return key.text;
}

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

void render_if(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  const AbcStmt* current = &stmt;
  indent(out, depth);
  out += "if (" + render_expr(current->expr) + ") {\n";
  render_statements(out, current->body, depth + 1);
  while (!current->other.empty()) {
    indent(out, depth);
    if (current->other.size() == 1 && current->other[0].kind == AbcStmtKind::If) {
      current = &current->other[0];
      out += "} else if (" + render_expr(current->expr) + ") {\n";
      render_statements(out, current->body, depth + 1);
    } else {
      out += "} else {\n";
      render_statements(out, current->other, depth + 1);
      break;
    }
  }
  indent(out, depth);
  out += "}\n";
}

void render_stmt(std::string& out, const AbcStmt& stmt, std::size_t depth) {
  switch (stmt.kind) {
    case AbcStmtKind::Expr:
      indent(out, depth);
      out += render_expr(stmt.expr) + ";\n";
      return;
    case AbcStmtKind::Return:
      indent(out, depth);
      out += stmt.has_expr ? "return " + render_expr(stmt.expr) + ";\n" : "return;\n";
      return;
    case AbcStmtKind::Throw:
      indent(out, depth);
      out += "throw " + render_expr(stmt.expr) + ";\n";
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
      render_if(out, stmt, depth);
      return;
    case AbcStmtKind::While:
    case AbcStmtKind::With:
      indent(out, depth);
      out += std::string(stmt.kind == AbcStmtKind::While ? "while" : "with") + " (" +
             render_expr(stmt.expr) + ") {\n";
      render_statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "}\n";
      return;
    case AbcStmtKind::Loop:
      indent(out, depth);
      out += "while (true) {\n";
      render_statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "}\n";
      return;
    case AbcStmtKind::DoWhile:
      indent(out, depth);
      out += "do {\n";
      render_statements(out, stmt.body, depth + 1);
      indent(out, depth);
      out += "} while (" + render_expr(stmt.expr) + ");\n";
      return;
    case AbcStmtKind::Switch:
      indent(out, depth);
      out += "switch (" + render_expr(stmt.expr) + ") {\n";
      for (const AbcStmtCase& one : stmt.cases) {
        for (const std::uint32_t value : one.values) {
          indent(out, depth + 1);
          out += "case " + std::to_string(value) + ":\n";
        }
        if (one.is_default) {
          indent(out, depth + 1);
          out += "default:\n";
        }
        render_statements(out, one.body, depth + 2);
      }
      indent(out, depth);
      out += "}\n";
      return;
    case AbcStmtKind::Try:
      indent(out, depth);
      out += "try {\n";
      render_statements(out, stmt.body, depth + 1);
      for (const AbcStmtCatch& one : stmt.catches) {
        indent(out, depth);
        out += "} catch (" + one.variable + ":" + one.type + ") {\n";
        render_statements(out, one.body, depth + 1);
      }
      indent(out, depth);
      out += "}\n";
      return;
  }
}

}  // namespace

std::string quote_string(const std::string& text) {
  std::string out = "\"";
  for (const char c : text) {
    const unsigned char byte = static_cast<unsigned char>(c);
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (byte < 0x20 || byte == 0x7F) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\x%02X", static_cast<unsigned>(byte));
          out += buffer;
        } else {
          out += c;
        }
        break;
    }
  }
  out += '"';
  return out;
}

std::string format_double(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0 ? "-Infinity" : "Infinity";
  }
  char buffer[40];
  if (value == 0) {
    return "0";
  }
  if (std::fabs(value) < 1e15 && std::floor(value) == value) {
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return buffer;
  }
  for (int precision = 1; precision <= 17; ++precision) {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (std::strtod(buffer, nullptr) == value) {
      break;
    }
  }
  return buffer;
}

std::string render_expr(const AbcExpr& expr) {
  const std::vector<AbcExpr>& args = expr.args;
  switch (expr.kind) {
    case AbcExprKind::Literal:
    case AbcExprKind::Local:
      return expr.text;
    case AbcExprKind::This:
      return "this";
    case AbcExprKind::Super:
      return "super";
    case AbcExprKind::Name:
      return expr.attribute ? "@" + expr.text : expr.text;
    case AbcExprKind::Member: {
      std::string out = wrap(args[0], kMemberPrec) + (expr.attribute ? ".@" : ".");
      if (args.size() > 1) {
        out += wrap(args[1], kPrimaryPrec) + "::";
      }
      return out + expr.text;
    }
    case AbcExprKind::Index: {
      std::string out = wrap(args[0], kMemberPrec);
      if (args.size() > 2) {
        out += expr.attribute ? ".@" : ".";
        return out + wrap(args[1], kPrimaryPrec) + "::[" + render_expr(args[2]) + "]";
      }
      return out + (expr.attribute ? ".@[" : "[") + render_expr(args[1]) + "]";
    }
    case AbcExprKind::Descendants:
      return wrap(args[0], kMemberPrec) + (expr.attribute ? "..@" : "..") + expr.text;
    case AbcExprKind::Call:
      return wrap(args[0], kMemberPrec) + "(" + join_args(args, 1) + ")";
    case AbcExprKind::Construct: {
      const std::string callee = plain_callee(args[0]) ? render_expr(args[0]) : "(" + render_expr(args[0]) + ")";
      return "new " + callee + "(" + join_args(args, 1) + ")";
    }
    case AbcExprKind::ApplyType: {
      std::string out = wrap(args[0], kMemberPrec) + ".<";
      for (std::size_t i = 1; i < args.size(); ++i) {
        if (i != 1) {
          out += ", ";
        }
        out += render_expr(args[i]);
      }
      return out + ">";
    }
    case AbcExprKind::Unary: {
      std::string operand = wrap(args[0], kUnaryPrec);
      if (!operand.empty() && !expr.text.empty() && operand[0] == expr.text.back() &&
          (operand[0] == '-' || operand[0] == '+')) {
        operand = "(" + operand + ")";
      }
      return expr.text + operand;
    }
    case AbcExprKind::Postfix:
      return wrap(args[0], kMemberPrec) + expr.text;
    case AbcExprKind::Binary: {
      const int level = binary_precedence(expr.text);
      return wrap(args[0], level) + " " + expr.text + " " + wrap(args[1], level + 1);
    }
    case AbcExprKind::Conditional:
      return wrap(args[0], 4) + " ? " + wrap(args[1], kAssignPrec) + " : " + wrap(args[2], kConditionalPrec);
    case AbcExprKind::Cast:
      return expr.text + "(" + render_expr(args[0]) + ")";
    case AbcExprKind::ArrayLiteral:
      return "[" + join_args(args, 0) + "]";
    case AbcExprKind::ObjectLiteral: {
      std::string out = "{";
      for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
        if (i != 0) {
          out += ", ";
        }
        out += object_key(args[i]) + ": " + wrap(args[i + 1], kAssignPrec);
      }
      return out + "}";
    }
    case AbcExprKind::Assign:
      return render_expr(args[0]) + " = " + wrap(args[1], kAssignPrec);
    case AbcExprKind::ScopeRef:
    case AbcExprKind::GlobalScope:
    case AbcExprKind::Activation:
    case AbcExprKind::CatchScope:
      return std::string("<") + expr_kind_name(expr.kind) + ">";
  }
  return std::string{};
}

void render_statements(std::string& out, const std::vector<AbcStmt>& list, std::size_t depth) {
  for (const AbcStmt& stmt : list) {
    render_stmt(out, stmt, depth);
  }
}

}  // namespace fo4::abc::detail
