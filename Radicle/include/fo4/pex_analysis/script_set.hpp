#pragma once

#include "fo4/archive/assets.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::pex_analysis {

enum class ScriptSourceKind : std::uint8_t { archive = 0, loose = 1 };

struct ScriptCandidate {
  std::string name;
  ScriptSourceKind kind{ScriptSourceKind::archive};
  std::string container;
  std::int32_t load_index{0};
  std::uint64_t size{0};
};

struct ScriptWinner {
  ScriptCandidate winner;
  std::size_t index{0};
  std::size_t replaced{0};
};

std::string normalize_script_name(std::string_view data_relative_path);
// Requires resolver roots that do not overlap.
std::vector<ScriptCandidate> enumerate_script_candidates(const fo4::archive::AssetResolver& resolver);
std::vector<ScriptWinner> resolve_script_set(std::span<const ScriptCandidate> candidates);

}  // namespace fo4::pex_analysis
