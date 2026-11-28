#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/lod/object_merge.hpp"
#include "fo4/lod/terrain_heightfield.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::lod {

enum class LodCandidateSkip : std::uint8_t {
  missing_position,
  unsupported_base,
  no_lod_model,
  // Invariant: a base type that never carries object LOD (ACTI, FURN, MSTT, NPC) is not an unsupported base.
  not_lod_base,
  // Invariant: a MultirefLOD child whose linked target has its own mesh at the level is drawn by that target.
  multiref_child,
  malformed,
};

// Invariant: malformed must stay the last enumerator, the count derives from it.
inline constexpr std::size_t k_lod_candidate_skip_count =
    static_cast<std::size_t>(LodCandidateSkip::malformed) + 1;

// Invariant: the class names the base record that supplied the mesh.
enum class LodCandidateClass : std::uint8_t { stat, scol_part };

struct LodObjectCandidate {
  FormKey reference;
  FormKey base;
  std::array<float, 3> position{};
  std::array<float, 3> rotation{};
  float scale{1.0f};
  std::string mesh;
  std::int32_t cell_x{0};
  std::int32_t cell_y{0};
  LodCandidateClass class_tag{LodCandidateClass::stat};
  // Invariant: a collection member carries the composed placement matrix, not an Euler triple.
  std::optional<std::array<float, 9>> rotation_basis;
  // Invariant: the runtime FormID of a flagged enable parent, 0 when the object always shows; the tile groups by it.
  std::uint32_t toggle_ref{0};
  // Invariant: set for a reference of the worldspace's persistent cell, which vanilla appends before the loaded cells' references.
  bool persistent{false};
  // Invariant: the reference's material swap as (original, replacement) pairs, applied to a LOD shape whose material matches.
  std::vector<std::pair<std::string, std::string>> material_swap;
};

struct LodCandidateCounts {
  std::size_t references_seen{0};
  std::size_t included{0};
  std::array<std::size_t, k_lod_candidate_skip_count> skipped{};
};

struct LodCandidateCollection {
  std::vector<LodObjectCandidate> candidates;
  LodCandidateCounts counts;
  // Invariant: the world's LAND rides with its candidates, so a preview and a generate cull below terrain alike.
  std::shared_ptr<const LandGrids> terrain;
};

// Constraint: a caller that builds several levels passes one LandGrids, otherwise each call decodes the LAND again.
LodCandidateCollection collect_object_lod_candidates(const env::Environment& environment,
                                                     const FormKey& worldspace,
                                                     std::int32_t level,
                                                     std::shared_ptr<const LandGrids> land = nullptr);

// Invariant: candidates and the tile writer share this placement matrix, so a part and a plain reference turn alike.
std::array<float, 9> lod_placement_basis(const std::array<float, 3>& rotation);

std::vector<LodObject> to_lod_objects(const LodCandidateCollection& collection);

std::string_view lod_candidate_skip_name(LodCandidateSkip reason);

std::string_view lod_candidate_class_name(LodCandidateClass value);

}  // namespace fo4::lod
