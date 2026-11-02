#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"
#include "fo4/schema/vmad.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::pex_analysis {

struct PexFragmentTarget {
  std::uint16_t stage{0};
  std::int32_t stageIndex{0};
  std::string script;
  std::string function;
  bool scriptFound{false};
  bool functionFound{false};
  bool nativeTarget{false};
  PexFunctionRef target;
};

std::vector<PexFragmentTarget> resolve_fragments(
    const fo4::pex::PexFile& file,
    std::span<const fo4::QuestVmadFragment> fragments);

}
