#include "decompile_eval.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

Evaluator::Evaluator(const AbcFile& file, const AbcStructure& structure, const AbcMethodInfo& method,
                     const AbcMethodBody& body, AbcFunction& function, AbcDecompileIssue& issue)
    : file_(file), s_(structure), method_(method), body_(body), fn_(function), issue_(issue) {}

bool Evaluator::bad() const { return issue_.status != AbcDecompileStatus::Ok; }

void Evaluator::fail(AbcDecompileStatus status, const std::string& message) {
  if (issue_.status == AbcDecompileStatus::Ok) {
    issue_.status = status;
    issue_.offset = offset_;
    issue_.message = message;
  }
}

AbcExpr Evaluator::pop() {
  if (stack_.size() <= floor_) {
    fail(AbcDecompileStatus::StackShape, "the symbolic stack underflows");
    return make_expr(AbcExprKind::Literal, "undefined");
  }
  AbcExpr expr = std::move(stack_.back());
  stack_.pop_back();
  return expr;
}

void Evaluator::push(AbcExpr expr) { stack_.push_back(std::move(expr)); }

std::vector<AbcExpr> Evaluator::pop_args(std::uint32_t count) {
  std::vector<AbcExpr> args;
  if (count > stack_.size() - std::min<std::size_t>(stack_.size(), floor_)) {
    fail(AbcDecompileStatus::StackShape, "an argument count exceeds the symbolic stack");
    return args;
  }
  args.resize(count);
  for (std::uint32_t i = count; i-- > 0;) {
    args[i] = pop();
  }
  return args;
}

void Evaluator::emit(AbcStmt stmt) {
  if (bad()) {
    return;
  }
  if (value_depth_ > 0 || in_member_) {
    fail(AbcDecompileStatus::UnsupportedValueRegion, "a statement is needed inside a value expression");
    return;
  }
  if (stmt.has_expr && has_placeholder(stmt.expr)) {
    fail(AbcDecompileStatus::UnsupportedExpression, "a scope object is used as a value");
    return;
  }
  out_->push_back(std::move(stmt));
}

void Evaluator::emit_expr(AbcExpr expr) {
  if (is_placeholder(expr.kind)) {
    return;
  }
  AbcStmt stmt;
  stmt.kind = AbcStmtKind::Expr;
  stmt.has_expr = true;
  stmt.expr = std::move(expr);
  emit(std::move(stmt));
}

void Evaluator::emit_assign(AbcExpr target, AbcExpr value) {
  const bool same_leaf = target.kind == value.kind && target.text == value.text &&
                         (target.kind == AbcExprKind::Local || target.kind == AbcExprKind::Name);
  if (same_leaf) {
    return;
  }
  std::vector<AbcExpr> args;
  args.push_back(std::move(target));
  args.push_back(std::move(value));
  emit_expr(make_expr(AbcExprKind::Assign, "", std::move(args)));
}

void Evaluator::spill(AbcExpr& entry) {
  const std::string name = "_t" + std::to_string(temp_count_++);
  declare_local(name);
  AbcExpr value = std::move(entry);
  emit_assign(make_expr(AbcExprKind::Local, name), std::move(value));
  entry = make_expr(AbcExprKind::Local, name);
}

void Evaluator::flush_stack(const std::string* written_local) {
  for (std::size_t i = 0; i < stack_.size() && !bad(); ++i) {
    if (!is_simple_expr(stack_[i]) ||
        (written_local != nullptr && mentions_local(stack_[i], *written_local))) {
      AbcExpr entry = std::move(stack_[i]);
      spill(entry);
      stack_[i] = std::move(entry);
    }
  }
}

std::uint32_t Evaluator::operand(const AbcInstruction& in, std::size_t at) const {
  return at < in.operands.size() ? in.operands[at].value : 0;
}

std::string Evaluator::type_text(std::uint32_t index) const {
  return index == 0 ? std::string{"*"} : file_.multiname_at(index);
}

void Evaluator::name_params() {
  const std::uint32_t count = method_.param_count;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string name;
    if (method_.has_param_names() && i < method_.param_names.size()) {
      name = file_.string_at(method_.param_names[i]);
    }
    if (name.empty()) {
      name = "arg" + std::to_string(i + 1);
    }
    fn_.param_names.push_back(name);
  }
  if (method_.needs_rest()) {
    fn_.rest_name = "rest";
  }
}

std::string Evaluator::local_name(std::uint32_t index, bool& extra) {
  extra = false;
  const std::uint32_t count = method_.param_count;
  if (index >= 1 && index <= count) {
    return fn_.param_names[index - 1];
  }
  if (index == count + 1 && method_.needs_rest()) {
    return fn_.rest_name;
  }
  if (index == count + 1 && method_.needs_arguments()) {
    return "arguments";
  }
  extra = true;
  return "local" + std::to_string(index);
}

void Evaluator::declare_local(const std::string& name) {
  if (std::find(fn_.param_names.begin(), fn_.param_names.end(), name) != fn_.param_names.end() ||
      name == fn_.rest_name || name == "arguments") {
    return;
  }
  if (std::find(fn_.locals.begin(), fn_.locals.end(), name) == fn_.locals.end()) {
    fn_.locals.push_back(name);
  }
}

AbcExpr Evaluator::local_expr(std::uint32_t index) {
  const auto bound = bound_.find(index);
  if (bound != bound_.end()) {
    return bound->second;
  }
  if (index == 0) {
    return make_expr(AbcExprKind::This, "this");
  }
  bool extra = false;
  const std::string name = local_name(index, extra);
  if (extra) {
    declare_local(name);
  }
  return make_expr(AbcExprKind::Local, name);
}

AbcExpr Evaluator::local_target(std::uint32_t index) {
  if (index == 0) {
    fail(AbcDecompileStatus::UnsupportedExpression, "an assignment to register 0 (this)");
    return make_expr(AbcExprKind::Local, "this");
  }
  bound_.erase(index);
  bool extra = false;
  const std::string name = local_name(index, extra);
  if (extra) {
    declare_local(name);
  }
  return make_expr(AbcExprKind::Local, name);
}

void Evaluator::assign_local(std::uint32_t index, AbcExpr value) {
  if (value.kind == AbcExprKind::Activation || value.kind == AbcExprKind::CatchScope) {
    bound_[index] = std::move(value);
    return;
  }
  AbcExpr target = local_target(index);
  const std::string name = target.text;
  flush_stack(&name);
  emit_assign(std::move(target), std::move(value));
}

bool Evaluator::slot_name(std::uint32_t slot, std::string& name) {
  for (const AbcTrait& trait : body_.traits) {
    if ((trait.kind == AbcTraitKind::Slot || trait.kind == AbcTraitKind::Const) && trait.slot_id == slot) {
      name = file_.multiname_at(trait.name);
      return true;
    }
  }
  return false;
}

bool Evaluator::step(const AbcInstruction& in, const AbcInstruction* next, bool& consumed) {
  offset_ = static_cast<std::uint32_t>(in.offset);
  const std::string_view m = in.mnemonic == nullptr ? std::string_view{} : std::string_view{in.mnemonic};
  if (step_stack(in, m, next, consumed) || step_local(in, m) || step_scope(in, m) ||
      step_property(in, m) || step_call(in, m) || step_value(in, m)) {
    return !bad();
  }
  fail(AbcDecompileStatus::UnsupportedOpcode, "opcode " + std::string(m) + " is not recovered");
  return false;
}

bool Evaluator::step_stack(const AbcInstruction& in, std::string_view m, const AbcInstruction* next,
                           bool& consumed) {
  if (m == "nop" || m == "label" || m == "bkpt" || m == "debug" || m == "debugline" ||
      m == "debugfile" || m == "bkptline" || m == "timestamp" || m == "kill" || m == "jump") {
    return true;
  }
  if (m == "pushnull") {
    push(make_expr(AbcExprKind::Literal, "null"));
  } else if (m == "pushundefined") {
    push(make_expr(AbcExprKind::Literal, "undefined"));
  } else if (m == "pushtrue") {
    push(make_expr(AbcExprKind::Literal, "true"));
  } else if (m == "pushfalse") {
    push(make_expr(AbcExprKind::Literal, "false"));
  } else if (m == "pushnan") {
    push(make_expr(AbcExprKind::Literal, "NaN"));
  } else if (m == "pushbyte" || m == "pushshort") {
    const std::int32_t value = in.operands.empty() ? 0 : in.operands[0].immediate;
    push(make_expr(AbcExprKind::Literal, std::to_string(value)));
  } else if (m == "pushstring") {
    push(make_expr(AbcExprKind::Literal, quote_string(file_.string_at(operand(in, 0)))));
  } else if (m == "pushint") {
    const std::uint32_t index = operand(in, 0);
    const bool known = index < file_.pool.ints.size();
    push(make_expr(AbcExprKind::Literal, std::to_string(known ? file_.pool.ints[index] : 0)));
  } else if (m == "pushuint") {
    const std::uint32_t index = operand(in, 0);
    const bool known = index < file_.pool.uints.size();
    push(make_expr(AbcExprKind::Literal, std::to_string(known ? file_.pool.uints[index] : 0u)));
  } else if (m == "pushdouble") {
    const std::uint32_t index = operand(in, 0);
    const bool known = index < file_.pool.doubles.size();
    push(make_expr(AbcExprKind::Literal, format_double(known ? file_.pool.doubles[index] : 0.0)));
  } else if (m == "pop") {
    AbcExpr value = pop();
    flush_stack();
    emit_expr(std::move(value));
  } else if (m == "swap") {
    AbcExpr top = pop();
    AbcExpr below = pop();
    if (!is_simple_expr(below)) {
      spill(below);
    }
    if (!is_simple_expr(top)) {
      spill(top);
    }
    push(std::move(top));
    push(std::move(below));
  } else if (m == "dup") {
    AbcExpr top = pop();
    if (is_simple_expr(top)) {
      push(top);
      push(std::move(top));
    } else if (next != nullptr && is_set_local(next->opcode) &&
               bound_.find(local_index_of(*next)) == bound_.end()) {
      std::vector<AbcExpr> args;
      args.push_back(local_target(local_index_of(*next)));
      args.push_back(std::move(top));
      push(make_expr(AbcExprKind::Assign, "", std::move(args)));
      consumed = true;
    } else {
      spill(top);
      push(top);
      push(std::move(top));
    }
  } else {
    return false;
  }
  return true;
}

bool Evaluator::step_local(const AbcInstruction& in, std::string_view m) {
  if (m == "getlocal" || m == "getlocal0" || m == "getlocal1" || m == "getlocal2" || m == "getlocal3") {
    push(local_expr(local_index_of(in)));
    return true;
  }
  if (m == "setlocal" || m == "setlocal0" || m == "setlocal1" || m == "setlocal2" || m == "setlocal3") {
    AbcExpr value = pop();
    assign_local(local_index_of(in), std::move(value));
    return true;
  }
  if (m == "inclocal" || m == "declocal") {
    AbcExpr target = local_target(local_index_of(in));
    const std::string name = target.text;
    flush_stack(&name);
    std::vector<AbcExpr> args;
    args.push_back(std::move(target));
    emit_expr(make_expr(AbcExprKind::Postfix, m == "inclocal" ? "++" : "--", std::move(args)));
    return true;
  }
  return false;
}

bool Evaluator::step_scope(const AbcInstruction& in, std::string_view m) {
  if (m == "pushscope") {
    scope_.push_back(ScopeEntry{pop(), -1});
    return true;
  }
  if (m == "pushwith") {
    AbcExpr object = pop();
    flush_stack();
    ScopeEntry entry;
    entry.expr = object;
    entry.with_record = static_cast<int>(withs_.size());
    WithRecord record;
    record.list = out_;
    record.index = out_->size();
    withs_.push_back(record);
    AbcStmt stmt;
    stmt.kind = AbcStmtKind::With;
    stmt.has_expr = true;
    stmt.expr = std::move(object);
    emit(std::move(stmt));
    scope_.push_back(std::move(entry));
    return true;
  }
  if (m == "popscope") {
    if (scope_.empty()) {
      fail(AbcDecompileStatus::StackShape, "popscope with no scope object");
      return true;
    }
    const int record_index = scope_.back().with_record;
    scope_.pop_back();
    if (record_index >= 0) {
      WithRecord& record = withs_[static_cast<std::size_t>(record_index)];
      if (record.list != out_) {
        fail(AbcDecompileStatus::UnsupportedExpression,
             "a with scope is closed in a different statement list than it was opened");
        return true;
      }
      AbcStmt& marker = (*out_)[record.index];
      auto first = out_->begin() + static_cast<std::ptrdiff_t>(record.index) + 1;
      marker.body.assign(std::make_move_iterator(first), std::make_move_iterator(out_->end()));
      out_->erase(first, out_->end());
      record.list = nullptr;
    }
    return true;
  }
  if (m == "getglobalscope") {
    push(make_expr(AbcExprKind::GlobalScope));
    return true;
  }
  if (m == "newactivation") {
    push(make_expr(AbcExprKind::Activation));
    return true;
  }
  if (m == "getscopeobject") {
    const std::uint32_t index = operand(in, 0);
    if (index >= scope_.size()) {
      fail(AbcDecompileStatus::StackShape, "getscopeobject reads a scope entry that does not exist");
      return true;
    }
    push(scope_[index].expr);
    return true;
  }
  return false;
}

}  // namespace fo4::abc::detail
