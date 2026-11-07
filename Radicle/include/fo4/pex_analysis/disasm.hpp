#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/cfg.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::pex_analysis {

struct PexDisasmOptions {
  bool structured{true};
};

struct PexDisasmResult {
  PexAnalysisStatus status{PexAnalysisStatus::Ok};
  bool structured{false};
  std::size_t errorInstruction{kNoIndex};
  std::int64_t errorTarget{0};
  std::string text;
};

PexDisasmResult disassemble_function(const fo4::pex::PexFunction& function,
                                     const PexDisasmOptions& options = {});

}
