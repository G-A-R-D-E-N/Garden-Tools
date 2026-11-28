#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

struct MapperBoneSpec {
  std::string name;
  std::int16_t parent{-1};
  std::array<float, 3> position{};
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

inline std::array<float, 12> mapper_lanes(float tx, float ty, float tz, float qx, float qy, float qz, float qw,
                                          float pad = 0.0f) {
  return {tx, ty, tz, pad, qx, qy, qz, qw, 1.0f, 1.0f, 1.0f, 0.0f};
}

struct MapperRowSpec {
  std::int16_t bone_a{0};
  std::int16_t bone_b{0};
  std::array<float, 12> lanes = mapper_lanes(0, 0, 0, 0, 0, 0, 1);
};

struct MapperChainSpec {
  std::int16_t start_a{0};
  std::int16_t end_a{0};
  std::int16_t start_b{0};
  std::int16_t end_b{0};
  std::array<float, 12> start = mapper_lanes(0, 0, 0, 0, 0, 0, 1);
  std::array<float, 12> end = mapper_lanes(0, 0, 0, 0, 0, 0, 1);
};

struct MapperSpec {
  std::vector<MapperBoneSpec> rig_a;
  std::vector<MapperBoneSpec> rig_b;
  std::vector<MapperRowSpec> rows;
  std::vector<MapperChainSpec> chains;
  std::vector<std::int16_t> unmapped;
  std::array<float, 12> extracted = mapper_lanes(0.25f, 0, 0, 0, 0, 0, 1);
  std::int32_t mapping_type{0};
  bool keep{false};
  bool partition_ranges{false};
  bool second_mapper_shares_rows{false};
  bool skeleton_a_is_not_a_skeleton{false};
};

inline void patch_i16(std::vector<std::byte>& data, std::size_t at, std::int16_t value) {
  const auto bits = static_cast<std::uint16_t>(value);
  data[at] = static_cast<std::byte>(bits & 0xFF);
  data[at + 1] = static_cast<std::byte>(bits >> 8);
}

inline std::vector<MapperBoneSpec> mapper_rig_a() {
  return {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}};
}

inline std::vector<MapperBoneSpec> mapper_rig_b() {
  return {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 2, 0}}, {"Extra", 1, {0, 0, 3}}};
}

inline MapperSpec default_mapper_spec() {
  MapperSpec spec;
  spec.rig_a = mapper_rig_a();
  spec.rig_b = mapper_rig_b();
  spec.rows = {{0, 0, mapper_lanes(0, 0, 0, 0, 0, 0, 1, 0.125f)}, {1, 1, mapper_lanes(0, 1, 0, 0, 0, 0, 1)}};
  spec.chains = {{1, 2, 1, 2, mapper_lanes(0.25f, 0, 0, 0, 0, 0, 1), mapper_lanes(0.5f, 0, 0, 0, 0, 0, 1)}};
  spec.unmapped = {2};
  spec.mapping_type = 1;
  spec.keep = true;
  return spec;
}

// Constraint: row i carries the marker byte 0xA0 + i in bytes 4 to 15, which no model field covers, so a carried row must keep it.
inline std::vector<std::byte> mapper_packfile(const MapperSpec& spec) {
  using fo4::hkx::find_layout;
  std::vector<std::byte> names;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> virtuals;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;

  const auto place = [&](const char* class_name) {
    const auto name_at = add_class_name(names, find_layout(class_name)->signature, class_name);
    const std::size_t at = data.size();
    data.resize(at + find_layout(class_name)->size, std::byte{0});
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(at), 0u, name_at});
    return at;
  };
  const auto text = [&](const std::string& value) {
    const std::size_t at = data.size();
    for (char ch : value) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto array = [&](std::size_t field, std::size_t at, std::size_t count) {
    link(local, field, at);
    set_count(data, field, count);
    patch_u32(data, field + 12, 0x80000000u | static_cast<std::uint32_t>(count));
  };

  const std::size_t skeleton_a = place("hkaSkeleton");
  const std::size_t skeleton_b = place("hkaSkeleton");
  const std::size_t mapper = place("hkaSkeletonMapper");
  const std::size_t second = spec.second_mapper_shares_rows ? place("hkaSkeletonMapper") : 0;

  const auto rig = [&](std::size_t skeleton, const std::vector<MapperBoneSpec>& bones, const char* name) {
    const std::size_t bone_size = find_layout("hkaBone")->size;
    const std::size_t bones_at = data.size();
    data.resize(bones_at + bones.size() * bone_size, std::byte{0});
    for (std::size_t i = 0; i < bones.size(); ++i) {
      link(local, bones_at + i * bone_size + member_at("hkaBone", "name"), text(bones[i].name));
    }
    const std::size_t parents_at = data.size();
    for (const MapperBoneSpec& bone : bones) put_i16(data, bone.parent);
    const std::size_t pose_at = data.size();
    for (const MapperBoneSpec& bone : bones) {
      put_pose(data, bone.position[0], bone.position[1], bone.position[2], 1.0f, bone.rotation);
    }
    link(local, skeleton + member_at("hkaSkeleton", "name"), text(name));
    array(skeleton + member_at("hkaSkeleton", "parentIndices"), parents_at, bones.size());
    array(skeleton + member_at("hkaSkeleton", "bones"), bones_at, bones.size());
    array(skeleton + member_at("hkaSkeleton", "referencePose"), pose_at, bones.size());
  };
  rig(skeleton_a, spec.rig_a, "RigA");
  rig(skeleton_b, spec.rig_b, "RigB");

  const auto fill_mapper = [&](std::size_t object) {
    const std::size_t data_at = object + member_at("hkaSkeletonMapper", "mapping");
    const auto field = [data_at](const char* member) { return data_at + member_at("hkaSkeletonMapperData", member); };
    global.insert(global.end(), {static_cast<std::uint32_t>(field("skeletonA")), 2u,
                                 static_cast<std::uint32_t>(spec.skeleton_a_is_not_a_skeleton ? mapper : skeleton_a)});
    global.insert(global.end(), {static_cast<std::uint32_t>(field("skeletonB")), 2u,
                                 static_cast<std::uint32_t>(skeleton_b)});
    patch_u32(data, field("mappingType"), static_cast<std::uint32_t>(spec.mapping_type));
    data[field("keepUnmappedLocal")] = spec.keep ? std::byte{1} : std::byte{0};
    for (std::size_t lane = 0; lane < 12; ++lane) {
      patch_float(data, field("extractedMotionMapping") + lane * 4, spec.extracted[lane]);
    }
    return field;
  };
  const auto mapper_field = fill_mapper(mapper);

  const std::size_t row_size = find_layout("hkaSkeletonMapperDataSimpleMapping")->size;
  const std::size_t rows_at = data.size();
  data.resize(rows_at + spec.rows.size() * row_size, std::byte{0});
  for (std::size_t i = 0; i < spec.rows.size(); ++i) {
    const std::size_t at = rows_at + i * row_size;
    patch_i16(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "boneA"), spec.rows[i].bone_a);
    patch_i16(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "boneB"), spec.rows[i].bone_b);
    for (std::size_t pad = 4; pad < 16; ++pad) data[at + pad] = static_cast<std::byte>(0xA0 + i);
    for (std::size_t lane = 0; lane < 12; ++lane) {
      patch_float(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "aFromBTransform") + lane * 4,
                  spec.rows[i].lanes[lane]);
    }
  }
  if (!spec.rows.empty()) array(mapper_field("simpleMappings"), rows_at, spec.rows.size());

  const std::size_t chain_size = find_layout("hkaSkeletonMapperDataChainMapping")->size;
  const std::size_t chains_at = data.size();
  data.resize(chains_at + spec.chains.size() * chain_size, std::byte{0});
  for (std::size_t i = 0; i < spec.chains.size(); ++i) {
    const std::size_t at = chains_at + i * chain_size;
    const MapperChainSpec& chain = spec.chains[i];
    patch_i16(data, at + member_at("hkaSkeletonMapperDataChainMapping", "startBoneA"), chain.start_a);
    patch_i16(data, at + member_at("hkaSkeletonMapperDataChainMapping", "endBoneA"), chain.end_a);
    patch_i16(data, at + member_at("hkaSkeletonMapperDataChainMapping", "startBoneB"), chain.start_b);
    patch_i16(data, at + member_at("hkaSkeletonMapperDataChainMapping", "endBoneB"), chain.end_b);
    for (std::size_t lane = 0; lane < 12; ++lane) {
      patch_float(data, at + member_at("hkaSkeletonMapperDataChainMapping", "startAFromBTransform") + lane * 4,
                  chain.start[lane]);
      patch_float(data, at + member_at("hkaSkeletonMapperDataChainMapping", "endAFromBTransform") + lane * 4,
                  chain.end[lane]);
    }
  }
  if (!spec.chains.empty()) array(mapper_field("chainMappings"), chains_at, spec.chains.size());

  const std::size_t unmapped_at = data.size();
  for (const std::int16_t bone : spec.unmapped) put_i16(data, bone);
  if (!spec.unmapped.empty()) array(mapper_field("unmappedBones"), unmapped_at, spec.unmapped.size());

  if (spec.partition_ranges) {
    const std::size_t ranges_at = data.size();
    data.resize(ranges_at + 8, std::byte{0});
    patch_u32(data, ranges_at + 4, static_cast<std::uint32_t>(spec.rows.size()));
    array(mapper_field("simpleMappingPartitionRanges"), ranges_at, 1);
  }

  if (spec.second_mapper_shares_rows) {
    const auto second_field = fill_mapper(second);
    array(second_field("simpleMappings"), rows_at, spec.rows.size());
  }

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), std::move(global), std::move(virtuals)},
  });
}

}  // namespace fo4::test::hkx
