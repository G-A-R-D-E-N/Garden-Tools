#pragma once

#include "fo4/hkx/character_workspace.hpp"
#include "fo4/hkx/ragdoll_edit.hpp"
#include "fo4/hkx/skeleton_document.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "fo4/nif/skin.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

// Constraint: moved from FO4IDE; an empty string means the list parsed, otherwise it names the failing edit by its 1-based position.
std::string parse_skeleton_edits(const nlohmann::json& edits, std::vector<HkxSkeletonEdit>& out);

// Constraint: one request is one skin edit object {kind, block, vertex, influences, from_bone, to_bone, keep_source, lane, from_positive_side}; empty string means it parsed.
std::string parse_skin_edit(const nlohmann::json& item, nif::SkinEditRequest& out);

// Constraint: moved from FO4IDE; the JSON object keys bodies, constraints, bone_map, shapes and materials are the ragdoll edit rows; empty string means it parsed.
std::string parse_ragdoll_edits(const nlohmann::json& source, HkxRagdollEdits& out);

struct MapperRowDelta {
  // Invariant: error is empty when the row could be recomputed; otherwise the other fields are not filled.
  std::string error;
  HkxMapperTransform recomputed = HkxMapperTransform::identity();
  float translation_delta{0.0f};
  // Invariant: NaN when the rotation lanes are not finite.
  float rotation_delta{0.0f};
};

// Constraint: works on a copy of the stored transform, so the read never changes what it reports.
MapperRowDelta mapper_row_delta(const HkxSkeletonMapper& mapper, const HkxMapperSimpleRow& row);

// Invariant: returns the refusal text, empty on success; `out` is empty when the edits changed nothing.
using CharacterTransform = std::function<std::string(std::span<const std::byte> in, std::vector<std::byte>& out)>;

CharacterTransform skeleton_transform(std::vector<HkxSkeletonEdit> edits);
CharacterTransform ragdoll_transform(HkxRagdollEdits edits);
CharacterTransform skin_transform(std::vector<nif::SkinEditRequest> edits);
// Requires `apply` to edit the mapper in place and return a refusal text, empty on success.
CharacterTransform mapper_transform(std::size_t mapper, std::function<std::string(HkxSkeletonMapper&)> apply);

struct CharacterSaveStage {
  CharacterDocumentKind kind{CharacterDocumentKind::skeleton};
  std::string source;
  // Invariant: empty means in place, which only a skeleton stage allows.
  std::string target;
  // Invariant: the sha256 hex of the source the caller last saw; empty skips the check.
  std::string expected_source_sha256;
  CharacterTransform transform;
};

struct CharacterSaveRequest {
  std::vector<CharacterSaveStage> stages;
  // Invariant: given only so the plan can count references an index-changing skeleton edit would leave behind.
  std::vector<HkxSkeletonEdit> skeleton_edits;
  bool overwrite{false};
  // Constraint: called for every target before anything is written; returns a refusal text, empty when the target may be written.
  std::function<std::string(const std::string& target, CharacterDocumentKind kind)> target_policy;
};

struct CharacterSaveStepResult {
  std::size_t stage{0};
  CharacterDocumentKind kind{CharacterDocumentKind::skeleton};
  std::string reads;
  std::string writes;
  bool changed{false};
  bool written{false};
  std::size_t bytes{0};
  std::string source_sha256;
  std::string output_sha256;
};

struct CharacterSaveResult {
  // Invariant: true only when every planned step finished and its file was read back equal to what was produced.
  bool saved{false};
  // Invariant: nonempty when nothing was written (a refusal before the first write) or a step failed.
  std::string refusal;
  // Invariant: set when a step failed after the earlier steps had already written; names that step.
  bool partial{false};
  std::size_t failed_stage{0};
  std::vector<std::string> written;
  // Invariant: the .bak copies made before the first write; a file that already had a .bak is not listed, its .bak is kept as it was.
  std::vector<std::string> backups;
  CharacterSavePlan plan;
  std::vector<CharacterSaveStepResult> steps;
};

// Constraint: the whole plan is planned, read, edited and verified in memory before the first write, so a refusal writes nothing.
// Constraint: a file the save replaces gets a .bak copy before the first write, and a failed copy refuses the save with nothing written.
// Constraint: each step is published atomically and read back; a failed step stops the save and the result lists the files already written.
CharacterSaveResult save_character(const CharacterSaveRequest& request);

}  // namespace fo4::hkx
