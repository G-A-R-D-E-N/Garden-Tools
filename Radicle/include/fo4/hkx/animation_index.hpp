#pragma once

#include "fo4/archive/assets.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class AnimationKind : std::uint8_t { animation, behaviour, project, character, skeleton, other };

std::string_view animation_kind_name(AnimationKind kind);
std::optional<AnimationKind> animation_kind_from_name(std::string_view name);

struct AnimationProvider {
  std::string label;
  bool archive{false};
  std::filesystem::path source;
  std::filesystem::path file{};
};

struct AnimationIndexEntry {
  std::string path;
  AnimationKind kind{AnimationKind::other};
  std::string project;
  std::string race;
  std::string character;
  std::string folder;
  std::string actor_root;
  std::uint64_t size{0};
  AnimationProvider winner;
  std::vector<AnimationProvider> shadowed;
};

struct AnimationModel {
  std::string path;
  AnimationProvider provider;
};

struct AnimationIndex {
  // Invariant: entries are sorted by path and each path appears once, holding its winning provider and the providers it shadows.
  std::vector<AnimationIndexEntry> entries;
  // Invariant: models are the winning .nif files under an actors folder, sorted by path, so an actor match never rescans the disk.
  std::vector<AnimationModel> models;
  std::vector<std::string> notes;
};

struct AnimationIndexFilter {
  std::optional<AnimationKind> kind;
  std::string project;
  std::string race;
  std::string character;
  std::string query;
};

struct AnimationIndexFacets {
  std::vector<std::string> projects;
  std::vector<std::string> races;
  std::vector<std::string> characters;
};

struct AnimationActorAssets {
  std::string actor_root;
  std::optional<std::string> skeleton;
  std::optional<AnimationProvider> skeleton_provider;
  std::vector<std::string> skeleton_candidates;
  std::optional<std::string> skeleton_nif;
  std::optional<AnimationProvider> skeleton_nif_provider;
  std::vector<std::string> body_candidates;
  std::size_t body_candidate_total{0};
  std::optional<std::string> body;
  std::optional<AnimationProvider> body_provider;
  std::vector<std::string> notes;
};

// Constraint: the key is lowercased with forward slashes, and the kind, project, race and character come from the folders around the role folder.
AnimationIndexEntry classify_animation_path(std::string_view path);

// Requires a resolver whose roots are ordered highest priority first and whose archives are ordered lowest priority first.
AnimationIndex build_animation_index(const archive::AssetResolver& resolver);

// Invariant: the sources come back in winning order, loose roots first and then archives from the last one opened.
std::vector<AnimationProvider> animation_index_sources(const archive::AssetResolver& resolver);

AnimationProvider animation_provider(const archive::AssetResolver& resolver, const archive::AssetLocation& location);

std::vector<std::size_t> filter_animation_index(const AnimationIndex& index, const AnimationIndexFilter& filter);

AnimationIndexFacets animation_index_facets(const AnimationIndex& index);

// Invariant: an actor folder with several skeleton or body candidates reports them and picks none.
AnimationActorAssets resolve_animation_actor(const AnimationIndex& index, std::string_view entry_path);

struct AnimationAssetBytes {
  std::string path;
  AnimationProvider provider;
  std::filesystem::path loose_path;
  std::uint64_t size{0};
  std::vector<std::byte> bytes;
};

// Requires a path ending in .hkx, .hkt or .nif and throws archive::AssetPathError for any other extension or an unsafe path; returns nullopt when no source holds it.
// Invariant: the winner is the one locate picks, so it is the provider the index names, and nothing is written to disk.
// Invariant: bytes is filled only for an archive winner; a loose winner is named by loose_path and size, and a file over 256 MiB throws std::runtime_error.
std::optional<AnimationAssetBytes> read_animation_asset(const archive::AssetResolver& resolver, std::string_view path);

}  // namespace fo4::hkx
