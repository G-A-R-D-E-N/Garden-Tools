#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::archive {
class AssetResolver;
}

namespace fo4::hkx {

// Constraint: the id must be CRC-32 (reflected 0xEDB88320, init 0, no final xor) of the lowercased prefixes joined by '|' in the high half and the lowercased behaviour path in the low half; every shipped id recomputes this way.
std::string subgraph_normalize(std::string_view path);
std::uint32_t subgraph_raw_crc32(std::string_view text);
std::uint32_t subgraph_behavior_half(std::string_view behavior_path);
std::uint64_t subgraph_id(std::string_view behavior_path, std::span<const std::string> prefixes);

// Constraint: the manifest's id must be its third line; a path under \Behaviors\ is a behaviour and any other path an animation, with the first path standing in when none is named.
struct AnimationFileData {
  std::uint64_t id{0};
  std::vector<std::string> behavior_paths{};
  std::vector<std::string> animation_paths{};
};

std::optional<AnimationFileData> parse_animation_file_data(std::string_view text);

// Constraint: the third line's id must be readable on its own, so a manifest that names no path is a manifest of that id naming nothing, not an absent one.
std::optional<std::uint64_t> animation_file_data_id(std::string_view text);

// Constraint: an AnimationOffsets dump must start "V4" and carry one length-prefixed path at that offset; it names the behaviour a manifest may omit.
std::optional<std::string> parse_animation_offset_hint(std::span<const std::byte> bytes);

struct RaceSubgraph {
  std::string behavior_graph{};
  std::vector<std::string> path_prefixes{};
  std::uint64_t id{0};
};

struct RaceAnimationSet {
  std::vector<std::string> behavior_graphs{};  // RACE SGNM; ordering matters
  std::vector<std::string> path_prefixes{};    // RACE SAPT; ordering matters
  // Constraint: an entry must exist only for a record naming one behaviour graph; with several the record does not group prefixes, so this stays empty rather than inventing an id.
  std::vector<RaceSubgraph> subgraphs{};
};

// Constraint: a record that is not a RACE must yield empty lists, so a caller can report "not a race" without a second read.
RaceAnimationSet race_animation_set(const Record& race);

struct SubgraphManifestText {
  std::string text{};
  std::string source{};  // "loose" or the archive file name
};

// Constraint: the path must be the lowercase backslash form below Data's meshes folder, the same text FO4IDE reports as the manifest path.
std::string subgraph_manifest_path(std::uint64_t id);

// Constraint: a loose file must win over an archive; an unreadable loose file falls through to the archives, and any read error reads as "not found".
std::optional<SubgraphManifestText> read_subgraph_manifest(const archive::AssetResolver& assets, std::uint64_t id);

// Constraint: a path must be relative to meshes and may omit ".hkx"; only a file counts, never a folder of that name.
bool animation_exists(const archive::AssetResolver& assets, std::string_view path);

// Invariant: a lookup that throws is reported through `error` and reads as not found, so an unreadable archive is never counted as an absent clip.
bool animation_lookup(const archive::AssetResolver& assets, std::string_view path, std::string& error);

}  // namespace fo4::hkx
