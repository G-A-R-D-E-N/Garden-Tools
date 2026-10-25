#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::pex_analysis {

inline constexpr std::size_t k_max_parent_depth = 64;

enum class PexChainProblem : std::uint8_t {
  MissingParent = 0,
  Cycle = 1,
  TooDeep = 2,
};

struct PexChainFinding {
  std::size_t fileIndex{kNoIndex};
  std::size_t objectIndex{kNoIndex};
  std::string objectName;
  PexChainProblem problem{PexChainProblem::MissingParent};
  std::size_t depth{0};
  std::vector<std::string> chain;  // invariant: the parent names walked, seed's parent first
};

struct PexChainReport {
  std::vector<PexChainFinding> findings;
  std::size_t checkedObjects{0};
};

// Invariant: a parent name in seedTypes ends its chain, because the caller knows that class outside the set.
PexChainReport validate_parent_chains(std::span<const fo4::pex::PexFile* const> files,
                                      std::span<const std::string> seedTypes,
                                      std::size_t depthLimit = k_max_parent_depth);

}  // namespace fo4::pex_analysis
