#include "decompile_eval.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

namespace {

bool is_lookup_receiver(const AbcExpr& expr) {
  return is_placeholder(expr.kind);
}

bool is_numeric_expr(const AbcExpr& expr) {
  switch (expr.kind) {
    case AbcExprKind::Literal:
      return !expr.text.empty() && (expr.text[0] == '-' || (expr.text[0] >= '0' && expr.text[0] <= '9'));
    case AbcExprKind::Cast:
      return expr.text == "int" || expr.text == "uint" || expr.text == "Number";
    case AbcExprKind::Unary:
      return expr.text == "-" || expr.text == "~";
    case AbcExprKind::Binary:
      return expr.text == "-" || expr.text == "*" || expr.text == "/" || expr.text == "%" ||
             expr.text == "<<" || expr.text == ">>" || expr.text == ">>>" || expr.text == "&" ||
             expr.text == "|" || expr.text == "^";
    default:
      return false;
  }
}

AbcExpr cast_to(const std::string& type, AbcExpr value) {
  if (value.kind == AbcExprKind::Cast && value.text == type) {
    return value;
  }
  std::vector<AbcExpr> args;
  args.push_back(std::move(value));
  return make_expr(AbcExprKind::Cast, type, std::move(args));
}

AbcExpr unary_of(const char* text, AbcExpr value) {
  std::vector<AbcExpr> args;
  args.push_back(std::move(value));
  return make_expr(AbcExprKind::Unary, text, std::move(args));
}

AbcExpr binary_of(const char* text, AbcExpr left, AbcExpr right) {
  std::vector<AbcExpr> args;
  args.push_back(std::move(left));
  args.push_back(std::move(right));
  return make_expr(AbcExprKind::Binary, text, std::move(args));
}

}  // namespace

bool Evaluator::pop_name(std::uint32_t index, NameRef& ref) {
  ref = NameRef{};
  if (index >= file_.pool.multinames.size()) {
    fail(AbcDecompileStatus::UnsupportedExpression, "a multiname operand is outside the pool");
    return false;
  }
  const AbcMultiname& name = file_.pool.multinames[index];
  ref.text = file_.multiname_at(index);
  switch (name.kind) {
    case AbcMultinameKind::QNameA:
    case AbcMultinameKind::MultinameA:
      ref.attribute = true;
      break;
    case AbcMultinameKind::RTQName:
      ref.has_ns = true;
      ref.text = file_.string_at(name.name);
      break;
    case AbcMultinameKind::RTQNameA:
      ref.has_ns = true;
      ref.attribute = true;
      ref.text = file_.string_at(name.name);
      break;
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      ref.has_ns = true;
      ref.has_name = true;
      ref.attribute = name.kind == AbcMultinameKind::RTQNameLA;
      ref.text.clear();
      break;
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
      ref.has_name = true;
      ref.attribute = name.kind == AbcMultinameKind::MultinameLA;
      ref.text.clear();
      break;
    default:
      break;
  }
  if (ref.has_name) {
    ref.name = pop();
  }
  if (ref.has_ns) {
    ref.ns = pop();
  }
  return !bad();
}

AbcExpr Evaluator::member_expr(AbcExpr receiver, const NameRef& ref) {
  if (is_lookup_receiver(receiver)) {
    if (ref.has_ns || ref.has_name) {
      fail(AbcDecompileStatus::UnsupportedExpression, "a runtime name is looked up on a scope object");
      return make_expr(AbcExprKind::Literal, "undefined");
    }
    AbcExpr name = make_expr(AbcExprKind::Name, ref.text);
    name.attribute = ref.attribute;
    return name;
  }
  std::vector<AbcExpr> args;
  args.push_back(std::move(receiver));
  if (ref.has_ns) {
    args.push_back(ref.ns);
  }
  if (ref.has_name) {
    args.push_back(ref.name);
    AbcExpr index = make_expr(AbcExprKind::Index, "", std::move(args));
    index.attribute = ref.attribute;
    return index;
  }
  AbcExpr member = make_expr(AbcExprKind::Member, ref.text, std::move(args));
  member.attribute = ref.attribute;
  return member;
}

AbcExpr Evaluator::numeric_step(AbcExpr value, const char* op) {
  AbcExpr operand = is_numeric_expr(value) ? std::move(value) : cast_to("Number", std::move(value));
  return binary_of(op, std::move(operand), make_expr(AbcExprKind::Literal, "1"));
}

bool Evaluator::step_property(const AbcInstruction& in, std::string_view m) {
  if (m == "getlex" || m == "findpropstrict" || m == "findproperty") {
    NameRef ref;
    if (!pop_name(operand(in, 0), ref)) {
      return true;
    }
    if (ref.has_ns || ref.has_name) {
      fail(AbcDecompileStatus::UnsupportedExpression, "a name lookup with a runtime part");
      return true;
    }
    if (m == "getlex") {
      AbcExpr name = make_expr(AbcExprKind::Name, ref.text);
      name.attribute = ref.attribute;
      push(std::move(name));
    } else {
      push(make_expr(AbcExprKind::ScopeRef, ref.text));
    }
    return true;
  }
  if (m == "getproperty" || m == "getsuper" || m == "deleteproperty" || m == "getdescendants") {
    NameRef ref;
    if (!pop_name(operand(in, 0), ref)) {
      return true;
    }
    AbcExpr object = pop();
    if (m == "getsuper") {
      object = make_expr(AbcExprKind::Super, "super");
    }
    if (m == "getdescendants") {
      if (ref.has_ns || ref.has_name) {
        fail(AbcDecompileStatus::UnsupportedExpression, "a descendants query with a runtime name");
        return true;
      }
      std::vector<AbcExpr> args;
      args.push_back(std::move(object));
      AbcExpr query = make_expr(AbcExprKind::Descendants, ref.text, std::move(args));
      query.attribute = ref.attribute;
      push(std::move(query));
      return true;
    }
    AbcExpr member = member_expr(std::move(object), ref);
    push(m == "deleteproperty" ? unary_of("delete ", std::move(member)) : std::move(member));
    return true;
  }
  if (m == "setproperty" || m == "initproperty" || m == "setsuper") {
    AbcExpr value = pop();
    NameRef ref;
    if (!pop_name(operand(in, 0), ref)) {
      return true;
    }
    AbcExpr object = pop();
    if (m == "setsuper") {
      object = make_expr(AbcExprKind::Super, "super");
    }
    AbcExpr target = member_expr(std::move(object), ref);
    flush_stack();
    emit_assign(std::move(target), std::move(value));
    return true;
  }
  if (m == "getslot") {
    const std::uint32_t slot = operand(in, 0);
    AbcExpr object = pop();
    std::string name;
    if (object.kind == AbcExprKind::Activation && slot_name(slot, name)) {
      declare_local(name);
      push(make_expr(AbcExprKind::Local, name));
    } else if (object.kind == AbcExprKind::CatchScope && slot == 1) {
      push(make_expr(AbcExprKind::Name, object.text));
    } else {
      fail(AbcDecompileStatus::UnsupportedMember, "slot " + std::to_string(slot) +
                                                      " is read outside an activation object or has no trait");
    }
    return true;
  }
  if (m == "setslot") {
    const std::uint32_t slot = operand(in, 0);
    AbcExpr value = pop();
    AbcExpr object = pop();
    std::string name;
    if (object.kind == AbcExprKind::Activation && slot_name(slot, name)) {
      declare_local(name);
      flush_stack();
      emit_assign(make_expr(AbcExprKind::Local, name), std::move(value));
    } else if (object.kind == AbcExprKind::CatchScope && slot == 1) {
      flush_stack();
      emit_assign(make_expr(AbcExprKind::Name, object.text), std::move(value));
    } else {
      fail(AbcDecompileStatus::UnsupportedMember, "slot " + std::to_string(slot) +
                                                      " is written outside an activation object or has no trait");
    }
    return true;
  }
  return false;
}

bool Evaluator::step_call(const AbcInstruction& in, std::string_view m) {
  const bool super_call = m == "callsuper" || m == "callsupervoid";
  if (m == "callproperty" || m == "callproplex" || m == "callpropvoid" || m == "constructprop" ||
      super_call) {
    std::vector<AbcExpr> args = pop_args(operand(in, 1));
    NameRef ref;
    if (!pop_name(operand(in, 0), ref)) {
      return true;
    }
    AbcExpr receiver = pop();
    if (super_call) {
      receiver = make_expr(AbcExprKind::Super, "super");
    }
    std::vector<AbcExpr> parts;
    parts.push_back(member_expr(std::move(receiver), ref));
    for (AbcExpr& arg : args) {
      parts.push_back(std::move(arg));
    }
    if (m == "constructprop") {
      push(make_expr(AbcExprKind::Construct, "", std::move(parts)));
      return true;
    }
    AbcExpr call = make_expr(AbcExprKind::Call, "", std::move(parts));
    if (m == "callpropvoid" || m == "callsupervoid") {
      flush_stack();
      emit_expr(std::move(call));
    } else {
      push(std::move(call));
    }
    return true;
  }
  if (m == "construct" || m == "constructsuper") {
    std::vector<AbcExpr> args = pop_args(operand(in, 0));
    AbcExpr callee = pop();
    std::vector<AbcExpr> parts;
    parts.push_back(m == "constructsuper" ? make_expr(AbcExprKind::Super, "super") : std::move(callee));
    for (AbcExpr& arg : args) {
      parts.push_back(std::move(arg));
    }
    if (m == "construct") {
      push(make_expr(AbcExprKind::Construct, "", std::move(parts)));
    } else {
      flush_stack();
      emit_expr(make_expr(AbcExprKind::Call, "", std::move(parts)));
    }
    return true;
  }
  if (m == "call") {
    std::vector<AbcExpr> args = pop_args(operand(in, 0));
    AbcExpr receiver = pop();
    AbcExpr callee = pop();
    const bool bare = is_lookup_receiver(receiver) ||
                      (receiver.kind == AbcExprKind::Literal &&
                       (receiver.text == "null" || receiver.text == "undefined"));
    std::vector<AbcExpr> parts;
    if (bare) {
      parts.push_back(std::move(callee));
    } else {
      std::vector<AbcExpr> owner;
      owner.push_back(std::move(callee));
      parts.push_back(make_expr(AbcExprKind::Member, "call", std::move(owner)));
      parts.push_back(std::move(receiver));
    }
    for (AbcExpr& arg : args) {
      parts.push_back(std::move(arg));
    }
    push(make_expr(AbcExprKind::Call, "", std::move(parts)));
    return true;
  }
  if (m == "applytype") {
    std::vector<AbcExpr> args = pop_args(operand(in, 0));
    AbcExpr base = pop();
    std::vector<AbcExpr> parts;
    parts.push_back(std::move(base));
    for (AbcExpr& arg : args) {
      parts.push_back(std::move(arg));
    }
    push(make_expr(AbcExprKind::ApplyType, "", std::move(parts)));
    return true;
  }
  if (m == "newarray") {
    push(make_expr(AbcExprKind::ArrayLiteral, "", pop_args(operand(in, 0))));
    return true;
  }
  if (m == "newobject") {
    std::vector<AbcExpr> args = pop_args(operand(in, 0) * 2);
    for (std::size_t i = 0; i < args.size(); i += 2) {
      const bool text_key = args[i].kind == AbcExprKind::Literal && !args[i].text.empty() && args[i].text[0] == '"';
      if (!text_key) {
        fail(AbcDecompileStatus::UnsupportedExpression, "an object literal key is not a string constant");
        return true;
      }
    }
    push(make_expr(AbcExprKind::ObjectLiteral, "", std::move(args)));
    return true;
  }
  return false;
}

bool Evaluator::step_value(const AbcInstruction& in, std::string_view m) {
  if (const char* op = binary_operator(m)) {
    AbcExpr right = pop();
    AbcExpr left = pop();
    push(binary_of(op, std::move(left), std::move(right)));
    return true;
  }
  if (const char* type = conversion_type(m)) {
    push(cast_to(type, pop()));
    return true;
  }
  if (m == "coerce_a") {
    return true;
  }
  if (m == "coerce") {
    push(cast_to(type_text(operand(in, 0)), pop()));
    return true;
  }
  if (m == "istype" || m == "astype") {
    AbcExpr value = pop();
    push(binary_of(m == "istype" ? "is" : "as", std::move(value),
                   make_expr(AbcExprKind::Name, type_text(operand(in, 0)))));
    return true;
  }
  if (m == "negate") {
    push(unary_of("-", pop()));
    return true;
  }
  if (m == "not") {
    push(unary_of("!", pop()));
    return true;
  }
  if (m == "bitnot") {
    push(unary_of("~", pop()));
    return true;
  }
  if (m == "typeof") {
    push(unary_of("typeof ", pop()));
    return true;
  }
  if (m == "increment" || m == "decrement") {
    push(numeric_step(pop(), m == "increment" ? "+" : "-"));
    return true;
  }
  if (m == "returnvalue" || m == "throw") {
    AbcStmt stmt;
    stmt.kind = m == "throw" ? AbcStmtKind::Throw : AbcStmtKind::Return;
    stmt.has_expr = true;
    stmt.expr = pop();
    flush_stack();
    emit(std::move(stmt));
    return true;
  }
  if (m == "returnvoid") {
    AbcStmt stmt;
    stmt.kind = AbcStmtKind::Return;
    flush_stack();
    emit(std::move(stmt));
    return true;
  }
  return false;
}

}  // namespace fo4::abc::detail
