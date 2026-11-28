#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Invariant: the FO4 skin record holds four slots per vertex, so an influence list never exceeds four.
inline constexpr std::size_t k_max_influences_per_vertex = 4;

struct Influence {
  std::int32_t bone{0};
  float weight{0.0f};
};

// Requires a skinned shape and a vertex inside it; returns only positive slots, heaviest first.
std::vector<Influence> influences_of(const Shape& shape, std::uint32_t vertex);

struct BoneInfluence {
  std::int32_t bone{-1};
  std::string name;
  std::vector<std::uint32_t> vertices;
  float weight{0.0f};
  // Invariant: these bounds cover only the listed vertices, so a bone that moves nothing keeps zeroes.
  std::array<float, 3> min{};
  std::array<float, 3> max{};
  bool any() const noexcept { return !vertices.empty(); }
};

// Requires a shape; lists the vertices one bone moves at or above the minimum, in shape-local space.
BoneInfluence influenced_by(const Shape& shape, std::int32_t bone, float minimum = 0.01f);

// Invariant: partner holds one entry per bone, and a bone no opposite name matched points at itself.
struct SkeletonPairs {
  std::vector<std::int32_t> partner;
  std::vector<std::string> ambiguous;
  std::vector<std::string> unconfirmed;
  std::size_t pairs{0};
};

// Requires bone names; pairs left/right names at a word start, the way BGS SkeletonMirror.Of does.
SkeletonPairs skeleton_pairs(std::span<const std::string> names);

enum class SkinEditKind : std::uint8_t { set_influences, copy_bone, mirror_weights };

std::optional<SkinEditKind> parse_skin_edit_kind(std::string_view name) noexcept;
const char* skin_edit_kind_name(SkinEditKind kind) noexcept;

struct SkinEditRequest {
  SkinEditKind kind{SkinEditKind::set_influences};
  std::uint32_t block{0};
  // Invariant: only set_influences reads vertex and influences, so the other kinds leave them empty.
  std::uint32_t vertex{0};
  std::vector<Influence> influences;
  // Invariant: only copy_bone reads these three, so a mirror request leaves the bone fields at -1.
  std::int32_t from_bone{-1};
  std::int32_t to_bone{-1};
  bool keep_source{false};
  // Invariant: only mirror_weights reads the lane, the side and the two tolerances.
  std::uint8_t lane{0};
  bool from_positive_side{true};
  float vertex_tolerance{0.05f};
  float bone_tolerance{0.0f};
};

struct SkinEditReport {
  std::uint32_t block{0};
  std::size_t vertices_changed{0};
  float max_weight_change{0.0f};
  // Invariant: these four counters explain every vertex the mirror skipped, so they sum with mirrored.
  std::size_t mirrored{0};
  std::size_t no_partner_vertex{0};
  std::size_t ambiguous_vertex{0};
  std::size_t no_partner_bone{0};
  std::size_t unconfirmed_bone{0};
  float worst{0.0f};
  // Invariant: each entry names a bone the pairing declined, so a short mirror explains itself.
  std::vector<std::string> skipped;
};

// Requires a skinned tri-shape block and throws std::invalid_argument for a malformed request.
// Requires a request the skin can apply, or throws std::runtime_error, as when nothing would change.
std::vector<std::byte> apply_skin_edit(const NifFile& nif, const SkinEditRequest& request,
                                       SkinEditReport* report = nullptr);

}  // namespace fo4::nif
