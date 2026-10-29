#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {

inline constexpr std::uint32_t kNoFlowIndex = 0xFFFFFFFFu;

enum class AbcEdgeKind : std::uint8_t {
  Fallthrough = 0,
  Branch,
  Switch,
};

const char* edge_kind_name(AbcEdgeKind kind) noexcept;

enum class AbcFlowStatus : std::uint8_t {
  Ok = 0,
  DecodeFailed,
  EmptyCode,
  InstructionLayout,
  TargetOutsideCode,
  TargetInsideInstruction,
  BlockBudget,
  UnmodelledStackEffect,
  StackUnderflow,
  StackDepthMismatch,
  StackLimitExceeded,
  ScopeUnderflow,
  ScopeDepthMismatch,
  ScopeLimitExceeded,
  ScopeIndexOutOfRange,
  LocalCountTooSmall,
  LocalIndexOutOfRange,
  MethodIndexOutOfRange,
  ClassIndexOutOfRange,
  PoolIndexInvalid,
  MultinameIndexOutOfRange,
  ExceptionRangeInvalid,
  ExceptionTargetInvalid,
  ExceptionTypeUnresolved,
  ExceptionVariableInvalid,
  MethodFlagMissing,
  OpcodeNotPermitted,
  SlotIndexInvalid,
  FlowIterationLimit,
};

const char* flow_status_name(AbcFlowStatus status) noexcept;

/* Invariant: a failing report has method and offset set unless the fault is file-wide. */
struct AbcFlowIssue {
  AbcFlowStatus status{AbcFlowStatus::Ok};
  AbcStatus decode{AbcStatus::Ok};
  std::uint32_t method{kNoFlowIndex};
  std::uint32_t body{kNoFlowIndex};
  std::uint32_t offset{kNoFlowIndex};
  std::string message;

  bool ok() const noexcept { return status == AbcFlowStatus::Ok; }
};

struct AbcControlFlowEdge {
  std::uint32_t block{kNoFlowIndex};
  std::uint32_t target{0};
  AbcEdgeKind kind{AbcEdgeKind::Fallthrough};
};

struct AbcBasicBlock {
  std::uint32_t index{0};
  std::uint32_t start_offset{0};
  std::uint32_t end_offset{0};
  std::uint32_t first_instruction{0};
  std::uint32_t instruction_count{0};
  std::vector<AbcControlFlowEdge> successors;
  bool reachable{false};
};

struct AbcControlFlowGraph {
  std::uint32_t code_len{0};
  std::vector<AbcBasicBlock> blocks;

  bool empty() const noexcept { return blocks.empty(); }
  std::size_t unreachable_block_count() const noexcept;
  /* Requires a block start offset; returns the block index or kNoFlowIndex. */
  std::uint32_t block_index_at(std::uint32_t offset) const noexcept;
};

struct AbcExceptionRegion {
  std::uint32_t index{0};
  std::uint32_t from{0};
  std::uint32_t to{0};
  std::uint32_t target{0};
  std::uint32_t catch_type{0};
  std::uint32_t variable_name{0};

  /* Requires from <= to; the range is half open, as the AVM2 exception table defines it. */
  bool covers(std::uint32_t offset) const noexcept { return offset >= from && offset < to; }
};

/* Invariant: unreachable blocks keep kNoFlowIndex depths and reachable ones hold their entry depth. */
struct AbcMethodFlow {
  std::uint32_t body{0};
  std::uint32_t method{0};
  std::vector<AbcInstruction> instructions;
  AbcControlFlowGraph graph;
  std::vector<AbcExceptionRegion> exceptions;
  /* Invariant: parallel to graph.blocks, in block order. */
  std::vector<std::uint32_t> entry_stack_depth;
  std::vector<std::uint32_t> entry_scope_depth;
  std::uint32_t observed_max_stack{0};
  std::uint32_t observed_max_scope_depth{0};
  bool verified{false};
};

/* Requires contiguous instruction layout starting at offset 0; entries are extra block starts. */
AbcFlowStatus build_control_flow(const std::vector<AbcInstruction>& instructions,
                                 const std::vector<std::uint32_t>& extra_entries,
                                 AbcControlFlowGraph& out, AbcFlowIssue& issue);

/* Requires body_index below file.bodies.size(); fills the model even when verification refuses. */
AbcFlowStatus verify_method_flow(const AbcFile& file, std::uint32_t body_index, AbcMethodFlow& out,
                                 AbcFlowIssue& issue);

/* Invariant: `flows` stays parallel to `file.bodies` and `issues` keeps one entry per refusal. */
AbcFlowStatus verify_all_method_flows(const AbcFile& file, std::vector<AbcMethodFlow>& flows,
                                      std::vector<AbcFlowIssue>& issues);

/* Requires an in-pool index; returns kNoFlowIndex when the index is outside the multiname pool. */
std::uint32_t multiname_runtime_pops(const AbcFile& file, std::uint32_t index);

/* Invariant: index 0 and any index past the pool resolve to the empty name, the wildcard. */
bool multiname_local_name_is_wildcard(const AbcFile& file, std::uint32_t index);

}
