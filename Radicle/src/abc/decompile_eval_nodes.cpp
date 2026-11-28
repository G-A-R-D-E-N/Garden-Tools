#include "decompile_eval.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

bool Evaluator::run() {
  fn_.body = s_.body;
  fn_.method = s_.method;
  out_ = &fn_.statements;
  name_params();
  if (!eval_nodes(s_.root, fn_.statements)) {
    return false;
  }
  if (!fn_.statements.empty() && fn_.statements.back().kind == AbcStmtKind::Return &&
      !fn_.statements.back().has_expr) {
    fn_.statements.pop_back();
  }
  return !bad();
}

bool Evaluator::eval_instructions(std::uint32_t first, std::uint32_t last) {
  for (std::uint32_t i = first; i < last && !bad(); ++i) {
    bool consumed = false;
    const AbcInstruction* next = i + 1 < last ? &s_.instructions[i + 1] : nullptr;
    if (!step(s_.instructions[i], next, consumed)) {
      return false;
    }
    if (consumed) {
      ++i;
    }
  }
  return !bad();
}

std::uint32_t Evaluator::entry_skip(std::uint32_t b) {
  if (b != catch_block_) {
    return 0;
  }
  catch_block_ = kNone;
  return catch_skip_;
}

bool Evaluator::eval_block(std::uint32_t b, std::uint32_t from, std::uint32_t trim) {
  const AbcBasicBlock& block = blk(b);
  from += entry_skip(b);
  const std::uint32_t last = block.first_instruction + block.instruction_count - trim;
  return eval_instructions(block.first_instruction + from, last);
}

bool Evaluator::close_withs(std::vector<AbcStmt>& list) {
  for (std::size_t i = withs_.size(); i-- > 0;) {
    WithRecord& record = withs_[i];
    if (record.list != &list) {
      continue;
    }
    if (&list != &fn_.statements) {
      fail(AbcDecompileStatus::UnsupportedExpression,
           "a with scope is still open at the end of a statement list");
      return false;
    }
    AbcStmt& marker = list[record.index];
    auto first = list.begin() + static_cast<std::ptrdiff_t>(record.index) + 1;
    marker.body.assign(std::make_move_iterator(first), std::make_move_iterator(list.end()));
    list.erase(first, list.end());
    record.list = nullptr;
  }
  return !bad();
}

bool Evaluator::eval_nodes(const std::vector<AbcNode>& nodes, std::vector<AbcStmt>& out) {
  std::vector<AbcStmt>* saved = out_;
  out_ = &out;
  bool ok = true;
  for (const AbcNode& node : nodes) {
    if (!eval_node(node)) {
      ok = false;
      break;
    }
  }
  if (ok) {
    ok = close_withs(out);
  }
  out_ = saved;
  return ok && !bad();
}

bool Evaluator::eval_arm(const std::vector<AbcNode>& nodes, std::vector<AbcStmt>& out,
                         const std::vector<AbcExpr>& stack, const std::vector<ScopeEntry>& scope) {
  stack_ = stack;
  scope_ = scope;
  return eval_nodes(nodes, out);
}

bool Evaluator::eval_node(const AbcNode& node) {
  switch (node.kind) {
    case AbcNodeKind::Block:
      return eval_block(node.block, node.skip, 0);
    case AbcNodeKind::Break:
    case AbcNodeKind::Continue: {
      AbcStmt stmt;
      stmt.kind = node.kind == AbcNodeKind::Break ? AbcStmtKind::Break : AbcStmtKind::Continue;
      emit(std::move(stmt));
      return !bad();
    }
    case AbcNodeKind::If:
      return eval_if(node);
    case AbcNodeKind::Loop:
      return eval_loop(node);
    case AbcNodeKind::Switch:
      return eval_switch(node);
    case AbcNodeKind::Try:
      return eval_try(node);
    case AbcNodeKind::ShortCircuit:
      return eval_short_circuit(node);
    case AbcNodeKind::Ternary:
      return eval_ternary(node);
  }
  return false;
}

AbcExpr Evaluator::branch_condition(const AbcInstruction& in) {
  offset_ = static_cast<std::uint32_t>(in.offset);
  const std::string_view m = in.mnemonic == nullptr ? std::string_view{} : std::string_view{in.mnemonic};
  if (m == "iftrue") {
    return pop();
  }
  if (m == "iffalse") {
    return negate_condition(pop());
  }
  struct Row {
    std::string_view mnemonic;
    const char* op;
    bool negated;
  };
  static const Row kRows[] = {
      {"ifeq", "==", false},  {"ifne", "!=", false},      {"iflt", "<", false},
      {"ifle", "<=", false},  {"ifgt", ">", false},       {"ifge", ">=", false},
      {"ifstricteq", "===", false}, {"ifstrictne", "!==", false}, {"ifnlt", "<", true},
      {"ifnle", "<=", true},  {"ifngt", ">", true},       {"ifnge", ">=", true},
  };
  for (const Row& row : kRows) {
    if (row.mnemonic != m) {
      continue;
    }
    AbcExpr right = pop();
    AbcExpr left = pop();
    std::vector<AbcExpr> args;
    args.push_back(std::move(left));
    args.push_back(std::move(right));
    AbcExpr compare = make_expr(AbcExprKind::Binary, row.op, std::move(args));
    if (!row.negated) {
      return compare;
    }
    std::vector<AbcExpr> wrapped;
    wrapped.push_back(std::move(compare));
    return make_expr(AbcExprKind::Unary, "!", std::move(wrapped));
  }
  fail(AbcDecompileStatus::UnsupportedOpcode, "branch " + std::string(m) + " is not a recognised condition");
  return make_expr(AbcExprKind::Literal, "false");
}

AbcExpr Evaluator::build_condition(const std::vector<AbcCondStep>& steps,
                                   const std::vector<AbcExpr>& conds, std::uint32_t index) {
  const AbcCondStep& step = steps[index];
  const AbcExpr& test = conds[index];
  const bool taken_step = step.taken_kind == AbcCondRef::Step;
  const bool fall_step = step.fall_kind == AbcCondRef::Step;
  if (!taken_step && !fall_step) {
    return step.taken_kind == AbcCondRef::True ? test : negate_condition(test);
  }
  std::vector<AbcExpr> args;
  if (fall_step) {
    const bool direct = step.taken_kind == AbcCondRef::True;
    args.push_back(direct ? test : negate_condition(test));
    args.push_back(build_condition(steps, conds, step.fall_step));
    return make_expr(AbcExprKind::Binary, direct ? "||" : "&&", std::move(args));
  }
  const bool direct = step.fall_kind == AbcCondRef::False;
  args.push_back(direct ? test : negate_condition(test));
  args.push_back(build_condition(steps, conds, step.taken_step));
  return make_expr(AbcExprKind::Binary, direct ? "&&" : "||", std::move(args));
}

bool Evaluator::eval_condition(const std::vector<AbcCondStep>& steps, std::uint32_t skip,
                               AbcExpr& out) {
  if (steps.empty()) {
    fail(AbcDecompileStatus::UnsupportedCondition, "a condition has no steps");
    return false;
  }
  std::vector<AbcExpr> conds(steps.size());
  const AbcBasicBlock& head = blk(steps[0].block);
  const std::uint32_t head_last = head.first_instruction + head.instruction_count - 1;
  const std::uint32_t head_first = head.first_instruction + skip + entry_skip(steps[0].block);
  if (!eval_instructions(head_first, head_last)) {
    return false;
  }
  conds[0] = branch_condition(s_.instructions[head_last]);
  const std::vector<AbcExpr> base = stack_;
  const std::size_t saved_floor = floor_;
  for (std::size_t i = 1; i < steps.size() && !bad(); ++i) {
    const AbcBasicBlock& member = blk(steps[i].block);
    const std::uint32_t member_last = member.first_instruction + member.instruction_count - 1;
    stack_ = base;
    floor_ = base.size();
    in_member_ = true;
    const bool ok = eval_instructions(member.first_instruction, member_last);
    in_member_ = false;
    if (ok) {
      conds[i] = branch_condition(s_.instructions[member_last]);
    }
    floor_ = saved_floor;
    if (!ok || bad()) {
      return false;
    }
    if (stack_.size() != base.size()) {
      fail(AbcDecompileStatus::UnsupportedCondition, "a condition block leaves a value on the stack");
      return false;
    }
  }
  stack_ = base;
  if (bad()) {
    return false;
  }
  out = build_condition(steps, conds, 0);
  return true;
}

bool Evaluator::eval_condition_into(const std::vector<AbcCondStep>& steps, std::uint32_t skip,
                                    AbcExpr& cond, std::vector<AbcStmt>& list) {
  std::vector<AbcStmt>* saved = out_;
  out_ = &list;
  const bool ok = eval_condition(steps, skip, cond);
  out_ = saved;
  return ok;
}

bool Evaluator::eval_if(const AbcNode& node) {
  flush_stack();
  AbcExpr cond;
  if (!eval_condition(node.cond, node.skip, cond)) {
    return false;
  }
  const std::vector<AbcExpr> base = stack_;
  const std::vector<ScopeEntry> scope = scope_;
  AbcStmt stmt;
  stmt.kind = AbcStmtKind::If;
  stmt.has_expr = true;
  if (!eval_arm(node.body, stmt.body, base, scope) || !eval_arm(node.other, stmt.other, base, scope)) {
    return false;
  }
  stack_ = base;
  scope_ = scope;
  if (stmt.body.empty() && !stmt.other.empty()) {
    cond = negate_condition(std::move(cond));
    std::swap(stmt.body, stmt.other);
  }
  stmt.expr = std::move(cond);
  emit(std::move(stmt));
  return !bad();
}

bool Evaluator::eval_loop(const AbcNode& node) {
  flush_stack();
  const std::vector<AbcExpr> base = stack_;
  const std::vector<ScopeEntry> scope = scope_;
  AbcStmt stmt;
  if (node.loop_form == AbcLoopForm::Infinite) {
    stmt.kind = AbcStmtKind::Loop;
    if (!eval_arm(node.body, stmt.body, base, scope)) {
      return false;
    }
  } else if (node.loop_form == AbcLoopForm::While) {
    std::vector<AbcStmt> prefix;
    AbcExpr cond;
    if (!eval_condition_into(node.cond, node.skip, cond, prefix)) {
      return false;
    }
    const std::vector<AbcExpr> after = stack_;
    stmt.kind = AbcStmtKind::While;
    if (!eval_arm(node.body, stmt.body, after, scope)) {
      return false;
    }
    if (prefix.empty()) {
      stmt.has_expr = true;
      stmt.expr = std::move(cond);
    } else {
      stmt.kind = AbcStmtKind::Loop;
      AbcStmt guard;
      guard.kind = AbcStmtKind::If;
      guard.has_expr = true;
      guard.expr = negate_condition(std::move(cond));
      AbcStmt leave;
      leave.kind = AbcStmtKind::Break;
      guard.body.push_back(std::move(leave));
      prefix.push_back(std::move(guard));
      for (AbcStmt& inner : stmt.body) {
        prefix.push_back(std::move(inner));
      }
      stmt.body = std::move(prefix);
    }
  } else {
    stmt.kind = AbcStmtKind::DoWhile;
    if (!eval_arm(node.body, stmt.body, base, scope)) {
      return false;
    }
    AbcExpr cond;
    if (!eval_condition_into(node.cond, node.skip, cond, stmt.body)) {
      return false;
    }
    stmt.has_expr = true;
    stmt.expr = std::move(cond);
  }
  stack_ = base;
  scope_ = scope;
  emit(std::move(stmt));
  return !bad();
}

bool Evaluator::eval_switch(const AbcNode& node) {
  flush_stack();
  if (!eval_block(node.block, node.skip, 1)) {
    return false;
  }
  AbcStmt stmt;
  stmt.kind = AbcStmtKind::Switch;
  stmt.has_expr = true;
  stmt.expr = pop();
  const std::vector<AbcExpr> base = stack_;
  const std::vector<ScopeEntry> scope = scope_;
  for (const AbcSwitchCase& one : node.cases) {
    AbcStmtCase arm;
    arm.values = one.values;
    arm.is_default = one.is_default;
    if (!eval_arm(one.body, arm.body, base, scope)) {
      return false;
    }
    stmt.cases.push_back(std::move(arm));
  }
  stack_ = base;
  scope_ = scope;
  emit(std::move(stmt));
  return !bad();
}

bool Evaluator::eval_try(const AbcNode& node) {
  flush_stack();
  const std::vector<AbcExpr> base = stack_;
  const std::vector<ScopeEntry> scope = scope_;
  AbcStmt stmt;
  stmt.kind = AbcStmtKind::Try;
  if (!eval_arm(node.body, stmt.body, base, scope)) {
    return false;
  }
  for (const AbcCatch& one : node.catches) {
    const AbcExceptionRegion& region = s_.exceptions[one.exception];
    const std::uint32_t length = catch_prologue_length(s_, one.handler_block, one.exception);
    if (length == 0) {
      fail(AbcDecompileStatus::UnsupportedTry, "a handler does not open with the catch prologue");
      return false;
    }
    const AbcBasicBlock& handler = blk(one.handler_block);
    stack_.clear();
    scope_.clear();
    for (std::uint32_t pair = 0; pair < (length - 7) / 2; ++pair) {
      if (pair < scope.size()) {
        scope_.push_back(scope[pair]);
        continue;
      }
      const AbcInstruction& read = s_.instructions[handler.first_instruction + 2 * pair];
      scope_.push_back(ScopeEntry{local_expr(local_index_of(read)), -1});
    }
    const std::string variable = file_.multiname_at(region.variable_name);
    const AbcExpr catch_scope = make_expr(AbcExprKind::CatchScope, variable);
    scope_.push_back(ScopeEntry{catch_scope, -1});
    const std::uint32_t local = local_index_of(s_.instructions[handler.first_instruction + length - 5]);
    bound_[local] = catch_scope;
    catch_block_ = one.handler_block;
    catch_skip_ = length;
    AbcStmtCatch arm;
    arm.variable = variable;
    arm.type = type_text(region.catch_type);
    const bool ok = eval_nodes(one.body, arm.body);
    bound_.erase(local);
    if (!ok) {
      return false;
    }
    if (catch_block_ != kNone) {
      fail(AbcDecompileStatus::UnsupportedTry, "the catch prologue was not consumed");
      return false;
    }
    stmt.catches.push_back(std::move(arm));
  }
  stack_ = base;
  scope_ = scope;
  emit(std::move(stmt));
  return !bad();
}

bool Evaluator::eval_short_circuit(const AbcNode& node) {
  if (!eval_block(node.block, node.skip, 2)) {
    return false;
  }
  AbcExpr left = pop();
  const std::size_t depth = stack_.size();
  std::vector<AbcStmt> scratch;
  ++value_depth_;
  const bool ok = eval_nodes(node.body, scratch);
  --value_depth_;
  if (!ok) {
    return false;
  }
  if (stack_.size() != depth + 1) {
    fail(AbcDecompileStatus::UnsupportedValueRegion, "a short-circuit arm does not yield exactly one value");
    return false;
  }
  AbcExpr right = pop();
  std::vector<AbcExpr> args;
  args.push_back(std::move(left));
  args.push_back(std::move(right));
  push(make_expr(AbcExprKind::Binary, node.is_and ? "&&" : "||", std::move(args)));
  return !bad();
}

bool Evaluator::eval_ternary(const AbcNode& node) {
  AbcExpr cond;
  if (!eval_condition(node.cond, node.skip, cond)) {
    return false;
  }
  const std::vector<AbcExpr> base = stack_;
  const std::vector<AbcNode>* arms[2] = {&node.body, &node.other};
  AbcExpr values[2];
  for (int i = 0; i < 2; ++i) {
    stack_ = base;
    std::vector<AbcStmt> scratch;
    ++value_depth_;
    const bool ok = eval_nodes(*arms[i], scratch);
    --value_depth_;
    if (!ok) {
      return false;
    }
    if (stack_.size() != base.size() + 1) {
      fail(AbcDecompileStatus::UnsupportedValueRegion, "a conditional-expression arm does not yield exactly one value");
      return false;
    }
    values[i] = pop();
  }
  stack_ = base;
  std::vector<AbcExpr> args;
  args.push_back(std::move(cond));
  args.push_back(std::move(values[0]));
  args.push_back(std::move(values[1]));
  push(make_expr(AbcExprKind::Conditional, "", std::move(args)));
  return !bad();
}

}  // namespace fo4::abc::detail
