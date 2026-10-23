#pragma once

#include "fo4/archive/assets.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

struct ScriptCandidate {
  std::string name;  // invariant: lower case, relative to Scripts/, no ".pex"
  std::string path;  // invariant: normalized data-relative path, ".pex" kept
  archive::AssetLocation location;
  std::string container;  // invariant: the loose root or archive file the payload came from
  std::int32_t load_index{0};
  std::uint64_t size{0};
};

struct ScriptWinner {
  std::string name;
  ScriptCandidate winner;
  std::uint32_t replaced{0};  // invariant: the losing candidates this winner replaced
};

// Invariant: a name is a lower case path under Scripts/ without the ".pex" extension.
std::string normalize_script_name(std::string_view relative_path);

// Requires a "Scripts/**.pex" entry whose name is a safe relative path; anything else yields nothing.
std::optional<std::string> script_name_from_entry(std::string_view entry_path);

// Requires one load index per resolver root, or none to use the root order as high to low priority.
std::vector<ScriptCandidate> enumerate_script_candidates(const archive::AssetResolver& resolver,
                                                          std::span<const std::int32_t> root_load_index);

// Ordering: loose over archive, then a higher load index, then the ordinally greater container path.
std::vector<ScriptWinner> resolve_script_set(std::span<const ScriptCandidate> candidates);

std::vector<ScriptWinner> resolve_script_set(const archive::AssetResolver& resolver,
                                              std::span<const std::int32_t> root_load_index = {});

}  // namespace fo4::papyrus
