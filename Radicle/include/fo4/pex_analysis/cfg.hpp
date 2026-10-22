#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/opcodes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::pex_analysis {

enum class PexAnalysisStatus {
  Ok = 0,
  MalformedInstruction,
  JumpTargetOutOfRange,
};

std::string pex_analysis_status_text(PexAnalysisStatus status);

struct PexBasicBlock {
  std::size_t first{0};
  std::size_t count{0};
  std::vector<std::size_t> successors;
  std::vector<std::size_t> predecessors;
  bool reachable{false};
  bool isLoopHeader{false};
};

struct PexLoop {
  std::size_t headerBlock{kNoIndex};
  std::size_t latchBlock{kNoIndex};
  std::size_t backEdgeInstruction{kNoIndex};
  std::size_t headerInstruction{kNoIndex};
};

struct PexFunctionCfg {
  PexAnalysisStatus status{PexAnalysisStatus::Ok};
  std::size_t errorInstruction{kNoIndex};
  std::int64_t errorTarget{0};
  std::size_t exitBlock{kNoIndex};
  std::vector<PexBasicBlock> blocks;
  std::vector<PexLoop> loops;
  std::vector<std::size_t> unreachableBlocks;
};

PexFunctionCfg build_cfg(const fo4::pex::PexFunction& function);

std::size_t cfg_block_of(const PexFunctionCfg& cfg, std::size_t instructionIndex);

}
