#include "decompile_eval.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

AbcExpr make_expr(AbcExprKind kind, std::string text, std::vector<AbcExpr> args) {
  AbcExpr expr;
  expr.kind = kind;
  expr.text = std::move(text);
  expr.args = std::move(args);
  return expr;
}

AbcExpr negate_condition(AbcExpr expr) {
  if (expr.kind == AbcExprKind::Unary && expr.text == "!" && expr.args.size() == 1) {
    return std::move(expr.args[0]);
  }
  if (expr.kind == AbcExprKind::Binary) {
    if (expr.text == "==") {
      expr.text = "!=";
      return expr;
    }
    if (expr.text == "!=") {
      expr.text = "==";
      return expr;
    }
    if (expr.text == "===") {
      expr.text = "!==";
      return expr;
    }
    if (expr.text == "!==") {
      expr.text = "===";
      return expr;
    }
  }
  std::vector<AbcExpr> args;
  args.push_back(std::move(expr));
  return make_expr(AbcExprKind::Unary, "!", std::move(args));
}

const char* binary_operator(std::string_view mnemonic) {
  struct Row {
    std::string_view mnemonic;
    const char* op;
  };
  static const Row kRows[] = {
      {"add", "+"},          {"subtract", "-"},      {"multiply", "*"},     {"divide", "/"},
      {"modulo", "%"},       {"lshift", "<<"},       {"rshift", ">>"},      {"urshift", ">>>"},
      {"bitand", "&"},       {"bitor", "|"},         {"bitxor", "^"},       {"equals", "=="},
      {"strictequals", "==="}, {"lessthan", "<"},    {"lessequals", "<="},  {"greaterthan", ">"},
      {"greaterequals", ">="}, {"instanceof", "instanceof"}, {"istypelate", "is"},
      {"astypelate", "as"},  {"in", "in"},
  };
  for (const Row& row : kRows) {
    if (row.mnemonic == mnemonic) {
      return row.op;
    }
  }
  return nullptr;
}

const char* conversion_type(std::string_view mnemonic) {
  struct Row {
    std::string_view mnemonic;
    const char* type;
  };
  static const Row kRows[] = {
      {"convert_s", "String"}, {"convert_i", "int"},    {"convert_u", "uint"},
      {"convert_d", "Number"}, {"convert_b", "Boolean"}, {"convert_o", "Object"},
      {"coerce_s", "String"},  {"coerce_i", "int"},     {"coerce_u", "uint"},
      {"coerce_d", "Number"},  {"coerce_b", "Boolean"}, {"coerce_o", "Object"},
  };
  for (const Row& row : kRows) {
    if (row.mnemonic == mnemonic) {
      return row.type;
    }
  }
  return nullptr;
}

bool is_placeholder(AbcExprKind kind) {
  return kind == AbcExprKind::ScopeRef || kind == AbcExprKind::GlobalScope ||
         kind == AbcExprKind::Activation || kind == AbcExprKind::CatchScope;
}

bool is_simple_expr(const AbcExpr& expr) {
  switch (expr.kind) {
    case AbcExprKind::Literal:
    case AbcExprKind::Local:
    case AbcExprKind::This:
    case AbcExprKind::Super:
    case AbcExprKind::Name:
      return true;
    default:
      return is_placeholder(expr.kind);
  }
}

bool has_placeholder(const AbcExpr& expr) {
  if (is_placeholder(expr.kind)) {
    return true;
  }
  for (const AbcExpr& arg : expr.args) {
    if (has_placeholder(arg)) {
      return true;
    }
  }
  return false;
}

bool mentions_local(const AbcExpr& expr, const std::string& name) {
  if (expr.kind == AbcExprKind::Local && expr.text == name) {
    return true;
  }
  for (const AbcExpr& arg : expr.args) {
    if (mentions_local(arg, name)) {
      return true;
    }
  }
  return false;
}

std::uint32_t local_index_of(const AbcInstruction& instruction) {
  if (instruction.opcode >= 0xD0 && instruction.opcode <= 0xD3) {
    return static_cast<std::uint32_t>(instruction.opcode - 0xD0);
  }
  if (instruction.opcode >= 0xD4 && instruction.opcode <= 0xD7) {
    return static_cast<std::uint32_t>(instruction.opcode - 0xD4);
  }
  return instruction.operands.empty() ? kNone : instruction.operands[0].value;
}

}  // namespace fo4::abc::detail
