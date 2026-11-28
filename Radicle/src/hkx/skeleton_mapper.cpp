#include "fo4/hkx/skeleton_mapper.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::Elements;
using detail::elements_at;
using detail::in_section;
using detail::pointer_target;
using detail::value_at;

constexpr double kUnitTolerance = 1e-3;
constexpr double kScaleTolerance = 1e-3;
constexpr double kMinRotationLengthSquared = 1e-10;
constexpr double kRotationPresent = 1e-12;
constexpr std::size_t kTransformBytes = HkxMapperTransform::kLaneCount * sizeof(float);
constexpr std::size_t kArrayHeaderBytes = 16;
constexpr std::size_t kCountAt = 8;
constexpr std::size_t kCapacityAt = 12;
constexpr std::size_t kPointerBytes = 8;
constexpr std::size_t kRunAlign = 16;
constexpr std::size_t kMaxRows = std::size_t{1} << 20;
constexpr std::uint32_t kEmbeddedStorage = 0x80000000u;
constexpr std::uint32_t kCapacityFlags = 0xC0000000u;

struct MapperLayout {
  std::size_t mapper_size{0};
  std::size_t mapping{0};
  std::size_t skeleton_a{0};
  std::size_t skeleton_b{0};
  std::size_t simple_ranges{0};
  std::size_t simple{0};
  std::size_t chain{0};
  std::size_t unmapped{0};
  std::size_t extracted{0};
  std::size_t keep{0};
  std::size_t type{0};
  std::size_t simple_size{0};
  std::size_t simple_bone_a{0};
  std::size_t simple_bone_b{0};
  std::size_t simple_transform{0};
  std::size_t chain_size{0};
  std::size_t chain_start_a{0};
  std::size_t chain_end_a{0};
  std::size_t chain_start_b{0};
  std::size_t chain_end_b{0};
  std::size_t chain_start_transform{0};
  std::size_t chain_end_transform{0};
};

bool take(const char* class_name, const char* member, std::size_t& into) {
  const HkxClassLayout* layout = find_layout(class_name);
  if (layout == nullptr) return false;
  const auto at = find_member_offset(*layout, member);
  if (!at) return false;
  into = *at;
  return true;
}

std::optional<MapperLayout> mapper_layout() {
  const HkxClassLayout* mapper = find_layout("hkaSkeletonMapper");
  const HkxClassLayout* simple = find_layout("hkaSkeletonMapperDataSimpleMapping");
  const HkxClassLayout* chain = find_layout("hkaSkeletonMapperDataChainMapping");
  if (mapper == nullptr || simple == nullptr || chain == nullptr) return std::nullopt;
  MapperLayout out;
  out.mapper_size = mapper->size;
  out.simple_size = simple->size;
  out.chain_size = chain->size;
  constexpr const char* kData = "hkaSkeletonMapperData";
  constexpr const char* kSimple = "hkaSkeletonMapperDataSimpleMapping";
  constexpr const char* kChain = "hkaSkeletonMapperDataChainMapping";
  const bool found = take("hkaSkeletonMapper", "mapping", out.mapping) && take(kData, "skeletonA", out.skeleton_a) &&
                     take(kData, "skeletonB", out.skeleton_b) &&
                     take(kData, "simpleMappingPartitionRanges", out.simple_ranges) &&
                     take(kData, "simpleMappings", out.simple) && take(kData, "chainMappings", out.chain) &&
                     take(kData, "unmappedBones", out.unmapped) &&
                     take(kData, "extractedMotionMapping", out.extracted) &&
                     take(kData, "keepUnmappedLocal", out.keep) && take(kData, "mappingType", out.type) &&
                     take(kSimple, "boneA", out.simple_bone_a) && take(kSimple, "boneB", out.simple_bone_b) &&
                     take(kSimple, "aFromBTransform", out.simple_transform) &&
                     take(kChain, "startBoneA", out.chain_start_a) && take(kChain, "endBoneA", out.chain_end_a) &&
                     take(kChain, "startBoneB", out.chain_start_b) && take(kChain, "endBoneB", out.chain_end_b) &&
                     take(kChain, "startAFromBTransform", out.chain_start_transform) &&
                     take(kChain, "endAFromBTransform", out.chain_end_transform);
  if (!found) return std::nullopt;
  const bool fits = out.simple_transform + kTransformBytes <= out.simple_size &&
                    out.chain_start_transform + kTransformBytes <= out.chain_size &&
                    out.chain_end_transform + kTransformBytes <= out.chain_size &&
                    out.mapping + out.type + sizeof(std::int32_t) <= out.mapper_size;
  if (!fits) return std::nullopt;
  return out;
}

HkxStatus read_transform(const HkxSection& section, std::size_t at, HkxMapperTransform& out) {
  if (!in_section(section, at, kTransformBytes)) return HkxStatus::FixupOutOfRange;
  std::memcpy(out.lanes.data(), section.data.data() + at, kTransformBytes);
  return HkxStatus::Ok;
}

HkxStatus read_i16(const HkxSection& section, std::size_t at, std::int16_t& out) {
  if (!in_section(section, at, sizeof out)) return HkxStatus::FixupOutOfRange;
  out = value_at<std::int16_t>(section.data, at);
  return HkxStatus::Ok;
}

double rotation_length_squared(const HkxMapperTransform& transform) {
  double sum = 0.0;
  for (std::size_t lane = 4; lane < 8; ++lane) sum += static_cast<double>(transform.lanes[lane]) * transform.lanes[lane];
  return sum;
}

bool finite_pose(const HkxMapperTransform& transform) {
  for (std::size_t lane = 0; lane < 8; ++lane) {
    if (lane != 3 && !std::isfinite(transform.lanes[lane])) return false;
  }
  return true;
}

HkxStatus resolve_skeleton(const HkxPackfile& packfile, const HkxSection& section, std::size_t pointer_at,
                           std::size_t& object_index, HkxSkeleton& out) {
  const auto target = pointer_target(packfile, section, pointer_at);
  if (!target) return HkxStatus::FixupOutOfRange;
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    if (packfile.objects[i].offset != *target) continue;
    if (const auto status = read_skeleton_object(packfile, packfile.objects[i], out); status != HkxStatus::Ok) {
      return status;
    }
    if (out.bone_names.empty()) return HkxStatus::BadSection;
    object_index = i;
    return HkxStatus::Ok;
  }
  return HkxStatus::MissingObject;
}

HkxStatus read_mapper(const HkxPackfile& packfile, std::size_t index, const MapperLayout& layout,
                      HkxSkeletonMapper& out) {
  const HkxObjectView& object = packfile.objects[index];
  if (find_layout(object.class_name, object.signature) == nullptr) return HkxStatus::UnsupportedLayout;
  if (object.data.size() < layout.mapper_size) return HkxStatus::Truncated;
  const HkxSection& section = packfile.sections[packfile.data_section];
  const std::size_t data_at = object.offset + layout.mapping;
  const std::size_t pointer = packfile.pointer_size;

  out = HkxSkeletonMapper{};
  out.object_index = index;
  if (const auto status =
          resolve_skeleton(packfile, section, data_at + layout.skeleton_a, out.skeleton_a_object, out.skeleton_a);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status =
          resolve_skeleton(packfile, section, data_at + layout.skeleton_b, out.skeleton_b_object, out.skeleton_b);
      status != HkxStatus::Ok) {
    return status;
  }

  if (!in_section(section, data_at + layout.type, sizeof(std::int32_t)) ||
      !in_section(section, data_at + layout.keep, 1)) {
    return HkxStatus::Truncated;
  }
  out.mapping_type = value_at<std::int32_t>(section.data, data_at + layout.type);
  out.keep_unmapped_local = value_at<std::uint8_t>(section.data, data_at + layout.keep) != 0;
  if (const auto status = read_transform(section, data_at + layout.extracted, out.extracted_motion);
      status != HkxStatus::Ok) {
    return status;
  }

  Elements simple;
  if (const auto status = elements_at(section, data_at + layout.simple, pointer, layout.simple_size, simple);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < simple.count; ++i) {
    const std::size_t row_at = simple.first + i * layout.simple_size;
    HkxMapperSimpleRow row;
    if (const auto status = read_i16(section, row_at + layout.simple_bone_a, row.bone_a); status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = read_i16(section, row_at + layout.simple_bone_b, row.bone_b); status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = read_transform(section, row_at + layout.simple_transform, row.a_from_b);
        status != HkxStatus::Ok) {
      return status;
    }
    if (!finite_pose(row.a_from_b) || rotation_length_squared(row.a_from_b) < kMinRotationLengthSquared) {
      return HkxStatus::BadSection;
    }
    out.rows.push_back(row);
  }

  Elements chains;
  if (const auto status = elements_at(section, data_at + layout.chain, pointer, layout.chain_size, chains);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < chains.count; ++i) {
    const std::size_t at = chains.first + i * layout.chain_size;
    HkxMapperChainRow chain;
    for (const auto& [member, into] : {std::pair{layout.chain_start_a, &chain.start_bone_a},
                                       std::pair{layout.chain_end_a, &chain.end_bone_a},
                                       std::pair{layout.chain_start_b, &chain.start_bone_b},
                                       std::pair{layout.chain_end_b, &chain.end_bone_b}}) {
      if (const auto status = read_i16(section, at + member, *into); status != HkxStatus::Ok) return status;
    }
    if (const auto status = read_transform(section, at + layout.chain_start_transform, chain.start_a_from_b);
        status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = read_transform(section, at + layout.chain_end_transform, chain.end_a_from_b);
        status != HkxStatus::Ok) {
      return status;
    }
    out.chains.push_back(chain);
  }

  Elements unmapped;
  if (const auto status = elements_at(section, data_at + layout.unmapped, pointer, sizeof(std::int16_t), unmapped);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < unmapped.count; ++i) {
    std::int16_t bone = 0;
    if (const auto status = read_i16(section, unmapped.first + i * sizeof bone, bone); status != HkxStatus::Ok) {
      return status;
    }
    out.unmapped_bones.push_back(bone);
  }
  return HkxStatus::Ok;
}

bool mapper_matches(const HkxSkeletonMapper& a, const HkxSkeletonMapper& b) {
  return a.object_index == b.object_index && a.skeleton_a_object == b.skeleton_a_object &&
         a.skeleton_b_object == b.skeleton_b_object && a.mapping_type == b.mapping_type &&
         a.keep_unmapped_local == b.keep_unmapped_local && a.unmapped_bones == b.unmapped_bones &&
         a.extracted_motion == b.extracted_motion && a.rows == b.rows && a.chains == b.chains;
}

std::string joined(const std::vector<std::string>& parts) {
  std::string text;
  for (const std::string& part : parts) {
    if (!text.empty()) text += "; ";
    text += part;
  }
  return text;
}

std::string describe(const HkxSkeleton& skeleton) {
  return skeleton.name.empty() ? std::string("the skeleton") : "skeleton '" + skeleton.name + "'";
}

std::string bone_label(const HkxSkeleton& skeleton, std::size_t bone) {
  return bone < skeleton.bone_names.size() ? skeleton.bone_names[bone] : "bone " + std::to_string(bone);
}

bool bone_in_range(const HkxSkeleton& skeleton, std::int16_t bone) {
  return bone >= 0 && static_cast<std::size_t>(bone) < skeleton.bone_names.size();
}

std::string bone_refusal(const HkxSkeleton& skeleton, std::int16_t bone, const char* side) {
  if (bone_in_range(skeleton, bone)) return {};
  return "bone " + std::to_string(bone) + " is not a bone of the " + side + " " + describe(skeleton) + ", which holds " +
         std::to_string(skeleton.bone_names.size()) + " bone(s)";
}

std::string transform_refusal(const HkxMapperTransform& transform, const std::string& what) {
  for (const float lane : transform.lanes) {
    if (!std::isfinite(lane)) return what + " holds a non-finite lane";
  }
  const double squared = rotation_length_squared(transform);
  if (squared > kRotationPresent && std::abs(std::sqrt(squared) - 1.0) > kUnitTolerance) {
    return what + " holds a rotation of length " + std::to_string(std::sqrt(squared)) +
           ", which is not a unit quaternion";
  }
  return {};
}

// Invariant: read_skeleton_mappers refuses a simple row whose rotation length squared is below kMinRotationLengthSquared.
std::string row_refusal(const HkxMapperTransform& transform, const std::string& what) {
  if (auto why = transform_refusal(transform, what); !why.empty()) return why;
  if (rotation_length_squared(transform) < kMinRotationLengthSquared) {
    return what + " holds a zero-length rotation, which a simple row cannot read back";
  }
  return {};
}

struct Frame {
  std::array<double, 3> position{};
  std::array<double, 4> rotation{0.0, 0.0, 0.0, 1.0};
};

bool finite_frame(const Frame& frame) {
  for (const double value : frame.position) {
    if (!std::isfinite(value)) return false;
  }
  for (const double value : frame.rotation) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

double quaternion_length(const std::array<double, 4>& q) {
  return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
}

bool usable_frame(const Frame& frame) {
  return finite_frame(frame) && std::abs(quaternion_length(frame.rotation) - 1.0) <= kUnitTolerance;
}

std::array<double, 4> unit_rotation(std::array<double, 4> q) {
  const double length = quaternion_length(q);
  if (std::abs(length - 1.0) > kUnitTolerance) return q;
  for (double& value : q) value /= length;
  return q;
}

std::array<double, 4> multiply(const std::array<double, 4>& a, const std::array<double, 4>& b) {
  return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

std::array<double, 3> rotate(const std::array<double, 4>& q, const std::array<double, 3>& v) {
  const double tx = 2.0 * (q[1] * v[2] - q[2] * v[1]);
  const double ty = 2.0 * (q[2] * v[0] - q[0] * v[2]);
  const double tz = 2.0 * (q[0] * v[1] - q[1] * v[0]);
  return {v[0] + q[3] * tx + (q[1] * tz - q[2] * ty), v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
          v[2] + q[3] * tz + (q[0] * ty - q[1] * tx)};
}

bool compose(const Frame& outer, const Frame& inner, Frame& out) {
  const std::array<double, 3> moved = rotate(outer.rotation, inner.position);
  Frame composed;
  for (std::size_t axis = 0; axis < 3; ++axis) composed.position[axis] = outer.position[axis] + moved[axis];
  composed.rotation = multiply(outer.rotation, inner.rotation);
  if (!finite_frame(composed) || std::abs(quaternion_length(composed.rotation) - 1.0) > kUnitTolerance) return false;
  composed.rotation = unit_rotation(composed.rotation);
  out = composed;
  return true;
}

Frame invert(const Frame& frame) {
  const double squared = frame.rotation[0] * frame.rotation[0] + frame.rotation[1] * frame.rotation[1] +
                         frame.rotation[2] * frame.rotation[2] + frame.rotation[3] * frame.rotation[3];
  Frame out;
  out.rotation = {-frame.rotation[0] / squared, -frame.rotation[1] / squared, -frame.rotation[2] / squared,
                  frame.rotation[3] / squared};
  const std::array<double, 3> moved = rotate(out.rotation, frame.position);
  out.position = {-moved[0], -moved[1], -moved[2]};
  return out;
}

Frame frame_of(const HkxMapperTransform& transform) {
  Frame frame;
  for (std::size_t axis = 0; axis < 3; ++axis) frame.position[axis] = transform.lanes[axis];
  for (std::size_t axis = 0; axis < 4; ++axis) frame.rotation[axis] = transform.lanes[4 + axis];
  return frame;
}

void put_frame(const Frame& frame, HkxMapperTransform& transform) {
  for (std::size_t axis = 0; axis < 3; ++axis) transform.lanes[axis] = static_cast<float>(frame.position[axis]);
  for (std::size_t axis = 0; axis < 4; ++axis) transform.lanes[4 + axis] = static_cast<float>(frame.rotation[axis]);
}

std::vector<std::string> skeleton_refusals(const HkxSkeleton& skeleton) {
  std::vector<std::string> refusals;
  const std::size_t bones = skeleton.bone_names.size();
  if (bones == 0) {
    refusals.push_back(describe(skeleton) + " has no bones");
    return refusals;
  }
  if (skeleton.parent_indices.size() != bones) {
    refusals.push_back(describe(skeleton) + " has " + std::to_string(bones) + " bones and " +
                       std::to_string(skeleton.parent_indices.size()) + " parent indices");
  }
  if (skeleton.reference_pose.size() != bones) {
    refusals.push_back(describe(skeleton) + " has " + std::to_string(bones) + " bones and " +
                       std::to_string(skeleton.reference_pose.size()) + " reference-pose transforms");
  }
  const std::size_t shared = std::min({bones, skeleton.parent_indices.size(), skeleton.reference_pose.size()});
  for (std::size_t i = 0; i < shared; ++i) {
    const std::string label = "bone " + std::to_string(i) + " ('" + bone_label(skeleton, i) + "')";
    const std::int16_t parent = skeleton.parent_indices[i];
    if (parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= bones)) {
      refusals.push_back(label + " has parent " + std::to_string(parent) + ", which is not a bone of " +
                         describe(skeleton));
    } else if (parent >= 0 && static_cast<std::size_t>(parent) >= i) {
      refusals.push_back(label + " has parent " + std::to_string(parent) +
                         " stored at or after it; this primitive composes frames in array order and needs parents before children");
    }
    const HkxQsTransform& pose = skeleton.reference_pose[i];
    bool finite = true;
    for (const float value : pose.translation) finite = finite && std::isfinite(value);
    for (const float value : pose.rotation) finite = finite && std::isfinite(value);
    if (!finite) {
      refusals.push_back(label + " has a non-finite reference transform");
      continue;
    }
    std::array<double, 4> rotation{};
    for (std::size_t axis = 0; axis < 4; ++axis) rotation[axis] = pose.rotation[axis];
    const double length = quaternion_length(rotation);
    if (std::abs(length - 1.0) > kUnitTolerance) {
      refusals.push_back(label + " has a reference rotation of length " + std::to_string(length) +
                         ", which is not a unit quaternion");
    }
    bool scale_finite = true;
    for (const float value : pose.scale) scale_finite = scale_finite && std::isfinite(value);
    if (!scale_finite) {
      refusals.push_back(label + " has a non-finite reference scale");
    } else if (std::abs(pose.scale[0] - 1.0) > kScaleTolerance || std::abs(pose.scale[1] - 1.0) > kScaleTolerance ||
               std::abs(pose.scale[2] - 1.0) > kScaleTolerance) {
      refusals.push_back(label + " has a reference scale other than one; non-identity reference scale is not supported");
    }
  }
  return refusals;
}

// Constraint: skeleton_refusals must be empty first, so the parent and pose arrays have one entry per bone.
std::string world_frames(const HkxSkeleton& skeleton, std::vector<Frame>& frames) {
  frames.assign(skeleton.bone_names.size(), Frame{});
  for (std::size_t i = 0; i < frames.size(); ++i) {
    Frame local;
    for (std::size_t axis = 0; axis < 3; ++axis) local.position[axis] = skeleton.reference_pose[i].translation[axis];
    for (std::size_t axis = 0; axis < 4; ++axis) local.rotation[axis] = skeleton.reference_pose[i].rotation[axis];
    const std::int16_t parent = skeleton.parent_indices[i];
    if (parent < 0) {
      if (!usable_frame(local)) {
        return "bone " + std::to_string(i) + " ('" + bone_label(skeleton, i) + "') of " + describe(skeleton) +
               " has a reference transform that is not a usable unit rotation";
      }
      local.rotation = unit_rotation(local.rotation);
      frames[i] = local;
      continue;
    }
    if (!compose(frames[static_cast<std::size_t>(parent)], local, frames[i])) {
      return "bone " + std::to_string(i) + " ('" + bone_label(skeleton, i) + "') of " + describe(skeleton) +
             " accumulates a reference transform that is not a usable unit rotation";
    }
  }
  return {};
}

bool row_from_frames(const Frame& a, const Frame& b, HkxMapperTransform& transform) {
  Frame composed;
  if (!compose(invert(a), b, composed)) return false;
  put_frame(composed, transform);
  return true;
}

std::pair<std::size_t, std::size_t> duplicate_groups(const std::vector<HkxMapperSimpleRow>& rows) {
  std::map<std::int16_t, std::size_t> side_a;
  std::map<std::int16_t, std::size_t> side_b;
  for (const HkxMapperSimpleRow& row : rows) {
    ++side_a[row.bone_a];
    ++side_b[row.bone_b];
  }
  const auto repeated = [](const std::map<std::int16_t, std::size_t>& side) {
    return static_cast<std::size_t>(
        std::count_if(side.begin(), side.end(), [](const auto& entry) { return entry.second > 1; }));
  };
  return {repeated(side_a), repeated(side_b)};
}

bool adds_duplicates(const std::vector<HkxMapperSimpleRow>& was, const std::vector<HkxMapperSimpleRow>& now) {
  const auto before = duplicate_groups(was);
  const auto after = duplicate_groups(now);
  return after.first > before.first || after.second > before.second;
}

struct Range {
  std::size_t start{0};
  std::size_t length{0};
};

struct Patch {
  std::size_t at{0};
  std::vector<std::byte> bytes;
};

void put_bytes(std::vector<Patch>& patches, std::size_t at, const void* source, std::size_t count) {
  Patch patch;
  patch.at = at;
  patch.bytes.resize(count);
  std::memcpy(patch.bytes.data(), source, count);
  patches.push_back(std::move(patch));
}

HkxStatus refuse(std::string& why, std::string text) {
  why = std::move(text);
  return HkxStatus::UnsupportedLayout;
}

std::string mapper_label(const HkxSkeletonMapper& mapper) { return "mapper at object " + std::to_string(mapper.object_index); }

// Constraint: only a row or chain that changed is validated, so an odd shipped row never blocks an unrelated edit.
std::string edit_refusal(const HkxSkeletonMapper& was, const HkxSkeletonMapper& now) {
  const std::string label = mapper_label(now);
  if (now.object_index != was.object_index || now.skeleton_a_object != was.skeleton_a_object ||
      now.skeleton_b_object != was.skeleton_b_object) {
    return label + " does not name the object and skeletons the file holds, so it was not written";
  }
  if (now.unmapped_bones != was.unmapped_bones || !(now.extracted_motion == was.extracted_motion)) {
    return label + " changes unmapped bones or the extracted motion mapping, which this build does not write";
  }
  if (now.chains.size() != was.chains.size()) {
    return label + " would have " + std::to_string(now.chains.size()) + " chain row(s) where it holds " +
           std::to_string(was.chains.size()) + "; only simple rows are resized";
  }
  if (now.rows.size() > kMaxRows) return label + " would hold too many simple rows";
  if (now.mapping_type != was.mapping_type && now.mapping_type != 0 && now.mapping_type != 1) {
    return "mapping type " + std::to_string(now.mapping_type) + " is not one this build writes; 0 (ragdoll) and 1 (retargeting) are";
  }
  for (std::size_t i = 0; i < now.rows.size(); ++i) {
    if (i < was.rows.size() && was.rows[i] == now.rows[i]) continue;
    const HkxMapperSimpleRow& row = now.rows[i];
    if (auto why = bone_refusal(was.skeleton_a, row.bone_a, "A"); !why.empty()) return why;
    if (auto why = bone_refusal(was.skeleton_b, row.bone_b, "B"); !why.empty()) return why;
    if (auto why = row_refusal(row.a_from_b, "row " + std::to_string(i) + " of " + label); !why.empty()) {
      return why;
    }
  }
  for (std::size_t i = 0; i < now.chains.size(); ++i) {
    if (was.chains[i] == now.chains[i]) continue;
    const HkxMapperChainRow& chain = now.chains[i];
    const std::string what = "chain row " + std::to_string(i) + " of " + label;
    for (const std::int16_t bone : {chain.start_bone_a, chain.end_bone_a}) {
      if (auto why = bone_refusal(was.skeleton_a, bone, "A"); !why.empty()) return why;
    }
    for (const std::int16_t bone : {chain.start_bone_b, chain.end_bone_b}) {
      if (auto why = bone_refusal(was.skeleton_b, bone, "B"); !why.empty()) return why;
    }
    if (auto why = transform_refusal(chain.start_a_from_b, "the start transform of " + what); !why.empty()) return why;
    if (auto why = transform_refusal(chain.end_a_from_b, "the end transform of " + what); !why.empty()) return why;
  }
  return {};
}

// Constraint: a run is shared when any other pointer, fixup or object touches its bytes, because moving it would corrupt them.
std::string shared_storage(const HkxPackfile& file, std::size_t header, std::size_t stride, const char* what) {
  const HkxSection& data = file.sections[file.data_section];
  Elements run;
  if (elements_at(data, header, file.pointer_size, stride, run) != HkxStatus::Ok) {
    return std::string(what) + " storage cannot be located";
  }
  if (run.count == 0) return {};
  const std::size_t first = run.first;
  const std::size_t end = first + run.count * stride;
  const auto inside = [&](std::size_t at) { return at >= first && at < end; };
  const std::string shared = std::string(what) + " shares its storage with another object";
  for (const HkxLocalFixup& fixup : data.local_fixups) {
    if (inside(fixup.source) || (fixup.source != header && inside(fixup.destination))) return shared;
  }
  for (const HkxRemoteFixup& fixup : data.global_fixups) {
    if (inside(fixup.source)) return shared;
    if (fixup.source != header && fixup.section == file.data_section && inside(fixup.destination)) return shared;
  }
  for (std::size_t i = 0; i < file.sections.size(); ++i) {
    if (i == file.data_section) continue;
    for (const HkxRemoteFixup& fixup : file.sections[i].global_fixups) {
      if (fixup.section == file.data_section && inside(fixup.destination)) return shared;
    }
  }
  for (const HkxRemoteFixup& fixup : data.virtual_fixups) {
    if (inside(fixup.source)) return shared;
  }
  return {};
}

std::int32_t array_count(const HkxSection& data, std::size_t header) {
  return value_at<std::int32_t>(data.data, header + kCountAt);
}

std::string storage_refusal(const HkxPackfile& file, const MapperLayout& layout, const HkxSkeletonMapper& was,
                            const HkxSkeletonMapper& now) {
  const HkxSection& data = file.sections[file.data_section];
  const std::size_t data_at = file.objects[now.object_index].offset + layout.mapping;
  const std::string label = mapper_label(now);
  const bool resized = now.rows.size() != was.rows.size();
  if (resized) {
    const std::size_t ranges = data_at + layout.simple_ranges;
    if (!in_section(data, ranges, kArrayHeaderBytes)) return label + " has no partition range member this build can place";
    if (array_count(data, ranges) != 0) {
      return label + " carries simpleMappingPartitionRanges, which index its rows; a row resize would leave them stale";
    }
  }
  if (now.rows != was.rows && !was.rows.empty()) {
    if (auto why = shared_storage(file, data_at + layout.simple, layout.simple_size, "simple mapping rows"); !why.empty()) {
      return label + ": " + why;
    }
  }
  if (now.chains != was.chains && !was.chains.empty()) {
    if (auto why = shared_storage(file, data_at + layout.chain, layout.chain_size, "chain mapping rows"); !why.empty()) {
      return label + ": " + why;
    }
  }
  return {};
}

HkxStatus plan_values(const HkxPackfile& file, const MapperLayout& layout, const HkxSkeletonMapper& was,
                      const HkxSkeletonMapper& now, std::vector<Patch>& patches) {
  const HkxSection& data = file.sections[file.data_section];
  const std::size_t data_at = file.objects[now.object_index].offset + layout.mapping;
  const std::size_t pointer = file.pointer_size;

  if (!now.rows.empty()) {
    Elements run;
    if (const auto status = elements_at(data, data_at + layout.simple, pointer, layout.simple_size, run);
        status != HkxStatus::Ok) {
      return status;
    }
    if (run.count != now.rows.size()) return HkxStatus::BadSection;
    for (std::size_t i = 0; i < now.rows.size(); ++i) {
      if (i < was.rows.size() && was.rows[i] == now.rows[i]) continue;
      const std::size_t row_at = run.first + i * layout.simple_size;
      put_bytes(patches, row_at + layout.simple_bone_a, &now.rows[i].bone_a, sizeof now.rows[i].bone_a);
      put_bytes(patches, row_at + layout.simple_bone_b, &now.rows[i].bone_b, sizeof now.rows[i].bone_b);
      put_bytes(patches, row_at + layout.simple_transform, now.rows[i].a_from_b.lanes.data(), kTransformBytes);
    }
  }

  if (!now.chains.empty()) {
    Elements run;
    if (const auto status = elements_at(data, data_at + layout.chain, pointer, layout.chain_size, run);
        status != HkxStatus::Ok) {
      return status;
    }
    if (run.count != now.chains.size()) return HkxStatus::BadSection;
    for (std::size_t i = 0; i < now.chains.size(); ++i) {
      if (i < was.chains.size() && was.chains[i] == now.chains[i]) continue;
      const HkxMapperChainRow& chain = now.chains[i];
      const std::size_t at = run.first + i * layout.chain_size;
      put_bytes(patches, at + layout.chain_start_a, &chain.start_bone_a, sizeof chain.start_bone_a);
      put_bytes(patches, at + layout.chain_end_a, &chain.end_bone_a, sizeof chain.end_bone_a);
      put_bytes(patches, at + layout.chain_start_b, &chain.start_bone_b, sizeof chain.start_bone_b);
      put_bytes(patches, at + layout.chain_end_b, &chain.end_bone_b, sizeof chain.end_bone_b);
      put_bytes(patches, at + layout.chain_start_transform, chain.start_a_from_b.lanes.data(), kTransformBytes);
      put_bytes(patches, at + layout.chain_end_transform, chain.end_a_from_b.lanes.data(), kTransformBytes);
    }
  }

  if (now.mapping_type != was.mapping_type) {
    put_bytes(patches, data_at + layout.type, &now.mapping_type, sizeof now.mapping_type);
  }
  if (now.keep_unmapped_local != was.keep_unmapped_local) {
    const auto keep = static_cast<std::uint8_t>(now.keep_unmapped_local ? 1 : 0);
    put_bytes(patches, data_at + layout.keep, &keep, sizeof keep);
  }
  return HkxStatus::Ok;
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

void put_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

// Constraint: the pointer slot stays a local fixup whose source never moves, so the table keeps its source order.
HkxStatus relocate_rows(HkxSection& data, std::size_t header, std::size_t pointer, std::size_t stride,
                        std::size_t count, std::vector<Range>& allowed, std::string& why) {
  if (!in_section(data, header, kArrayHeaderBytes)) return HkxStatus::FixupOutOfRange;
  Elements old;
  if (const auto status = elements_at(data, header, pointer, stride, old); status != HkxStatus::Ok) return status;
  const std::size_t had = old.count;

  for (const HkxRemoteFixup& fixup : data.global_fixups) {
    if (fixup.source == header) return refuse(why, "the rows array pointer is a global fixup, so its storage cannot be moved");
  }
  auto found = std::lower_bound(data.local_fixups.begin(), data.local_fixups.end(), header,
                                [](const HkxLocalFixup& fixup, std::size_t value) { return fixup.source < value; });
  const bool has_fixup = found != data.local_fixups.end() && found->source == header;
  if (had > 0 && !has_fixup) return HkxStatus::FixupOutOfRange;

  const std::uint32_t capacity = value_at<std::uint32_t>(data.data, header + kCapacityAt);
  std::uint32_t flags = capacity & kCapacityFlags;
  if (count > 0) flags |= kEmbeddedStorage;
  allowed.push_back({header, kArrayHeaderBytes});

  if (count == 0) {
    if (has_fixup) data.local_fixups.erase(found);
    std::fill_n(data.data.begin() + static_cast<std::ptrdiff_t>(header), kPointerBytes, std::byte{0});
  } else {
    const std::size_t grown_from = data.data.size();
    const std::size_t start = align_up(grown_from, kRunAlign);
    data.data.resize(start + count * stride, std::byte{0});
    const std::size_t carried = std::min(had, count);
    std::copy_n(data.data.begin() + static_cast<std::ptrdiff_t>(old.first), carried * stride,
                data.data.begin() + static_cast<std::ptrdiff_t>(start));
    allowed.push_back({grown_from, data.data.size() - grown_from});
    if (has_fixup) {
      found->destination = static_cast<std::uint32_t>(start);
    } else {
      data.local_fixups.insert(found, HkxLocalFixup{static_cast<std::uint32_t>(header), static_cast<std::uint32_t>(start)});
    }
  }
  if (had > 0) {
    std::fill_n(data.data.begin() + static_cast<std::ptrdiff_t>(old.first), had * stride, std::byte{0});
    allowed.push_back({old.first, had * stride});
  }
  put_u32(data.data, header + kCountAt, static_cast<std::uint32_t>(count));
  put_u32(data.data, header + kCapacityAt, flags | static_cast<std::uint32_t>(count));
  return HkxStatus::Ok;
}

bool confined(const std::vector<std::byte>& before, const std::vector<std::byte>& after,
              const std::vector<Range>& allowed) {
  if (after.size() < before.size()) return false;
  std::vector<char> open(after.size(), 0);
  for (const Range& range : allowed) {
    for (std::size_t i = 0; i < range.length && range.start + i < open.size(); ++i) open[range.start + i] = 1;
  }
  for (std::size_t i = 0; i < after.size(); ++i) {
    if (i < before.size() && before[i] == after[i]) continue;
    if (open[i] == 0) return false;
  }
  return true;
}

bool same_locals(const std::vector<HkxLocalFixup>& before, const std::vector<HkxLocalFixup>& after,
                 const std::vector<std::size_t>& headers) {
  const auto kept = [&](const HkxLocalFixup& fixup) {
    return std::find(headers.begin(), headers.end(), fixup.source) == headers.end();
  };
  std::vector<HkxLocalFixup> a;
  std::vector<HkxLocalFixup> b;
  std::copy_if(before.begin(), before.end(), std::back_inserter(a), kept);
  std::copy_if(after.begin(), after.end(), std::back_inserter(b), kept);
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const HkxLocalFixup& x, const HkxLocalFixup& y) {
    return x.source == y.source && x.destination == y.destination;
  });
}

bool same_remotes(const std::vector<HkxRemoteFixup>& before, const std::vector<HkxRemoteFixup>& after) {
  return std::equal(before.begin(), before.end(), after.begin(), after.end(),
                    [](const HkxRemoteFixup& x, const HkxRemoteFixup& y) {
                      return x.source == y.source && x.section == y.section && x.destination == y.destination;
                    });
}

HkxStatus verify_written(const HkxPackfile& original, std::span<const std::byte> produced,
                         const std::vector<HkxSkeletonMapper>& edited, std::string& why) {
  HkxPackfile reread;
  if (const auto status = read_packfile(produced, reread); status != HkxStatus::Ok) {
    why = "the written file could not be read back";
    return status;
  }
  if (reread.objects.size() != original.objects.size()) return refuse(why, "the written file changed its object count");
  for (std::size_t i = 0; i < original.objects.size(); ++i) {
    if (reread.objects[i].offset != original.objects[i].offset ||
        reread.objects[i].class_name != original.objects[i].class_name) {
      return refuse(why, "the written file moved or renamed an object");
    }
  }
  std::vector<HkxSkeletonMapper> back;
  if (const auto status = read_skeleton_mappers(reread, back); status != HkxStatus::Ok) {
    why = "the written file's mappers could not be read back";
    return status;
  }
  if (back.size() != edited.size()) return refuse(why, "the written file holds a different number of mappers");
  for (std::size_t i = 0; i < back.size(); ++i) {
    if (!mapper_matches(back[i], edited[i])) {
      return refuse(why, "mapper " + std::to_string(i) + " did not read back as the model that was edited");
    }
  }
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus read_skeleton_mappers(const HkxPackfile& packfile, std::vector<HkxSkeletonMapper>& out) {
  out.clear();
  const auto layout = mapper_layout();
  if (!layout) return HkxStatus::UnsupportedLayout;
  std::vector<HkxSkeletonMapper> mappers;
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    if (packfile.objects[i].class_name != "hkaSkeletonMapper") continue;
    HkxSkeletonMapper mapper;
    if (const auto status = read_mapper(packfile, i, *layout, mapper); status != HkxStatus::Ok) return status;
    mappers.push_back(std::move(mapper));
  }
  if (mappers.empty()) return HkxStatus::MissingObject;
  out = std::move(mappers);
  return HkxStatus::Ok;
}

HkxStatus write_skeleton_mappers(std::span<const std::byte> source, const std::vector<HkxSkeletonMapper>& edited,
                                 std::vector<std::byte>& out, std::string* refusal) {
  std::string scratch;
  std::string& why = refusal != nullptr ? *refusal : scratch;
  why.clear();

  HkxPackfile original;
  if (const auto status = read_packfile(source, original); status != HkxStatus::Ok) return status;
  std::vector<HkxSkeletonMapper> was;
  if (const auto status = read_skeleton_mappers(original, was); status != HkxStatus::Ok) return status;
  const auto layout = mapper_layout();
  if (!layout) return HkxStatus::UnsupportedLayout;
  if (edited.size() != was.size()) {
    return refuse(why, "the edited list holds " + std::to_string(edited.size()) + " mapper(s) where the file holds " +
                           std::to_string(was.size()));
  }

  std::vector<std::size_t> changed;
  bool resized = false;
  for (std::size_t i = 0; i < was.size(); ++i) {
    if (auto text = edit_refusal(was[i], edited[i]); !text.empty()) return refuse(why, std::move(text));
    if (mapper_matches(was[i], edited[i])) continue;
    changed.push_back(i);
    if (auto text = storage_refusal(original, *layout, was[i], edited[i]); !text.empty()) {
      return refuse(why, std::move(text));
    }
    resized = resized || edited[i].rows.size() != was[i].rows.size();
  }
  if (changed.empty()) {
    out.assign(source.begin(), source.end());
    return HkxStatus::Ok;
  }

  std::vector<std::byte> produced;
  if (!resized) {
    std::vector<Patch> patches;
    for (const std::size_t i : changed) {
      if (const auto status = plan_values(original, *layout, was[i], edited[i], patches); status != HkxStatus::Ok) {
        return status;
      }
    }
    produced.assign(source.begin(), source.end());
    const std::size_t base = original.sections[original.data_section].file_offset;
    const std::size_t room = original.sections[original.data_section].data.size();
    std::vector<Range> allowed;
    for (const Patch& patch : patches) {
      if (patch.at > room || patch.bytes.size() > room - patch.at) return HkxStatus::FixupOutOfRange;
      std::copy(patch.bytes.begin(), patch.bytes.end(), produced.begin() + static_cast<std::ptrdiff_t>(base + patch.at));
      allowed.push_back({base + patch.at, patch.bytes.size()});
    }
    const std::vector<std::byte> before(source.begin(), source.end());
    if (produced.size() != before.size() || !confined(before, produced, allowed)) {
      return refuse(why, "a value edit reached bytes outside the fields it was meant to write");
    }
  } else {
    HkxPackfile work;
    if (const auto status = read_packfile(source, work); status != HkxStatus::Ok) return status;
    HkxSection& data = work.sections[work.data_section];
    const std::vector<std::byte> data_before = data.data;
    const std::vector<HkxLocalFixup> locals_before = data.local_fixups;
    const std::vector<HkxRemoteFixup> globals_before = data.global_fixups;
    const std::vector<HkxRemoteFixup> virtuals_before = data.virtual_fixups;

    std::vector<Range> allowed;
    std::vector<std::size_t> headers;
    for (const std::size_t i : changed) {
      if (edited[i].rows.size() == was[i].rows.size()) continue;
      const std::size_t header = work.objects[edited[i].object_index].offset + layout->mapping + layout->simple;
      headers.push_back(header);
      if (const auto status = relocate_rows(data, header, work.pointer_size, layout->simple_size, edited[i].rows.size(),
                                            allowed, why);
          status != HkxStatus::Ok) {
        return status;
      }
    }

    std::vector<Patch> patches;
    for (const std::size_t i : changed) {
      if (const auto status = plan_values(work, *layout, was[i], edited[i], patches); status != HkxStatus::Ok) {
        return status;
      }
    }
    for (const Patch& patch : patches) {
      if (patch.at > data.data.size() || patch.bytes.size() > data.data.size() - patch.at) {
        return HkxStatus::FixupOutOfRange;
      }
      std::copy(patch.bytes.begin(), patch.bytes.end(), data.data.begin() + static_cast<std::ptrdiff_t>(patch.at));
      allowed.push_back({patch.at, patch.bytes.size()});
    }

    if (!confined(data_before, data.data, allowed)) {
      return refuse(why, "a structural row edit reached bytes outside the row runs and fields it owns");
    }
    if (!same_locals(locals_before, data.local_fixups, headers) || !same_remotes(globals_before, data.global_fixups) ||
        !same_remotes(virtuals_before, data.virtual_fixups)) {
      return refuse(why, "a structural row edit changed a fixup it does not own");
    }
    if (const auto status = rewrite_packfile(source, work, produced); status != HkxStatus::Ok) return status;
  }

  if (const auto status = verify_written(original, produced, edited, why); status != HkxStatus::Ok) return status;
  out = std::move(produced);
  return HkxStatus::Ok;
}

std::string check_mapping_pairs(const HkxSkeleton& skeleton_a, const HkxSkeleton& skeleton_b,
                                std::span<const HkxMappingPair> pairs) {
  std::vector<std::string> refusals = skeleton_refusals(skeleton_a);
  for (std::string& text : skeleton_refusals(skeleton_b)) refusals.push_back(std::move(text));
  if (pairs.empty()) refusals.push_back("a mapping needs at least one bone pair");

  std::vector<bool> mapped_a(skeleton_a.bone_names.size(), false);
  std::vector<bool> mapped_b(skeleton_b.bone_names.size(), false);
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    const std::string prefix = "pair " + std::to_string(i) + ": ";
    if (!bone_in_range(skeleton_a, pairs[i].bone_a)) {
      refusals.push_back(prefix + "bone " + std::to_string(pairs[i].bone_a) + " is not a bone of " + describe(skeleton_a));
    } else if (mapped_a[static_cast<std::size_t>(pairs[i].bone_a)]) {
      refusals.push_back(prefix + "bone '" + bone_label(skeleton_a, static_cast<std::size_t>(pairs[i].bone_a)) + "' of " +
                         describe(skeleton_a) + " is already mapped");
    } else {
      mapped_a[static_cast<std::size_t>(pairs[i].bone_a)] = true;
    }
    if (!bone_in_range(skeleton_b, pairs[i].bone_b)) {
      refusals.push_back(prefix + "bone " + std::to_string(pairs[i].bone_b) + " is not a bone of " + describe(skeleton_b));
    } else if (mapped_b[static_cast<std::size_t>(pairs[i].bone_b)]) {
      refusals.push_back(prefix + "bone '" + bone_label(skeleton_b, static_cast<std::size_t>(pairs[i].bone_b)) + "' of " +
                         describe(skeleton_b) + " is already mapped");
    } else {
      mapped_b[static_cast<std::size_t>(pairs[i].bone_b)] = true;
    }
  }
  return joined(refusals);
}

std::string build_skeleton_mapping(const HkxSkeleton& skeleton_a, const HkxSkeleton& skeleton_b,
                                   std::span<const HkxMappingPair> pairs, std::vector<HkxMapperSimpleRow>& out) {
  if (auto why = check_mapping_pairs(skeleton_a, skeleton_b, pairs); !why.empty()) return why;
  std::vector<Frame> frames_a;
  std::vector<Frame> frames_b;
  if (auto why = world_frames(skeleton_a, frames_a); !why.empty()) return why;
  if (auto why = world_frames(skeleton_b, frames_b); !why.empty()) return why;

  std::vector<HkxMapperSimpleRow> rows;
  rows.reserve(pairs.size());
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    HkxMapperSimpleRow row;
    row.bone_a = pairs[i].bone_a;
    row.bone_b = pairs[i].bone_b;
    if (!row_from_frames(frames_a[static_cast<std::size_t>(row.bone_a)], frames_b[static_cast<std::size_t>(row.bone_b)],
                         row.a_from_b)) {
      return "pair " + std::to_string(i) + ": the composed transform for bone " + std::to_string(row.bone_a) +
             " -> bone " + std::to_string(row.bone_b) + " is not usable";
    }
    rows.push_back(row);
  }
  out = std::move(rows);
  return {};
}

std::string reverse_mapping(std::span<const HkxMapperSimpleRow> rows, std::vector<HkxMapperSimpleRow>& out) {
  std::vector<std::string> refusals;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (!finite_pose(rows[i].a_from_b) || !usable_frame(frame_of(rows[i].a_from_b))) {
      refusals.push_back("row " + std::to_string(i) + " (" + std::to_string(rows[i].bone_a) + " -> " +
                         std::to_string(rows[i].bone_b) + ") has a transform that cannot be inverted");
    }
  }
  if (!refusals.empty()) return joined(refusals);

  std::vector<HkxMapperSimpleRow> reversed;
  reversed.reserve(rows.size());
  for (const HkxMapperSimpleRow& row : rows) {
    HkxMapperSimpleRow next;
    next.bone_a = row.bone_b;
    next.bone_b = row.bone_a;
    put_frame(invert(frame_of(row.a_from_b)), next.a_from_b);
    reversed.push_back(next);
  }
  out = std::move(reversed);
  return {};
}

std::string recompute_mapping_row(const HkxSkeleton& skeleton_a, std::int16_t bone_a, const HkxSkeleton& skeleton_b,
                                  std::int16_t bone_b, HkxMapperTransform& transform) {
  std::vector<std::string> refusals = skeleton_refusals(skeleton_a);
  for (std::string& text : skeleton_refusals(skeleton_b)) refusals.push_back(std::move(text));
  if (auto why = bone_refusal(skeleton_a, bone_a, "A"); !why.empty()) refusals.push_back(std::move(why));
  if (auto why = bone_refusal(skeleton_b, bone_b, "B"); !why.empty()) refusals.push_back(std::move(why));
  if (!refusals.empty()) {
    return "row (bone " + std::to_string(bone_a) + " -> bone " + std::to_string(bone_b) +
           ") cannot be recomputed: " + joined(refusals);
  }
  std::vector<Frame> frames_a;
  std::vector<Frame> frames_b;
  if (auto why = world_frames(skeleton_a, frames_a); !why.empty()) return why;
  if (auto why = world_frames(skeleton_b, frames_b); !why.empty()) return why;
  HkxMapperTransform next = transform;
  if (!row_from_frames(frames_a[static_cast<std::size_t>(bone_a)], frames_b[static_cast<std::size_t>(bone_b)], next)) {
    return "the composed transform for bone " + std::to_string(bone_a) + " -> bone " + std::to_string(bone_b) +
           " is not usable";
  }
  transform = next;
  return {};
}

HkxMapperEditReport apply_mapper_edit(const HkxSkeletonMapper& mapper, const HkxMapperEdit& edit,
                                      HkxSkeletonMapper& out) {
  HkxMapperEditReport report;
  const auto refuse_with = [&](std::string text) {
    report.refusal = std::move(text);
    return report;
  };
  HkxSkeletonMapper next = mapper;
  const auto no_row = [&](std::size_t count, const char* what) {
    return "mapper holds " + std::to_string(count) + " " + what + " row(s), so there is no row " + std::to_string(edit.row);
  };

  switch (edit.kind) {
    case HkxMapperEditKind::SetRowBones: {
      if (edit.row >= next.rows.size()) return refuse_with(no_row(next.rows.size(), "simple"));
      if (auto why = bone_refusal(next.skeleton_a, edit.bone_a, "A"); !why.empty()) return refuse_with(why);
      if (auto why = bone_refusal(next.skeleton_b, edit.bone_b, "B"); !why.empty()) return refuse_with(why);
      next.rows[edit.row].bone_a = edit.bone_a;
      next.rows[edit.row].bone_b = edit.bone_b;
      if (adds_duplicates(mapper.rows, next.rows)) {
        return refuse_with("that bone is already mapped in this mapper; a second row would pair one bone twice");
      }
      break;
    }
    case HkxMapperEditKind::SetRowTransform: {
      if (edit.row >= next.rows.size()) return refuse_with(no_row(next.rows.size(), "simple"));
      if (auto why = row_refusal(edit.transform, "row " + std::to_string(edit.row)); !why.empty()) {
        return refuse_with(why);
      }
      next.rows[edit.row].a_from_b = edit.transform;
      break;
    }
    case HkxMapperEditKind::SetRowPose: {
      if (edit.row >= next.rows.size()) return refuse_with(no_row(next.rows.size(), "simple"));
      HkxMapperTransform posed = next.rows[edit.row].a_from_b;
      std::copy(edit.transform.lanes.begin(), edit.transform.lanes.begin() + 3, posed.lanes.begin());
      std::copy(edit.transform.lanes.begin() + 4, edit.transform.lanes.begin() + 8, posed.lanes.begin() + 4);
      if (auto why = row_refusal(posed, "row " + std::to_string(edit.row)); !why.empty()) return refuse_with(why);
      next.rows[edit.row].a_from_b = posed;
      break;
    }
    case HkxMapperEditKind::InsertRow:
    case HkxMapperEditKind::InsertRowWithTransform: {
      if (edit.row > next.rows.size()) {
        return refuse_with("this mapper has " + std::to_string(next.rows.size()) + " simple row(s), so there is no position " +
                           std::to_string(edit.row) + " to insert at");
      }
      if (auto why = bone_refusal(next.skeleton_a, edit.bone_a, "A"); !why.empty()) return refuse_with(why);
      if (auto why = bone_refusal(next.skeleton_b, edit.bone_b, "B"); !why.empty()) return refuse_with(why);
      HkxMapperSimpleRow row;
      row.bone_a = edit.bone_a;
      row.bone_b = edit.bone_b;
      if (edit.kind == HkxMapperEditKind::InsertRow) {
        if (auto why = recompute_mapping_row(next.skeleton_a, edit.bone_a, next.skeleton_b, edit.bone_b, row.a_from_b);
            !why.empty()) {
          return refuse_with(why);
        }
      } else {
        if (auto why = row_refusal(edit.transform, "the row to insert"); !why.empty()) return refuse_with(why);
        row.a_from_b = edit.transform;
      }
      next.rows.insert(next.rows.begin() + static_cast<std::ptrdiff_t>(edit.row), row);
      if (!edit.allow_duplicate_pair && adds_duplicates(mapper.rows, next.rows)) {
        return refuse_with("that bone is already mapped in this mapper; a second row would pair one bone twice");
      }
      break;
    }
    case HkxMapperEditKind::RemoveRow: {
      if (edit.row >= next.rows.size()) return refuse_with(no_row(next.rows.size(), "simple"));
      next.rows.erase(next.rows.begin() + static_cast<std::ptrdiff_t>(edit.row));
      break;
    }
    case HkxMapperEditKind::SetChainBones: {
      if (edit.row >= next.chains.size()) return refuse_with(no_row(next.chains.size(), "chain"));
      for (const std::int16_t bone : {edit.bone_a, edit.end_bone_a}) {
        if (auto why = bone_refusal(next.skeleton_a, bone, "A"); !why.empty()) return refuse_with(why);
      }
      for (const std::int16_t bone : {edit.bone_b, edit.end_bone_b}) {
        if (auto why = bone_refusal(next.skeleton_b, bone, "B"); !why.empty()) return refuse_with(why);
      }
      HkxMapperChainRow& chain = next.chains[edit.row];
      chain.start_bone_a = edit.bone_a;
      chain.end_bone_a = edit.end_bone_a;
      chain.start_bone_b = edit.bone_b;
      chain.end_bone_b = edit.end_bone_b;
      break;
    }
    case HkxMapperEditKind::SetChainTransforms: {
      if (edit.row >= next.chains.size()) return refuse_with(no_row(next.chains.size(), "chain"));
      const std::string what = "chain row " + std::to_string(edit.row);
      if (auto why = transform_refusal(edit.transform, "the start transform of " + what); !why.empty()) {
        return refuse_with(why);
      }
      if (auto why = transform_refusal(edit.end_transform, "the end transform of " + what); !why.empty()) {
        return refuse_with(why);
      }
      next.chains[edit.row].start_a_from_b = edit.transform;
      next.chains[edit.row].end_a_from_b = edit.end_transform;
      break;
    }
    case HkxMapperEditKind::SetMappingType: {
      if (edit.mapping_type != 0 && edit.mapping_type != 1) {
        return refuse_with("mapping type " + std::to_string(edit.mapping_type) +
                           " is not one this build writes; 0 (ragdoll) and 1 (retargeting) are");
      }
      next.mapping_type = edit.mapping_type;
      break;
    }
    case HkxMapperEditKind::SetKeepUnmappedLocal: {
      next.keep_unmapped_local = edit.flag;
      break;
    }
    case HkxMapperEditKind::RecomputeRow: {
      if (edit.row >= next.rows.size()) return refuse_with(no_row(next.rows.size(), "simple"));
      HkxMapperSimpleRow& row = next.rows[edit.row];
      if (auto why = recompute_mapping_row(next.skeleton_a, row.bone_a, next.skeleton_b, row.bone_b, row.a_from_b);
          !why.empty()) {
        return refuse_with(why);
      }
      break;
    }
    default:
      return refuse_with("unknown mapper edit kind");
  }

  out = std::move(next);
  report.changed = true;
  return report;
}

}  // namespace fo4::hkx
