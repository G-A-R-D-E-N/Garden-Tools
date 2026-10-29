#pragma once

#include "fo4/archive/assets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::pex_analysis {

enum class ScriptSourceKind : std::uint8_t { archive = 0, loose = 1 };

struct ScriptCandidate {
  std::string name;  // invariant: lower case, relative to Scripts/, no ".pex"
  ScriptSourceKind kind{ScriptSourceKind::archive};  // invariant: matches location.origin
  std::string container;  // invariant: the loose payload path or the archive file path
  std::int32_t load_index{0};
  std::uint64_t size{0};
  std::string path;  // invariant: normalized data-relative path, ".pex" kept; empty for a caller-built candidate
  fo4::archive::AssetLocation location{};  // invariant: names the payload this candidate stands for; empty for a caller-built candidate
};

struct ScriptWinner {
  ScriptCandidate winner;
  std::size_t index{0};
  std::size_t replaced{0};
};

std::string normalize_script_name(std::string_view data_relative_path);
// Requires a "scripts/" entry whose name before ".pex" is a safe relative path; anything else yields nothing.
std::optional<std::string> script_name_from_entry(std::string_view entry_path);
// Requires one load index per resolver root, or none to take the root order as high to low priority.
// Requires resolver roots that do not overlap.
std::vector<ScriptCandidate> enumerate_script_candidates(const fo4::archive::AssetResolver& resolver,
                                                         std::span<const std::int32_t> root_load_index = {});
// Invariant: loose beats archive, then a higher load index wins, then the ordinally greater container path, and an exact tie keeps the first seen.
std::vector<ScriptWinner> resolve_script_set(std::span<const ScriptCandidate> candidates);
// Requires one load index per resolver root, or none to take the root order as high to low priority.
std::vector<ScriptWinner> resolve_script_set(const fo4::archive::AssetResolver& resolver,
                                             std::span<const std::int32_t> root_load_index = {});

}  // namespace fo4::pex_analysis
