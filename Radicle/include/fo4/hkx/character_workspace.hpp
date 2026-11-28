#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"
#include "fo4/hkx/project_chain.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_document.hpp"
#include "fo4/nif/skin_binding.hpp"
#include "fo4/nif/skin_validate.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class CharacterLevel : std::uint8_t { error, warning, info };

const char* character_level_name(CharacterLevel level) noexcept;

struct CharacterFinding {
  CharacterLevel level{CharacterLevel::error};
  // Invariant: area is one of skeleton, ragdoll, mapper, skin, animation, save, so a consumer can group findings without parsing text.
  std::string area;
  std::string where;
  std::string message;
};

// Constraint: findings and wording follow BGS SkeletonValidator.Check; reads only.
std::vector<CharacterFinding> validate_skeleton(const HkxSkeleton& skeleton);

struct SkeletonMirrorReport {
  std::size_t pairs{0};
  std::vector<std::string> ambiguous;
  std::vector<std::string> unconfirmed;
  // Invariant: lane is 0 for X, 1 for Y, 2 for Z, and -1 when the rig has no pair or no pair had a pose.
  std::int32_t lane{-1};
  float typical{0.0f};
  float worst{0.0f};
};

// Constraint: BGS "Inspect mirror pairs": the pairing of SkeletonMirror.Of and the axis of SkeletonMirror.MirrorAxis.
SkeletonMirrorReport inspect_mirror_pairs(const HkxSkeleton& skeleton);

// Invariant: a span is "not given" when empty; ragdoll and mapper may view the same bytes as skeleton, and an empty one falls back to them.
struct CharacterAssets {
  std::span<const std::byte> skeleton;
  std::span<const std::byte> ragdoll;
  std::span<const std::byte> mapper;
  std::span<const std::byte> body_nif;
  std::span<const std::byte> skeleton_nif;
  std::string skeleton_nif_label{"skeleton.nif"};
  // Invariant: the animation links are checked only when both are given; project_hkx is a data-relative path below meshes.
  const archive::AssetResolver* assets{nullptr};
  std::string project_hkx;
};

enum class CharacterRepairKind : std::uint8_t { normalize_weights, set_influences };

const char* character_repair_kind_name(CharacterRepairKind kind) noexcept;

// Invariant: a repair is a suggestion naming a BGS weight operation; nothing is applied by the report.
struct CharacterSkinRepair {
  CharacterRepairKind kind{CharacterRepairKind::normalize_weights};
  std::uint32_t block{0};
  // Invariant: the first vertices the repair applies to; total counts all of them.
  std::vector<std::uint32_t> vertices;
  std::size_t total{0};
  std::string reason;
};

struct CharacterSkinShape {
  nif::SkinBindingReport binding;
  nif::SkinReport skin;
  // Invariant: vertices with a positive weight on a bone that matches no skeleton bone, no helper node and no _skin guess.
  std::size_t unbound_vertices{0};
  std::vector<CharacterSkinRepair> repairs;
};

struct CharacterRagdollSummary {
  bool present{false};
  std::string status;
  std::string headline;
  std::size_t bodies{0};
  std::size_t shapes{0};
  std::size_t constraints{0};
  std::size_t bone_bindings{0};
  std::size_t physics_bones{0};
  std::size_t animation_bones{0};
};

struct CharacterMapperSummary {
  std::size_t object_index{0};
  std::string skeleton_a;
  std::string skeleton_b;
  std::size_t bones_a{0};
  std::size_t bones_b{0};
  std::size_t rows{0};
  std::size_t chains{0};
  bool a_has_character_bones{false};
  bool b_has_character_bones{false};
};

struct CharacterChainSummary {
  bool checked{false};
  std::string root;
  std::size_t links{0};
  std::size_t animations{0};
  std::size_t missing_animations{0};
  std::vector<std::string> problems;
  std::vector<std::string> warnings;
};

struct CharacterReport {
  // Invariant: empty when the skeleton was read; otherwise the reason it was not, and nothing else is filled.
  std::string refusal;
  std::string skeleton_name;
  std::size_t bones{0};
  SkeletonMirrorReport mirror;
  CharacterRagdollSummary ragdoll;
  std::vector<CharacterMapperSummary> mappers;
  CharacterChainSummary chain;
  std::vector<CharacterSkinShape> shapes;
  std::vector<nif::SkinShapeRefusal> skin_refused;
  std::size_t unskinned_shapes{0};
  // Invariant: empty when the skin was bound; otherwise the status name of the refusal (no_skinned_shape, parse_error).
  std::string skin_status;
  std::vector<CharacterFinding> findings;
  std::size_t errors() const noexcept;
  std::size_t warnings() const noexcept;
  bool ok() const noexcept { return refusal.empty() && errors() == 0; }
};

// Constraint: reads only; a document that cannot be read is a finding, so one broken file never hides the others.
// Constraint: renaming or adopting a skin bone has no verified write path, so it is never suggested.
CharacterReport check_character(const CharacterAssets& assets);

enum class CharacterDocumentKind : std::uint8_t { skeleton, ragdoll, mapper, skin };

const char* character_document_kind_name(CharacterDocumentKind kind) noexcept;

struct CharacterSaveDocument {
  CharacterDocumentKind kind{CharacterDocumentKind::skeleton};
  std::string source;
  // Invariant: empty means in place, which only a skeleton save allows and only with CharacterSaveInput::overwrite.
  std::string target;
};

struct CharacterSaveInput {
  std::vector<CharacterSaveDocument> documents;
  // Invariant: with skeleton bytes and edits the plan also counts the references an index-changing edit would leave behind.
  std::span<const std::byte> skeleton;
  std::span<const HkxSkeletonEdit> skeleton_edits;
  std::span<const std::byte> ragdoll;
  std::span<const std::byte> mapper;
  // Invariant: false refuses any document whose target is its own source file, an empty skeleton target included.
  bool overwrite{false};
};

// Constraint: weakly_canonical form, ASCII lower case, '/' separators; two names for one file give one key.
std::string character_path_key(std::string_view path);
// Constraint: equal keys, or both files exist and std::filesystem::equivalent says so.
bool character_same_file(std::string_view a, std::string_view b);

struct CharacterSaveStep {
  std::size_t document{0};
  CharacterDocumentKind kind{CharacterDocumentKind::skeleton};
  std::string reads;
  std::string writes;
  // Invariant: true when reads is the previous step's output, so the caller must take that file's hash again.
  bool reads_previous_output{false};
};

struct CharacterSavePlan {
  // Invariant: skeleton, ragdoll, mapper, skin in that order, one step per document.
  std::vector<CharacterSaveStep> steps;
  std::vector<CharacterFinding> findings;
  std::size_t shifted_references{0};
  bool ok() const noexcept;
};

// Constraint: orders and checks the saves of the existing writers; it never writes and there is no combined writer.
CharacterSavePlan plan_character_save(const CharacterSaveInput& input);

}  // namespace fo4::hkx
