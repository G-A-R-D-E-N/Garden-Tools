#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

// Constraint: lanes 3 and 11 are padding the file stores and the writer must keep, so equality compares every lane bit for bit.
struct HkxMapperTransform {
  static constexpr std::size_t kLaneCount = 12;
  std::array<float, kLaneCount> lanes{};

  static HkxMapperTransform identity() {
    HkxMapperTransform out;
    out.lanes[7] = 1.0f;
    out.lanes[8] = 1.0f;
    out.lanes[9] = 1.0f;
    out.lanes[10] = 1.0f;
    return out;
  }
  std::array<float, 3> translation() const { return {lanes[0], lanes[1], lanes[2]}; }
  std::array<float, 4> rotation() const { return {lanes[4], lanes[5], lanes[6], lanes[7]}; }
  std::array<float, 3> scale() const { return {lanes[8], lanes[9], lanes[10]}; }
};

inline bool operator==(const HkxMapperTransform& a, const HkxMapperTransform& b) {
  return std::memcmp(a.lanes.data(), b.lanes.data(), sizeof a.lanes) == 0;
}

struct HkxMapperSimpleRow {
  std::int16_t bone_a{0};
  std::int16_t bone_b{0};
  HkxMapperTransform a_from_b = HkxMapperTransform::identity();
  bool operator==(const HkxMapperSimpleRow&) const = default;
};

struct HkxMapperChainRow {
  std::int16_t start_bone_a{0};
  std::int16_t end_bone_a{0};
  std::int16_t start_bone_b{0};
  std::int16_t end_bone_b{0};
  HkxMapperTransform start_a_from_b = HkxMapperTransform::identity();
  HkxMapperTransform end_a_from_b = HkxMapperTransform::identity();
  bool operator==(const HkxMapperChainRow&) const = default;
};

struct HkxSkeletonMapper {
  // Constraint: the three indices are positions in the packfile's object order, so they must name the same objects on write.
  std::size_t object_index{0};
  std::size_t skeleton_a_object{0};
  std::size_t skeleton_b_object{0};
  HkxSkeleton skeleton_a;
  HkxSkeleton skeleton_b;
  std::int32_t mapping_type{0};
  bool keep_unmapped_local{false};
  std::vector<std::int16_t> unmapped_bones;
  HkxMapperTransform extracted_motion = HkxMapperTransform::identity();
  std::vector<HkxMapperSimpleRow> rows;
  std::vector<HkxMapperChainRow> chains;
};

// Constraint: every hkaSkeletonMapper must decode whole, so one unreadable member fails the read instead of being dropped.
// Constraint: a file with no mapper is MissingObject, so a caller never edits an empty list by accident.
HkxStatus read_skeleton_mappers(const HkxPackfile& packfile, std::vector<HkxSkeletonMapper>& out);

// Constraint: `edited` must be the list read from `source` with values changed: same count, order and skeleton pointers.
// Constraint: simple rows resize through a relocated run; chain rows, unmapped bones and extracted motion never resize or change.
// Constraint: a refusal must leave `out` untouched and, when `refusal` is given, fill it with the reason.
HkxStatus write_skeleton_mappers(std::span<const std::byte> source, const std::vector<HkxSkeletonMapper>& edited,
                                 std::vector<std::byte>& out, std::string* refusal = nullptr);

struct HkxMappingPair {
  std::int16_t bone_a{0};
  std::int16_t bone_b{0};
};

// Constraint: each authoring function returns an empty string on success and the refusal text otherwise.
// Constraint: a skeleton must have a finite unit-rotation reference pose, scale one and every parent before its child.
std::string check_mapping_pairs(const HkxSkeleton& skeleton_a, const HkxSkeleton& skeleton_b,
                                std::span<const HkxMappingPair> pairs);

// Constraint: a row must map A to B as inverse(world(A)) * world(B) in the reference pose, with scale one.
std::string build_skeleton_mapping(const HkxSkeleton& skeleton_a, const HkxSkeleton& skeleton_b,
                                   std::span<const HkxMappingPair> pairs, std::vector<HkxMapperSimpleRow>& out);

std::string reverse_mapping(std::span<const HkxMapperSimpleRow> rows, std::vector<HkxMapperSimpleRow>& out);

// Constraint: only the translation and rotation lanes of `transform` are replaced, so its scale and padding survive.
std::string recompute_mapping_row(const HkxSkeleton& skeleton_a, std::int16_t bone_a, const HkxSkeleton& skeleton_b,
                                  std::int16_t bone_b, HkxMapperTransform& transform);

enum class HkxMapperEditKind : std::uint8_t {
  SetRowBones = 0,
  SetRowTransform = 1,
  SetRowPose = 2,
  InsertRow = 3,
  InsertRowWithTransform = 4,
  RemoveRow = 5,
  SetChainBones = 6,
  SetChainTransforms = 7,
  SetMappingType = 8,
  SetKeepUnmappedLocal = 9,
  RecomputeRow = 10,
};

struct HkxMapperEdit {
  HkxMapperEditKind kind{HkxMapperEditKind::SetRowBones};
  std::size_t row{0};
  // Constraint: chain edits read bone_a/bone_b as the start bones and end_bone_a/end_bone_b as the end bones.
  std::int16_t bone_a{0};
  std::int16_t bone_b{0};
  std::int16_t end_bone_a{0};
  std::int16_t end_bone_b{0};
  // Constraint: SetRowPose requires only the translation and rotation lanes of `transform`; chain edits use both transforms.
  HkxMapperTransform transform = HkxMapperTransform::identity();
  HkxMapperTransform end_transform = HkxMapperTransform::identity();
  std::int32_t mapping_type{0};
  bool flag{false};
  bool allow_duplicate_pair{false};
};

struct HkxMapperEditReport {
  bool changed{false};
  std::string refusal;
};

// Constraint: the edit must keep bones in range, transforms finite with a unit rotation, and mapping types 0 or 1.
// Constraint: a new duplicate pairing is refused, measured against `mapper` as given, because no saved copy exists here.
// Constraint: a refused edit must leave `out` untouched.
HkxMapperEditReport apply_mapper_edit(const HkxSkeletonMapper& mapper, const HkxMapperEdit& edit,
                                      HkxSkeletonMapper& out);

}  // namespace fo4::hkx
