#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: exact and node_chain are bindings, skin_parent is a placement guess, and unmatched moves with nothing.
enum class SkinBoneMatch : std::uint8_t { exact, node_chain, skin_parent, unmatched };

const char* skin_bone_match_name(SkinBoneMatch match) noexcept;

struct SkinBoneBinding {
  std::string name;
  SkinBoneMatch match{SkinBoneMatch::unmatched};
  // Invariant: -1 exactly when match is unmatched, otherwise an index into the skeleton bone list.
  std::int32_t skeleton_bone{-1};
  // Invariant: via and source name the node and the node source of a node_chain match, and stay empty otherwise.
  std::string via;
  std::string source;
  bool ambiguous{false};
  // Invariant: counts the vertices that give this bone a weight above zero, or a non-finite one, once per vertex.
  std::size_t weighted_vertices{0};
};

struct SkinBindingReport {
  std::uint32_t block{0};
  std::string name;
  std::vector<SkinBoneBinding> bones;
  std::size_t exact{0};
  std::size_t node_chain{0};
  std::size_t skin_parent{0};
  std::size_t unmatched{0};
  std::size_t ambiguous{0};
  std::size_t vertices{0};
  // Invariant: a vertex is fully bound when every positive-weight slot lands on an exact bone.
  std::size_t fully_bound_vertices{0};
  // Invariant: empty when every bone is exact or node_chain and none is ambiguous.
  std::string warning;
  bool complete() const noexcept { return !bones.empty() && unmatched == 0 && skin_parent == 0; }
};

// Invariant: a node source is a NIF whose node tree may bridge a skin bone to a skeleton bone; nif must outlive the call.
struct SkinNodeSource {
  const NifFile* nif{nullptr};
  std::string label;
};

// Requires a skinned shape from read_shapes; throws std::invalid_argument for a shape that is unskinned, lost its skin or has no four-slot weights.
// Invariant: names compare ASCII case-insensitively and the first skeleton bone of a repeated name wins; nothing is written.
SkinBindingReport bind_skin_to_skeleton(const Shape& shape, std::span<const std::string> skeleton_bones,
                                        std::span<const SkinNodeSource> node_sources = {});

enum class SkinShapesStatus : std::uint8_t { ok, invalid_block, not_a_shape, refused, parse_error, no_skinned_shape };

const char* skin_shapes_status_name(SkinShapesStatus status) noexcept;

struct SkinShapeRefusal {
  std::uint32_t block{0};
  std::string name;
  std::string reason;
};

struct SkinShapesBinding {
  SkinShapesStatus status{SkinShapesStatus::ok};
  // Invariant: empty when status is ok; otherwise the reason for the refusal.
  std::string message;
  std::vector<SkinBindingReport> shapes;
  // Invariant: filled only when every shape is asked for; a single refused block sets status instead.
  std::vector<SkinShapeRefusal> refused;
  std::size_t unskinned{0};
};

// Requires nif to outlive the call; with a block only that shape is bound, without one every shape block is bound.
// Invariant: a shape that cannot be read or bound never fails the other shapes when no block is given; nothing is written.
SkinShapesBinding bind_skin_shapes(const NifFile& nif, std::optional<std::uint32_t> block,
                                   std::span<const std::string> skeleton_bones,
                                   std::span<const SkinNodeSource> node_sources = {});

}  // namespace fo4::nif
