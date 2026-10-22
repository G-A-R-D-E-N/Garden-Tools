#include <fo4/pex_analysis/cfg.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>

namespace fo4::pex_analysis {

namespace {

void add_successor(PexBasicBlock& block, std::size_t target) {
  if (target == kNoIndex) return;
  if (std::find(block.successors.begin(), block.successors.end(), target) ==
      block.successors.end()) {
    block.successors.push_back(target);
  }
}

}

std::string pex_analysis_status_text(PexAnalysisStatus status) {
  switch (status) {
    case PexAnalysisStatus::Ok:
      return "ok";
    case PexAnalysisStatus::MalformedInstruction:
      return "malformed instruction";
    case PexAnalysisStatus::JumpTargetOutOfRange:
      return "jump target out of range";
  }
  return "unknown";
}

std::size_t cfg_block_of(const PexFunctionCfg& cfg, std::size_t instructionIndex) {
  if (cfg.blocks.empty()) return kNoIndex;
  const auto it = std::upper_bound(
      cfg.blocks.begin(), cfg.blocks.end(), instructionIndex,
      [](std::size_t value, const PexBasicBlock& block) {
        return value < block.first;
      });
  if (it == cfg.blocks.begin()) return kNoIndex;
  const auto index = static_cast<std::size_t>(it - cfg.blocks.begin() - 1);
  const auto& block = cfg.blocks[index];
  if (block.count == 0) return instructionIndex == block.first ? index : kNoIndex;
  return instructionIndex < block.first + block.count ? index : kNoIndex;
}

PexFunctionCfg build_cfg(const fo4::pex::PexFunction& function) {
  PexFunctionCfg cfg;
  const auto count = function.instructions.size();

  std::vector<std::size_t> leaders;
  leaders.push_back(0);
  leaders.push_back(count);

  for (std::size_t i = 0; i < count; ++i) {
    const auto& instruction = function.instructions[i];
    if (pex_is_jump(instruction.opCode)) {
      std::int64_t offset = 0;
      if (!pex_jump_offset(instruction, &offset)) {
        cfg.status = PexAnalysisStatus::MalformedInstruction;
        cfg.errorInstruction = i;
        return cfg;
      }
      const auto target = static_cast<std::int64_t>(i) + offset;
      if (target < 0 || target > static_cast<std::int64_t>(count)) {
        cfg.status = PexAnalysisStatus::JumpTargetOutOfRange;
        cfg.errorInstruction = i;
        cfg.errorTarget = target;
        return cfg;
      }
      leaders.push_back(static_cast<std::size_t>(target));
      leaders.push_back(i + 1);
      continue;
    }
    if (instruction.opCode == static_cast<std::uint8_t>(PexOp::Return)) leaders.push_back(i + 1);
  }

  std::sort(leaders.begin(), leaders.end());
  leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());

  cfg.blocks.resize(leaders.size());
  for (std::size_t b = 0; b + 1 < leaders.size(); ++b) {
    cfg.blocks[b].first = leaders[b];
    cfg.blocks[b].count = leaders[b + 1] - leaders[b];
  }
  cfg.blocks.back().first = leaders.back();
  cfg.exitBlock = cfg.blocks.size() - 1;

  for (auto& block : cfg.blocks) {
    if (block.count == 0) continue;
    const auto last = block.first + block.count - 1;
    const auto& instruction = function.instructions[last];
    if (instruction.opCode == static_cast<std::uint8_t>(PexOp::Return)) continue;

    const auto fallthrough = cfg_block_of(cfg, last + 1);
    if (pex_is_jump(instruction.opCode)) {
      std::int64_t offset = 0;
      if (!pex_jump_offset(instruction, &offset)) {
        cfg.status = PexAnalysisStatus::MalformedInstruction;
        cfg.errorInstruction = last;
        return cfg;
      }
      const auto target =
          static_cast<std::size_t>(static_cast<std::int64_t>(last) + offset);
      add_successor(block, cfg_block_of(cfg, target));
      if (pex_is_conditional_jump(instruction.opCode)) add_successor(block, fallthrough);
      continue;
    }
    add_successor(block, fallthrough);
  }

  for (std::size_t b = 0; b < cfg.blocks.size(); ++b) {
    for (const auto successor : cfg.blocks[b].successors) {
      cfg.blocks[successor].predecessors.push_back(b);
    }
  }

  std::vector<bool> visited(cfg.blocks.size(), false);
  std::deque<std::size_t> queue;
  visited[0] = true;
  queue.push_back(0);
  while (!queue.empty()) {
    const auto b = queue.front();
    queue.pop_front();
    for (const auto successor : cfg.blocks[b].successors) {
      if (!visited[successor]) {
        visited[successor] = true;
        queue.push_back(successor);
      }
    }
  }
  for (std::size_t b = 0; b < cfg.blocks.size(); ++b) {
    cfg.blocks[b].reachable = visited[b];
    if (!visited[b] && b != cfg.exitBlock) cfg.unreachableBlocks.push_back(b);
  }

  struct Frame {
    std::size_t block;
    std::size_t next;
  };

  std::vector<int> state(cfg.blocks.size(), 0);
  std::vector<Frame> stack;
  std::vector<std::pair<std::size_t, std::size_t>> backEdges;
  state[0] = 1;
  stack.push_back(Frame{0, 0});
  while (!stack.empty()) {
    const auto top = stack.size() - 1;
    if (stack[top].next >= cfg.blocks[stack[top].block].successors.size()) {
      state[stack[top].block] = 2;
      stack.pop_back();
      continue;
    }
    const auto successor = cfg.blocks[stack[top].block].successors[stack[top].next];
    ++stack[top].next;
    if (state[successor] == 1) {
      backEdges.emplace_back(stack[top].block, successor);
      continue;
    }
    if (state[successor] == 0) {
      state[successor] = 1;
      stack.push_back(Frame{successor, 0});
    }
  }

  std::sort(backEdges.begin(), backEdges.end());
  for (const auto& edge : backEdges) {
    PexLoop loop;
    loop.headerBlock = edge.second;
    loop.latchBlock = edge.first;
    loop.headerInstruction = cfg.blocks[edge.second].first;
    const auto& latch = cfg.blocks[edge.first];
    loop.backEdgeInstruction = latch.first + latch.count - 1;
    cfg.blocks[edge.second].isLoopHeader = true;
    cfg.loops.push_back(loop);
  }

  return cfg;
}

}
