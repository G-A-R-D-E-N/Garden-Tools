#include <fo4/abc/flow.hpp>

#include "abc_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {
namespace detail {

AbcFlowStatus stack_effect(const AbcFile& file, const AbcInstruction& instruction,
                           std::uint64_t& pops, std::uint64_t& pushes, AbcFlowIssue& issue) {
  const std::uint32_t opcode = instruction.opcode;
  const auto multiname_pops = [&](std::uint64_t& out) {
    std::uint32_t index = 0;
    if (!u30_operand(instruction, 0, AbcOperandRole::Multiname, index)) {
      return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                       static_cast<std::uint32_t>(instruction.offset),
                       std::string{mnemonic_of(instruction)} + " has no multiname operand");
    }
    const std::uint32_t runtime = multiname_runtime_pops(file, index);
    if (runtime == kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::MultinameIndexOutOfRange,
                       static_cast<std::uint32_t>(instruction.offset),
                       std::string{mnemonic_of(instruction)} + " reads multiname " +
                           std::to_string(index) + " outside the pool");
    }
    out = runtime;
    return AbcFlowStatus::Ok;
  };
  const auto count_at = [&](std::size_t index, std::uint64_t& out) {
    std::uint32_t value = 0;
    if (!u30_operand(instruction, index, AbcOperandRole::Count, value)) {
      return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                       static_cast<std::uint32_t>(instruction.offset),
                       std::string{mnemonic_of(instruction)} + " has no count operand " +
                           std::to_string(index));
    }
    out = value;
    return AbcFlowStatus::Ok;
  };

  pops = 0;
  pushes = 0;
  std::uint64_t runtime = 0;
  std::uint64_t argc = 0;
  switch (opcode) {
    case 0x01:
    case 0x02:
    case 0x06:
    case 0x08:
    case 0x09:
    case 0x10:
    case 0x1D:
    case 0x47:
    case 0x92:
    case 0x94:
    case 0xC2:
    case 0xC3:
    case 0xEF:
    case 0xF0:
    case 0xF1:
    case 0xF2:
    case 0xF3:
      return AbcFlowStatus::Ok;
    case 0x03:
    case 0x07:
    case 0x1C:
    case 0x30:
    case 0x48:
    case 0x63:
    case 0xD4:
    case 0xD5:
    case 0xD6:
    case 0xD7:
      pops = 1;
      return AbcFlowStatus::Ok;
    case 0x04:
    case 0x59:
    case 0x66:
    case 0x6A:
      if (const AbcFlowStatus status = multiname_pops(runtime); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = runtime + 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x05:
    case 0x61:
    case 0x68:
      if (const AbcFlowStatus status = multiname_pops(runtime); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = runtime + 2;
      return AbcFlowStatus::Ok;
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
      pops = 2;
      return AbcFlowStatus::Ok;
    case 0x11:
    case 0x12:
    case 0x1B:
      pops = 1;
      return AbcFlowStatus::Ok;
    case 0x1E:
    case 0x1F:
    case 0x23:
      pops = 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x20:
    case 0x21:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
    case 0x31:
    case 0x32:
    case 0x64:
    case 0x65:
    case 0x67:
    case 0x6E:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3:
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x29:
      pops = 1;
      return AbcFlowStatus::Ok;
    case 0x2A:
      pops = 1;
      pushes = 2;
      return AbcFlowStatus::Ok;
    case 0x2B:
      pops = 2;
      pushes = 2;
      return AbcFlowStatus::Ok;
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
      pops = 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
      pops = 2;
      return AbcFlowStatus::Ok;
    case 0x40:
    case 0x57:
    case 0x5A:
    case 0x5F:
    case 0x60:
    case 0x62:
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x41:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc + 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x42:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc + 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x43:
    case 0x44:
      if (const AbcFlowStatus status = count_at(1, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc + 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x45:
    case 0x46:
    case 0x4A:
    case 0x4C:
      if (const AbcFlowStatus status = multiname_pops(runtime); status != AbcFlowStatus::Ok) {
        return status;
      }
      if (const AbcFlowStatus status = count_at(1, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = runtime + argc + 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x49:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc + 1;
      return AbcFlowStatus::Ok;
    case 0x4E:
    case 0x4F:
      if (const AbcFlowStatus status = multiname_pops(runtime); status != AbcFlowStatus::Ok) {
        return status;
      }
      if (const AbcFlowStatus status = count_at(1, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = runtime + argc + 1;
      return AbcFlowStatus::Ok;
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x88:
    case 0x89:
    case 0x90:
    case 0x91:
    case 0x93:
    case 0x95:
    case 0x96:
    case 0x97:
    case 0xB2:
    case 0xC0:
    case 0xC1:
    case 0xC4:
      pops = 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x53:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc + 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x55:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc * 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x56:
      if (const AbcFlowStatus status = count_at(0, argc); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = argc;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x58:
      pops = 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
      if (const AbcFlowStatus status = multiname_pops(runtime); status != AbcFlowStatus::Ok) {
        return status;
      }
      pops = runtime;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x69:
      pops = 3;
      return AbcFlowStatus::Ok;
    case 0x6B:
      pops = 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x6C:
      pops = 1;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0x6D:
      pops = 2;
      return AbcFlowStatus::Ok;
    case 0x6F:
      pops = 1;
      return AbcFlowStatus::Ok;
    case 0x87:
    case 0xB3:
      pops = 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
    case 0xA8:
    case 0xA9:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
    case 0xAE:
    case 0xAF:
    case 0xB0:
    case 0xB1:
    case 0xB4:
    case 0xC5:
    case 0xC6:
    case 0xC7:
      pops = 2;
      pushes = 1;
      return AbcFlowStatus::Ok;
    default:
      return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                       static_cast<std::uint32_t>(instruction.offset),
                       "opcode " + hex_offset(opcode) + " (" + mnemonic_of(instruction) +
                           ") has no modelled stack effect");
  }
}

AbcFlowStatus verify_stack(const AbcFile& file, const AbcMethodBody& body,
                           const AbcMethodFlow& flow,
                           const std::vector<std::uint32_t>& handler_entries,
                           std::vector<std::uint32_t>& entry_depths,
                           std::uint32_t& observed_max_stack, AbcFlowIssue& issue) {
  const AbcControlFlowGraph& graph = flow.graph;
  entry_depths.assign(graph.blocks.size(), kNoFlowIndex);
  observed_max_stack = 0;
  if (graph.blocks.empty()) {
    return AbcFlowStatus::Ok;
  }
  entry_depths[0] = 0;
  std::vector<std::uint32_t> pending;
  pending.push_back(0);
  for (const std::uint32_t target : handler_entries) {
    const std::uint32_t index = graph.block_index_at(target);
    if (index == kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::ExceptionTargetInvalid, target,
                       "handler target " + hex_offset(target) + " has no basic block");
    }
    if (entry_depths[index] == 1) {
      continue;
    }
    if (entry_depths[index] != kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::StackDepthMismatch, target,
                       "handler target " + hex_offset(target) + " already enters with depth " +
                           std::to_string(entry_depths[index]));
    }
    entry_depths[index] = 1;
    pending.push_back(index);
  }

  while (!pending.empty()) {
    const std::uint32_t index = pending.back();
    pending.pop_back();
    const AbcBasicBlock& block = graph.blocks[index];
    if (!block.reachable) {
      continue;
    }
    const std::uint32_t depth_in = entry_depths[index];
    if (depth_in == kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::StackDepthMismatch, block.start_offset,
                       "reachable block " + std::to_string(index) + " has no entry depth");
    }
    std::uint64_t depth = depth_in;
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      std::uint64_t pops = 0;
      std::uint64_t pushes = 0;
      if (const AbcFlowStatus status = stack_effect(file, instruction, pops, pushes, issue);
          status != AbcFlowStatus::Ok) {
        return status;
      }
      if (depth < pops) {
        return set_issue(issue, AbcFlowStatus::StackUnderflow,
                         static_cast<std::uint32_t>(instruction.offset),
                         std::string{mnemonic_of(instruction)} + " needs " +
                             std::to_string(pops) + " stack value(s) but " +
                             std::to_string(depth) + " are live");
      }
      depth = depth - pops + pushes;
      if (depth > observed_max_stack) {
        observed_max_stack = static_cast<std::uint32_t>(depth);
      }
      if (depth > body.max_stack) {
        return set_issue(issue, AbcFlowStatus::StackLimitExceeded,
                         static_cast<std::uint32_t>(instruction.offset),
                         "operand stack reaches " + std::to_string(depth) +
                             " past declared max_stack " + std::to_string(body.max_stack));
      }
    }
    const std::uint32_t depth_out = static_cast<std::uint32_t>(depth);
    for (const AbcControlFlowEdge& edge : block.successors) {
      if (edge.block == kNoFlowIndex) {
        return set_issue(issue, AbcFlowStatus::TargetOutsideCode, edge.target,
                         "edge target " + hex_offset(edge.target) + " has no basic block");
      }
      if (entry_depths[edge.block] == kNoFlowIndex) {
        entry_depths[edge.block] = depth_out;
        pending.push_back(edge.block);
        continue;
      }
      if (entry_depths[edge.block] != depth_out) {
        return set_issue(issue, AbcFlowStatus::StackDepthMismatch, edge.target,
                         "block at " + hex_offset(edge.target) + " is entered with depth " +
                             std::to_string(entry_depths[edge.block]) + " and " +
                             std::to_string(depth_out));
      }
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_scope(const AbcMethodBody& body, const AbcMethodFlow& flow,
                           const std::vector<std::uint32_t>& handler_entries,
                           std::vector<std::uint32_t>& entry_depths,
                           std::uint32_t& observed_max_scope_depth, AbcFlowIssue& issue) {
  const AbcControlFlowGraph& graph = flow.graph;
  const std::uint32_t init_scope_depth = body.init_scope_depth;
  entry_depths.assign(graph.blocks.size(), kNoFlowIndex);
  observed_max_scope_depth = init_scope_depth;
  if (graph.blocks.empty()) {
    return AbcFlowStatus::Ok;
  }
  entry_depths[0] = init_scope_depth;
  std::vector<std::uint32_t> pending;
  pending.push_back(0);
  for (const std::uint32_t target : handler_entries) {
    const std::uint32_t index = graph.block_index_at(target);
    if (index == kNoFlowIndex || entry_depths[index] == init_scope_depth) {
      continue;
    }
    if (entry_depths[index] != kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::ScopeDepthMismatch, target,
                       "handler target " + hex_offset(target) +
                           " already enters with scope depth " +
                           std::to_string(entry_depths[index]));
    }
    entry_depths[index] = init_scope_depth;
    pending.push_back(index);
  }

  while (!pending.empty()) {
    const std::uint32_t index = pending.back();
    pending.pop_back();
    const AbcBasicBlock& block = graph.blocks[index];
    if (!block.reachable) {
      continue;
    }
    std::uint64_t depth = entry_depths[index];
    if (depth == kNoFlowIndex) {
      return set_issue(issue, AbcFlowStatus::ScopeDepthMismatch, block.start_offset,
                       "reachable block " + std::to_string(index) + " has no entry scope depth");
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      switch (instruction.opcode) {
        case 0x1C:
        case 0x30:
          ++depth;
          if (depth > observed_max_scope_depth) {
            observed_max_scope_depth = static_cast<std::uint32_t>(depth);
          }
          if (depth > body.max_scope_depth) {
            return set_issue(issue, AbcFlowStatus::ScopeLimitExceeded,
                             static_cast<std::uint32_t>(instruction.offset),
                             "scope depth reaches " + std::to_string(depth) +
                                 " past declared max_scope_depth " +
                                 std::to_string(body.max_scope_depth));
          }
          break;
        case 0x1D:
          if (depth <= init_scope_depth) {
            return set_issue(issue, AbcFlowStatus::ScopeUnderflow,
                             static_cast<std::uint32_t>(instruction.offset),
                             "popscope would drop below init_scope_depth " +
                                 std::to_string(init_scope_depth));
          }
          depth -= 1;
          break;
        case 0x65: {
          std::uint32_t scope_index = 0;
          if (!u30_operand(instruction, 0, AbcOperandRole::Count, scope_index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             "getscopeobject has no scope index operand");
          }
          const std::uint64_t local = depth - init_scope_depth;
          if (scope_index >= local) {
            return set_issue(issue, AbcFlowStatus::ScopeIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             "local scope index " + std::to_string(scope_index) +
                                 " is out of range with " + std::to_string(local) +
                                 " local scope entries");
          }
          break;
        }
        default:
          break;
      }
    }
    const std::uint32_t depth_out = static_cast<std::uint32_t>(depth);
    for (const AbcControlFlowEdge& edge : block.successors) {
      if (edge.block == kNoFlowIndex) {
        return set_issue(issue, AbcFlowStatus::TargetOutsideCode, edge.target,
                         "edge target " + hex_offset(edge.target) + " has no basic block");
      }
      if (entry_depths[edge.block] == kNoFlowIndex) {
        entry_depths[edge.block] = depth_out;
        pending.push_back(edge.block);
        continue;
      }
      if (entry_depths[edge.block] != depth_out) {
        return set_issue(issue, AbcFlowStatus::ScopeDepthMismatch, edge.target,
                         "block at " + hex_offset(edge.target) +
                             " is entered with scope depth " +
                             std::to_string(entry_depths[edge.block]) + " and " +
                             std::to_string(depth_out));
      }
    }
  }
  return AbcFlowStatus::Ok;
}

}  // namespace detail
}  // namespace fo4::abc
