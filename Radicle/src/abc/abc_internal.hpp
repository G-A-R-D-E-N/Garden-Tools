#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/flow.hpp>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail {

AbcStatus parse_constant_pool(ByteReader& reader, AbcConstantPool& out);
AbcStatus parse_metadata(ByteReader& reader, std::vector<AbcMetadata>& out);
AbcStatus check_pool_indices(const AbcFile& file);

inline constexpr std::size_t kMaxTypeNameExpansion = 64;
inline constexpr std::uint32_t kVerifierStackCeiling = 4096;
std::uint32_t required_local_count(const std::vector<AbcInstruction>& code);

/* Invariant: method and body survive a set, so one seed per body lasts through a refusal. */
inline AbcFlowStatus set_issue(AbcFlowIssue& issue, AbcFlowStatus status, std::uint32_t offset,
                               std::string message) {
  issue.status = status;
  issue.offset = offset;
  issue.message = std::move(message);
  return status;
}

inline const char* mnemonic_of(const AbcInstruction& instruction) {
  return instruction.mnemonic == nullptr ? "" : instruction.mnemonic;
}

inline std::string hex_offset(std::uint32_t offset) {
  std::ostringstream text;
  text << "0x" << std::hex << std::setw(4) << std::setfill('0') << offset;
  return text.str();
}

/* Requires the caller to name the role the opcode's operand table gives that slot. */
inline bool u30_operand(const AbcInstruction& instruction, std::size_t index, AbcOperandRole role,
                        std::uint32_t& out) {
  if (index >= instruction.operands.size()) {
    return false;
  }
  const AbcDecodedOperand& operand = instruction.operands[index];
  if (operand.role != role) {
    return false;
  }
  out = operand.value;
  return true;
}

/* Requires a decoded body; entries are the handler targets reached from throwing opcodes. */
AbcFlowStatus build_exception_graph(const AbcMethodBody& body,
                                    const std::vector<AbcInstruction>& instructions,
                                    AbcControlFlowGraph& graph,
                                    std::vector<AbcExceptionRegion>& regions,
                                    std::vector<std::uint32_t>& entries, AbcFlowIssue& issue);

/* Invariant: entry_depths stays parallel to flow.graph.blocks, kNoFlowIndex if unreachable. */
AbcFlowStatus verify_stack(const AbcFile& file, const AbcMethodBody& body,
                           const AbcMethodFlow& flow,
                           const std::vector<std::uint32_t>& handler_entries,
                           std::vector<std::uint32_t>& entry_depths,
                           std::uint32_t& observed_max_stack, AbcFlowIssue& issue);

AbcFlowStatus verify_scope(const AbcMethodBody& body, const AbcMethodFlow& flow,
                           const std::vector<std::uint32_t>& handler_entries,
                           std::vector<std::uint32_t>& entry_depths,
                           std::uint32_t& observed_max_scope_depth, AbcFlowIssue& issue);

/* Invariant: pops and pushes come from the declared operand roles, never from a simulated run. */
AbcFlowStatus stack_effect(const AbcFile& file, const AbcInstruction& instruction,
                           std::uint64_t& pops, std::uint64_t& pushes, AbcFlowIssue& issue);

}  // namespace fo4::abc::detail
