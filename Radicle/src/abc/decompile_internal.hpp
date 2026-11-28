#pragma once

#include <fo4/abc/decompile.hpp>

#include "abc_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc::detail {

inline constexpr std::uint32_t kNone = kNoFlowIndex;

struct HandlerEdge {
  std::uint32_t from_block{kNone};
  std::uint32_t handler_block{kNone};
};

/* Invariant: every vector is parallel to the block list, with the virtual exit appended to ipdom only. */
struct FlowFacts {
  std::uint32_t count{0};
  std::vector<bool> live;
  std::vector<std::uint32_t> depth;
  std::vector<std::uint32_t> exit_depth;
  std::vector<std::vector<std::uint32_t>> succ;
  std::vector<std::vector<std::uint32_t>> pred;
  std::vector<std::uint32_t> idom;
  std::vector<std::uint32_t> ipdom;
  std::vector<std::uint32_t> rpo;
  std::vector<bool> is_header;
  std::vector<std::vector<std::uint32_t>> latches;
  std::vector<std::vector<std::uint32_t>> region;

  bool dominates(std::uint32_t a, std::uint32_t b) const;
  bool in_region(std::uint32_t header, std::uint32_t block) const;
};

AbcDecompileStatus compute_depths(const AbcFile& file, const AbcStructure& structure,
                                  const std::vector<HandlerEdge>& handlers, FlowFacts& facts,
                                  AbcDecompileIssue& issue);

AbcDecompileStatus analyse_flow(const AbcStructure& structure,
                                const std::vector<HandlerEdge>& handlers, FlowFacts& facts,
                                AbcDecompileIssue& issue);

bool is_conditional_branch(std::uint8_t opcode);
bool is_noise_opcode(std::uint8_t opcode);
bool is_condition_hostile(std::uint8_t opcode);
bool is_get_local(std::uint8_t opcode);
bool is_set_local(std::uint8_t opcode);

struct TryGroup {
  std::uint32_t from_block{kNone};
  std::uint32_t from{0};
  std::uint32_t to{0};
  std::vector<std::uint32_t> exceptions;
  bool started{false};
};

struct Frame {
  bool is_loop{false};
  std::uint32_t header{kNone};
  std::uint32_t exit{kNone};
  bool entered{false};
};

struct Chain {
  std::vector<std::uint32_t> members;
  std::vector<std::uint32_t> exits;
};

enum class Jump : std::uint8_t { None = 0, Break, Continue, Labeled };

class Structurer {
public:
  Structurer(AbcStructure& structure, const FlowFacts& facts, std::vector<TryGroup> tries,
             std::vector<bool> handler_blocks, AbcDecompileIssue& issue);

  bool run();

private:
  AbcStructure& s_;
  const FlowFacts& f_;
  std::vector<TryGroup> tries_;
  std::vector<bool> handler_;
  AbcDecompileIssue& issue_;
  std::vector<bool> emitted_;
  std::vector<bool> loop_started_;
  std::vector<std::uint32_t> skip_;
  std::vector<std::uint32_t> emit_log_;
  std::vector<std::uint32_t> stops_;
  std::vector<Frame> frames_;

  bool fail(AbcDecompileStatus status, std::uint32_t block, const std::string& message);
  const AbcBasicBlock& blk(std::uint32_t b) const { return s_.graph.blocks[b]; }
  const AbcInstruction& last_of(std::uint32_t b) const;
  const AbcInstruction& instr_of(std::uint32_t b, std::uint32_t i) const;
  std::uint32_t taken_of(std::uint32_t b) const;
  std::uint32_t fall_of(std::uint32_t b) const;
  std::uint32_t single_succ(std::uint32_t b) const;
  std::uint32_t take_skip(std::uint32_t b);
  void mark(std::uint32_t b);

  bool is_trivial(std::uint32_t b) const;
  std::uint32_t resolve(std::uint32_t b, bool mark_skipped);
  TryGroup* pending_try(std::uint32_t b);
  bool try_starts_at(std::uint32_t b) const;
  Frame* innermost_loop();
  Jump classify(std::uint32_t b);
  std::uint32_t valid_join(std::uint32_t b);
  bool in_stops(std::uint32_t b) const;

  bool region_impl(std::uint32_t b, std::uint32_t stop, std::vector<AbcNode>& out);
  bool region(std::uint32_t b, std::uint32_t stop, std::vector<AbcNode>& out);
  bool statement_if(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& next);

  bool block_is_condition_only(std::uint32_t b) const;
  bool same_try_cover(std::uint32_t a, std::uint32_t b) const;
  bool chain_candidate(const Chain& chain, std::uint32_t x) const;
  std::vector<std::uint32_t> external_exits(const Chain& chain) const;
  bool build_chain(std::uint32_t head, Chain& chain);
  bool chain_steps(const Chain& chain, std::uint32_t true_exit, std::vector<AbcCondStep>& steps);
  bool step_for(const Chain& chain, std::uint32_t member, std::uint32_t true_exit,
                std::vector<AbcCondStep>& steps, std::uint32_t& index);

  int value_kind(std::uint32_t b) const;
  std::uint32_t leading_pop(std::uint32_t b) const;
  bool value_region(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& join);
  bool value_arm(std::uint32_t b, std::uint32_t join, std::vector<AbcNode>& out);

  bool structure_loop(std::uint32_t h, std::vector<AbcNode>& out, std::uint32_t& next);
  void shape_loop(AbcNode& loop, std::uint32_t header);
  bool structure_switch(std::uint32_t b, std::vector<AbcNode>& out, std::uint32_t& next);
  bool structure_try(TryGroup& group, std::vector<AbcNode>& out, std::uint32_t& next);
  bool catch_prologue(std::uint32_t b, std::uint32_t exception) const;
};

bool contains_continue(const std::vector<AbcNode>& nodes);
void trim_tail_continue(std::vector<AbcNode>& nodes);

}  // namespace fo4::abc::detail
