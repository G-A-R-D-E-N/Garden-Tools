#include <fo4/abc/flow.hpp>

#include "abc_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc {

namespace {

bool is_branch_opcode(std::uint8_t opcode) { return opcode >= 0x0C && opcode <= 0x1A; }

bool is_terminal_opcode(std::uint8_t opcode) {
  return opcode == 0x03 || opcode == 0x47 || opcode == 0x48;
}

bool ends_block(std::uint8_t opcode) {
  return is_branch_opcode(opcode) || opcode == 0x1B || is_terminal_opcode(opcode);
}

std::uint32_t instruction_index_at(const std::vector<AbcInstruction>& code, std::uint32_t offset) {
  std::size_t low = 0;
  std::size_t high = code.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    if (code[middle].offset < offset) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < code.size() && code[low].offset == offset) {
    return static_cast<std::uint32_t>(low);
  }
  return kNoFlowIndex;
}

/* Requires the decoded pass to have resolved every target to an absolute byte offset. */
void explicit_targets(const AbcInstruction& instruction, std::vector<std::uint32_t>& out) {
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Offset) {
      out.push_back(static_cast<std::uint32_t>(operand.target));
    } else if (operand.role == AbcOperandRole::Switch) {
      out.push_back(static_cast<std::uint32_t>(operand.target));
      for (const std::int32_t target : operand.case_targets) {
        out.push_back(static_cast<std::uint32_t>(target));
      }
    }
  }
}

bool is_target_offset(const AbcInstruction& instruction) {
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Offset) {
      return operand.target >= 0;
    }
  }
  return true;
}

bool ends_with_switch(const AbcInstruction& instruction) {
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Switch) {
      return true;
    }
  }
  return false;
}

void push_edge(AbcBasicBlock& block, std::uint32_t target, AbcEdgeKind kind) {
  AbcControlFlowEdge edge;
  edge.target = target;
  edge.kind = kind;
  block.successors.push_back(edge);
}

/* Constraint: duplicates are removed once per block, so a wide switch stays linear. */
void sort_unique_edges(AbcBasicBlock& block) {
  const auto order = [](const AbcControlFlowEdge& left, const AbcControlFlowEdge& right) {
    if (left.target != right.target) {
      return left.target < right.target;
    }
    return left.kind < right.kind;
  };
  const auto same = [](const AbcControlFlowEdge& left, const AbcControlFlowEdge& right) {
    return left.target == right.target && left.kind == right.kind;
  };
  std::sort(block.successors.begin(), block.successors.end(), order);
  block.successors.erase(std::unique(block.successors.begin(), block.successors.end(), same),
                         block.successors.end());
}

AbcFlowStatus fail(AbcFlowIssue& issue, AbcFlowStatus status, std::uint32_t offset,
                   std::string message) {
  issue.status = status;
  issue.offset = offset;
  issue.message = std::move(message);
  return status;
}

/* Invariant: method and body survive a reset so a caller can seed them once per body. */
void reset_issue(AbcFlowIssue& issue) {
  issue.status = AbcFlowStatus::Ok;
  issue.decode = AbcStatus::Ok;
  issue.offset = kNoFlowIndex;
  issue.message.clear();
}

}  // namespace

const char* edge_kind_name(AbcEdgeKind kind) noexcept {
  switch (kind) {
    case AbcEdgeKind::Fallthrough:
      return "fallthrough";
    case AbcEdgeKind::Branch:
      return "branch";
    case AbcEdgeKind::Switch:
      return "switch";
  }
  return "unknown";
}

const char* flow_status_name(AbcFlowStatus status) noexcept {
  switch (status) {
    case AbcFlowStatus::Ok:
      return "ok";
    case AbcFlowStatus::DecodeFailed:
      return "decode failed";
    case AbcFlowStatus::EmptyCode:
      return "empty code";
    case AbcFlowStatus::InstructionLayout:
      return "instruction layout";
    case AbcFlowStatus::TargetOutsideCode:
      return "target outside code";
    case AbcFlowStatus::TargetInsideInstruction:
      return "target inside instruction";
    case AbcFlowStatus::BlockBudget:
      return "block budget";
    case AbcFlowStatus::UnmodelledStackEffect:
      return "unmodelled stack effect";
    case AbcFlowStatus::StackUnderflow:
      return "stack underflow";
    case AbcFlowStatus::StackDepthMismatch:
      return "stack depth mismatch";
    case AbcFlowStatus::StackLimitExceeded:
      return "stack limit exceeded";
    case AbcFlowStatus::ScopeUnderflow:
      return "scope underflow";
    case AbcFlowStatus::ScopeDepthMismatch:
      return "scope depth mismatch";
    case AbcFlowStatus::ScopeLimitExceeded:
      return "scope limit exceeded";
    case AbcFlowStatus::ScopeIndexOutOfRange:
      return "scope index out of range";
    case AbcFlowStatus::LocalCountTooSmall:
      return "local count too small";
    case AbcFlowStatus::LocalIndexOutOfRange:
      return "local index out of range";
    case AbcFlowStatus::MethodIndexOutOfRange:
      return "method index out of range";
    case AbcFlowStatus::ClassIndexOutOfRange:
      return "class index out of range";
    case AbcFlowStatus::PoolIndexInvalid:
      return "pool index invalid";
    case AbcFlowStatus::MultinameIndexOutOfRange:
      return "multiname index out of range";
    case AbcFlowStatus::ExceptionRangeInvalid:
      return "exception range invalid";
    case AbcFlowStatus::ExceptionTargetInvalid:
      return "exception target invalid";
    case AbcFlowStatus::ExceptionTypeUnresolved:
      return "exception type unresolved";
    case AbcFlowStatus::ExceptionVariableInvalid:
      return "exception variable invalid";
    case AbcFlowStatus::MethodFlagMissing:
      return "method flag missing";
    case AbcFlowStatus::OpcodeNotPermitted:
      return "opcode not permitted";
    case AbcFlowStatus::SlotIndexInvalid:
      return "slot index invalid";
    case AbcFlowStatus::FlowIterationLimit:
      return "flow iteration limit";
  }
  return "unknown";
}

std::size_t AbcControlFlowGraph::unreachable_block_count() const noexcept {
  std::size_t count = 0;
  for (const AbcBasicBlock& block : blocks) {
    if (!block.reachable) {
      ++count;
    }
  }
  return count;
}

std::uint32_t AbcControlFlowGraph::block_index_at(std::uint32_t offset) const noexcept {
  std::size_t low = 0;
  std::size_t high = blocks.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    if (blocks[middle].start_offset < offset) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < blocks.size() && blocks[low].start_offset == offset) {
    return static_cast<std::uint32_t>(low);
  }
  return kNoFlowIndex;
}

AbcFlowStatus build_control_flow(const std::vector<AbcInstruction>& instructions,
                                 const std::vector<std::uint32_t>& extra_entries,
                                 AbcControlFlowGraph& out, AbcFlowIssue& issue) {
  reset_issue(issue);
  out = {};
  if (instructions.empty()) {
    if (!extra_entries.empty()) {
      return fail(issue, AbcFlowStatus::TargetOutsideCode, extra_entries.front(),
                  "a control-flow entry needs at least one instruction");
    }
    return AbcFlowStatus::Ok;
  }

  std::uint32_t expected = 0;
  for (const AbcInstruction& instruction : instructions) {
    if (instruction.length == 0 || instruction.offset != expected) {
      return fail(issue, AbcFlowStatus::InstructionLayout,
                  static_cast<std::uint32_t>(instruction.offset),
                  "instruction layout at 0x" + std::to_string(instruction.offset) +
                      " is not contiguous at 0x" + std::to_string(expected));
    }
    expected = static_cast<std::uint32_t>(instruction.offset) +
               static_cast<std::uint32_t>(instruction.length);
  }
  const std::uint32_t code_len = expected;
  out.code_len = code_len;

  std::vector<std::uint32_t> leaders;
  leaders.push_back(0);
  std::vector<std::uint32_t> targets;
  for (std::size_t index = 0; index < instructions.size(); ++index) {
    const AbcInstruction& instruction = instructions[index];
    targets.clear();
    explicit_targets(instruction, targets);
    for (const std::uint32_t target : targets) {
      if (is_target_offset(instruction) && target < code_len &&
          instruction_index_at(instructions, target) != kNoFlowIndex) {
        leaders.push_back(target);
      }
    }
    if (ends_block(instruction.opcode)) {
      const std::uint32_t next = static_cast<std::uint32_t>(instruction.offset) +
                                 static_cast<std::uint32_t>(instruction.length);
      if (next < code_len) {
        leaders.push_back(next);
      }
    }
  }
  for (const std::uint32_t entry : extra_entries) {
    if (entry >= code_len || instruction_index_at(instructions, entry) == kNoFlowIndex) {
      return fail(issue, AbcFlowStatus::TargetOutsideCode, entry,
                  "extra entry 0x" + std::to_string(entry) + " is not an instruction start");
    }
    leaders.push_back(entry);
  }
  std::sort(leaders.begin(), leaders.end());
  leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());
  if (leaders.size() > instructions.size() + 1) {
    return fail(issue, AbcFlowStatus::BlockBudget, 0, "more blocks than instructions");
  }

  out.blocks.reserve(leaders.size());
  for (std::size_t index = 0; index < leaders.size(); ++index) {
    AbcBasicBlock block;
    block.index = static_cast<std::uint32_t>(index);
    block.start_offset = leaders[index];
    block.end_offset = index + 1 < leaders.size() ? leaders[index + 1] : code_len;
    block.first_instruction = instruction_index_at(instructions, block.start_offset);
    if (block.first_instruction == kNoFlowIndex) {
      return fail(issue, AbcFlowStatus::InstructionLayout, block.start_offset,
                  "a block does not start on an instruction");
    }
    const std::uint32_t room = static_cast<std::uint32_t>(instructions.size()) -
                               block.first_instruction;
    std::uint32_t count = 0;
    while (count < room &&
           instructions[block.first_instruction + count].offset < block.end_offset) {
      ++count;
    }
    if (count == 0) {
      return fail(issue, AbcFlowStatus::InstructionLayout, block.start_offset,
                  "a block holds no instructions");
    }
    block.instruction_count = count;
    const AbcInstruction& last = instructions[block.first_instruction + count - 1];
    targets.clear();
    explicit_targets(last, targets);
    if (ends_with_switch(last)) {
      for (const std::uint32_t target : targets) {
        push_edge(block, target, AbcEdgeKind::Switch);
      }
    } else if (is_branch_opcode(last.opcode)) {
      for (const std::uint32_t target : targets) {
        push_edge(block, target, AbcEdgeKind::Branch);
      }
      if (last.opcode != 0x10 && block.end_offset < code_len) {
        push_edge(block, block.end_offset, AbcEdgeKind::Fallthrough);
      }
    } else if (!is_terminal_opcode(last.opcode) && block.end_offset < code_len) {
      push_edge(block, block.end_offset, AbcEdgeKind::Fallthrough);
    }
    sort_unique_edges(block);
    out.blocks.push_back(std::move(block));
  }

  for (AbcBasicBlock& block : out.blocks) {
    for (AbcControlFlowEdge& edge : block.successors) {
      edge.block = out.block_index_at(edge.target);
    }
  }

  std::vector<std::uint32_t> pending;
  pending.push_back(0);
  for (const std::uint32_t entry : extra_entries) {
    pending.push_back(out.block_index_at(entry));
  }
  while (!pending.empty()) {
    const std::uint32_t index = pending.back();
    pending.pop_back();
    AbcBasicBlock& block = out.blocks[index];
    if (block.reachable) {
      continue;
    }
    block.reachable = true;
    for (const AbcControlFlowEdge& edge : block.successors) {
      if (edge.block != kNoFlowIndex) {
        pending.push_back(edge.block);
      }
    }
  }

  for (const AbcBasicBlock& block : out.blocks) {
    if (!block.reachable) {
      continue;
    }
    const AbcInstruction& last =
        instructions[block.first_instruction + block.instruction_count - 1];
    targets.clear();
    explicit_targets(last, targets);
    for (const std::uint32_t target : targets) {
      if (!is_target_offset(last) || target >= code_len) {
        return fail(issue, AbcFlowStatus::TargetOutsideCode,
                  static_cast<std::uint32_t>(last.offset),
                    "control-flow target 0x" + std::to_string(target) +
                        " is outside code length 0x" + std::to_string(code_len));
      }
      if (instruction_index_at(instructions, target) == kNoFlowIndex) {
        return fail(issue, AbcFlowStatus::TargetInsideInstruction,
                  static_cast<std::uint32_t>(last.offset),
                    "control-flow target 0x" + std::to_string(target) +
                        " lands inside an instruction");
      }
    }
  }
  return AbcFlowStatus::Ok;
}

std::uint32_t multiname_runtime_pops(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.multinames.size()) {
    return kNoFlowIndex;
  }
  switch (file.pool.multinames[index].kind) {
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
      return 1;
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return 2;
    default:
      return 0;
  }
}

bool multiname_local_name_is_wildcard(const AbcFile& file, std::uint32_t index) {
  std::size_t remaining = file.pool.multinames.size();
  std::uint32_t current = index;
  while (remaining > 0) {
    if (static_cast<std::size_t>(current) >= file.pool.multinames.size()) {
      return true;
    }
    const AbcMultiname& multiname = file.pool.multinames[current];
    if (multiname.kind != AbcMultinameKind::TypeName) {
      return multiname.name == 0;
    }
    current = multiname.name;
    remaining -= 1;
  }
  return true;
}

namespace detail {

namespace {

/* Requires an opcode the range scan treats as throwing; avmplus' verifier uses the same set. */
bool opcode_can_throw(std::uint8_t opcode) {
  switch (opcode) {
    case 0x01:
    case 0x02:
    case 0x08:
    case 0x09:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1D:
    case 0x20:
    case 0x21:
    case 0x31:
    case 0x47:
    case 0x67:
    case 0x6E:
    case 0x95:
    case 0x96:
    case 0xF2:
      return false;
    default:
      break;
  }
  if (opcode >= 0x24 && opcode <= 0x2F) {
    return false;
  }
  if (opcode >= 0x62 && opcode <= 0x65) {
    return false;
  }
  if (opcode >= 0xD0 && opcode <= 0xD7) {
    return false;
  }
  return true;
}

/* Requires a region the range scan already hit; the table is only checked where it can fire. */
AbcFlowStatus validate_region(const AbcExceptionRegion& region, std::uint32_t code_len,
                              AbcFlowIssue& issue) {
  if (region.from > region.to || region.to > code_len) {
    return fail(issue, AbcFlowStatus::ExceptionRangeInvalid, region.from,
                "exception " + std::to_string(region.index) + " range " +
                    std::to_string(region.from) + ".." + std::to_string(region.to) +
                    " is outside code length " + std::to_string(code_len));
  }
  if (region.target >= code_len) {
    return fail(issue, AbcFlowStatus::ExceptionTargetInvalid, region.target,
                "exception " + std::to_string(region.index) + " handler target " +
                    std::to_string(region.target) + " is outside code length " +
                    std::to_string(code_len));
  }
  if (region.target < region.to) {
    return fail(issue, AbcFlowStatus::ExceptionTargetInvalid, region.target,
                "exception " + std::to_string(region.index) + " handler target " +
                    std::to_string(region.target) + " precedes protected range end " +
                    std::to_string(region.to));
  }
  return AbcFlowStatus::Ok;
}

}  // namespace

AbcFlowStatus build_exception_graph(const AbcMethodBody& body,
                                    const std::vector<AbcInstruction>& instructions,
                                    AbcControlFlowGraph& graph,
                                    std::vector<AbcExceptionRegion>& regions,
                                    std::vector<std::uint32_t>& entries, AbcFlowIssue& issue) {
  regions.clear();
  regions.reserve(body.exceptions.size());
  for (std::size_t index = 0; index < body.exceptions.size(); ++index) {
    const AbcException& exception = body.exceptions[index];
    AbcExceptionRegion region;
    region.index = static_cast<std::uint32_t>(index);
    region.from = exception.from;
    region.to = exception.to;
    region.target = exception.target;
    region.catch_type = exception.exc_type;
    region.variable_name = exception.var_name;
    regions.push_back(region);
  }

  entries.clear();
  // Constraint: each reachable instruction is scanned once, because the entry set only grows.
  std::vector<std::uint8_t> scanned(instructions.size(), 0);
  std::vector<std::uint32_t> found;
  const std::size_t guard_limit = instructions.size() + 2;
  for (std::size_t guard = 0; guard < guard_limit; ++guard) {
    const AbcFlowStatus status = build_control_flow(instructions, entries, graph, issue);
    if (status != AbcFlowStatus::Ok) {
      return status;
    }
    found.clear();
    for (const AbcBasicBlock& block : graph.blocks) {
      if (!block.reachable) {
        continue;
      }
      const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                              static_cast<std::size_t>(block.instruction_count);
      for (std::size_t at = block.first_instruction; at < end; ++at) {
        if (scanned[at] != 0) {
          continue;
        }
        scanned[at] = 1;
        const AbcInstruction& instruction = instructions[at];
        if (!opcode_can_throw(instruction.opcode)) {
          continue;
        }
        for (const AbcExceptionRegion& region : regions) {
          if (!region.covers(static_cast<std::uint32_t>(instruction.offset))) {
            continue;
          }
          const AbcFlowStatus region_status = validate_region(region, graph.code_len, issue);
          if (region_status != AbcFlowStatus::Ok) {
            return region_status;
          }
          found.push_back(region.target);
        }
      }
    }
    if (found.empty()) {
      return AbcFlowStatus::Ok;
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    std::vector<std::uint32_t> merged;
    merged.reserve(entries.size() + found.size());
    std::set_union(entries.begin(), entries.end(), found.begin(), found.end(),
                   std::back_inserter(merged));
    if (merged == entries) {
      return AbcFlowStatus::Ok;
    }
    entries = std::move(merged);
  }
  return fail(issue, AbcFlowStatus::FlowIterationLimit, kNoFlowIndex,
              "the exception-entry fixpoint did not converge");
}

}  // namespace detail

}  // namespace fo4::abc
