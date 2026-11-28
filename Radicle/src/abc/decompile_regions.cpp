#include "decompile_eval.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

namespace {

bool contains(const std::vector<std::uint32_t>& list, std::uint32_t value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

bool is_single(const std::vector<AbcNode>& arm, AbcNodeKind kind) {
  return arm.size() == 1 && arm[0].kind == kind;
}

}  // namespace

bool contains_continue(const std::vector<AbcNode>& nodes) {
  for (const AbcNode& node : nodes) {
    switch (node.kind) {
      case AbcNodeKind::Continue:
        return true;
      case AbcNodeKind::If:
        if (contains_continue(node.body) || contains_continue(node.other)) {
          return true;
        }
        break;
      case AbcNodeKind::Switch:
        for (const AbcSwitchCase& one : node.cases) {
          if (contains_continue(one.body)) {
            return true;
          }
        }
        break;
      case AbcNodeKind::Try:
        if (contains_continue(node.body)) {
          return true;
        }
        for (const AbcCatch& one : node.catches) {
          if (contains_continue(one.body)) {
            return true;
          }
        }
        break;
      default:
        break;
    }
  }
  return false;
}

void trim_tail_continue(std::vector<AbcNode>& nodes) {
  if (nodes.empty()) {
    return;
  }
  AbcNode& last = nodes.back();
  if (last.kind == AbcNodeKind::Continue) {
    nodes.pop_back();
  } else if (last.kind == AbcNodeKind::If) {
    trim_tail_continue(last.body);
    trim_tail_continue(last.other);
  }
}

void Structurer::shape_loop(AbcNode& loop, std::uint32_t header) {
  std::vector<AbcNode>& body = loop.body;
  if (!body.empty() && body.front().kind == AbcNodeKind::If && body.front().block == header &&
      body.front().body.empty() && is_single(body.front().other, AbcNodeKind::Break)) {
    loop.loop_form = AbcLoopForm::While;
    loop.cond = std::move(body.front().cond);
    loop.block = header;
    loop.skip = body.front().skip;
    body.erase(body.begin());
    trim_tail_continue(body);
    return;
  }
  if (!body.empty() && body.back().kind == AbcNodeKind::If &&
      is_single(body.back().body, AbcNodeKind::Continue) &&
      is_single(body.back().other, AbcNodeKind::Break)) {
    AbcNode tail = std::move(body.back());
    body.pop_back();
    if (!contains_continue(body)) {
      loop.loop_form = AbcLoopForm::DoWhile;
      loop.cond = std::move(tail.cond);
      loop.block = tail.block;
      loop.skip = tail.skip;
      return;
    }
    body.push_back(std::move(tail));
  }
  loop.loop_form = AbcLoopForm::Infinite;
  trim_tail_continue(body);
}

bool Structurer::structure_loop(std::uint32_t h, std::vector<AbcNode>& out, std::uint32_t& next) {
  loop_started_[h] = true;
  std::vector<std::uint32_t> exits;
  for (const std::uint32_t member : f_.region[h]) {
    for (const std::uint32_t target : f_.succ[member]) {
      if (f_.in_region(h, target)) {
        continue;
      }
      const std::uint32_t resolved = resolve(target, false);
      if (!contains(exits, resolved)) {
        exits.push_back(resolved);
      }
    }
  }
  if (exits.size() > 1) {
    return fail(AbcDecompileStatus::UnstructuredFlow, h,
                "the loop at " + hex_offset(blk(h).start_offset) +
                    " has several exits, which would need a labeled break");
  }
  const std::uint32_t exit_block = exits.empty() ? kNone : exits[0];
  AbcNode node;
  node.kind = AbcNodeKind::Loop;
  node.block = h;
  Frame frame;
  frame.is_loop = true;
  frame.header = h;
  frame.exit = exit_block;
  frames_.push_back(frame);
  const bool ok = region(h, kNone, node.body);
  frames_.pop_back();
  if (!ok) {
    return false;
  }
  shape_loop(node, h);
  out.push_back(std::move(node));
  next = exit_block;
  return true;
}

bool Structurer::structure_switch(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& next) {
  const AbcDecodedOperand* operand = nullptr;
  for (const AbcDecodedOperand& one : last_of(b).operands) {
    if (one.role == AbcOperandRole::Switch) {
      operand = &one;
    }
  }
  if (operand == nullptr) {
    return fail(AbcDecompileStatus::UnsupportedSwitch, b, "a switch block has no switch operand");
  }
  const std::uint32_t default_raw = s_.graph.block_index_at(static_cast<std::uint32_t>(operand->target));
  std::vector<std::uint32_t> case_raw;
  for (const std::int32_t target : operand->case_targets) {
    case_raw.push_back(s_.graph.block_index_at(static_cast<std::uint32_t>(target)));
  }
  if (default_raw == kNone || contains(case_raw, kNone)) {
    return fail(AbcDecompileStatus::UnsupportedSwitch, b, "a switch target is not a block start");
  }
  AbcNode node;
  node.kind = AbcNodeKind::Switch;
  node.block = b;
  node.skip = take_skip(b);
  mark(b);
  const std::uint32_t join = valid_join(b);
  const std::uint32_t default_target = resolve(default_raw, true);
  std::vector<std::uint32_t> case_target;
  for (const std::uint32_t raw : case_raw) {
    case_target.push_back(resolve(raw, true));
  }
  std::vector<std::uint32_t> distinct;
  const auto note_target = [&](std::uint32_t target) {
    if (target != join && !contains(distinct, target)) {
      distinct.push_back(target);
    }
  };
  note_target(default_target);
  for (const std::uint32_t target : case_target) {
    note_target(target);
  }
  std::sort(distinct.begin(), distinct.end());
  Frame frame;
  frame.exit = join;
  frames_.push_back(frame);
  for (std::size_t i = 0; i < distinct.size(); ++i) {
    AbcSwitchCase one;
    for (std::size_t k = 0; k < case_target.size(); ++k) {
      if (case_target[k] == distinct[i]) {
        one.values.push_back(static_cast<std::uint32_t>(k));
      }
    }
    one.is_default = default_target == distinct[i];
    const std::uint32_t case_stop = i + 1 < distinct.size() ? distinct[i + 1] : join;
    if (!region(distinct[i], case_stop, one.body)) {
      frames_.pop_back();
      return false;
    }
    node.cases.push_back(std::move(one));
  }
  frames_.pop_back();
  if (join != kNone) {
    AbcSwitchCase tail;
    for (std::size_t k = 0; k < case_target.size(); ++k) {
      if (case_target[k] == join) {
        tail.values.push_back(static_cast<std::uint32_t>(k));
      }
    }
    if (!tail.values.empty()) {
      node.cases.push_back(std::move(tail));
    }
  }
  out.push_back(std::move(node));
  next = join;
  return true;
}

std::uint32_t catch_prologue_length(const AbcStructure& structure, std::uint32_t block,
                                    std::uint32_t exception) {
  const AbcBasicBlock& info = structure.graph.blocks[block];
  const std::uint32_t count = info.instruction_count;
  const auto at = [&](std::uint32_t i) -> const AbcInstruction& {
    return structure.instructions[info.first_instruction + i];
  };
  std::uint32_t i = 0;
  std::uint32_t pairs = 0;
  while (i + 1 < count && is_get_local(at(i).opcode) && at(i + 1).opcode == 0x30) {
    i += 2;
    ++pairs;
  }
  if (pairs == 0 || i + 7 > count) {
    return 0;
  }
  std::uint32_t index = 0;
  if (at(i).opcode != 0x5A || !u30_operand(at(i), 0, AbcOperandRole::Count, index) || index != exception) {
    return 0;
  }
  const bool shape = at(i + 1).opcode == 0x2A && is_set_local(at(i + 2).opcode) &&
                     at(i + 3).opcode == 0x2A && at(i + 4).opcode == 0x30 &&
                     at(i + 5).opcode == 0x2B && at(i + 6).opcode == 0x6D;
  return shape ? i + 7 : 0;
}

bool Structurer::catch_prologue(std::uint32_t b, std::uint32_t exception) const {
  return catch_prologue_length(s_, b, exception) != 0;
}

bool Structurer::structure_try(TryGroup& group, std::vector<AbcNode>& out, std::uint32_t& next) {
  group.started = true;
  std::vector<std::uint32_t> handler_blocks;
  for (const std::uint32_t index : group.exceptions) {
    const AbcExceptionRegion& handler = s_.exceptions[index];
    if (handler.variable_name == 0) {
      return fail(AbcDecompileStatus::UnsupportedTry, group.from_block,
                  "try at " + hex_offset(group.from) +
                      " has a finally or catch-all handler, whose layout is not recognised");
    }
    const std::uint32_t handler_block = s_.graph.block_index_at(handler.target);
    if (handler_block == kNone || !f_.live[handler_block] || emitted_[handler_block] ||
        contains(handler_blocks, handler_block)) {
      return fail(AbcDecompileStatus::UnsupportedTry, group.from_block,
                  "a handler of the try at " + hex_offset(group.from) + " is not a fresh block");
    }
    if (!catch_prologue(handler_block, index)) {
      return fail(AbcDecompileStatus::UnsupportedTry, handler_block,
                  "the handler at " + hex_offset(handler.target) + " does not start with the catch prologue");
    }
    handler_blocks.push_back(handler_block);
  }
  const std::uint32_t join = valid_join(group.from_block);
  const std::uint32_t arm_stop = join != kNone ? join : stops_.back();
  AbcNode node;
  node.kind = AbcNodeKind::Try;
  node.block = group.from_block;
  const std::size_t log_start = emit_log_.size();
  if (!region(group.from_block, arm_stop, node.body)) {
    return false;
  }
  for (std::size_t i = log_start; i < emit_log_.size(); ++i) {
    const std::uint32_t offset = blk(emit_log_[i]).start_offset;
    if (offset < group.from || offset >= group.to) {
      return fail(AbcDecompileStatus::UnsupportedTry, emit_log_[i],
                  "the try body at " + hex_offset(group.from) + " runs outside its protected range");
    }
  }
  for (std::uint32_t b = 0; b < f_.count; ++b) {
    const std::uint32_t offset = blk(b).start_offset;
    if (f_.live[b] && !emitted_[b] && offset >= group.from && offset < group.to) {
      return fail(AbcDecompileStatus::UnsupportedTry, b,
                  "the protected range at " + hex_offset(group.from) + " holds a block the body never reaches");
    }
  }
  for (std::size_t i = 0; i < group.exceptions.size(); ++i) {
    AbcCatch one;
    one.exception = group.exceptions[i];
    one.handler_block = handler_blocks[i];
    if (!region(handler_blocks[i], arm_stop, one.body)) {
      return false;
    }
    node.catches.push_back(std::move(one));
  }
  out.push_back(std::move(node));
  next = join;
  return true;
}

}  // namespace fo4::abc::detail
