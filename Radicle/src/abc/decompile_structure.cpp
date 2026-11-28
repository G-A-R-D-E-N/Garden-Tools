#include "decompile_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

namespace {

AbcNode simple_node(AbcNodeKind kind) {
  AbcNode node;
  node.kind = kind;
  return node;
}

AbcDecompileStatus refuse(AbcDecompileIssue& issue, AbcDecompileStatus status, std::uint32_t offset,
                          std::string message) {
  issue.status = status;
  issue.offset = offset;
  issue.message = std::move(message);
  return status;
}

bool contains(const std::vector<std::uint32_t>& list, std::uint32_t value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

Structurer::Structurer(AbcStructure& structure, const FlowFacts& facts, std::vector<TryGroup> tries,
                       std::vector<bool> handler_blocks, AbcDecompileIssue& issue)
    : s_(structure),
      f_(facts),
      tries_(std::move(tries)),
      handler_(std::move(handler_blocks)),
      issue_(issue) {}

bool Structurer::fail(AbcDecompileStatus status, std::uint32_t block, const std::string& message) {
  if (issue_.status == AbcDecompileStatus::Ok) {
    issue_.status = status;
    issue_.offset = block < s_.graph.blocks.size() ? s_.graph.blocks[block].start_offset : kNone;
    issue_.message = message;
  }
  return false;
}

const AbcInstruction& Structurer::instr_of(std::uint32_t b, std::uint32_t i) const {
  return s_.instructions[blk(b).first_instruction + i];
}

const AbcInstruction& Structurer::last_of(std::uint32_t b) const {
  return instr_of(b, blk(b).instruction_count - 1);
}

std::uint32_t Structurer::taken_of(std::uint32_t b) const {
  for (const AbcControlFlowEdge& edge : blk(b).successors) {
    if (edge.kind == AbcEdgeKind::Branch) {
      return edge.block;
    }
  }
  return kNone;
}

std::uint32_t Structurer::fall_of(std::uint32_t b) const {
  for (const AbcControlFlowEdge& edge : blk(b).successors) {
    if (edge.kind == AbcEdgeKind::Fallthrough) {
      return edge.block;
    }
  }
  return kNone;
}

std::uint32_t Structurer::single_succ(std::uint32_t b) const {
  return f_.succ[b].size() == 1 ? f_.succ[b][0] : kNone;
}

std::uint32_t Structurer::take_skip(std::uint32_t b) {
  const std::uint32_t value = skip_[b];
  skip_[b] = 0;
  return value;
}

void Structurer::mark(std::uint32_t b) {
  emitted_[b] = true;
  emit_log_.push_back(b);
}

bool Structurer::try_starts_at(std::uint32_t b) const {
  for (const TryGroup& group : tries_) {
    if (group.from_block == b) {
      return true;
    }
  }
  return false;
}

TryGroup* Structurer::pending_try(std::uint32_t b) {
  for (TryGroup& group : tries_) {
    if (!group.started && group.from_block == b) {
      return &group;
    }
  }
  return nullptr;
}

Frame* Structurer::innermost_loop() {
  for (std::size_t i = frames_.size(); i-- > 0;) {
    if (frames_[i].is_loop) {
      return &frames_[i];
    }
  }
  return nullptr;
}

Jump Structurer::classify(std::uint32_t b) {
  if (b == kNone) {
    return Jump::None;
  }
  const Frame* loop = innermost_loop();
  if (loop != nullptr && loop->entered && loop->header == b) {
    return Jump::Continue;
  }
  if (!frames_.empty() && frames_.back().exit == b) {
    return Jump::Break;
  }
  for (const Frame& frame : frames_) {
    if (frame.exit == b || (frame.is_loop && frame.entered && frame.header == b)) {
      return Jump::Labeled;
    }
  }
  return Jump::None;
}

bool Structurer::in_stops(std::uint32_t b) const {
  for (const std::uint32_t stop : stops_) {
    if (stop != kNone && stop == b) {
      return true;
    }
  }
  return false;
}

bool Structurer::is_trivial(std::uint32_t b) const {
  if (f_.is_header[b] || try_starts_at(b) || handler_[b] || skip_[b] != 0) {
    return false;
  }
  if (last_of(b).opcode != 0x10 || f_.succ[b].size() != 1) {
    return false;
  }
  for (std::uint32_t i = 0; i + 1 < blk(b).instruction_count; ++i) {
    if (!is_noise_opcode(instr_of(b, i).opcode)) {
      return false;
    }
  }
  return true;
}

std::uint32_t Structurer::resolve(std::uint32_t b, bool mark_skipped) {
  for (std::uint32_t steps = 0; steps < f_.count && b != kNone && f_.live[b] && is_trivial(b);
       ++steps) {
    const std::uint32_t next = single_succ(b);
    if (mark_skipped) {
      emitted_[b] = true;
    }
    b = next;
  }
  return b;
}

std::uint32_t Structurer::valid_join(std::uint32_t b) {
  const std::uint32_t join = f_.ipdom[b];
  if (join == kNone || join >= f_.count || !f_.live[join] || emitted_[join]) {
    return kNone;
  }
  if (classify(join) != Jump::None) {
    return kNone;
  }
  const Frame* loop = innermost_loop();
  if (loop != nullptr && !f_.in_region(loop->header, join)) {
    return kNone;
  }
  if (in_stops(join) && join != stops_.back()) {
    return kNone;
  }
  return join;
}

bool Structurer::run() {
  const std::uint32_t count = f_.count;
  emitted_.assign(count, false);
  loop_started_.assign(count, false);
  skip_.assign(count, 0);
  if (!region(0, kNone, s_.root)) {
    return false;
  }
  for (std::uint32_t b = 0; b < count; ++b) {
    if (f_.live[b] && !emitted_[b]) {
      return fail(AbcDecompileStatus::UnstructuredFlow, b,
                  "block at " + hex_offset(blk(b).start_offset) + " is live but no structure covers it");
    }
  }
  return true;
}

bool Structurer::region(std::uint32_t b, std::uint32_t stop, std::vector<AbcNode>& out) {
  stops_.push_back(stop);
  const bool ok = region_impl(b, stop, out);
  stops_.pop_back();
  return ok;
}

bool Structurer::region_impl(std::uint32_t b, std::uint32_t stop, std::vector<AbcNode>& out) {
  const std::uint32_t limit = 8 * f_.count + 64;
  for (std::uint32_t guard = 0; guard < limit; ++guard) {
    if (b == stop || b == kNone) {
      return true;
    }
    b = resolve(b, true);
    if (b == stop || b == kNone) {
      return true;
    }
    if (!f_.live[b]) {
      return fail(AbcDecompileStatus::UnstructuredFlow, b, "control reaches a block with no stack depth");
    }
    Frame* loop = innermost_loop();
    if (loop != nullptr && !loop->entered && loop->header == b) {
      loop->entered = true;
    } else {
      const Jump jump = classify(b);
      if (jump == Jump::Continue) {
        out.push_back(simple_node(AbcNodeKind::Continue));
        return true;
      }
      if (jump == Jump::Break) {
        out.push_back(simple_node(AbcNodeKind::Break));
        return true;
      }
      if (jump == Jump::Labeled) {
        return fail(AbcDecompileStatus::UnstructuredFlow, b,
                    "labeled break or continue to " + hex_offset(blk(b).start_offset));
      }
    }
    if (in_stops(b)) {
      return fail(AbcDecompileStatus::UnstructuredFlow, b,
                  "control leaves its region through " + hex_offset(blk(b).start_offset));
    }
    if (emitted_[b]) {
      return fail(AbcDecompileStatus::UnstructuredFlow, b,
                  "block at " + hex_offset(blk(b).start_offset) + " is reached by paths that do not join");
    }
    std::uint32_t next = kNone;
    if (TryGroup* group = pending_try(b)) {
      if (!structure_try(*group, out, next)) {
        return false;
      }
      b = next;
      continue;
    }
    if (f_.is_header[b] && !loop_started_[b]) {
      if (!structure_loop(b, out, next)) {
        return false;
      }
      b = next;
      continue;
    }
    const AbcInstruction& last = last_of(b);
    if (last.opcode == 0x1B) {
      if (!structure_switch(b, out, next)) {
        return false;
      }
      b = next;
      continue;
    }
    if (is_conditional_branch(last.opcode)) {
      if (value_kind(b) != 0) {
        if (!value_region(b, out, next)) {
          return false;
        }
      } else if (!statement_if(b, out, next)) {
        return false;
      }
      b = next;
      continue;
    }
    AbcNode node = simple_node(AbcNodeKind::Block);
    node.block = b;
    node.skip = take_skip(b);
    mark(b);
    out.push_back(std::move(node));
    b = single_succ(b);
  }
  return fail(AbcDecompileStatus::UnstructuredFlow, b, "structure recovery did not terminate");
}

bool Structurer::block_is_condition_only(std::uint32_t b) const {
  for (std::uint32_t i = 0; i + 1 < blk(b).instruction_count; ++i) {
    if (is_condition_hostile(instr_of(b, i).opcode)) {
      return false;
    }
  }
  return true;
}

bool Structurer::same_try_cover(std::uint32_t a, std::uint32_t b) const {
  for (const TryGroup& group : tries_) {
    const bool in_a = blk(a).start_offset >= group.from && blk(a).start_offset < group.to;
    const bool in_b = blk(b).start_offset >= group.from && blk(b).start_offset < group.to;
    if (in_a != in_b) {
      return false;
    }
  }
  return true;
}

std::vector<std::uint32_t> Structurer::external_exits(const Chain& chain) const {
  std::vector<std::uint32_t> exits;
  const std::uint32_t head = chain.members.front();
  for (const std::uint32_t member : chain.members) {
    for (const std::uint32_t target : f_.succ[member]) {
      if ((target == head || !contains(chain.members, target)) && !contains(exits, target)) {
        exits.push_back(target);
      }
    }
  }
  return exits;
}

bool Structurer::chain_candidate(const Chain& chain, std::uint32_t x) const {
  const std::uint32_t head = chain.members.front();
  if (!f_.live[x] || emitted_[x] || f_.is_header[x] || handler_[x] || skip_[x] != 0) {
    return false;
  }
  if (f_.pred[x].size() != 1 || try_starts_at(x) || f_.succ[x].size() != 2) {
    return false;
  }
  if (!is_conditional_branch(last_of(x).opcode) || !block_is_condition_only(x)) {
    return false;
  }
  const int kind = value_kind(x);
  if (kind == 1 || (kind == 2 && (value_kind(head) != 2 || f_.ipdom[x] != f_.ipdom[head]))) {
    return false;
  }
  if (!same_try_cover(head, x)) {
    return false;
  }
  for (const std::uint32_t target : f_.succ[x]) {
    if (contains(chain.members, target)) {
      return false;
    }
  }
  return true;
}

bool Structurer::build_chain(std::uint32_t head, Chain& chain) {
  chain.members.assign(1, head);
  chain.exits.clear();
  bool grew = true;
  while (grew) {
    grew = false;
    for (std::size_t i = 0; i < chain.members.size() && !grew; ++i) {
      for (const std::uint32_t x : f_.succ[chain.members[i]]) {
        if (contains(chain.members, x) || !chain_candidate(chain, x)) {
          continue;
        }
        chain.members.push_back(x);
        if (external_exits(chain).size() > 2) {
          chain.members.pop_back();
          continue;
        }
        grew = true;
        break;
      }
    }
  }
  chain.exits = external_exits(chain);
  if (chain.exits.size() != 2 || taken_of(head) == fall_of(head)) {
    return fail(AbcDecompileStatus::UnsupportedCondition, head,
                "the conditional at " + hex_offset(blk(head).start_offset) + " does not split into two exits");
  }
  return true;
}

bool Structurer::step_for(const Chain& chain, std::uint32_t member, std::uint32_t true_exit,
                          std::vector<AbcCondStep>& steps, std::uint32_t& index) {
  index = static_cast<std::uint32_t>(steps.size());
  steps.emplace_back();
  steps[index].block = member;
  const std::uint32_t taken = taken_of(member);
  const std::uint32_t fall = fall_of(member);
  if (taken == kNone || fall == kNone || taken == fall) {
    return fail(AbcDecompileStatus::UnsupportedCondition, member,
                "a condition block has no distinct taken and fall-through successor");
  }
  AbcCondRef kinds[2] = {AbcCondRef::False, AbcCondRef::False};
  std::uint32_t refs[2] = {0, 0};
  const std::uint32_t targets[2] = {taken, fall};
  for (int side = 0; side < 2; ++side) {
    if (targets[side] != chain.members.front() && contains(chain.members, targets[side])) {
      kinds[side] = AbcCondRef::Step;
      if (!step_for(chain, targets[side], true_exit, steps, refs[side])) {
        return false;
      }
    } else {
      kinds[side] = targets[side] == true_exit ? AbcCondRef::True : AbcCondRef::False;
    }
  }
  if (kinds[0] == AbcCondRef::Step && kinds[1] == AbcCondRef::Step) {
    return fail(AbcDecompileStatus::UnsupportedCondition, member,
                "both successors of a condition block continue the condition");
  }
  if (kinds[0] == kinds[1] && kinds[0] != AbcCondRef::Step) {
    return fail(AbcDecompileStatus::UnsupportedCondition, member,
                "both successors of a condition block reach the same exit");
  }
  steps[index].taken_kind = kinds[0];
  steps[index].taken_step = refs[0];
  steps[index].fall_kind = kinds[1];
  steps[index].fall_step = refs[1];
  return true;
}

bool Structurer::chain_steps(const Chain& chain, std::uint32_t true_exit,
                             std::vector<AbcCondStep>& steps) {
  steps.clear();
  std::uint32_t root = 0;
  return step_for(chain, chain.members.front(), true_exit, steps, root);
}

bool Structurer::statement_if(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& next) {
  Chain chain;
  if (!build_chain(b, chain)) {
    return false;
  }
  const std::uint32_t last_member = *std::max_element(chain.members.begin(), chain.members.end());
  const std::uint32_t fall_exit = fall_of(last_member);
  if (!contains(chain.exits, fall_exit)) {
    return fail(AbcDecompileStatus::UnsupportedCondition, b,
                "the last condition block does not fall through to an exit");
  }
  const std::uint32_t resolved[2] = {resolve(chain.exits[0], false), resolve(chain.exits[1], false)};
  if (resolved[0] == resolved[1]) {
    return fail(AbcDecompileStatus::UnstructuredFlow, b, "both branches of a conditional reach the same block");
  }
  const Jump jumps[2] = {classify(resolved[0]), classify(resolved[1])};
  if (jumps[0] == Jump::Labeled || jumps[1] == Jump::Labeled) {
    return fail(AbcDecompileStatus::UnstructuredFlow, b, "labeled break or continue out of a conditional");
  }
  std::uint32_t true_exit = fall_exit;
  if (jumps[0] != Jump::None && jumps[1] != Jump::None) {
    if (jumps[0] == Jump::Continue && jumps[1] != Jump::Continue) {
      true_exit = chain.exits[0];
    } else if (jumps[1] == Jump::Continue && jumps[0] != Jump::Continue) {
      true_exit = chain.exits[1];
    }
  } else if (jumps[0] != Jump::None) {
    true_exit = chain.exits[1];
  } else if (jumps[1] != Jump::None) {
    true_exit = chain.exits[0];
  }
  const std::uint32_t false_exit = chain.exits[0] == true_exit ? chain.exits[1] : chain.exits[0];
  const Jump true_jump = true_exit == chain.exits[0] ? jumps[0] : jumps[1];
  const Jump false_jump = true_exit == chain.exits[0] ? jumps[1] : jumps[0];

  AbcNode node = simple_node(AbcNodeKind::If);
  node.block = b;
  node.skip = take_skip(b);
  for (const std::uint32_t member : chain.members) {
    mark(member);
  }
  if (!chain_steps(chain, true_exit, node.cond)) {
    return false;
  }
  const auto special = [](Jump jump) {
    return simple_node(jump == Jump::Break ? AbcNodeKind::Break : AbcNodeKind::Continue);
  };
  if (true_jump != Jump::None && false_jump != Jump::None) {
    resolve(true_exit, true);
    resolve(false_exit, true);
    node.body.push_back(special(true_jump));
    node.other.push_back(special(false_jump));
    out.push_back(std::move(node));
    next = kNone;
    return true;
  }
  if (false_jump != Jump::None) {
    resolve(false_exit, true);
    node.other.push_back(special(false_jump));
    out.push_back(std::move(node));
    next = true_exit;
    return true;
  }
  const std::uint32_t join = valid_join(b);
  const std::uint32_t arm_stop = join != kNone ? join : stops_.back();
  if (!region(true_exit, arm_stop, node.body) || !region(false_exit, arm_stop, node.other)) {
    return false;
  }
  out.push_back(std::move(node));
  next = join;
  return true;
}

std::uint32_t Structurer::leading_pop(std::uint32_t b) const {
  std::uint32_t i = 0;
  while (i < blk(b).instruction_count && is_noise_opcode(instr_of(b, i).opcode)) {
    ++i;
  }
  if (i < blk(b).instruction_count && instr_of(b, i).opcode == 0x29) {
    return i + 1;
  }
  return 0;
}

int Structurer::value_kind(std::uint32_t b) const {
  const AbcBasicBlock& block = blk(b);
  const AbcInstruction& last = last_of(b);
  if (!is_conditional_branch(last.opcode) || !f_.live[b]) {
    return 0;
  }
  if ((last.opcode == 0x11 || last.opcode == 0x12) && block.instruction_count >= 2 &&
      instr_of(b, block.instruction_count - 2).opcode == 0x2A) {
    const std::uint32_t fall = fall_of(b);
    const std::uint32_t taken = taken_of(b);
    if (fall != kNone && taken != kNone && f_.ipdom[b] == taken && f_.live[fall] &&
        leading_pop(fall) != 0) {
      return 1;
    }
  }
  const std::uint32_t join = f_.ipdom[b];
  if (join == kNone || join >= f_.count || !f_.live[join]) {
    return 0;
  }
  if (f_.exit_depth[b] != kNone && f_.depth[join] != kNone && f_.depth[join] > f_.exit_depth[b]) {
    return 2;
  }
  return 0;
}

bool Structurer::value_region(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& join) {
  const int kind = value_kind(b);
  AbcNode node = simple_node(kind == 1 ? AbcNodeKind::ShortCircuit : AbcNodeKind::Ternary);
  node.block = b;
  node.skip = take_skip(b);
  if (kind == 1) {
    node.is_and = last_of(b).opcode == 0x12;
    const std::uint32_t fall = fall_of(b);
    join = taken_of(b);
    if (join == kNone || emitted_[join] || !f_.live[join]) {
      return fail(AbcDecompileStatus::UnsupportedValueRegion, b, "a short-circuit join is not available");
    }
    mark(b);
    skip_[fall] = leading_pop(fall);
    if (!value_arm(fall, join, node.body)) {
      return false;
    }
    out.push_back(std::move(node));
    return true;
  }
  join = f_.ipdom[b];
  if (join == kNone || join >= f_.count || emitted_[join] || !f_.live[join]) {
    return fail(AbcDecompileStatus::UnsupportedValueRegion, b, "a conditional-expression join is not available");
  }
  Chain chain;
  if (!build_chain(b, chain)) {
    return false;
  }
  for (const std::uint32_t member : chain.members) {
    mark(member);
  }
  const std::uint32_t last_member = *std::max_element(chain.members.begin(), chain.members.end());
  const std::uint32_t true_exit = fall_of(last_member);
  if (!contains(chain.exits, true_exit)) {
    return fail(AbcDecompileStatus::UnsupportedCondition, b,
                "the last condition block does not fall through to an exit");
  }
  const std::uint32_t false_exit = chain.exits[0] == true_exit ? chain.exits[1] : chain.exits[0];
  if (!chain_steps(chain, true_exit, node.cond) || !value_arm(true_exit, join, node.body) ||
      !value_arm(false_exit, join, node.other)) {
    return false;
  }
  out.push_back(std::move(node));
  return true;
}

bool Structurer::value_arm(std::uint32_t b, std::uint32_t join, std::vector<AbcNode>& out) {
  const std::uint32_t limit = 4 * f_.count + 16;
  for (std::uint32_t guard = 0; guard < limit; ++guard) {
    b = resolve(b, true);
    if (b == join) {
      return true;
    }
    if (b == kNone || !f_.live[b]) {
      return fail(AbcDecompileStatus::UnsupportedValueRegion, join, "a value arm ends before its join");
    }
    if (emitted_[b] || f_.is_header[b] || try_starts_at(b) || handler_[b]) {
      return fail(AbcDecompileStatus::UnsupportedValueRegion, b,
                  "a value arm shares or restarts a block at " + hex_offset(blk(b).start_offset));
    }
    const std::uint8_t opcode = last_of(b).opcode;
    if (opcode == 0x1B) {
      return fail(AbcDecompileStatus::UnsupportedValueRegion, b, "a switch inside a value arm");
    }
    if (is_conditional_branch(opcode)) {
      if (value_kind(b) == 0) {
        return fail(AbcDecompileStatus::UnsupportedValueRegion, b,
                    "a statement-level branch inside a value arm at " + hex_offset(blk(b).start_offset));
      }
      std::uint32_t next = kNone;
      if (!value_region(b, out, next)) {
        return false;
      }
      b = next;
      continue;
    }
    AbcNode node = simple_node(AbcNodeKind::Block);
    node.block = b;
    node.skip = take_skip(b);
    mark(b);
    out.push_back(std::move(node));
    b = single_succ(b);
  }
  return fail(AbcDecompileStatus::UnsupportedValueRegion, join, "a value arm did not terminate");
}

static AbcDecompileStatus recover_structure_impl(const AbcFile& file, std::uint32_t body_index,
                                                 AbcStructure& out, AbcDecompileIssue& issue) {
  AbcMethodFlow flow;
  AbcFlowIssue flow_issue;
  if (verify_method_flow(file, body_index, flow, flow_issue) != AbcFlowStatus::Ok) {
    return refuse(issue, AbcDecompileStatus::FlowRefused, flow_issue.offset,
                  std::string(flow_status_name(flow_issue.status)) + ": " + flow_issue.message);
  }
  out.body = body_index;
  out.method = file.bodies[body_index].method;
  out.instructions = std::move(flow.instructions);
  out.exceptions = flow.exceptions;
  const std::uint32_t code_len = flow.graph.code_len;
  std::vector<std::uint32_t> entries;
  for (const AbcExceptionRegion& region : out.exceptions) {
    entries.push_back(region.from);
    if (region.to < code_len) {
      entries.push_back(region.to);
    }
    entries.push_back(region.target);
  }
  AbcFlowIssue graph_issue;
  if (build_control_flow(out.instructions, entries, out.graph, graph_issue) != AbcFlowStatus::Ok) {
    return refuse(issue, AbcDecompileStatus::FlowRefused, graph_issue.offset,
                  std::string(flow_status_name(graph_issue.status)) + ": " + graph_issue.message);
  }
  const std::uint32_t count = static_cast<std::uint32_t>(out.graph.blocks.size());
  std::vector<HandlerEdge> handlers;
  std::vector<bool> handler_blocks(count, false);
  std::vector<TryGroup> groups;
  for (std::size_t position = 0; position < out.exceptions.size(); ++position) {
    const AbcExceptionRegion& region = out.exceptions[position];
    HandlerEdge edge;
    edge.from_block = out.graph.block_index_at(region.from);
    edge.handler_block = out.graph.block_index_at(region.target);
    if (edge.from_block == kNone || edge.handler_block == kNone) {
      return refuse(issue, AbcDecompileStatus::FlowRefused, region.from,
                    "an exception range does not start on a block");
    }
    handlers.push_back(edge);
    handler_blocks[edge.handler_block] = true;
    TryGroup* group = nullptr;
    for (TryGroup& existing : groups) {
      if (existing.from == region.from && existing.to == region.to) {
        group = &existing;
      }
    }
    if (group == nullptr) {
      groups.emplace_back();
      group = &groups.back();
      group->from_block = edge.from_block;
      group->from = region.from;
      group->to = region.to;
    }
    group->exceptions.push_back(static_cast<std::uint32_t>(position));
  }
  std::stable_sort(groups.begin(), groups.end(), [](const TryGroup& a, const TryGroup& b) {
    if (a.from_block != b.from_block) {
      return a.from_block < b.from_block;
    }
    return a.to > b.to;
  });
  for (const TryGroup& a : groups) {
    for (const TryGroup& b : groups) {
      if (a.from < b.from && b.from < a.to && a.to < b.to) {
        return refuse(issue, AbcDecompileStatus::UnsupportedTry, b.from,
                      "two try ranges overlap without nesting");
      }
    }
  }
  FlowFacts facts;
  if (compute_depths(file, out, handlers, facts, issue) != AbcDecompileStatus::Ok) {
    return issue.status;
  }
  if (analyse_flow(out, handlers, facts, issue) != AbcDecompileStatus::Ok) {
    return issue.status;
  }
  out.entry_stack_depth = facts.depth;
  Structurer structurer(out, facts, std::move(groups), std::move(handler_blocks), issue);
  if (!structurer.run()) {
    return issue.status;
  }
  return AbcDecompileStatus::Ok;
}

}  // namespace fo4::abc::detail

namespace fo4::abc {

AbcDecompileStatus recover_structure(const AbcFile& file, std::uint32_t body_index,
                                     AbcStructure& out, AbcDecompileIssue& issue) {
  issue = AbcDecompileIssue{};
  out = AbcStructure{};
  if (body_index >= file.bodies.size()) {
    issue.status = AbcDecompileStatus::BodyNotFound;
    issue.message = "no method body at index " + std::to_string(body_index);
    return issue.status;
  }
  issue.body = body_index;
  issue.method = file.bodies[body_index].method;
  const AbcDecompileStatus status = detail::recover_structure_impl(file, body_index, out, issue);
  if (status != AbcDecompileStatus::Ok) {
    out = AbcStructure{};
  }
  return status;
}

}  // namespace fo4::abc
