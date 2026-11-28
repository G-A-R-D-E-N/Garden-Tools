#include "fo4/hkx/behavior.hpp"
#include "fo4/hkx/convert.hpp"
#include "fo4/hkx/convert_packfile.hpp"
#include "fo4/hkx/decode.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/object_graph.hpp"
#include "fo4/hkx/object_view.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/project_index.hpp"
#include "fo4/hkx/scene.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_document.hpp"
#include "fo4/hkx/skeleton_write.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_packfile;
using fo4::hkx::read_skeleton;

namespace {

using namespace fo4::test::hkx;

constexpr std::uint32_t kSkeletonSignature = 0xfec1cedbu;

std::vector<std::byte> skeleton_packfile(const std::vector<std::string>& names, const std::vector<std::int16_t>& parents,
                                         std::size_t pose_count, bool with_extras = true) {
  const std::size_t bone_size = fo4::hkx::find_layout("hkaBone")->size;
  const std::size_t bone_name = member_at("hkaBone", "name");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSkeleton")->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const auto append_string = [&](const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto set_count = [&](std::size_t field, std::size_t count) {
    patch_u32(data, field + 8, static_cast<std::uint32_t>(count));
  };

  const std::size_t bones_at = data.size();
  data.resize(bones_at + names.size() * bone_size, std::byte{0});
  for (std::size_t i = 0; i < names.size(); ++i) {
    link(local, bones_at + i * bone_size + bone_name, append_string(names[i]));
  }

  const std::size_t parents_at = data.size();
  for (std::int16_t parent : parents) {
    const auto bits = static_cast<std::uint16_t>(parent);
    data.push_back(static_cast<std::byte>(bits & 0xFF));
    data.push_back(static_cast<std::byte>(bits >> 8));
  }

  const std::size_t skeleton_name = member_at("hkaSkeleton", "name");
  const std::size_t parents_field = member_at("hkaSkeleton", "parentIndices");
  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  const std::size_t pose_field = member_at("hkaSkeleton", "referencePose");
  link(local, skeleton_name, append_string("Rig"));
  link(local, parents_field, parents_at);
  set_count(parents_field, parents.size());
  link(local, bones_field, bones_at);
  set_count(bones_field, names.size());

  // Constraint: every other bone is locked, so the flag must be read per bone.
  for (std::size_t i = 0; i < names.size(); i += 2) {
    data[bones_at + i * bone_size + member_at("hkaBone", "lockTranslation")] = std::byte{1};
  }

  const std::size_t floats_at = data.size();
  for (float value : {1.5f, -2.25f}) put_float(data, value);
  const std::size_t floats_field = member_at("hkaSkeleton", "referenceFloats");
  link(local, floats_field, floats_at);
  set_count(floats_field, 2);

  const std::size_t slots_at = data.size();
  data.resize(slots_at + 3 * 8, std::byte{0});
  link(local, slots_at + 0 * 8, append_string("root_pos"));
  // Constraint: slot 1 is a null pointer, so an unnamed slot keeps its index.
  link(local, slots_at + 2 * 8, append_string("twist"));
  const std::size_t slots_field = member_at("hkaSkeleton", "floatSlots");
  link(local, slots_field, slots_at);
  set_count(slots_field, 3);

  if (with_extras) {
    const std::size_t frame_size = fo4::hkx::find_layout("hkaSkeletonLocalFrameOnBone")->size;
    const std::size_t frames_at = data.size();
    data.resize(frames_at + 2 * frame_size, std::byte{0});
    for (std::size_t i = 0; i < 2; ++i) {
      const std::size_t element = frames_at + i * frame_size;
      const std::uint16_t bone = static_cast<std::uint16_t>(i == 0 ? 0 : 2);
      data[element + member_at("hkaSkeletonLocalFrameOnBone", "boneIndex")] = static_cast<std::byte>(bone & 0xFF);
      data[element + member_at("hkaSkeletonLocalFrameOnBone", "boneIndex") + 1] = static_cast<std::byte>(bone >> 8);
      link(local, element + member_at("hkaSkeletonLocalFrameOnBone", "localFrame"), append_string("frame" + std::to_string(i)));
    }
    const std::size_t frames_field = member_at("hkaSkeleton", "localFrames");
    link(local, frames_field, frames_at);
    set_count(frames_field, 2);

    const std::size_t partition_size = fo4::hkx::find_layout("hkaSkeletonPartition")->size;
    const std::size_t partitions_at = data.size();
    data.resize(partitions_at + 2 * partition_size, std::byte{0});
    link(local, partitions_at + member_at("hkaSkeletonPartition", "name"), append_string("whole"));
    const auto put_index = [&data](std::size_t at, std::uint16_t value) {
      data[at] = static_cast<std::byte>(value & 0xFF);
      data[at + 1] = static_cast<std::byte>(value >> 8);
    };
    put_index(partitions_at + member_at("hkaSkeletonPartition", "startBoneIndex"), 0);
    put_index(partitions_at + member_at("hkaSkeletonPartition", "numBones"), static_cast<std::uint16_t>(names.size()));
    // Constraint: the second partition is unnamed and still carries its bone range.
    const std::size_t second_partition = partitions_at + partition_size;
    put_index(second_partition + member_at("hkaSkeletonPartition", "startBoneIndex"), 1);
    put_index(second_partition + member_at("hkaSkeletonPartition", "numBones"), 1);
    const std::size_t partitions_field = member_at("hkaSkeleton", "partitions");
    link(local, partitions_field, partitions_at);
    set_count(partitions_field, 2);
  }


  // Constraint: the pose stays the last bytes of __data__, so a test that patches the final transform hits the pose.

  const std::size_t pose_at = data.size();
  for (std::size_t i = 0; i < pose_count; ++i) {
    const float translation_x = static_cast<float>(i + 1);
    for (float value : {translation_x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f}) {
      put_float(data, value);
    }
  }
  link(local, pose_field, pose_at);
  set_count(pose_field, pose_count);

  return single_object_packfile("hkaSkeleton", kSkeletonSignature, std::move(data), std::move(local));
}

std::vector<std::byte> two_skeleton_packfile(const std::string& first_name, std::size_t first_bones,
                                             const std::string& second_name, std::size_t second_bones,
                                             const std::string& binding_target = "") {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::size_t size = fo4::hkx::find_layout("hkaSkeleton")->size;
  const std::size_t bone_size = fo4::hkx::find_layout("hkaBone")->size;
  std::vector<std::byte> data(2 * size, std::byte{0});
  std::vector<std::uint32_t> local;
  const auto append_text = [&data](const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto add_rig = [&](std::size_t object, const std::string& rig_name, std::size_t bones) {
    link(local, object + member_at("hkaSkeleton", "name"), append_text(rig_name));
    const std::size_t bone_text = append_text("bone");
    const std::size_t bones_at = data.size();
    data.resize(bones_at + bones * bone_size, std::byte{0});
    for (std::size_t i = 0; i < bones; ++i) {
      link(local, bones_at + i * bone_size + member_at("hkaBone", "name"), bone_text);
    }
    const std::size_t parents_at = data.size();
    for (std::size_t i = 0; i < bones; ++i) {
      const std::byte low = i == 0 ? std::byte{0xFF} : std::byte{0};
      data.push_back(low);
      data.push_back(low);
    }
    const std::size_t pose_at = data.size();
    data.resize(pose_at + 48 * bones, std::byte{0});
    link(local, object + member_at("hkaSkeleton", "parentIndices"), parents_at);
    set_count(data, object + member_at("hkaSkeleton", "parentIndices"), bones);
    link(local, object + member_at("hkaSkeleton", "bones"), bones_at);
    set_count(data, object + member_at("hkaSkeleton", "bones"), bones);
    link(local, object + member_at("hkaSkeleton", "referencePose"), pose_at);
    set_count(data, object + member_at("hkaSkeleton", "referencePose"), bones);
  };
  add_rig(0, first_name, first_bones);
  add_rig(size, second_name, second_bones);
  const auto second_at = static_cast<std::uint32_t>(size);
  std::vector<std::uint32_t> virtuals{0, 0, name_at, second_at, 0, name_at};
  if (!binding_target.empty()) {
    const std::size_t binding_size = fo4::hkx::find_layout("hkaAnimationBinding")->size;
    const auto binding_at = static_cast<std::uint32_t>(data.size());
    data.resize(data.size() + binding_size, std::byte{0});
    link(local, binding_at + member_at("hkaAnimationBinding", "originalSkeletonName"), append_text(binding_target));
    const auto binding_name_at =
        add_class_name(names, fo4::hkx::find_layout("hkaAnimationBinding")->signature, "hkaAnimationBinding");
    virtuals.insert(virtuals.end(), {binding_at, 0, binding_name_at});
  }
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, std::move(virtuals)},
  });
}

}  // namespace

// Regression: a rig that ships locked bones, float slots, frames and partitions must keep every slot and partition name.
TEST(HkxSkeleton, ReadsTheBoneLockFloatSlotsFramesAndPartitions) {
  const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  ASSERT_EQ(read_skeleton(packfile, skeleton), HkxStatus::Ok);

  ASSERT_EQ(skeleton.lock_translation.size(), skeleton.bone_names.size());
  EXPECT_TRUE(skeleton.lock_translation[0]);
  EXPECT_FALSE(skeleton.lock_translation[1]);
  EXPECT_TRUE(skeleton.lock_translation[2]);

  ASSERT_EQ(skeleton.reference_floats.size(), 2u);
  EXPECT_FLOAT_EQ(skeleton.reference_floats[0], 1.5f);
  EXPECT_FLOAT_EQ(skeleton.reference_floats[1], -2.25f);

  ASSERT_EQ(skeleton.float_slots.size(), 3u);
  EXPECT_TRUE(skeleton.float_slots[0].named);
  EXPECT_EQ(skeleton.float_slots[0].name, "root_pos");
  EXPECT_FALSE(skeleton.float_slots[1].named);
  EXPECT_TRUE(skeleton.float_slots[2].named);
  EXPECT_EQ(skeleton.float_slots[2].name, "twist");

  ASSERT_EQ(skeleton.local_frames.size(), 2u);
  EXPECT_EQ(skeleton.local_frames[0].bone_index, 0);
  EXPECT_EQ(skeleton.local_frames[1].bone_index, 2);
  EXPECT_TRUE(skeleton.local_frames[0].has_frame);
  EXPECT_NE(skeleton.local_frames[0].frame_offset, skeleton.local_frames[1].frame_offset);

  ASSERT_EQ(skeleton.partitions.size(), 2u);
  EXPECT_TRUE(skeleton.partitions[0].named);
  EXPECT_EQ(skeleton.partitions[0].name, "whole");
  EXPECT_EQ(skeleton.partitions[0].start_bone, 0);
  EXPECT_EQ(skeleton.partitions[0].num_bones, 3);
  EXPECT_FALSE(skeleton.partitions[1].named);
  EXPECT_EQ(skeleton.partitions[1].start_bone, 1);
  EXPECT_EQ(skeleton.partitions[1].num_bones, 1);
}

TEST(HkxSkeleton, ReadSkeletonRefusesTwoRigsThatTieOnBoneCount) {
  const auto bytes = two_skeleton_packfile("RigA", 1, "RigB", 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(skeleton.bone_names.empty());
}

TEST(HkxSkeleton, ReadSkeletonPicksTheMainRigOverASmallerRagdoll) {
  const auto bytes = two_skeleton_packfile("Root", 2, "Ragdoll_NPC COM", 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  ASSERT_EQ(read_skeleton(packfile, skeleton), HkxStatus::Ok);
  EXPECT_EQ(skeleton.name, "Root");
  EXPECT_EQ(skeleton.bone_names.size(), 2u);
}

TEST(HkxPackfile, ObjectSpansRunToTheNextObjectAndKeepUnknownClasses) {
  std::vector<std::byte> names;
  const std::uint32_t skeleton_name = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::uint32_t unknown_name = add_class_name(names, 0x12345678u, "hkUnknownSample");
  std::vector<std::byte> data = filled(16, std::byte{0xA0});
  const auto second = filled(24, std::byte{0xB0});
  data.insert(data.end(), second.begin(), second.end());
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", data, {}, {}, {0, 0, skeleton_name, 16, 0, unknown_name}},
  });

  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(packfile.objects.size(), 2u);
  EXPECT_EQ(packfile.objects[0].offset, 0u);
  EXPECT_EQ(packfile.objects[0].signature, kSkeletonSignature);
  EXPECT_EQ(packfile.objects[0].class_name, "hkaSkeleton");
  EXPECT_EQ(packfile.objects[0].data.size(), 16u);
  EXPECT_EQ(packfile.objects[1].offset, 16u);
  EXPECT_EQ(packfile.objects[1].signature, 0x12345678u);
  EXPECT_EQ(packfile.objects[1].class_name, "hkUnknownSample");
  EXPECT_EQ(packfile.objects[1].data.size(), 24u);
  EXPECT_EQ(packfile.objects[1].data.front(), std::byte{0xB0});
}

TEST(HkxPackfile, RejectsBadMagicInsteadOfDecodingTheBody) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  patch_u32(bytes, 0, 0);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadMagic);
}

TEST(HkxPackfile, RejectsAFileCutOffInsideTheVirtualFixupTableOfTheDataSection) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  bytes.resize(bytes.size() - 4);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Truncated);
}

TEST(HkxPackfile, RejectsSectionMarksThatDecreaseInsteadOfSlicingBackwards) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  const std::size_t data_header = 0x50 + 2 * 0x40;
  patch_u32(bytes, data_header + 0x18 + 4, 0);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsLocalFixupTargetPastTheSectionData) {
  const auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {0, 999});
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsGlobalFixupNamingASectionThatDoesNotExist) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {0, 7, 0}, {0, 0, name_at}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsVirtualFixupNamingAClassPastTheClassNameTable) {
  std::vector<std::byte> names;
  add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, 9999}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsTwoObjectsClaimingTheSameOffset) {
  std::vector<std::byte> names;
  const std::uint32_t first = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::uint32_t second = add_class_name(names, 0x12345678u, "hkUnknownSample");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, first, 0, 0, second}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::DuplicateFixup);
}

TEST(HkxPackfile, RejectsTwoGlobalFixupsOnOnePointerNamingDifferentTargets) {
  std::vector<std::byte> names;
  const std::uint32_t first = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::uint32_t second = add_class_name(names, 0x12345678u, "hkUnknownSample");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {0, 0, first, 0, 0, second}, {}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::DuplicateFixup);
}

TEST(HkxPackfile, RejectsALocalAndAGlobalFixupOnOnePointerNamingDifferentTargets) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {0, 8}, {0, 0, name_at}, {}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::DuplicateFixup);
}

TEST(HkxPackfile, AcceptsAFillerTailThatStopsInsideAFixupEntry) {
  // Detects a length check that must accept TurnLeft90.hkx, whose virtual table ends in filler.
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {},
                  {0, 0, name_at, 0xFFFFFFFFu, 0xFFFFFFFFu}},
  });
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(packfile.objects.size(), 1u);
  EXPECT_EQ(packfile.objects[0].class_name, "hkaSkeleton");
}

TEST(HkxPackfile, RejectsAFixupTableWhosePartialTailIsNotFiller) {
  // Detects a reader that ignores a truncated entry: bytes after the last whole entry must be filler.
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {},
                  {0, 0, name_at, 0x12345678u, 0x9ABCDEF0u}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsAPackfileVersionTheLayoutTableWasNotMeasuredFrom) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  patch_u32(bytes, 0x0C, 10);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::UnsupportedLayout);
}

// Requires each refused container field to be named in the refusal sentence and to stay UnsupportedLayout.
TEST(HkxPackfile, RefusalNamesTheContainerFieldThatIsNotSupported) {
  const auto good = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  EXPECT_TRUE(fo4::hkx::describe_packfile_refusal(good).empty());

  struct Case {
    std::size_t at;
    std::uint8_t value;
    const char* expected;
  };
  const Case cases[] = {
      {0x10, 4, "pointer size 4"},
      {0x11, 0, "little-endian"},
      {0x12, 1, "reuse-padding"},
      {0x13, 0, "empty-base-class"},
  };
  for (const Case& refused : cases) {
    auto bytes = good;
    bytes[refused.at] = static_cast<std::byte>(refused.value);
    HkxPackfile packfile;
    EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::UnsupportedLayout) << refused.expected;
    EXPECT_NE(fo4::hkx::describe_packfile_refusal(bytes).find(refused.expected), std::string::npos) << refused.expected;
  }

  auto old_version = good;
  patch_u32(old_version, 0x0C, 8);
  const auto version_text = fo4::hkx::describe_packfile_refusal(old_version);
  EXPECT_NE(version_text.find("version 8"), std::string::npos);
}

TEST(HkxPackfile, ATagfileIsNamedAsOneAndStillReadsAsBadMagic) {
  std::vector<std::byte> tagfile(0x40, std::byte{0});
  patch_u32(tagfile, 0, 0x40);
  tagfile[4] = std::byte{'T'};
  tagfile[5] = std::byte{'A'};
  tagfile[6] = std::byte{'G'};
  tagfile[7] = std::byte{'0'};
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(tagfile, packfile), HkxStatus::BadMagic);
  EXPECT_NE(fo4::hkx::describe_packfile_refusal(tagfile).find("TAG0"), std::string::npos);
  EXPECT_TRUE(fo4::hkx::describe_packfile_refusal(std::vector<std::byte>(0x40, std::byte{0})).empty());
}

TEST(HkxSkeleton, ReadSkeletonFollowsTheBindingNameOverTheLargerRig) {
  // Requires the binding's named rig to win over a larger rig.
  const auto bytes = two_skeleton_packfile("Big", 3, "Small", 1, "Small");
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  ASSERT_EQ(read_skeleton(packfile, skeleton), HkxStatus::Ok);
  EXPECT_EQ(skeleton.name, "Small");
  EXPECT_EQ(skeleton.bone_names.size(), 1u);
}

TEST(HkxSkeleton, ReadSkeletonRefusesABindingThatNamesNoRigInTheFile) {
  const auto bytes = two_skeleton_packfile("Big", 3, "Small", 1, "Other");
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(skeleton.bone_names.empty());
}

TEST(HkxSkeleton, ReadSkeletonRejectsBoneArrayThatRunsPastTheSection) {
  auto data = filled(136, std::byte{0});
  patch_u32(data, 48, 1000);
  const auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, data, {40, 128});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::FixupOutOfRange);
}

TEST(HkxSkeleton, ReadSkeletonRefusesASkeletonWhoseSignatureDoesNotMatchTheLayout) {
  const auto bytes = single_object_packfile("hkaSkeleton", 0x00000001u, filled(136, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::UnsupportedLayout);
}

TEST(HkxSkeleton, ReadSkeletonReportsMissingObjectWhenNoSkeletonIsPresent) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", 0x4B3E2F10u, filled(32, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::MissingObject);
}

TEST(HkxSkeleton, DecodesBoneNamesParentsAndPoseOfASmallSkeleton) {
  const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  ASSERT_EQ(read_skeleton(packfile, skeleton), HkxStatus::Ok);
  EXPECT_EQ(skeleton.name, "Rig");
  EXPECT_EQ(skeleton.bone_names, (std::vector<std::string>{"Root", "Spine", "Head"}));
  EXPECT_EQ(skeleton.parent_indices, (std::vector<std::int16_t>{-1, 0, 1}));
  ASSERT_EQ(skeleton.reference_pose.size(), 3u);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[2].translation[0], 3.0f);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[1].rotation[3], 1.0f);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[1].scale[0], 1.0f);
}

TEST(HkxSkeleton, RejectsParentIndicesThatAreOutOfRangeSelfOrForwardOrMiscounted) {
  struct Case {
    const char* what;
    std::vector<std::int16_t> parents;
    std::size_t poses;
  };
  const std::vector<Case> cases{
      {"parent below -1", {-1, 0, -2}, 3},
      {"self parent", {-1, 1, 0}, 3},
      {"forward parent", {-1, 2, 0}, 3},
      {"parent count short of the bone count", {-1, 0}, 3},
      {"pose count short of the bone count", {-1, 0, 1}, 2},
  };
  for (const Case& c : cases) {
    const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, c.parents, c.poses);
    HkxPackfile packfile;
    ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok) << c.what;
    HkxSkeleton skeleton;
    EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::BadSection) << c.what;
  }
}

TEST(HkxSkeleton, RejectsABoneNameLongerThanTheScanCapInsteadOfCopyingIt) {
  const auto bytes = skeleton_packfile({std::string(300, 'b'), "Spine"}, {-1, 0}, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::FixupOutOfRange);
}

TEST(HkxSkeleton, ReadSkeletonRefusesANonFiniteReferencePoseComponent) {
  for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    for (std::size_t component_at : {std::size_t{0}, std::size_t{16}, std::size_t{32}}) {
      auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
      HkxPackfile packfile;
      ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
      const auto& data = packfile.sections[packfile.data_section];
      patch_float(bytes, data.file_offset + data.data.size() - 48 + component_at, value);
      HkxPackfile patched;
      ASSERT_EQ(read_packfile(bytes, patched), HkxStatus::Ok);
      HkxSkeleton skeleton;
      EXPECT_EQ(read_skeleton(patched, skeleton), HkxStatus::BadSection) << component_at;
    }
  }
}

TEST(HkxPackfile, EveryCutOffLengthOfASkeletonFileIsRejectedWithoutReadingPastTheEnd) {
  const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const std::span<const std::byte> whole(bytes);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    HkxPackfile packfile;
    const auto status = read_packfile(whole.first(length), packfile);
    EXPECT_NE(status, HkxStatus::Ok) << "length " << length;
    if (status == HkxStatus::Ok) {
      HkxSkeleton skeleton;
      EXPECT_NE(read_skeleton(packfile, skeleton), HkxStatus::Ok) << "length " << length;
    }
  }
}

TEST(HkxPackfile, RejectsAPackfileWithMoreSectionsThanTheCapAllows) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  std::vector<TestSection> sections{
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, name_at}},
  };
  for (int i = 0; i < 14; ++i) sections.push_back(TestSection{"__types__", {}, {}, {}, {}});
  const auto bytes = build_packfile(sections);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsASectionWhoseBytesOverlapAnotherSection) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  const std::size_t classnames_at = 0x50 + 3 * 0x40;
  const std::size_t data_header = 0x50 + 2 * 0x40;
  patch_u32(bytes, data_header + 0x14, static_cast<std::uint32_t>(classnames_at));
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsAClassNameLongerThanTheScanCapInsteadOfCopyingIt) {
  const auto bytes = single_object_packfile(std::string(300, 'a'), kSkeletonSignature, filled(16, std::byte{0}), {});
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadClassName);
}

namespace {

std::vector<std::byte> object_graph_packfile() {
  const auto skeleton_class = fo4::hkx::find_layout("hkaSkeleton");
  const auto bone_class = fo4::hkx::find_layout("hkaBone");
  std::vector<std::byte> names;
  const auto skeleton_name = add_class_name(names, skeleton_class->signature, "hkaSkeleton");
  const auto bone_name = add_class_name(names, bone_class->signature, "hkaBone");
  const auto unknown_name = add_class_name(names, 0x12345678u, "hkaUnverifiedThing");

  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  std::vector<std::byte> data(skeleton_class->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t bone_at = data.size();
  data.resize(bone_at + bone_class->size, std::byte{0});
  // Requires one bone in the array, so the skeleton holds exactly one reference to resolve.
  patch_u32(data, bones_field + 8, 1);
  link(local, bones_field, bone_at);

  const std::size_t unknown_at = data.size();
  data.resize(unknown_at + 0x10, std::byte{0x5A});

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {},
                  {0, 0, skeleton_name, static_cast<std::uint32_t>(bone_at), 0, bone_name,
                   static_cast<std::uint32_t>(unknown_at), 0, unknown_name}},
  });
}

}  // namespace

TEST(HkxObjectView, ResolvesTheSkeletonBonesPointerToItsBoneObject) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectReport view;
  ASSERT_EQ(fo4::hkx::inspect_objects(packfile, {}, view), HkxStatus::Ok);
  ASSERT_EQ(view.object_count, 3U);
  ASSERT_EQ(view.objects.size(), 3U);
  const auto& skeleton = view.objects[0];
  EXPECT_EQ(skeleton.class_name, "hkaSkeleton");
  EXPECT_TRUE(skeleton.layout_known);
  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  bool saw_bones = false;
  for (const auto& field : skeleton.fields) {
    if (field.name != "bones") continue;
    saw_bones = true;
    EXPECT_EQ(field.offset, bones_field);
  }
  EXPECT_TRUE(saw_bones);
  // Constraint: a missed pointer would read as a plain number, so its target object and class are the assertion.
  ASSERT_EQ(skeleton.references.size(), 1U);
  EXPECT_EQ(skeleton.references[0].member, "bones");
  EXPECT_EQ(skeleton.references[0].target_object, 1);
  EXPECT_EQ(skeleton.references[0].target_class, "hkaBone");
}

TEST(HkxObjectView, ClassPrefixKeepsMatchingObjectsAndCountsThem) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectQuery query;
  query.class_prefix = "hkaBone";
  fo4::hkx::HkxObjectReport view;
  ASSERT_EQ(fo4::hkx::inspect_objects(packfile, query, view), HkxStatus::Ok);
  EXPECT_EQ(view.matched, 1U);
  ASSERT_EQ(view.objects.size(), 1U);
  EXPECT_EQ(view.objects[0].class_name, "hkaBone");
  EXPECT_EQ(view.objects[0].index, 1U);

  query.class_prefix = "hkaNothing";
  fo4::hkx::HkxObjectReport empty;
  ASSERT_EQ(fo4::hkx::inspect_objects(packfile, query, empty), HkxStatus::Ok);
  EXPECT_EQ(empty.matched, 0U);
  EXPECT_TRUE(empty.objects.empty());
}

TEST(HkxObjectView, UnknownClassKeepsItsBytesAndIsDiagnosed) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectReport view;
  ASSERT_EQ(fo4::hkx::inspect_objects(packfile, {}, view), HkxStatus::Ok);
  ASSERT_EQ(view.objects.size(), 3U);
  const auto& unknown = view.objects[2];
  EXPECT_EQ(unknown.class_name, "hkaUnverifiedThing");
  EXPECT_FALSE(unknown.layout_known);
  EXPECT_EQ(unknown.size, 0x10U);
  EXPECT_TRUE(unknown.fields.empty());
  // Constraint: an unverified class must be named in the report, because its fields cannot be read.
  ASSERT_EQ(view.diagnostics.size(), 1U);
  EXPECT_NE(view.diagnostics[0].find("hkaUnverifiedThing"), std::string::npos);
}

namespace {

std::size_t inherited_at(const char* layout, const char* member) {
  for (const auto& entry : fo4::hkx::all_members(*fo4::hkx::find_layout(layout))) {
    if (entry.name == member) return entry.offset;
  }
  ADD_FAILURE() << layout << " has no member " << member;
  return 0;
}

void put_real(std::vector<std::byte>& data, std::size_t at, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  patch_u32(data, at, bits);
}

const fo4::hkx::HkxObjectField* find_field(const std::vector<fo4::hkx::HkxObjectField>& fields, const char* name) {
  for (const auto& field : fields) {
    if (field.name == name) return &field;
  }
  ADD_FAILURE() << "no field " << name;
  return nullptr;
}

// Invariant: object 0 is the skeleton, 1 the bone and 2 the clip, whose inherited variableBindingSet points at the bone.
std::vector<std::byte> decoded_packfile(std::uint8_t lock_byte, bool stray_trigger) {
  const auto skeleton = fo4::hkx::find_layout("hkaSkeleton");
  const auto bone = fo4::hkx::find_layout("hkaBone");
  const auto clip = fo4::hkx::find_layout("hkbClipGenerator");
  std::vector<std::byte> names;
  const auto skeleton_name = add_class_name(names, skeleton->signature, "hkaSkeleton");
  const auto bone_name = add_class_name(names, bone->signature, "hkaBone");
  const auto clip_name = add_class_name(names, clip->signature, "hkbClipGenerator");
  const std::size_t bone_at = skeleton->size;
  const std::size_t clip_at = bone_at + bone->size;
  std::vector<std::byte> data(clip_at + clip->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const auto append = [&data](const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  link(local, member_at("hkaSkeleton", "name"), append("Rig"));
  link(local, bone_at + member_at("hkaBone", "name"), append("Root"));
  link(local, clip_at + inherited_at("hkbClipGenerator", "name"), append("Idle"));
  link(local, clip_at + member_at("hkbClipGenerator", "animationName"), append("walk.hkx"));
  data[bone_at + member_at("hkaBone", "lockTranslation")] = static_cast<std::byte>(lock_byte);
  put_real(data, clip_at + member_at("hkbClipGenerator", "playbackSpeed"), 1.5f);
  put_real(data, clip_at + member_at("hkbClipGenerator", "localTime"), 9.0f);
  data[clip_at + member_at("hkbClipGenerator", "mode")] = std::byte{1};
  data[clip_at + member_at("hkbClipGenerator", "flags")] = std::byte{3};
  if (stray_trigger) data[clip_at + member_at("hkbClipGenerator", "triggers")] = std::byte{1};

  const std::size_t parents_at = data.size();
  for (const std::uint8_t byte : {std::uint8_t{0xFF}, std::uint8_t{0xFF}, std::uint8_t{0x00}, std::uint8_t{0x00}}) data.push_back(static_cast<std::byte>(byte));
  const std::size_t parents_field = member_at("hkaSkeleton", "parentIndices");
  link(local, parents_field, parents_at);
  set_count(data, parents_field, 2);

  const std::uint32_t binding_source = static_cast<std::uint32_t>(clip_at + inherited_at("hkbClipGenerator", "variableBindingSet"));
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {binding_source, 2, static_cast<std::uint32_t>(bone_at)},
                  {0, 0, skeleton_name, static_cast<std::uint32_t>(bone_at), 0, bone_name,
                   static_cast<std::uint32_t>(clip_at), 0, clip_name}},
  });
}

}  // namespace

// Regression: a node name, an animation name, a speed or an enum must be readable without decoding hex by hand.
TEST(HkxObjectView, DecodesNamesRealsEnumsAndPointersAndSkipsSlotsTheGameNeverStores) {
  const auto bytes = decoded_packfile(1, false);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectLookup clip;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 2, clip), HkxStatus::Ok);
  const auto& fields = clip.detail.fields;
  const auto* name = find_field(fields, "name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->value, "\"Idle\"");
  EXPECT_EQ(name->type, "stringptr");
  EXPECT_EQ(name->offset, inherited_at("hkbClipGenerator", "name"));
  EXPECT_EQ(find_field(fields, "animationName")->value, "\"walk.hkx\"");
  EXPECT_EQ(find_field(fields, "animationBundleName")->value, "null");
  EXPECT_EQ(find_field(fields, "playbackSpeed")->value, "1.5");
  EXPECT_EQ(find_field(fields, "mode")->value, "1 (MODE_LOOPING)");
  EXPECT_EQ(find_field(fields, "flags")->value, "3");
  EXPECT_EQ(find_field(fields, "variableBindingSet")->value, "object 1 (hkaBone)");
  const auto* local_time = find_field(fields, "localTime");
  EXPECT_TRUE(local_time->value.empty());
  EXPECT_EQ(local_time->reason, "not stored in files");
  EXPECT_FALSE(local_time->hex.empty());

  fo4::hkx::HkxObjectLookup skeleton;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 0, skeleton), HkxStatus::Ok);
  EXPECT_EQ(find_field(skeleton.detail.fields, "name")->value, "\"Rig\"");
  EXPECT_EQ(find_field(skeleton.detail.fields, "parentIndices")->value, "2 items [-1, 0]");
  EXPECT_EQ(find_field(skeleton.detail.fields, "bones")->value, "0 items");
  EXPECT_FALSE(find_field(skeleton.detail.fields, "memSizeAndRefCount")->reason.empty());
}

// Regression: a bool byte that is neither 0 nor 1 and a pointer with no fixup but a non-zero value must stay raw with a reason, never be shown as a clean value.
TEST(HkxObjectView, ARejectedValueKeepsItsHexAndSaysWhy) {
  HkxPackfile packfile;
  const auto good = decoded_packfile(1, false);
  ASSERT_EQ(read_packfile(good, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectLookup bone;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 1, bone), HkxStatus::Ok);
  EXPECT_EQ(find_field(bone.detail.fields, "name")->value, "\"Root\"");
  EXPECT_EQ(find_field(bone.detail.fields, "lockTranslation")->value, "true");

  const auto bad = decoded_packfile(2, true);
  HkxPackfile broken;
  ASSERT_EQ(read_packfile(bad, broken), HkxStatus::Ok);
  fo4::hkx::HkxObjectLookup lock;
  ASSERT_EQ(fo4::hkx::lookup_object(broken, 1, lock), HkxStatus::Ok);
  const auto* flag = find_field(lock.detail.fields, "lockTranslation");
  EXPECT_TRUE(flag->value.empty());
  EXPECT_NE(flag->reason.find("bool"), std::string::npos);
  EXPECT_FALSE(flag->hex.empty());
  fo4::hkx::HkxObjectLookup clip;
  ASSERT_EQ(fo4::hkx::lookup_object(broken, 2, clip), HkxStatus::Ok);
  const auto* triggers = find_field(clip.detail.fields, "triggers");
  EXPECT_TRUE(triggers->value.empty());
  EXPECT_NE(triggers->reason.find("not zero"), std::string::npos);
}

namespace {

std::vector<fo4::hkx::HkxObjectField> decoded_fields(const std::vector<std::byte>& bytes, const std::string& class_name) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  for (std::size_t index = 0; index < packfile.objects.size(); ++index) {
    if (packfile.objects[index].class_name != class_name) continue;
    fo4::hkx::HkxObjectLookup lookup;
    EXPECT_EQ(fo4::hkx::lookup_object(packfile, index, lookup), HkxStatus::Ok);
    return lookup.detail.fields;
  }
  ADD_FAILURE() << "no object of class " << class_name;
  return {};
}

std::size_t trigger_array(BehaviorFixture& f, std::size_t at) {
  return f.array(at + f.member("hkbClipTriggerArray", "triggers"), 1, fo4::hkx::find_layout("hkbClipTrigger")->size);
}

}  // namespace

// Regression: an array of structs holding a nested struct must decode instead of staying hex, so a trigger's time, event id and flags can be read.
TEST(HkxObjectView, DecodesAStructArrayAndItsNestedStructInsteadOfLeavingHex) {
  BehaviorFixture f;
  const std::size_t at = f.add("hkbClipTriggerArray");
  const std::size_t first = trigger_array(f, at);
  f.set<float>(first + member_at("hkbClipTrigger", "localTime"), 0.5f);
  f.set<std::int32_t>(first + member_at("hkbClipTrigger", "event"), 7);
  f.set<std::uint8_t>(first + member_at("hkbClipTrigger", "acyclic"), 1);
  const auto fields = decoded_fields(f.bytes(), "hkbClipTriggerArray");
  const auto* triggers = find_field(fields, "triggers");
  ASSERT_NE(triggers, nullptr);
  EXPECT_EQ(triggers->value,
            "1 item [{localTime=0.5, event={id=7, payload=null}, relativeToEndOfClip=false, acyclic=true, isAnnotation=false}]");
  EXPECT_TRUE(triggers->reason.empty());
}

// Regression: a struct member the reader rejects must not blank the whole struct or pass as clean; the rest stays readable and the reason names the member.
TEST(HkxObjectView, ARejectedMemberInsideAStructKeepsTheRestAndNamesTheMember) {
  BehaviorFixture f;
  const std::size_t at = f.add("hkbClipTriggerArray");
  const std::size_t first = trigger_array(f, at);
  f.set<std::uint8_t>(first + member_at("hkbClipTrigger", "relativeToEndOfClip"), 2);
  f.set<std::uint8_t>(first + member_at("hkbClipTrigger", "acyclic"), 1);
  const auto fields = decoded_fields(f.bytes(), "hkbClipTriggerArray");
  const auto* triggers = find_field(fields, "triggers");
  ASSERT_NE(triggers, nullptr);
  EXPECT_NE(triggers->value.find("relativeToEndOfClip=?"), std::string::npos);
  EXPECT_NE(triggers->value.find("acyclic=true"), std::string::npos);
  EXPECT_EQ(triggers->reason, "relativeToEndOfClip: bool byte is not 0 or 1");
}

// Regression: vectors, repeated vectors, half floats, a transform and one-byte structs must decode instead of staying hex, so a motion state can be compared with a reference.
TEST(HkxObjectView, DecodesVectorsHalfsTransformsAndSmallStructsOfAMotionState) {
  BehaviorFixture f;
  const std::size_t at = f.add("hkMotionState");
  for (std::size_t lane = 0; lane < 4; ++lane) {
    f.set<float>(at + f.member("hkMotionState", "deltaAngle") + 4 * lane, static_cast<float>(lane + 1));
    f.set<float>(at + f.member("hkMotionState", "sweptTransform") + 16 + 4 * lane, static_cast<float>(lane + 5));
  }
  f.set<std::uint16_t>(at + f.member("hkMotionState", "linearDamping"), 0x3c00);
  f.set<std::uint8_t>(at + f.member("hkMotionState", "maxLinearVelocity"), 200);
  const auto fields = decoded_fields(f.bytes(), "hkMotionState");
  EXPECT_EQ(find_field(fields, "deltaAngle")->value, "[1, 2, 3, 4]");
  EXPECT_EQ(find_field(fields, "sweptTransform")->value,
            "[[0, 0, 0, 0], [5, 6, 7, 8], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]]");
  EXPECT_EQ(find_field(fields, "linearDamping")->value, "half 0x3c00");
  EXPECT_EQ(find_field(fields, "angularDamping")->value, "half 0x0000");
  EXPECT_EQ(find_field(fields, "maxLinearVelocity")->value, "{value=200}");
  EXPECT_EQ(find_field(fields, "transform")->value, "[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]");
  EXPECT_TRUE(find_field(fields, "transform")->reason.empty());
}

// Regression: a matrix member must decode instead of staying hex, so an inertia tensor can be read without decoding floats by hand.
TEST(HkxObjectView, DecodesAMatrixAndAVectorOfAMassProperties) {
  BehaviorFixture f;
  const std::size_t at = f.add("hkMassProperties");
  f.set<float>(at + f.member("hkMassProperties", "centerOfMass") + 8, 2.5f);
  f.set<float>(at + f.member("hkMassProperties", "inertiaTensor"), 1.5f);
  f.set<float>(at + f.member("hkMassProperties", "inertiaTensor") + 20, -2.0f);
  const auto fields = decoded_fields(f.bytes(), "hkMassProperties");
  EXPECT_EQ(find_field(fields, "centerOfMass")->value, "[0, 0, 2.5, 0]");
  EXPECT_EQ(find_field(fields, "inertiaTensor")->value, "[1.5, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0]");
}

// Regression: a relative array (count and offset from the member itself) must decode, and the offset must be taken from the member's own address, not the object's.
TEST(HkxObjectView, DecodesRelativeArraysFromTheirOwnAddress) {
  BehaviorFixture f;
  const std::size_t at = f.add("hknpConvexPolytopeShape");
  const std::size_t planes_field = at + f.member("hknpConvexPolytopeShape", "planes");
  const std::size_t faces_field = at + f.member("hknpConvexPolytopeShape", "faces");
  const std::size_t indices_field = at + f.member("hknpConvexPolytopeShape", "indices");
  const std::size_t planes = f.block(32);
  const std::size_t faces = f.block(8);
  const std::size_t indices = f.block(3);
  const auto relative = [&](std::size_t field, std::size_t target, std::uint16_t count) {
    f.set<std::uint16_t>(field, count);
    f.set<std::uint16_t>(field + 2, static_cast<std::uint16_t>(target - field));
  };
  relative(planes_field, planes, 2);
  relative(faces_field, faces, 2);
  relative(indices_field, indices, 3);
  f.set<float>(planes, 1.0f);
  f.set<float>(planes + 12, 5.0f);
  f.set<float>(planes + 16 + 4, 1.0f);
  f.set<float>(planes + 16 + 12, 6.0f);
  f.set<std::uint8_t>(faces + 2, 2);
  f.set<std::uint8_t>(faces + 3, 9);
  f.set<std::uint16_t>(faces + 4, 2);
  f.set<std::uint8_t>(faces + 6, 1);
  f.set<std::uint8_t>(indices + 1, 1);
  f.set<std::uint8_t>(indices + 2, 2);
  const auto fields = decoded_fields(f.bytes(), "hknpConvexPolytopeShape");
  EXPECT_EQ(find_field(fields, "planes")->value, "2 items [[1, 0, 0, 5], [0, 1, 0, 6]]");
  EXPECT_EQ(find_field(fields, "faces")->value,
            "2 items [{firstIndex=0, numIndices=2, minHalfAngle=9}, {firstIndex=2, numIndices=1, minHalfAngle=0}]");
  EXPECT_EQ(find_field(fields, "indices")->value, "3 items [0, 1, 2]");
  EXPECT_EQ(find_field(fields, "vertices")->value, "0 items");
}

// Regression: a relative array whose offset leaves the data section must keep its hex and say why, never read past the section.
TEST(HkxObjectView, ARelativeArrayPastTheSectionKeepsItsHexAndSaysWhy) {
  BehaviorFixture f;
  const std::size_t at = f.add("hknpConvexPolytopeShape");
  const std::size_t planes_field = at + f.member("hknpConvexPolytopeShape", "planes");
  f.set<std::uint16_t>(planes_field, 4);
  f.set<std::uint16_t>(planes_field + 2, 40000);
  const auto fields = decoded_fields(f.bytes(), "hknpConvexPolytopeShape");
  const auto* planes = find_field(fields, "planes");
  EXPECT_TRUE(planes->value.empty());
  EXPECT_FALSE(planes->hex.empty());
  EXPECT_EQ(planes->reason, "relative array runs past the data section");
}

// Regression: an enum member showed only a number; a named value must read as "number (NAME)" and an unnamed one must stay a bare number.
TEST(HkxObjectView, NamesEnumValuesAndKeepsAnUnnamedValueNumeric) {
  const auto context_of = [](std::int8_t value) {
    BehaviorFixture f;
    const std::size_t at = f.add("BSAlignBoneModifier");
    f.set<std::int8_t>(at + f.member("BSAlignBoneModifier", "context"), value);
    const auto fields = decoded_fields(f.bytes(), "BSAlignBoneModifier");
    return find_field(fields, "context")->value;
  };
  EXPECT_EQ(context_of(1), "1 (WeaponGrip)");
  EXPECT_EQ(context_of(0), "0 (None)");
  EXPECT_EQ(context_of(9), "9");
}

// Regression: a flags member must decompose into names only when every set bit is named, and an unnamed bit must leave the number alone.
TEST(HkxObjectView, DecomposesFlagsOnlyWhenEveryBitIsNamed) {
  const auto flags_of = [](std::uint16_t value) {
    BehaviorFixture f;
    const std::size_t at = f.add("hknpConvexPolytopeShape");
    f.set<std::uint16_t>(at + f.member("hknpShape", "flags"), value);
    const auto fields = decoded_fields(f.bytes(), "hknpConvexPolytopeShape");
    return find_field(fields, "flags")->value;
  };
  EXPECT_EQ(flags_of(3), "3 (IS_CONVEX_SHAPE|IS_CONVEX_POLYTOPE_SHAPE)");
  EXPECT_EQ(flags_of(0), "0");
  EXPECT_EQ(flags_of(0x8000), "32768");
}

// Regression: a width the layout tables do not state must keep its hex and an UNVERIFIED reason, because a guessed width would misread the next member.
TEST(HkxObjectView, AMemberWithNoVerifiedWidthKeepsItsHexAndSaysUnverified) {
  BehaviorFixture f;
  f.add("hkbClipGenerator");
  const auto fields = decoded_fields(f.bytes(), "hkbClipGenerator");
  const auto* user_data = find_field(fields, "userData");
  ASSERT_NE(user_data, nullptr);
  EXPECT_TRUE(user_data->value.empty());
  EXPECT_FALSE(user_data->hex.empty());
  EXPECT_NE(user_data->reason.find("UNVERIFIED"), std::string::npos);
}

// Regression: a string member with bytes that are not UTF-8 made the JSON reply throw; the value must be valid UTF-8 with the bad bytes escaped.
TEST(HkxObjectView, ANonUtf8StringIsEscapedAndWellFormedUtf8PassesThrough) {
  BehaviorFixture f;
  const std::size_t at = f.add("BSAlignBoneModifier");
  f.text(at + f.member("BSAlignBoneModifier", "targetName"), "caf\xC3\xA9 \xFF" "b\x01");
  const auto fields = decoded_fields(f.bytes(), "BSAlignBoneModifier");
  EXPECT_EQ(find_field(fields, "targetName")->value, "\"caf\xC3\xA9 \\xffb\\x01\"");
}

// Regression: a tree row must carry its decoded string members so two clips differ, and a pointer held by a base class must keep its member label.
TEST(HkxObjectGraph, TreeRowsCarryTheDecodedNamesAndInheritedMemberLabels) {
  const auto bytes = decoded_packfile(1, false);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectTree tree;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, {}, tree), HkxStatus::Ok);
  ASSERT_EQ(tree.nodes.size(), 3U);
  EXPECT_EQ(tree.nodes[0].summary, "name=\"Rig\"");
  EXPECT_EQ(tree.nodes[1].object, 2U);
  EXPECT_EQ(tree.nodes[1].summary, "name=\"Idle\" animationName=\"walk.hkx\"");
  EXPECT_EQ(tree.nodes[2].object, 1U);
  EXPECT_EQ(tree.nodes[2].member, "variableBindingSet");
  EXPECT_EQ(tree.nodes[2].summary, "name=\"Root\"");

  const auto unknown = object_graph_packfile();
  HkxPackfile other;
  ASSERT_EQ(read_packfile(unknown, other), HkxStatus::Ok);
  fo4::hkx::HkxObjectTree plain;
  ASSERT_EQ(fo4::hkx::build_object_tree(other, {}, plain), HkxStatus::Ok);
  for (const auto& node : plain.nodes) {
    if (node.object == 2U) { EXPECT_TRUE(node.summary.empty()); }
  }
}

namespace {

std::vector<std::byte> skeleton_bone_packfile(bool bone_points_back) {
  const auto skeleton_class = fo4::hkx::find_layout("hkaSkeleton");
  const auto bone_class = fo4::hkx::find_layout("hkaBone");
  std::vector<std::byte> names;
  const auto skeleton_name = add_class_name(names, skeleton_class->signature, "hkaSkeleton");
  const auto bone_name = add_class_name(names, bone_class->signature, "hkaBone");
  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  std::vector<std::byte> data(skeleton_class->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const std::size_t bone_at = data.size();
  data.resize(bone_at + bone_class->size, std::byte{0});
  patch_u32(data, bones_field + 8, 1);
  link(local, bones_field, bone_at);
  if (bone_points_back) link(local, bone_at, 0);
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {},
                  {0, 0, skeleton_name, static_cast<std::uint32_t>(bone_at), 0, bone_name}},
  });
}

std::vector<std::byte> skeleton_with_trailing_array_packfile() {
  const auto skeleton_class = fo4::hkx::find_layout("hkaSkeleton");
  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  std::vector<std::byte> data(skeleton_class->size + 16, std::byte{0});
  patch_u32(data, bones_field + 8, 1);
  std::vector<std::uint32_t> local;
  link(local, bones_field, skeleton_class->size + 4);
  return single_object_packfile("hkaSkeleton", skeleton_class->signature, std::move(data), std::move(local));
}

}  // namespace

// Regression: an incoming reference must be a pointer that lands on the object, not the owner's own array bytes.
TEST(HkxObjectGraph, LookupSeparatesIncomingFromOutgoingByObjectIndex) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectLookup bone;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 1, bone), HkxStatus::Ok);
  EXPECT_EQ(bone.detail.index, 1U);
  EXPECT_EQ(bone.detail.class_name, "hkaBone");
  EXPECT_TRUE(bone.outgoing.empty());
  ASSERT_EQ(bone.incoming.size(), 1U);
  EXPECT_EQ(bone.incoming[0].source_object, 0);
  EXPECT_EQ(bone.incoming[0].member, "bones");
  EXPECT_EQ(bone.incoming[0].target_object, 1);

  fo4::hkx::HkxObjectLookup skeleton;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 0, skeleton), HkxStatus::Ok);
  ASSERT_EQ(skeleton.outgoing.size(), 1U);
  EXPECT_EQ(skeleton.outgoing[0].target_object, 1);
  EXPECT_EQ(skeleton.outgoing[0].target_class, "hkaBone");
  EXPECT_FALSE(skeleton.outgoing[0].inner);
  EXPECT_TRUE(skeleton.incoming.empty());

  fo4::hkx::HkxObjectLookup missing;
  EXPECT_EQ(fo4::hkx::lookup_object(packfile, 3, missing), HkxStatus::MissingObject);
}

// Regression: a pointer into the middle of an object (an array body or string) must never read as a reference to the object itself.
TEST(HkxObjectGraph, InnerPointerIsNotAnObjectReference) {
  const auto bytes = skeleton_with_trailing_array_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectLookup skeleton;
  ASSERT_EQ(fo4::hkx::lookup_object(packfile, 0, skeleton), HkxStatus::Ok);
  ASSERT_EQ(skeleton.outgoing.size(), 1U);
  EXPECT_TRUE(skeleton.outgoing[0].inner);
  EXPECT_EQ(skeleton.outgoing[0].target_object, -1);
  EXPECT_TRUE(skeleton.incoming.empty());

  fo4::hkx::HkxObjectTree tree;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, {}, tree), HkxStatus::Ok);
  EXPECT_EQ(tree.nodes.size(), 1U);

  fo4::hkx::HkxPackfileDiagnostics report;
  ASSERT_EQ(fo4::hkx::diagnose_packfile(packfile, report), HkxStatus::Ok);
  EXPECT_EQ(report.inner_links, 1U);
  EXPECT_EQ(report.object_links, 0U);
}

// Regression: a cycle must end at a repeated marker, not walk forever or lose the second visit.
TEST(HkxObjectGraph, TreeMarksARepeatedObjectInsteadOfWalkingTheCycle) {
  const auto bytes = skeleton_bone_packfile(true);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectTree tree;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, {}, tree), HkxStatus::Ok);
  ASSERT_EQ(tree.nodes.size(), 3U);
  EXPECT_EQ(tree.nodes[0].object, 0U);
  EXPECT_EQ(tree.nodes[1].object, 1U);
  EXPECT_EQ(tree.nodes[1].member, "bones");
  EXPECT_EQ(tree.nodes[1].parent, 0);
  EXPECT_EQ(tree.nodes[2].object, 0U);
  EXPECT_TRUE(tree.nodes[2].repeated);
  EXPECT_EQ(tree.unreachable, 0U);
}

// Regression: depth and node caps must cut the walk and say so, and unvisited objects must be counted.
TEST(HkxObjectGraph, TreeHonoursDepthAndNodeCaps) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxObjectTree full;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, {}, full), HkxStatus::Ok);
  ASSERT_EQ(full.roots.size(), 2U);
  ASSERT_EQ(full.nodes.size(), 3U);
  EXPECT_EQ(full.nodes[1].depth, 1U);

  fo4::hkx::HkxTreeQuery shallow;
  shallow.max_depth = 0;
  fo4::hkx::HkxObjectTree cut;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, shallow, cut), HkxStatus::Ok);
  ASSERT_EQ(cut.nodes.size(), 2U);
  EXPECT_TRUE(cut.nodes[0].cut);
  EXPECT_EQ(cut.unreachable, 1U);

  fo4::hkx::HkxTreeQuery small;
  small.max_nodes = 1;
  fo4::hkx::HkxObjectTree capped;
  ASSERT_EQ(fo4::hkx::build_object_tree(packfile, small, capped), HkxStatus::Ok);
  EXPECT_EQ(capped.nodes.size(), 1U);
  EXPECT_TRUE(capped.truncated);
}

// Regression: the report must name each class with its layout status and count unreferenced objects without flagging a healthy file.
TEST(HkxObjectGraph, DiagnosticsReportClassesLayoutStatusAndLinks) {
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxPackfileDiagnostics report;
  ASSERT_EQ(fo4::hkx::diagnose_packfile(packfile, report), HkxStatus::Ok);
  EXPECT_EQ(report.file_version, 11);
  EXPECT_EQ(report.sections.size(), 3U);
  EXPECT_EQ(report.sections[2].virtual_fixups, 3U);
  EXPECT_EQ(report.sections[2].local_fixups, 1U);
  ASSERT_EQ(report.classes.size(), 3U);
  EXPECT_EQ(report.classes[0].name, "hkaSkeleton");
  EXPECT_EQ(report.classes[0].status, fo4::hkx::HkxClassLayoutStatus::Match);
  EXPECT_EQ(report.classes[2].status, fo4::hkx::HkxClassLayoutStatus::NoLayout);
  EXPECT_EQ(report.object_links, 1U);
  EXPECT_EQ(report.unreferenced_objects, 2U);
  EXPECT_TRUE(report.issues.empty());
}

// Regression: a wrong class signature or an object shorter than its layout must be an issue, not a silent partial read.
TEST(HkxObjectGraph, DiagnosticsFlagAShortObjectAndASignatureMismatch) {
  const auto skeleton_class = fo4::hkx::find_layout("hkaSkeleton");
  HkxPackfile packfile;
  const auto short_bytes = single_object_packfile("hkaSkeleton", skeleton_class->signature, filled(8, std::byte{0}), {});
  ASSERT_EQ(read_packfile(short_bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxPackfileDiagnostics report;
  ASSERT_EQ(fo4::hkx::diagnose_packfile(packfile, report), HkxStatus::Ok);
  ASSERT_EQ(report.issues.size(), 1U);
  EXPECT_NE(report.issues[0].find("shorter"), std::string::npos);

  const auto wrong_bytes =
      single_object_packfile("hkaSkeleton", skeleton_class->signature ^ 0x1u, filled(skeleton_class->size, std::byte{0}), {});
  HkxPackfile wrong;
  ASSERT_EQ(read_packfile(wrong_bytes, wrong), HkxStatus::Ok);
  fo4::hkx::HkxPackfileDiagnostics mismatch;
  ASSERT_EQ(fo4::hkx::diagnose_packfile(wrong, mismatch), HkxStatus::Ok);
  ASSERT_EQ(mismatch.classes.size(), 1U);
  EXPECT_EQ(mismatch.classes[0].status, fo4::hkx::HkxClassLayoutStatus::SignatureMismatch);
  ASSERT_EQ(mismatch.issues.size(), 1U);
  EXPECT_NE(mismatch.issues[0].find("signature"), std::string::npos);
}

// Regression: a virtual fixup table stored out of order must be reported, because read_packfile sorts it and the file order is otherwise lost.
TEST(HkxObjectGraph, DiagnosticsFlagAVirtualFixupTableThatIsNotInAscendingOrder) {
  const auto skeleton = fo4::hkx::find_layout("hkaSkeleton");
  const auto bone = fo4::hkx::find_layout("hkaBone");
  const auto build = [&](bool ascending) {
    std::vector<std::byte> names;
    const auto skeleton_name = add_class_name(names, skeleton->signature, "hkaSkeleton");
    const auto bone_name = add_class_name(names, bone->signature, "hkaBone");
    const auto bone_at = static_cast<std::uint32_t>(skeleton->size);
    std::vector<std::uint32_t> virtuals{0, 0, skeleton_name, bone_at, 0, bone_name};
    if (!ascending) virtuals = {bone_at, 0, bone_name, 0, 0, skeleton_name};
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", filled(skeleton->size + bone->size, std::byte{0}), {}, {}, virtuals},
    });
  };
  for (const bool ascending : {true, false}) {
    const auto bytes = build(ascending);
    HkxPackfile packfile;
    ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
    ASSERT_EQ(packfile.objects.size(), 2U);
    fo4::hkx::HkxPackfileDiagnostics report;
    ASSERT_EQ(fo4::hkx::diagnose_packfile(packfile, report), HkxStatus::Ok);
    EXPECT_EQ(report.issues.size(), ascending ? 0U : 1U);
  }
}

namespace {

fo4::hkx::HkxSkeleton three_bone_skeleton() {
  fo4::hkx::HkxSkeleton skeleton;
  skeleton.name = "Rig";
  skeleton.bone_names = {"Root", "LArm", "RArm"};
  skeleton.parent_indices = {-1, 0, 0};
  skeleton.reference_pose = {fo4::hkx::HkxQsTransform{}, fo4::hkx::HkxQsTransform{}, fo4::hkx::HkxQsTransform{}};
  return skeleton;
}

}  // namespace

TEST(HkxSkeletonDocument, ReportsDepthsChildrenAndTheLeftRightPairs) {
  const auto document = fo4::hkx::skeleton_document(three_bone_skeleton());
  ASSERT_EQ(document.bones.size(), 3U);
  EXPECT_EQ(document.bones[0].depth, 0U);
  EXPECT_EQ(document.bones[1].depth, 1U);
  ASSERT_EQ(document.bones[0].children.size(), 2U);
  EXPECT_EQ(document.bones[1].children.size(), 0U);
  ASSERT_EQ(document.roots.size(), 1U);
  EXPECT_EQ(document.roots[0], "Root");
  // Constraint: a pairing guessed from the prefix alone would name a bone that does not exist.
  EXPECT_EQ(document.bones[1].opposite, "RArm");
  EXPECT_EQ(document.bones[2].opposite, "LArm");
  EXPECT_TRUE(document.bones[0].opposite.empty());
  EXPECT_TRUE(document.has_reference_pose);
}

TEST(HkxSkeletonDocument, RefusesARenameThatCollidesInsteadOfMergingTwoBones) {
  fo4::hkx::HkxSkeletonEdit edit;
  edit.kind = fo4::hkx::HkxSkeletonEditKind::Rename;
  edit.bone = 1;
  edit.name = "rarm";
  fo4::hkx::HkxSkeleton out;
  const auto report = fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out);
  EXPECT_FALSE(report.changed);
  EXPECT_NE(report.refusal.find("already named"), std::string::npos);
  EXPECT_TRUE(out.bone_names.empty());

  edit.name = "LHand";
  const auto renamed = fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out);
  EXPECT_TRUE(renamed.changed);
  ASSERT_EQ(out.bone_names.size(), 3U);
  EXPECT_EQ(out.bone_names[1], "LHand");
}

TEST(HkxSkeletonDocument, RefusesAParentThatComesAfterItsChild) {
  // Constraint: detects a reparent that leaves the bone list unsorted, which every pose walk then misreads.
  fo4::hkx::HkxSkeletonEdit edit;
  edit.kind = fo4::hkx::HkxSkeletonEditKind::Reparent;
  edit.bone = 0;
  edit.parent = 2;
  fo4::hkx::HkxSkeleton out;
  const auto report = fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out);
  EXPECT_FALSE(report.changed);
  EXPECT_NE(report.refusal.find("child of this bone"), std::string::npos);
}

TEST(HkxSkeletonDocument, RemovesALeafAndRemapsLaterParentIndices) {
  const auto skeleton = three_bone_skeleton();
  fo4::hkx::HkxSkeletonEdit edit;
  edit.kind = fo4::hkx::HkxSkeletonEditKind::Remove;
  edit.bone = 1;
  fo4::hkx::HkxSkeleton out;
  const auto report = fo4::hkx::apply_skeleton_edit(skeleton, edit, out);
  EXPECT_TRUE(report.changed);
  ASSERT_EQ(out.bone_names.size(), 2U);
  EXPECT_EQ(out.bone_names[1], "RArm");
  ASSERT_EQ(out.parent_indices.size(), 2U);
  EXPECT_EQ(out.parent_indices[1], 0);
  EXPECT_EQ(out.reference_pose.size(), 2U);

  edit.bone = 0;
  fo4::hkx::HkxSkeleton refused;
  const auto parent = fo4::hkx::apply_skeleton_edit(skeleton, edit, refused);
  EXPECT_FALSE(parent.changed);
  EXPECT_NE(parent.refusal.find("children"), std::string::npos);
}

// Constraint: detects a reparent to the root refused because -1 was cast to an unsigned index before the range check.
TEST(HkxSkeletonDocument, ReparentsABoneToTheRootAndRefusesAnOverlongName) {
  fo4::hkx::HkxSkeletonEdit edit;
  edit.kind = fo4::hkx::HkxSkeletonEditKind::Reparent;
  edit.bone = 2;
  edit.parent = -1;
  fo4::hkx::HkxSkeleton out;
  EXPECT_TRUE(fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out).changed);
  ASSERT_EQ(out.parent_indices.size(), 3U);
  EXPECT_EQ(out.parent_indices[2], -1);

  edit.parent = -2;
  EXPECT_FALSE(fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out).changed);

  edit.kind = fo4::hkx::HkxSkeletonEditKind::Rename;
  edit.name = std::string(256, 'n');
  const auto report = fo4::hkx::apply_skeleton_edit(three_bone_skeleton(), edit, out);
  EXPECT_FALSE(report.changed);
  EXPECT_NE(report.refusal.find("255"), std::string::npos);
}

// Constraint: detects a removal that leaves lock_translation longer than bone_names, so a saved flag lands on the wrong bone.
TEST(HkxSkeletonDocument, RemoveKeepsTheLockFlagsAlignedWithTheBones) {
  auto skeleton = three_bone_skeleton();
  skeleton.lock_translation = {true, false, true};
  fo4::hkx::HkxSkeletonEdit edit;
  edit.kind = fo4::hkx::HkxSkeletonEditKind::Remove;
  edit.bone = 1;
  fo4::hkx::HkxSkeleton out;
  ASSERT_TRUE(fo4::hkx::apply_skeleton_edit(skeleton, edit, out).changed);
  ASSERT_EQ(out.lock_translation.size(), 2U);
  EXPECT_TRUE(out.lock_translation[0]);
  EXPECT_TRUE(out.lock_translation[1]);
}

// Constraint: detects an edit list that does not name the failing edit or that leaves a half-applied skeleton behind.
TEST(HkxSkeletonDocument, AppliesAListInOrderAndRefusesTheWholeListOnTheFirstBadEdit) {
  using fo4::hkx::HkxSkeletonEdit;
  using fo4::hkx::HkxSkeletonEditKind;
  HkxSkeletonEdit rename;
  rename.kind = HkxSkeletonEditKind::Rename;
  rename.bone = 1;
  rename.name = "Elbow";
  HkxSkeletonEdit remove;
  remove.kind = HkxSkeletonEditKind::Remove;
  remove.bone = 2;
  fo4::hkx::HkxSkeleton out;
  std::vector<std::size_t> origin;
  const std::vector<HkxSkeletonEdit> good{rename, remove};
  EXPECT_TRUE(fo4::hkx::apply_skeleton_edits(three_bone_skeleton(), good, out, &origin).changed);
  EXPECT_EQ(out.bone_names, (std::vector<std::string>{"Root", "Elbow"}));
  EXPECT_EQ(origin, (std::vector<std::size_t>{0, 1}));

  HkxSkeletonEdit absent = remove;
  absent.bone = 7;
  fo4::hkx::HkxSkeleton untouched;
  const std::vector<HkxSkeletonEdit> bad{rename, absent};
  const auto report = fo4::hkx::apply_skeleton_edits(three_bone_skeleton(), bad, untouched);
  EXPECT_FALSE(report.changed);
  EXPECT_NE(report.refusal.find("edit 2"), std::string::npos);
  EXPECT_TRUE(untouched.bone_names.empty());
}

namespace {

using fo4::hkx::HkxSkeletonEdit;
using fo4::hkx::HkxSkeletonEditKind;
using fo4::hkx::HkxSkeletonWrite;

HkxSkeletonEdit edit_of(HkxSkeletonEditKind kind, std::size_t bone, std::string name = {}, std::int16_t parent = -1) {
  HkxSkeletonEdit edit;
  edit.kind = kind;
  edit.bone = bone;
  edit.name = std::move(name);
  edit.parent = parent;
  return edit;
}

HkxStatus write_edits(const std::vector<std::byte>& source, const std::vector<HkxSkeletonEdit>& edits,
                      HkxSkeletonWrite& out) {
  return fo4::hkx::write_skeleton(source, edits, out);
}

bool read_rig(std::span<const std::byte> bytes, HkxSkeleton& out) {
  HkxPackfile packfile;
  return read_packfile(bytes, packfile) == HkxStatus::Ok && read_skeleton(packfile, out) == HkxStatus::Ok;
}

}  // namespace

// Constraint: detects a rename that rewrites a shared string or moves an unrelated byte of the data section.
TEST(HkxSkeletonWrite, RenameAppendsOneStringAndKeepsEveryOriginalDataByte) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const std::vector<HkxSkeletonEdit> edits{edit_of(HkxSkeletonEditKind::Rename, 1, "Torso")};
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, edits, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);

  HkxSkeleton before;
  HkxSkeleton after;
  ASSERT_TRUE(read_rig(source, before));
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.bone_names, (std::vector<std::string>{"Root", "Torso", "Head"}));
  EXPECT_EQ(after.parent_indices, before.parent_indices);
  EXPECT_EQ(after.lock_translation, before.lock_translation);
  EXPECT_EQ(after.reference_floats, before.reference_floats);
  ASSERT_EQ(after.float_slots.size(), before.float_slots.size());
  EXPECT_EQ(after.float_slots[2].name, "twist");
  EXPECT_EQ(after.local_frames.size(), before.local_frames.size());
  EXPECT_EQ(after.partitions.size(), before.partitions.size());
  for (std::size_t i = 0; i < before.reference_pose.size(); ++i) {
    EXPECT_EQ(after.reference_pose[i].translation, before.reference_pose[i].translation);
  }

  HkxPackfile was;
  HkxPackfile now;
  ASSERT_EQ(read_packfile(source, was), HkxStatus::Ok);
  ASSERT_EQ(read_packfile(written.bytes, now), HkxStatus::Ok);
  const auto& old_data = was.sections[was.data_section].data;
  const auto& new_data = now.sections[now.data_section].data;
  ASSERT_GE(new_data.size(), old_data.size());
  EXPECT_TRUE(std::equal(old_data.begin(), old_data.end(), new_data.begin()));
  EXPECT_EQ(now.objects.size(), was.objects.size());
}

// Constraint: detects a rename that renames every bone pointing at one shared string.
TEST(HkxSkeletonWrite, RenameRetargetsOnlyTheNamedBoneWhenStringsAreShared) {
  const auto source = two_skeleton_packfile("Big", 3, "Small", 1, "Big");
  const std::vector<HkxSkeletonEdit> edits{edit_of(HkxSkeletonEditKind::Rename, 1, "Elbow")};
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, edits, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);

  HkxSkeleton big;
  ASSERT_TRUE(read_rig(written.bytes, big));
  EXPECT_EQ(big.bone_names, (std::vector<std::string>{"bone", "Elbow", "bone"}));

  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(written.bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(packfile.objects.size(), 3U);
  HkxSkeleton ragdoll;
  ASSERT_EQ(fo4::hkx::read_skeleton_object(packfile, packfile.objects[1], ragdoll), HkxStatus::Ok);
  EXPECT_EQ(ragdoll.bone_names, (std::vector<std::string>{"bone"}));
}

// Constraint: detects a reparent that changes any byte of the file other than that bone parent index.
TEST(HkxSkeletonWrite, ReparentPatchesOnlyTheParentIndexBytes) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Reparent, 2, {}, 0)}, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);
  ASSERT_EQ(written.bytes.size(), source.size());
  std::size_t differing = 0;
  for (std::size_t i = 0; i < source.size(); ++i) differing += source[i] != written.bytes[i] ? 1 : 0;
  EXPECT_EQ(differing, 1U);

  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.parent_indices, (std::vector<std::int16_t>{-1, 0, 0}));
  EXPECT_EQ(after.bone_names, (std::vector<std::string>{"Root", "Spine", "Head"}));

  HkxSkeletonWrite to_root;
  ASSERT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Reparent, 2, {}, -1)}, to_root),
            HkxStatus::Ok);
  HkxSkeleton rooted;
  ASSERT_TRUE(read_rig(to_root.bytes, rooted));
  EXPECT_EQ(rooted.parent_indices, (std::vector<std::int16_t>{-1, 0, -1}));
}

// Constraint: detects a removal that does not compact bones, parents, pose and lock flags together.
TEST(HkxSkeletonWrite, RemoveCompactsBonesParentsPoseAndLockFlagsTogether) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 0}, 3, false);
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Remove, 1)}, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);

  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.bone_names, (std::vector<std::string>{"Root", "Head"}));
  EXPECT_EQ(after.parent_indices, (std::vector<std::int16_t>{-1, 0}));
  ASSERT_EQ(after.reference_pose.size(), 2U);
  EXPECT_EQ(after.reference_pose[0].translation[0], 1.0f);
  EXPECT_EQ(after.reference_pose[1].translation[0], 3.0f);
  EXPECT_EQ(after.lock_translation, (std::vector<bool>{true, true}));
  EXPECT_EQ(after.reference_floats, (std::vector<float>{1.5f, -2.25f}));
  EXPECT_EQ(after.float_slots.size(), 3U);
}

// Constraint: detects a removal that silently shifts bone indices other data or partitions may use.
TEST(HkxSkeletonWrite, RemoveIsRefusedWhenOtherDataCouldUseTheBoneIndices) {
  const auto partitioned = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeletonWrite written;
  EXPECT_EQ(write_edits(partitioned, {edit_of(HkxSkeletonEditKind::Remove, 2)}, written),
            HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("partitions"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());

  const auto bound = two_skeleton_packfile("Big", 3, "Small", 1, "Big");
  EXPECT_EQ(write_edits(bound, {edit_of(HkxSkeletonEditKind::Remove, 2)}, written),
            HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("hkaAnimationBinding"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());
}

// Constraint: detects a no-op edit that still produces a file, so Save cannot rewrite an unchanged skeleton.
TEST(HkxSkeletonWrite, EditsThatChangeNothingProduceNoBytes) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeletonWrite written;
  EXPECT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Rename, 1, "Spine")}, written),
            HkxStatus::Ok);
  EXPECT_FALSE(written.changed);
  EXPECT_TRUE(written.bytes.empty());
  EXPECT_EQ(write_edits(source, {}, written), HkxStatus::Ok);
  EXPECT_FALSE(written.changed);
}

// Constraint: detects a refused or malformed request that returns bytes or fails to name the failing edit.
TEST(HkxSkeletonWrite, RefusedAndMalformedRequestsReturnNoBytes) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeletonWrite written;
  EXPECT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Rename, 2, "root")}, written),
            HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("edit 1"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());

  const auto lone = skeleton_packfile({"Root"}, {-1}, 1, false);
  EXPECT_EQ(write_edits(lone, {edit_of(HkxSkeletonEditKind::Remove, 0)}, written),
            HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("at least one bone"), std::string::npos);

  const std::vector<std::byte> cut(source.begin(), source.begin() + 40);
  EXPECT_NE(write_edits(cut, {edit_of(HkxSkeletonEditKind::Rename, 1, "Torso")}, written), HkxStatus::Ok);
  EXPECT_TRUE(written.bytes.empty());
  EXPECT_FALSE(written.changed);
}

namespace {

HkxSkeletonEdit pose_edit_of(std::size_t bone, std::array<float, 3> translation,
                             std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                             std::array<float, 3> scale = {1.0f, 1.0f, 1.0f}) {
  HkxSkeletonEdit edit;
  edit.kind = HkxSkeletonEditKind::SetPose;
  edit.bone = bone;
  edit.pose.translation = translation;
  edit.pose.rotation = rotation;
  edit.pose.scale = scale;
  return edit;
}

HkxSkeletonEdit lock_edit_of(std::size_t bone, bool locked) {
  HkxSkeletonEdit edit;
  edit.kind = HkxSkeletonEditKind::SetLock;
  edit.bone = bone;
  edit.locked = locked;
  return edit;
}

std::size_t pose_file_offset(std::span<const std::byte> bytes) {
  HkxPackfile packfile;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) return 0;
  const auto& data = packfile.sections[packfile.data_section];
  for (const auto& object : packfile.objects) {
    if (object.class_name != "hkaSkeleton") continue;
    const std::size_t field = object.offset + member_at("hkaSkeleton", "referencePose");
    for (const auto& fixup : data.local_fixups) {
      if (fixup.source == field) return data.file_offset + fixup.destination;
    }
  }
  return 0;
}

std::vector<std::size_t> differing_offsets(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
  std::vector<std::size_t> at;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
    if (a[i] != b[i]) at.push_back(i);
  }
  return at;
}

}  // namespace

// Constraint: detects a pose edit (the BGS SkeletonPoseSave case) that rewrites a whole row and clobbers padding or a neighbour.
TEST(HkxSkeletonWrite, ReferencePoseEditPatchesOnlyTheEditedLaneAndKeepsPadding) {
  auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const std::size_t pose_at = pose_file_offset(source);
  ASSERT_NE(pose_at, 0U);
  source[pose_at + 12] = std::byte{0xAB};
  HkxSkeleton before;
  ASSERT_TRUE(read_rig(source, before));

  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {pose_edit_of(0, {12.5f, 0.0f, 0.0f})}, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);
  ASSERT_EQ(written.bytes.size(), source.size());
  const auto differing = differing_offsets(source, written.bytes);
  ASSERT_FALSE(differing.empty());
  for (const std::size_t at : differing) {
    EXPECT_GE(at, pose_at);
    EXPECT_LT(at, pose_at + 4) << "only translation x of bone 0 may change";
  }
  EXPECT_EQ(written.bytes[pose_at + 12], std::byte{0xAB});

  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.reference_pose[0].translation, (std::array<float, 3>{12.5f, 0.0f, 0.0f}));
  EXPECT_EQ(after.reference_pose[1].translation, before.reference_pose[1].translation);
  EXPECT_EQ(after.reference_pose[2].translation, before.reference_pose[2].translation);
  EXPECT_EQ(after.bone_names, before.bone_names);
  EXPECT_EQ(after.parent_indices, before.parent_indices);
  EXPECT_EQ(after.lock_translation, before.lock_translation);
  EXPECT_EQ(after.reference_floats, before.reference_floats);
  EXPECT_EQ(after.partitions.size(), before.partitions.size());
}

// Constraint: detects rotation and scale lanes written at the wrong row offset.
TEST(HkxSkeletonWrite, ReferencePoseEditWritesRotationAndScaleToTheRightLanes) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const std::array<float, 4> turn{0.0f, 0.5f, 0.0f, 0.8660254f};
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {pose_edit_of(2, {4.0f, -5.0f, 6.0f}, turn, {2.0f, 3.0f, 4.0f})}, written),
            HkxStatus::Ok);
  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.reference_pose[2].translation, (std::array<float, 3>{4.0f, -5.0f, 6.0f}));
  EXPECT_EQ(after.reference_pose[2].rotation, turn);
  EXPECT_EQ(after.reference_pose[2].scale, (std::array<float, 3>{2.0f, 3.0f, 4.0f}));
  EXPECT_EQ(after.reference_pose[1].rotation, (std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
}

// Constraint: detects a lock edit that writes the wrong byte of a bone row and flips another bone.
TEST(HkxSkeletonWrite, LockTranslationEditFlipsOnlyThatBonesLockByte) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeleton before;
  ASSERT_TRUE(read_rig(source, before));
  ASSERT_EQ(before.lock_translation, (std::vector<bool>{true, false, true}));

  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {lock_edit_of(1, true), lock_edit_of(2, false)}, written), HkxStatus::Ok);
  ASSERT_TRUE(written.changed);
  ASSERT_EQ(written.bytes.size(), source.size());
  EXPECT_EQ(differing_offsets(source, written.bytes).size(), 2U);
  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.lock_translation, (std::vector<bool>{true, true, false}));
  EXPECT_EQ(after.bone_names, before.bone_names);
  EXPECT_EQ(after.parent_indices, before.parent_indices);
  for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(after.reference_pose[i].translation, before.reference_pose[i].translation);

  HkxSkeletonWrite same;
  EXPECT_EQ(write_edits(source, {lock_edit_of(0, true), pose_edit_of(0, {1.0f, 0.0f, 0.0f})}, same), HkxStatus::Ok);
  EXPECT_FALSE(same.changed);
  EXPECT_TRUE(same.bytes.empty());
}

// Constraint: detects a pose or lock edit after a Remove that lands on the bone that held the index in the file.
TEST(HkxSkeletonWrite, PoseAndLockEditsAfterARemoveLandOnTheShiftedBone) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 0}, 3, false);
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source,
                        {edit_of(HkxSkeletonEditKind::Remove, 1), pose_edit_of(1, {9.0f, 0.0f, 0.0f}),
                         lock_edit_of(1, false)},
                        written),
            HkxStatus::Ok);
  HkxSkeleton after;
  ASSERT_TRUE(read_rig(written.bytes, after));
  EXPECT_EQ(after.bone_names, (std::vector<std::string>{"Root", "Head"}));
  EXPECT_EQ(after.reference_pose[0].translation[0], 1.0f);
  EXPECT_EQ(after.reference_pose[1].translation[0], 9.0f);
  EXPECT_EQ(after.lock_translation, (std::vector<bool>{true, false}));
}

// Constraint: detects a NaN, infinite, zero-length or off-unit rotation that differs from the stored one, or a missing bone, returning bytes.
TEST(HkxSkeletonWrite, UnusablePoseValuesAreRefusedWithNoBytes) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const std::vector<HkxSkeletonEdit> bad{
      pose_edit_of(0, {nan, 0.0f, 0.0f}),
      pose_edit_of(0, {inf, 0.0f, 0.0f}),
      pose_edit_of(0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 2.0f}),
      pose_edit_of(0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}),
      pose_edit_of(0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, inf, 1.0f}),
      pose_edit_of(3, {0.0f, 0.0f, 0.0f}),
      lock_edit_of(3, true),
  };
  for (const auto& edit : bad) {
    HkxSkeletonWrite written;
    EXPECT_EQ(write_edits(source, {edit}, written), HkxStatus::UnsupportedLayout);
    EXPECT_NE(written.refusal.find("edit 1"), std::string::npos) << written.refusal;
    EXPECT_TRUE(written.bytes.empty());
    EXPECT_FALSE(written.changed);
  }
}

// Constraint: detects SetPose or SetLock on a skeleton without one value per bone indexing past the vector.
TEST(HkxSkeletonDocument, PoseAndLockEditsRefuseASkeletonThatLacksThePerBoneValues) {
  auto skeleton = three_bone_skeleton();
  fo4::hkx::HkxSkeleton out;
  EXPECT_FALSE(fo4::hkx::apply_skeleton_edit(skeleton, lock_edit_of(1, true), out).changed);
  EXPECT_TRUE(out.bone_names.empty());
  skeleton.lock_translation = {false, false, false};
  EXPECT_TRUE(fo4::hkx::apply_skeleton_edit(skeleton, lock_edit_of(1, true), out).changed);
  EXPECT_TRUE(out.lock_translation[1]);
  skeleton.reference_pose.clear();
  EXPECT_FALSE(fo4::hkx::apply_skeleton_edit(skeleton, pose_edit_of(1, {1.0f, 0.0f, 0.0f}), out).changed);
}

// Constraint: detects a translation edit refused because the stored rotation is slightly off unit and the panel resent it unchanged.
TEST(HkxSkeletonDocument, PoseEditResendingAStoredOffUnitRotationIsAccepted) {
  auto skeleton = three_bone_skeleton();
  const std::array<float, 4> stored{0.0f, 0.0f, 0.0f, 1.01f};
  skeleton.reference_pose[1].rotation = stored;
  fo4::hkx::HkxSkeleton out;
  const auto resent = fo4::hkx::apply_skeleton_edit(skeleton, pose_edit_of(1, {2.0f, 0.0f, 0.0f}, stored), out);
  EXPECT_TRUE(resent.changed) << resent.refusal;
  EXPECT_EQ(out.reference_pose[1].rotation, stored);
  EXPECT_EQ(out.reference_pose[1].translation, (std::array<float, 3>{2.0f, 0.0f, 0.0f}));
  EXPECT_FALSE(fo4::hkx::apply_skeleton_edit(skeleton, pose_edit_of(1, {2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.02f}), out).changed);
  skeleton.reference_pose[1].rotation = {0.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_FALSE(fo4::hkx::apply_skeleton_edit(skeleton, pose_edit_of(1, {2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}), out).changed);
}

namespace {

std::vector<TestSection> sections_of(const HkxPackfile& packfile) {
  std::vector<TestSection> out;
  for (const auto& section : packfile.sections) {
    TestSection copy{section.tag, section.data, {}, {}, {}};
    for (const auto& fixup : section.local_fixups) {
      copy.local.insert(copy.local.end(), {fixup.source, fixup.destination});
    }
    for (const auto& fixup : section.global_fixups) {
      copy.global.insert(copy.global.end(), {fixup.source, fixup.section, fixup.destination});
    }
    for (const auto& fixup : section.virtual_fixups) {
      copy.virtuals.insert(copy.virtuals.end(), {fixup.source, fixup.section, fixup.destination});
    }
    out.push_back(std::move(copy));
  }
  return out;
}

std::uint32_t local_destination(const TestSection& section, std::size_t source) {
  for (std::size_t i = 0; i + 1 < section.local.size(); i += 2) {
    if (section.local[i] == source) return section.local[i + 1];
  }
  return 0;
}

void retarget_local(TestSection& section, std::size_t source, std::size_t destination) {
  for (std::size_t i = 0; i + 1 < section.local.size(); i += 2) {
    if (section.local[i] == source) section.local[i + 1] = static_cast<std::uint32_t>(destination);
  }
}

}  // namespace

// Constraint: detects a count change that drops the 0x80000000 flag of an array capacity word or leaves a stale capacity.
TEST(HkxSkeletonWrite, RemoveKeepsTheCapacityFlagsAndSetsTheNewCount) {
  auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 0}, 3, false);
  HkxPackfile before;
  ASSERT_EQ(read_packfile(source, before), HkxStatus::Ok);
  const std::array<const char*, 3> members{"bones", "parentIndices", "referencePose"};
  for (const char* member : members) {
    patch_u32(source, before.sections[before.data_section].file_offset + before.objects[0].offset +
                          member_at("hkaSkeleton", member) + 12,
              0x80000003u);
  }
  HkxSkeletonWrite written;
  ASSERT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Remove, 1)}, written), HkxStatus::Ok);
  HkxPackfile after;
  ASSERT_EQ(read_packfile(written.bytes, after), HkxStatus::Ok);
  const auto& data = after.sections[after.data_section].data;
  for (const char* member : members) {
    std::uint32_t count = 0;
    std::uint32_t capacity = 0;
    const std::size_t field = after.objects[0].offset + member_at("hkaSkeleton", member);
    std::memcpy(&count, data.data() + field + 8, sizeof count);
    std::memcpy(&capacity, data.data() + field + 12, sizeof capacity);
    EXPECT_EQ(count, 2U) << member;
    EXPECT_EQ(capacity, 0x80000002u) << member;
  }
}

// Constraint: detects a writer that moves arrays another object points into, reads from, or shares with a sibling array.
TEST(HkxSkeletonWrite, ArraysThatOtherDataSharesOrPointsIntoAreRefused) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3, false);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(source, packfile), HkxStatus::Ok);
  const std::size_t bones_header = packfile.objects[0].offset + member_at("hkaSkeleton", "bones");
  const std::size_t parents_header = packfile.objects[0].offset + member_at("hkaSkeleton", "parentIndices");
  const std::size_t slots_header = packfile.objects[0].offset + member_at("hkaSkeleton", "floatSlots");
  const std::vector<HkxSkeletonEdit> edits{edit_of(HkxSkeletonEditKind::Reparent, 2, {}, 0)};
  HkxSkeletonWrite written;

  auto sections = sections_of(packfile);
  auto& data = sections[packfile.data_section];
  const auto bones_at = local_destination(data, bones_header);
  const auto parents_at = local_destination(data, parents_header);
  const auto slots_at = local_destination(data, slots_header);
  const std::size_t bone_size = fo4::hkx::find_layout("hkaBone")->size;

  auto pointed = sections;
  pointed[packfile.data_section].local.insert(pointed[packfile.data_section].local.end(), {slots_at + 8, parents_at});
  EXPECT_EQ(write_edits(build_packfile(pointed), edits, written), HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("shares the storage"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());

  auto inside = sections;
  const auto extra = add_class_name(inside[packfile.class_names_section].data, 0x1234u, "hkaTestExtra");
  inside[packfile.data_section].virtuals.insert(
      inside[packfile.data_section].virtuals.end(),
      {static_cast<std::uint32_t>(bones_at + 16), static_cast<std::uint32_t>(packfile.class_names_section), extra});
  EXPECT_EQ(write_edits(build_packfile(inside), edits, written), HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("starts inside"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());

  auto shared = sections;
  const std::size_t spot = bones_at + 2 * bone_size + member_at("hkaBone", "name");
  const std::array<unsigned char, 6> indices{0xFF, 0xFF, 0, 0, 1, 0};
  for (std::size_t i = 0; i < indices.size(); ++i) {
    shared[packfile.data_section].data[spot + i] = static_cast<std::byte>(indices[i]);
  }
  retarget_local(shared[packfile.data_section], parents_header, spot);
  EXPECT_EQ(write_edits(build_packfile(shared), edits, written), HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("share storage"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());
}

// Constraint: detects an overlong name that the reader would later refuse to read back.
TEST(HkxSkeletonWrite, ANameOverTheReaderCapIsRefusedBeforeAnyBytesAreProduced) {
  const auto source = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxSkeletonWrite written;
  EXPECT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Rename, 1, std::string(256, 'n'))}, written),
            HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("255"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());
  EXPECT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Rename, 1, std::string(255, 'n'))}, written),
            HkxStatus::Ok);
  EXPECT_TRUE(written.changed);
}

// Constraint: detects a removal whose result the reader would pick another rig for, which fails after an in-place replace.
TEST(HkxSkeletonWrite, RemovalThatMakesTwoRigsTieIsRefusedBeforeWriting) {
  const auto source = two_skeleton_packfile("Big", 5, "Small", 4);
  HkxSkeletonWrite written;
  EXPECT_EQ(write_edits(source, {edit_of(HkxSkeletonEditKind::Remove, 4)}, written), HkxStatus::UnsupportedLayout);
  EXPECT_NE(written.refusal.find("no longer"), std::string::npos);
  EXPECT_TRUE(written.bytes.empty());
}

namespace {

std::vector<std::byte> behavior_packfile() {
  const auto graph = fo4::hkx::find_layout("hkbAssetBundle");
  const auto generator = fo4::hkx::find_layout("hkbAnimatedSkeletonGenerator");
  std::vector<std::byte> names;
  const auto graph_name = add_class_name(names, graph->signature, "hkbAssetBundle");
  const auto generator_name = add_class_name(names, generator->signature, "hkbAnimatedSkeletonGenerator");

  std::vector<std::byte> data(graph->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const std::size_t generator_at = data.size();
  data.resize(generator_at + generator->size, std::byte{0});
  link(local, 0, generator_at);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {},
                  {0, 0, graph_name, static_cast<std::uint32_t>(generator_at), 0, generator_name}},
  });
}

}  // namespace

TEST(HkxBehaviorGraph, LinksTwoBehaviorObjectsThroughTheirPointer) {
  const auto bytes = behavior_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxBehaviorGraph graph;
  ASSERT_EQ(fo4::hkx::inspect_behavior(packfile, 64, graph), HkxStatus::Ok);
  EXPECT_EQ(graph.matched, 2U);
  ASSERT_EQ(graph.nodes.size(), 2U);
  EXPECT_EQ(graph.nodes[0].class_name, "hkbAssetBundle");
  EXPECT_EQ(graph.nodes[1].class_name, "hkbAnimatedSkeletonGenerator");
  ASSERT_EQ(graph.links.size(), 1U);
  EXPECT_EQ(graph.links[0].from, 0U);
  EXPECT_EQ(graph.links[0].to, 1U);
  EXPECT_EQ(graph.nodes[0].references_out, 1U);
  EXPECT_EQ(graph.nodes[1].references_in, 1U);
  ASSERT_EQ(graph.classes.size(), 2U);
}

TEST(HkxBehaviorGraph, SaysAPackfileWithoutBehaviorObjectsHasNoGraph) {
  // Constraint: detects a skeleton-only file reported as an empty graph, which reads as a graph with no nodes.
  const auto bytes = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxBehaviorGraph graph;
  ASSERT_EQ(fo4::hkx::inspect_behavior(packfile, 64, graph), HkxStatus::Ok);
  EXPECT_TRUE(graph.nodes.empty());
  ASSERT_EQ(graph.diagnostics.size(), 1U);
  EXPECT_NE(graph.diagnostics[0].find("no behavior objects"), std::string::npos);
}

TEST(HkxProjectIndex, CountsBehaviorAnimationAndSkeletonObjectsPerFile) {
  const auto behavior = behavior_packfile();
  const auto graph = fo4::hkx::index_project_file("Characters/graph.hkx", behavior);
  EXPECT_TRUE(graph.read_ok);
  EXPECT_EQ(graph.behavior_objects, 2U);
  EXPECT_EQ(graph.object_count, 2U);
  // Constraint: a zero hash would group every behavior file with every other, so the entry must hash its objects.
  EXPECT_NE(graph.behavior_hash, 0U);

  const auto rig = fo4::hkx::index_project_file("rig.hkx", object_graph_packfile());
  EXPECT_TRUE(rig.read_ok);
  EXPECT_EQ(rig.behavior_objects, 0U);
  EXPECT_EQ(rig.behavior_hash, 0U);
  EXPECT_EQ(rig.skeleton_objects, 1U);
}

TEST(HkxProjectIndex, KeepsAFileThatDoesNotParseWithItsError) {
  // Constraint: detects a scan that drops an unreadable file, which reads as a project without it.
  const std::vector<std::byte> junk{std::byte{1}, std::byte{2}, std::byte{3}};
  const auto entry = fo4::hkx::index_project_file("broken.hkx", junk);
  EXPECT_FALSE(entry.read_ok);
  EXPECT_FALSE(entry.error.empty());
  EXPECT_EQ(entry.behavior_hash, 0U);
}

TEST(HkxProjectIndex, SearchesByFileNameAndByClassIgnoringCase) {
  fo4::hkx::HkxProjectIndex index;
  index.files.push_back(fo4::hkx::index_project_file("walk.hkx", object_graph_packfile()));
  index.files.push_back(fo4::hkx::index_project_file("graph.hkx", behavior_packfile()));

  const auto by_name = fo4::hkx::search_project_index(index, "GRAPH");
  ASSERT_EQ(by_name.size(), 1U);
  EXPECT_EQ(index.files[by_name[0]].name, "graph.hkx");

  const auto by_class = fo4::hkx::search_project_index(index, "hkbanimatedskeleton");
  ASSERT_EQ(by_class.size(), 1U);
  EXPECT_EQ(index.files[by_class[0]].name, "graph.hkx");

  EXPECT_EQ(fo4::hkx::search_project_index(index, "").size(), 2U);
  EXPECT_TRUE(fo4::hkx::search_project_index(index, "nothing-here").empty());
}

TEST(HkxProjectIndex, GroupsFilesThatHoldTheSameBehaviorBytes) {
  fo4::hkx::HkxProjectIndex index;
  index.files.push_back(fo4::hkx::index_project_file("a/graph.hkx", behavior_packfile()));
  index.files.push_back(fo4::hkx::index_project_file("b/graph.hkx", behavior_packfile()));
  index.files.push_back(fo4::hkx::index_project_file("rig.hkx", object_graph_packfile()));

  const auto groups = fo4::hkx::duplicate_behavior_groups(index);
  ASSERT_EQ(groups.size(), 1U);
  ASSERT_EQ(groups[0].size(), 2U);
  EXPECT_EQ(index.files[groups[0][0]].name, "a/graph.hkx");
  EXPECT_EQ(index.files[groups[0][1]].name, "b/graph.hkx");
}

namespace {

constexpr std::uint32_t kSceneSignature = 0x5C3E0001u;
constexpr std::uint32_t kEnvironmentSignature = 0x5C3E0002u;

// Invariant: every object starts on 16 bytes and the data section ends on 16, so a rewrite adds no filler to the last object.
std::vector<std::byte> scene_packfile(const std::string& environment_declared_class = "hkxEnvironment") {
  const auto* root_class = fo4::hkx::find_layout("hkRootLevelContainer");
  const auto* variant_class = fo4::hkx::find_layout("hkRootLevelContainerNamedVariant");
  std::vector<std::byte> names;
  const auto root_name = add_class_name(names, root_class->signature, "hkRootLevelContainer");
  const auto scene_name = add_class_name(names, kSceneSignature, "hkxScene");
  const auto environment_name = add_class_name(names, kEnvironmentSignature, "hkxEnvironment");
  const auto* animation_class = fo4::hkx::find_layout("hkaAnimationContainer");
  const auto animation_name = add_class_name(names, animation_class->signature, "hkaAnimationContainer");

  std::vector<std::byte> data(root_class->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const std::size_t array_at = member_at("hkRootLevelContainer", "namedVariants");
  const std::size_t name_off = member_at("hkRootLevelContainerNamedVariant", "name");
  const std::size_t class_off = member_at("hkRootLevelContainerNamedVariant", "className");
  const std::size_t variant_off = member_at("hkRootLevelContainerNamedVariant", "variant");
  patch_u32(data, array_at + 8, 3);
  const std::size_t entries_at = data.size();
  data.resize(entries_at + 3 * variant_class->size, std::byte{0});
  link(local, array_at, entries_at);

  const auto append_string = [&](const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto pad16 = [&] { data.resize((data.size() + 15) / 16 * 16, std::byte{0}); };
  const std::size_t second = entries_at + variant_class->size;
  link(local, entries_at + name_off, append_string("Scene Data"));
  link(local, entries_at + class_off, append_string("hkxScene"));
  link(local, second + name_off, append_string("Environment"));
  link(local, second + class_off, append_string(environment_declared_class));
  const std::size_t third = second + variant_class->size;
  link(local, third + name_off, append_string("Animation"));
  link(local, third + class_off, append_string("hkaAnimationContainer"));
  pad16();

  const std::size_t scene_at = data.size();
  data.resize(scene_at + 32, std::byte{0x5A});
  std::fill(data.begin() + static_cast<std::ptrdiff_t>(scene_at), data.begin() + static_cast<std::ptrdiff_t>(scene_at + 8),
            std::byte{0});
  link(local, scene_at, append_string("Z-up"));
  pad16();
  const std::size_t environment_at = data.size();
  data.resize(environment_at + 16, std::byte{0xA5});
  const std::size_t animation_at = data.size();
  data.resize(animation_at + animation_class->size, std::byte{0});
  pad16();

  const auto at32 = [](std::size_t value) { return static_cast<std::uint32_t>(value); };
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local),
                  {at32(entries_at + variant_off), 2, at32(scene_at), at32(second + variant_off), 2, at32(environment_at),
                   at32(third + variant_off), 2, at32(animation_at)},
                  {0, 0, root_name, at32(scene_at), 0, scene_name, at32(environment_at), 0, environment_name,
                   at32(animation_at), 0, animation_name}},
  });
}

}  // namespace

TEST(HkxScene, ListsTheRootVariantsAndNamesTheSceneClassesItCannotDecode) {
  // Constraint: detects a scene wrapper skipped by the asset tree, or unit and axis data invented for a class with no verified layout.
  const auto bytes = scene_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSceneStructure scene;
  ASSERT_EQ(fo4::hkx::read_scene_structure(packfile, scene), HkxStatus::Ok);
  EXPECT_TRUE(scene.has_root);
  EXPECT_EQ(scene.object_count, 4U);
  ASSERT_EQ(scene.variants.size(), 3U);
  EXPECT_EQ(scene.variants[0].name, "Scene Data");
  EXPECT_EQ(scene.variants[0].declared_class, "hkxScene");
  EXPECT_EQ(scene.variants[0].class_name, "hkxScene");
  EXPECT_EQ(scene.variants[0].object, 1);
  EXPECT_FALSE(scene.variants[0].layout_known);
  EXPECT_EQ(scene.variants[1].name, "Environment");
  EXPECT_EQ(scene.variants[1].object, 2);
  EXPECT_FALSE(scene.variants[1].layout_known);
  EXPECT_EQ(scene.variants[2].name, "Animation");
  EXPECT_EQ(scene.variants[2].declared_class, "hkaAnimationContainer");
  EXPECT_EQ(scene.variants[2].class_name, "hkaAnimationContainer");
  EXPECT_EQ(scene.variants[2].object, 3);
  EXPECT_TRUE(scene.variants[2].layout_known);
  ASSERT_EQ(scene.scene_objects.size(), 2U);
  EXPECT_EQ(scene.scene_objects[0].class_name, "hkxScene");
  EXPECT_EQ(scene.scene_objects[0].object, 1U);
  EXPECT_EQ(scene.scene_objects[1].class_name, "hkxEnvironment");
  EXPECT_EQ(scene.unknown_classes, (std::vector<std::string>{"hkxScene", "hkxEnvironment"}));
  std::size_t undecoded = 0;
  for (const auto& line : scene.diagnostics) {
    if (line.find("not decoded") != std::string::npos) ++undecoded;
  }
  EXPECT_EQ(undecoded, 2U);
}

TEST(HkxScene, FlagsAVariantWhoseDeclaredClassDiffersFromItsObject) {
  // Constraint: detects a variant table trusted by its label alone, which would read an environment as a scene.
  const auto bytes = scene_packfile("hkxScene");
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSceneStructure scene;
  ASSERT_EQ(fo4::hkx::read_scene_structure(packfile, scene), HkxStatus::Ok);
  ASSERT_EQ(scene.variants.size(), 3U);
  EXPECT_EQ(scene.variants[1].class_name, "hkxEnvironment");
  EXPECT_EQ(scene.variants[1].declared_class, "hkxScene");
  const bool flagged = std::any_of(scene.diagnostics.begin(), scene.diagnostics.end(), [](const std::string& line) {
    return line.find("declares hkxScene but holds hkxEnvironment") != std::string::npos;
  });
  EXPECT_TRUE(flagged);
}

TEST(HkxScene, ReportsAFileWithoutARootAndRefusesAMalformedVariantTable) {
  // Constraint: detects a file with no container read as an empty asset, and a corrupt count that walks past the data section.
  const auto skeleton_only = object_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(skeleton_only, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSceneStructure scene;
  ASSERT_EQ(fo4::hkx::read_scene_structure(packfile, scene), HkxStatus::Ok);
  EXPECT_FALSE(scene.has_root);
  EXPECT_TRUE(scene.variants.empty());
  ASSERT_FALSE(scene.diagnostics.empty());
  EXPECT_NE(scene.diagnostics.back().find("no hkRootLevelContainer"), std::string::npos);

  const auto* root_class = fo4::hkx::find_layout("hkRootLevelContainer");
  const std::size_t array_at = member_at("hkRootLevelContainer", "namedVariants");
  auto huge = filled(root_class->size, std::byte{0});
  patch_u32(huge, array_at + 8, 5000);
  const auto huge_bytes = single_object_packfile("hkRootLevelContainer", root_class->signature, huge, {});
  HkxPackfile huge_file;
  ASSERT_EQ(read_packfile(huge_bytes, huge_file), HkxStatus::Ok);
  EXPECT_EQ(fo4::hkx::read_scene_structure(huge_file, scene), HkxStatus::BadSection);

  auto no_pointer = filled(root_class->size, std::byte{0});
  patch_u32(no_pointer, array_at + 8, 1);
  const auto no_pointer_bytes = single_object_packfile("hkRootLevelContainer", root_class->signature, no_pointer, {});
  HkxPackfile no_pointer_file;
  ASSERT_EQ(read_packfile(no_pointer_bytes, no_pointer_file), HkxStatus::Ok);
  EXPECT_EQ(fo4::hkx::read_scene_structure(no_pointer_file, scene), HkxStatus::BadSection);
}

TEST(HkxScene, UnknownSceneObjectSurvivesRewritePackfile) {
  // Constraint: detects a save that drops or reshuffles an object whose class has no layout, including its string pointer fixup.
  const auto bytes = scene_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  std::vector<std::byte> written;
  ASSERT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, written), HkxStatus::Ok);
  HkxPackfile reread;
  ASSERT_EQ(read_packfile(written, reread), HkxStatus::Ok);

  ASSERT_EQ(reread.objects.size(), packfile.objects.size());
  for (std::size_t index = 0; index < packfile.objects.size(); ++index) {
    EXPECT_EQ(reread.objects[index].class_name, packfile.objects[index].class_name);
    EXPECT_EQ(reread.objects[index].signature, packfile.objects[index].signature);
    EXPECT_EQ(reread.objects[index].offset, packfile.objects[index].offset);
    EXPECT_TRUE(std::equal(reread.objects[index].data.begin(), reread.objects[index].data.end(),
                           packfile.objects[index].data.begin(), packfile.objects[index].data.end()));
  }
  const auto& before = packfile.sections[packfile.data_section].local_fixups;
  const auto& after = reread.sections[reread.data_section].local_fixups;
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(after[index].source, before[index].source);
    EXPECT_EQ(after[index].destination, before[index].destination);
  }

  fo4::hkx::HkxSceneStructure first;
  fo4::hkx::HkxSceneStructure second;
  ASSERT_EQ(fo4::hkx::read_scene_structure(packfile, first), HkxStatus::Ok);
  ASSERT_EQ(fo4::hkx::read_scene_structure(reread, second), HkxStatus::Ok);
  ASSERT_EQ(second.variants.size(), first.variants.size());
  for (std::size_t index = 0; index < first.variants.size(); ++index) {
    EXPECT_EQ(second.variants[index].name, first.variants[index].name);
    EXPECT_EQ(second.variants[index].declared_class, first.variants[index].declared_class);
    EXPECT_EQ(second.variants[index].class_name, first.variants[index].class_name);
    EXPECT_EQ(second.variants[index].object, first.variants[index].object);
    EXPECT_EQ(second.variants[index].layout_known, first.variants[index].layout_known);
  }
  EXPECT_EQ(second.unknown_classes, first.unknown_classes);
}

namespace {

using fo4::hkx::HkxConversionMap;
using fo4::hkx::HkxConversionResult;
using fo4::hkx::HkxConvertStatus;
using fo4::hkx::HkxSemanticDocument;
using fo4::hkx::HkxValue;

HkxValue integer_value(std::int64_t value) {
  HkxValue out;
  out.kind = HkxValue::Kind::Integer;
  out.integer = value;
  return out;
}

HkxValue bool_value(bool value) {
  HkxValue out;
  out.kind = HkxValue::Kind::Bool;
  out.boolean = value;
  return out;
}

HkxValue reference_value(std::uint64_t id) {
  HkxValue out;
  out.kind = HkxValue::Kind::Reference;
  out.target = id;
  return out;
}

void add_object(HkxSemanticDocument& document, std::uint64_t id, const std::string& type,
                std::map<std::string, HkxValue> members = {}) {
  document.objects[id] = fo4::hkx::HkxSemanticObject{id, type, std::move(members)};
}

HkxConversionMap shared_reference_map() {
  HkxConversionMap map;
  map.source_version = "hk_new";
  map.target_version = "hk_2014.1.0-r1";
  map.evidence = "synthetic fixture";
  auto& rule = map.rules["hkaNewSkeleton"];
  rule.target_type = "hkaSkeleton";
  rule.renamed["childA"] = "primaryChild";
  rule.dropped.insert("newOnly");
  rule.defaults["enabled"] = bool_value(true);
  map.identity.insert("hkaBone");
  return map;
}

HkxSemanticDocument shared_reference_document() {
  HkxSemanticDocument document;
  document.root = 1;
  add_object(document, 1, "hkaNewSkeleton",
             {{"childA", reference_value(2)}, {"childB", reference_value(2)}, {"newOnly", integer_value(99)}});
  add_object(document, 2, "hkaBone");
  return document;
}

HkxSemanticDocument one_bone(std::int64_t mode) {
  HkxSemanticDocument document;
  add_object(document, 1, "hkaBone", {{"mode", integer_value(mode)}});
  return document;
}

HkxConversionMap bone_enum_map(std::map<std::int64_t, std::int64_t> values) {
  HkxConversionMap map;
  map.evidence = "synthetic fixture";
  auto& rule = map.rules["hkaBone"];
  rule.target_type = "hkaBone";
  rule.enums["mode"] = std::move(values);
  return map;
}

}  // namespace

TEST(HkxConvert, KeepsSharedReferencesAndCountsRenamesDropsAndDefaults) {
  // Constraint: detects a conversion that splits a shared child into two objects or counts an applied rename, drop or default as exact.
  HkxConversionResult result;
  ASSERT_EQ(fo4::hkx::convert_document(shared_reference_document(), shared_reference_map(), result), HkxConvertStatus::Ok);
  const auto& converted = result.document.objects.at(1);
  EXPECT_EQ(converted.type_name, "hkaSkeleton");
  ASSERT_TRUE(converted.members.at("primaryChild").target.has_value());
  EXPECT_EQ(*converted.members.at("primaryChild").target, 2U);
  EXPECT_EQ(*converted.members.at("childB").target, 2U);
  EXPECT_TRUE(converted.members.at("enabled").boolean);
  EXPECT_EQ(converted.members.count("newOnly"), 0U);
  EXPECT_EQ(result.document.objects.count(2), 1U);
  ASSERT_TRUE(result.document.root.has_value());
  EXPECT_EQ(*result.document.root, 1U);
  EXPECT_EQ(result.report.exact_objects, 1U);
  EXPECT_EQ(result.report.patched_objects, 1U);
  EXPECT_EQ(result.report.defaulted_fields, 1U);
  EXPECT_EQ(result.report.dropped_fields, 1U);
  EXPECT_EQ(result.report.dropped_references, 0U);
}

TEST(HkxConvert, UnusedRulesAndIdentityEnumValuesKeepAnObjectExactAndARemapPatchesIt) {
  // Constraint: detects the BGS counting bug where a declared but unused operation marked an untouched object as patched.
  auto idle = shared_reference_map();
  idle.rules.clear();
  auto& rule = idle.rules["hkaBone"];
  rule.target_type = "hkaBone";
  rule.renamed["missingRename"] = "otherName";
  rule.dropped.insert("missingDrop");
  rule.defaults["mode"] = integer_value(5);
  rule.enums["missingEnum"] = {{1, 2}};
  rule.enums["mode"] = {{2, 2}};
  HkxConversionResult same;
  ASSERT_EQ(fo4::hkx::convert_document(one_bone(2), idle, same), HkxConvertStatus::Ok);
  EXPECT_EQ(same.report.exact_objects, 1U);
  EXPECT_EQ(same.report.patched_objects, 0U);
  EXPECT_EQ(same.report.defaulted_fields, 0U);
  EXPECT_EQ(same.report.dropped_fields, 0U);
  EXPECT_EQ(same.report.enum_mapped_fields, 1U);

  HkxConversionResult remapped;
  ASSERT_EQ(fo4::hkx::convert_document(one_bone(2), bone_enum_map({{0, 0}, {2, 7}}), remapped), HkxConvertStatus::Ok);
  EXPECT_EQ(remapped.document.objects.at(1).members.at("mode").integer, 7);
  EXPECT_EQ(remapped.report.enum_mapped_fields, 1U);
  EXPECT_EQ(remapped.report.patched_objects, 1U);
  EXPECT_EQ(remapped.report.exact_objects, 0U);
}

TEST(HkxConvert, RefusesAnEnumValueWithNoMappingAndReturnsNoDocument) {
  // Constraint: detects an unmapped enum value copied through unchanged, which writes a value the target class does not define.
  HkxConversionResult result;
  EXPECT_EQ(fo4::hkx::convert_document(one_bone(99), bone_enum_map({{2, 7}}), result), HkxConvertStatus::Refused);
  EXPECT_TRUE(result.document.objects.empty());
  EXPECT_EQ(result.report.unsupported_enum_values, 1U);
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].level, fo4::hkx::HkxDiagnosticLevel::Error);
  EXPECT_EQ(result.diagnostics[0].member, "mode");
  EXPECT_NE(result.diagnostics[0].message.find("99"), std::string::npos);
}

TEST(HkxConvert, RefusesAnUnverifiedOrInconsistentMappingBeforeReadingTheDocument) {
  // Constraint: detects a mapping table applied without evidence, and a rename collision that would merge two members silently.
  auto unverified = shared_reference_map();
  unverified.evidence.clear();
  HkxConversionResult result;
  EXPECT_EQ(fo4::hkx::convert_document(shared_reference_document(), unverified, result), HkxConvertStatus::UnverifiedMapping);
  EXPECT_TRUE(result.document.objects.empty());
  EXPECT_EQ(result.report.exact_objects + result.report.patched_objects, 0U);

  auto doubled = shared_reference_map();
  doubled.rules["hkaNewSkeleton"].renamed["childB"] = "primaryChild";
  EXPECT_EQ(fo4::hkx::convert_document(shared_reference_document(), doubled, result), HkxConvertStatus::InvalidMap);
  EXPECT_TRUE(result.document.objects.empty());

  auto both = shared_reference_map();
  both.rules["hkaNewSkeleton"].dropped.insert("childA");
  EXPECT_EQ(fo4::hkx::convert_document(shared_reference_document(), both, result), HkxConvertStatus::InvalidMap);
}

TEST(HkxConvert, RefusesAnObjectWithNoRuleAndCountsTheReferenceItLeavesDangling) {
  // Constraint: detects a reference to a dropped object kept pointing at an id that no longer exists in the converted document.
  auto source = shared_reference_document();
  add_object(source, 3, "hkaUnmappedThing");
  source.objects.at(2).members["peer"] = reference_value(3);
  source.root = 3;
  HkxConversionResult result;
  EXPECT_EQ(fo4::hkx::convert_document(source, shared_reference_map(), result), HkxConvertStatus::Refused);
  EXPECT_TRUE(result.document.objects.empty());
  EXPECT_EQ(result.report.unsupported_objects, 1U);
  EXPECT_EQ(result.report.dropped_references, 1U);
  const bool root_named = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
    return diagnostic.message.find("has no root") != std::string::npos;
  });
  EXPECT_TRUE(root_named);
}

TEST(HkxConvert, RefusesATargetTypeThatHasNoVerifiedLayout) {
  // Constraint: detects a mapping onto a class name the Fallout 4 layout table does not know, which could never be written.
  auto map = shared_reference_map();
  map.rules["hkaNewSkeleton"].target_type = "hkaNotAFallout4Class";
  HkxConversionResult result;
  EXPECT_EQ(fo4::hkx::convert_document(shared_reference_document(), map, result), HkxConvertStatus::Refused);
  const bool named = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
    return diagnostic.message.find("hkaNotAFallout4Class has no verified layout") != std::string::npos;
  });
  EXPECT_TRUE(named);
}

TEST(HkxConvert, ReportTextIsDeterministicWhateverTheInsertionOrder) {
  // Constraint: detects a report ordered by hash or insertion order, which makes two runs of one conversion differ.
  const auto map = shared_reference_map();
  HkxConversionResult first;
  ASSERT_EQ(fo4::hkx::convert_document(shared_reference_document(), map, first), HkxConvertStatus::Ok);

  HkxSemanticDocument reversed;
  reversed.root = 1;
  add_object(reversed, 2, "hkaBone");
  add_object(reversed, 1, "hkaNewSkeleton",
             {{"newOnly", integer_value(99)}, {"childB", reference_value(2)}, {"childA", reference_value(2)}});
  HkxConversionResult second;
  ASSERT_EQ(fo4::hkx::convert_document(reversed, map, second), HkxConvertStatus::Ok);

  const auto text = fo4::hkx::render_conversion_report(map, HkxConvertStatus::Ok, first);
  EXPECT_EQ(text, fo4::hkx::render_conversion_report(map, HkxConvertStatus::Ok, second));
  EXPECT_EQ(text,
            "conversion hk_new -> hk_2014.1.0-r1 status ok\n"
            "objects exact 1 patched 1 unsupported 0\n"
            "fields defaulted 1 dropped 1 enum_mapped 0 enum_unsupported 0\n"
            "references dropped 0\n"
            "warning object 1 member newOnly: field was explicitly dropped by the conversion rule\n");
}

namespace {

using fo4::hkx::HkxDecodeOptions;
using fo4::hkx::HkxDecodeStatus;

constexpr std::uint32_t kUnlistedSignature = 0x12345678u;

enum class PartitionAlias { None, TrackArray, ObjectStart };

std::vector<std::byte> binding_packfile(std::int8_t blend_hint, const std::vector<std::int16_t>& tracks,
                                        PartitionAlias alias = PartitionAlias::None) {
  const auto* entry = fo4::hkx::find_decode_class("hkaAnimationBinding");
  std::vector<std::byte> data(entry->size, std::byte{0});
  std::vector<std::uint32_t> local;
  data[member_at("hkaAnimationBinding", "blendHint")] = static_cast<std::byte>(blend_hint);
  const std::size_t text_at = data.size();
  for (char ch : std::string("Rig")) data.push_back(static_cast<std::byte>(ch));
  data.push_back(std::byte{0});
  link(local, member_at("hkaAnimationBinding", "originalSkeletonName"), text_at);
  const std::size_t tracks_at = data.size();
  for (std::int16_t track : tracks) {
    const auto bits = static_cast<std::uint16_t>(track);
    data.push_back(static_cast<std::byte>(bits & 0xFF));
    data.push_back(static_cast<std::byte>(bits >> 8));
  }
  const std::size_t tracks_field = member_at("hkaAnimationBinding", "transformTrackToBoneIndices");
  link(local, tracks_field, tracks_at);
  set_count(data, tracks_field, tracks.size());
  if (alias != PartitionAlias::None) {
    const std::size_t partitions_field = member_at("hkaAnimationBinding", "partitionIndices");
    link(local, partitions_field, alias == PartitionAlias::TrackArray ? tracks_at : 0);
    set_count(data, partitions_field, tracks.size());
  }
  return single_object_packfile("hkaAnimationBinding", entry->signature, std::move(data), std::move(local));
}

void patch_data_u32(std::vector<std::byte>& file, std::size_t at, std::uint32_t value) {
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(file, packfile), HkxStatus::Ok);
  patch_u32(file, packfile.sections[packfile.data_section].file_offset + at, value);
}

}  // namespace

TEST(HkxDecodeTable, EveryEntryNamesItsSourceAndAgreesWithTheGeneratedLayouts) {
  // Constraint: detects a member row added without a cited source, or one whose offset, size or signature drifts from the generated layouts.
  ASSERT_FALSE(fo4::hkx::decode_classes().empty());
  for (const fo4::hkx::HkxDecodeClass& entry : fo4::hkx::decode_classes()) {
    SCOPED_TRACE(std::string(entry.name));
    EXPECT_TRUE(entry.source.find("commonlib 7a939d9") != std::string_view::npos ||
                entry.source.find("RE Docs vault") != std::string_view::npos);
    const auto* layout = fo4::hkx::find_layout(entry.name);
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->signature, entry.signature);
    EXPECT_EQ(layout->size, entry.size);
    std::uint32_t previous_end = 0;
    for (const fo4::hkx::HkxMemberType& member : entry.members) {
      SCOPED_TRACE(std::string(member.name));
      const auto size = fo4::hkx::member_size(member);
      ASSERT_TRUE(size.has_value());
      EXPECT_GE(member.offset, previous_end);
      EXPECT_LE(member.offset + *size, entry.size);
      previous_end = member.offset + *size;
      if (const auto own = fo4::hkx::find_member_offset(*layout, member.name)) { EXPECT_EQ(*own, member.offset); }
      if (member.kind != fo4::hkx::HkxMemberKind::Array) { EXPECT_EQ(member.element, fo4::hkx::HkxMemberKind::None); }
      if (member.kind == fo4::hkx::HkxMemberKind::Struct || member.element == fo4::hkx::HkxMemberKind::Struct) {
        EXPECT_NE(fo4::hkx::find_decode_class(member.struct_class), nullptr);
      }
    }
  }
}

TEST(HkxDecode, ReadsASyntheticBindingAndEmitsItByteIdentical) {
  // Constraint: detects a decoder or emitter that changes a byte of an unedited file, or reads a string, array or null pointer as the wrong kind.
  const auto bytes = binding_packfile(2, {5, -1, 300});
  const auto decoded = fo4::hkx::decode_document(bytes);
  ASSERT_EQ(decoded.status, HkxDecodeStatus::Ok);
  ASSERT_EQ(decoded.document.objects.size(), 1u);
  const auto& members = decoded.document.objects.at(0).members;
  EXPECT_EQ(decoded.document.objects.at(0).type_name, "hkaAnimationBinding");
  EXPECT_EQ(members.at("originalSkeletonName").kind, HkxValue::Kind::String);
  EXPECT_EQ(members.at("originalSkeletonName").text, "Rig");
  EXPECT_EQ(members.at("animation").kind, HkxValue::Kind::Null);
  EXPECT_EQ(members.at("blendHint").integer, 2);
  const auto& tracks = members.at("transformTrackToBoneIndices");
  ASSERT_EQ(tracks.items.size(), 3u);
  EXPECT_EQ(tracks.items[0].integer, 5);
  EXPECT_EQ(tracks.items[1].integer, -1);
  EXPECT_EQ(tracks.items[2].integer, 300);
  EXPECT_EQ(members.at("floatTrackToFloatSlotIndices").kind, HkxValue::Kind::Array);
  EXPECT_TRUE(members.at("floatTrackToFloatSlotIndices").items.empty());

  const auto emitted = fo4::hkx::emit_document(bytes, decoded.document);
  ASSERT_EQ(emitted.status, HkxDecodeStatus::Ok);
  EXPECT_TRUE(emitted.bytes == bytes);
}

TEST(HkxDecode, ScalarEditChangesOnlyTheBytesOfThoseScalars) {
  // Constraint: detects an emitter that rewrites neighbouring bytes, or stores an edited scalar at the wrong width or offset.
  const auto bytes = binding_packfile(2, {5, -1, 300});
  auto document = fo4::hkx::decode_document(bytes).document;
  auto& members = document.objects.at(0).members;
  members.at("blendHint").integer = -3;
  members.at("transformTrackToBoneIndices").items[1].integer = 7;

  const auto emitted = fo4::hkx::emit_document(bytes, document);
  ASSERT_EQ(emitted.status, HkxDecodeStatus::Ok);
  ASSERT_EQ(emitted.bytes.size(), bytes.size());
  std::size_t changed = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (emitted.bytes[i] != bytes[i]) ++changed;
  }
  EXPECT_EQ(changed, 3u);

  const auto reread = fo4::hkx::decode_document(emitted.bytes);
  ASSERT_EQ(reread.status, HkxDecodeStatus::Ok);
  const auto& after = reread.document.objects.at(0).members;
  EXPECT_EQ(after.at("blendHint").integer, -3);
  EXPECT_EQ(after.at("transformTrackToBoneIndices").items[0].integer, 5);
  EXPECT_EQ(after.at("transformTrackToBoneIndices").items[1].integer, 7);
  EXPECT_EQ(after.at("transformTrackToBoneIndices").items[2].integer, 300);
}

TEST(HkxDecode, EmitRefusesEditsThatWouldMoveBytesAndNamesTheMember) {
  // Constraint: detects an emitter that accepts a longer array, new string text, a new pointer target, an out-of-range integer or an object-set change it cannot write in place.
  const auto bytes = binding_packfile(2, {5, -1, 300});
  const auto base = fo4::hkx::decode_document(bytes).document;
  const auto refused = [&](const char* member, const auto& edit) {
    auto document = base;
    edit(document.objects.at(0).members);
    const auto result = fo4::hkx::emit_document(bytes, document);
    EXPECT_EQ(result.status, HkxDecodeStatus::Refused) << member;
    EXPECT_EQ(result.block.member, member);
    EXPECT_EQ(result.block.class_name, "hkaAnimationBinding");
    EXPECT_FALSE(result.block.reason.empty());
    EXPECT_TRUE(result.bytes.empty());
  };
  refused("transformTrackToBoneIndices", [](auto& m) { m.at("transformTrackToBoneIndices").items.pop_back(); });
  refused("transformTrackToBoneIndices[0]", [](auto& m) { m.at("transformTrackToBoneIndices").items[0].integer = 40000; });
  refused("originalSkeletonName", [](auto& m) { m.at("originalSkeletonName").text = "Other"; });
  refused("animation", [](auto& m) { m.at("animation") = reference_value(0); });
  refused("blendHint", [](auto& m) { m.at("blendHint").integer = 200; });
  refused("blendHint", [](auto& m) { m.at("blendHint") = bool_value(true); });
  refused("blendHint", [](auto& m) { m.erase("blendHint"); });
  refused("bogus", [](auto& m) { m["bogus"] = integer_value(1); });

  const auto object_level = [&](const char* what, const auto& edit) {
    auto document = base;
    edit(document);
    const auto result = fo4::hkx::emit_document(bytes, document);
    EXPECT_EQ(result.status, HkxDecodeStatus::Refused) << what;
    EXPECT_FALSE(result.block.reason.empty()) << what;
    EXPECT_TRUE(result.bytes.empty()) << what;
  };
  object_level("object removed", [](auto& d) { d.objects.clear(); });
  object_level("object added", [](auto& d) { add_object(d, 99, "hkaBone"); });
  object_level("type renamed", [](auto& d) { d.objects.at(0).type_name = "hkaBone"; });
  object_level("root changed", [](auto& d) { d.root = 0; });
}

TEST(HkxDecode, DecodeFailsClosedAndNamesTheClassAndMemberThatBlocked) {
  // Constraint: detects a decoder that guesses at an unlisted class, a bad bool, an unfixed pointer, a runaway array or an unterminated string instead of refusing with the blocking class and member.
  const auto expect_blocked = [](const std::vector<std::byte>& file, const char* class_name, const char* member,
                                 const char* reason_part) {
    const auto result = fo4::hkx::decode_document(file);
    EXPECT_EQ(result.status, HkxDecodeStatus::Refused) << member;
    EXPECT_EQ(result.block.class_name, class_name);
    EXPECT_EQ(result.block.member, member);
    EXPECT_NE(result.block.reason.find(reason_part), std::string::npos) << result.block.reason;
    EXPECT_TRUE(result.document.objects.empty());
    EXPECT_EQ(fo4::hkx::emit_document(file, {}).status, HkxDecodeStatus::Refused) << member;
  };

  const auto unlisted = single_object_packfile("hkaNotAFallout4Class", kUnlistedSignature, filled(32, std::byte{0}), {});
  expect_blocked(unlisted, "hkaNotAFallout4Class", "", "no verified member table");
  HkxDecodeOptions opaque;
  opaque.opaque_unlisted = true;
  const auto kept = fo4::hkx::decode_document(unlisted, opaque);
  ASSERT_EQ(kept.status, HkxDecodeStatus::Ok);
  EXPECT_TRUE(kept.document.objects.empty());
  EXPECT_EQ(kept.opaque_classes, std::vector<std::string>{"hkaNotAFallout4Class"});
  const auto kept_bytes = fo4::hkx::emit_document(unlisted, kept.document, opaque);
  ASSERT_EQ(kept_bytes.status, HkxDecodeStatus::Ok);
  EXPECT_TRUE(kept_bytes.bytes == unlisted);

  expect_blocked(single_object_packfile("hkaAnimationBinding", kUnlistedSignature, filled(88, std::byte{0}), {}),
                 "hkaAnimationBinding", "", "signature");
  expect_blocked(single_object_packfile("hkaAnimationBinding", fo4::hkx::find_decode_class("hkaAnimationBinding")->signature,
                                        filled(40, std::byte{0}), {}),
                 "hkaAnimationBinding", "", "shorter");

  auto skeleton = skeleton_packfile({"Root", "Spine"}, {-1, 0}, 2);
  expect_blocked(skeleton, "hkaSkeleton", "localFrames[0].localFrame", "local fixup");
  auto bad_bool = skeleton;
  patch_data_u32(bad_bool, fo4::hkx::find_layout("hkaSkeleton")->size + member_at("hkaBone", "lockTranslation"), 2);
  expect_blocked(bad_bool, "hkaSkeleton", "bones[0].lockTranslation", "bool");
  expect_blocked(skeleton_packfile({std::string(300, 'b'), "Spine"}, {-1, 0}, 2), "hkaSkeleton", "bones[0].name",
                 "terminator");

  const std::size_t tracks_field = member_at("hkaAnimationBinding", "transformTrackToBoneIndices");
  auto long_array = binding_packfile(2, {5, -1, 300});
  patch_data_u32(long_array, tracks_field + 8, 1000);
  expect_blocked(long_array, "hkaAnimationBinding", "transformTrackToBoneIndices", "past the data section");
  auto negative_array = binding_packfile(2, {5, -1, 300});
  patch_data_u32(negative_array, tracks_field + 8, 0xFFFFFFFFu);
  expect_blocked(negative_array, "hkaAnimationBinding", "transformTrackToBoneIndices", "negative");
  auto loose_pointer = binding_packfile(2, {5, -1, 300});
  patch_data_u32(loose_pointer, member_at("hkaAnimationBinding", "animation"), 5);
  expect_blocked(loose_pointer, "hkaAnimationBinding", "animation", "no fixup");

  const auto junk = fo4::hkx::decode_document(filled(64, std::byte{0}));
  EXPECT_EQ(junk.status, HkxDecodeStatus::Unreadable);
  EXPECT_NE(junk.packfile_status, HkxStatus::Ok);
}

TEST(HkxDecode, RefusesArraysThatAliasEachOtherOrTheirObjectAndKeepsALargeDistinctArray) {
  // Constraint: detects a decoder that reads one region as several arrays or as an object's own slots, which lets a small crafted file expand into millions of values, and one that refuses a legitimate large array.
  for (const auto alias : {PartitionAlias::TrackArray, PartitionAlias::ObjectStart}) {
    const auto aliased = binding_packfile(2, {5, -1, 300}, alias);
    const auto result = fo4::hkx::decode_document(aliased);
    EXPECT_EQ(result.status, HkxDecodeStatus::Refused);
    EXPECT_EQ(result.block.class_name, "hkaAnimationBinding");
    EXPECT_EQ(result.block.member, "partitionIndices");
    EXPECT_NE(result.block.reason.find("overlaps"), std::string::npos) << result.block.reason;
    EXPECT_TRUE(result.document.objects.empty());
  }

  const std::vector<std::int16_t> many(20000, 7);
  const auto large = binding_packfile(2, many);
  const auto decoded = fo4::hkx::decode_document(large);
  ASSERT_EQ(decoded.status, HkxDecodeStatus::Ok);
  EXPECT_EQ(decoded.document.objects.at(0).members.at("transformTrackToBoneIndices").items.size(), many.size());
  const auto emitted = fo4::hkx::emit_document(large, decoded.document);
  ASSERT_EQ(emitted.status, HkxDecodeStatus::Ok);
  EXPECT_TRUE(emitted.bytes == large);
}

namespace {

using fo4::hkx::HkxPackfileConversion;
using fo4::hkx::HkxPackfileConvertStatus;

constexpr const char* kFo4Version = "hk_2014.1.0-r1";

void put_text(std::vector<std::byte>& bytes, std::size_t at, std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) bytes[at + i] = static_cast<std::byte>(text[i]);
}

std::vector<std::byte> stamped(std::vector<std::byte> file, std::string_view version = kFo4Version) {
  put_text(file, 0x28, version);
  return file;
}

HkxConversionMap blend_hint_map(std::map<std::int64_t, std::int64_t> values, std::string target = kFo4Version) {
  HkxConversionMap map;
  map.source_version = kFo4Version;
  map.target_version = std::move(target);
  map.evidence = "synthetic fixture";
  auto& rule = map.rules["hkaAnimationBinding"];
  rule.target_type = "hkaAnimationBinding";
  rule.enums["blendHint"] = std::move(values);
  return map;
}

std::size_t differing_bytes(const std::vector<std::byte>& left, const std::vector<std::byte>& right) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (left[i] != right[i]) ++count;
  }
  return count;
}

std::vector<std::byte> binding_referencing_an_unlisted_object() {
  const auto* entry = fo4::hkx::find_decode_class("hkaAnimationBinding");
  std::vector<std::byte> names;
  const std::uint32_t binding_name = add_class_name(names, entry->signature, "hkaAnimationBinding");
  const std::uint32_t other_name = add_class_name(names, kUnlistedSignature, "hkaNotAFallout4Class");
  const std::uint32_t other_at = entry->size;
  const auto animation = static_cast<std::uint32_t>(member_at("hkaAnimationBinding", "animation"));
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(entry->size + 32, std::byte{0}), {}, {animation, 2, other_at},
                  {0, 0, binding_name, other_at, 0, other_name}},
  });
}

}  // namespace

TEST(HkxConvertPackfile, ProbeReadsTheVersionTokenAndRefusesOneCutByTheWindowEdge) {
  // Constraint: detects a probe that reports a truncated version (hk_201) as a valid one, or misses a token that is not at the file start.
  std::vector<std::byte> bytes(200, std::byte{0});
  put_text(bytes, 10, kFo4Version);
  const auto found = fo4::hkx::probe_havok_version(bytes);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->version, kFo4Version);
  EXPECT_EQ(found->offset, 10u);

  std::vector<std::byte> no_digit(200, std::byte{0});
  put_text(no_digit, 10, "hk_abc");
  EXPECT_FALSE(fo4::hkx::probe_havok_version(no_digit).has_value());

  std::vector<std::byte> cut(200, std::byte{0});
  put_text(cut, 90, "hk_2014.1.0");
  EXPECT_FALSE(fo4::hkx::probe_havok_version(cut).has_value());
  const std::vector<std::byte> edge(cut.begin(), cut.begin() + 96);
  const auto complete = fo4::hkx::probe_havok_version(edge);
  ASSERT_TRUE(complete.has_value());
  EXPECT_EQ(complete->version, "hk_201");
}

TEST(HkxConvertPackfile, SameVersionRunsTheWholePipelineAndReportsNoChange) {
  // Constraint: detects a convert path that rewrites bytes, or counts a changed field, when the same-version identity map has nothing to convert.
  const auto bytes = stamped(binding_packfile(2, {5, -1, 300}));
  const auto result = fo4::hkx::convert_packfile(bytes, kFo4Version);
  ASSERT_EQ(result.status, HkxPackfileConvertStatus::Ok) << result.reason;
  EXPECT_TRUE(result.same_version);
  EXPECT_EQ(result.source_version, kFo4Version);
  EXPECT_FALSE(result.evidence.empty());
  EXPECT_EQ(result.object_count, 1u);
  EXPECT_EQ(result.report.exact_objects, 1u);
  EXPECT_EQ(result.report.patched_objects, 0u);
  EXPECT_EQ(result.changed_total, 0u);
  EXPECT_TRUE(result.changes.empty());
  EXPECT_TRUE(result.opaque_classes.empty());
  EXPECT_TRUE(result.bytes == bytes);
}

TEST(HkxConvertPackfile, ANaNFloatSurvivesTheSameVersionPassAsNoChange) {
  // Constraint: detects a diff that compares reals with == and reports a NaN as changed (NaN != NaN) in a byte-identical pass.
  auto raw = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3, false);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(raw, packfile), HkxStatus::Ok);
  const auto& data = packfile.sections[packfile.data_section];
  patch_float(raw, data.file_offset + data.data.size() - 48, std::numeric_limits<float>::quiet_NaN());
  const auto bytes = stamped(raw);
  const auto result = fo4::hkx::convert_packfile(bytes, kFo4Version);
  ASSERT_EQ(result.status, HkxPackfileConvertStatus::Ok) << result.reason;
  EXPECT_EQ(result.changed_total, 0u);
  EXPECT_TRUE(result.changes.empty());
  EXPECT_TRUE(result.bytes == bytes);
}

TEST(HkxConvertPackfile, ARuleThatRemapsAFieldReportsExactlyThatFieldAndWritesOnlyItsByte) {
  // Constraint: detects a convert path that reports no change, reports the wrong field or values, or rewrites bytes beyond the converted scalar.
  const auto bytes = stamped(binding_packfile(2, {5, -1, 300}));
  const std::vector<HkxConversionMap> maps{blend_hint_map({{2, 7}})};
  const auto result = fo4::hkx::convert_packfile(bytes, kFo4Version, maps);
  ASSERT_EQ(result.status, HkxPackfileConvertStatus::Ok) << result.reason;
  EXPECT_EQ(result.report.patched_objects, 1u);
  EXPECT_EQ(result.report.enum_mapped_fields, 1u);
  ASSERT_EQ(result.changes.size(), 1u);
  EXPECT_EQ(result.changed_total, 1u);
  EXPECT_EQ(result.changes[0].class_name, "hkaAnimationBinding");
  EXPECT_EQ(result.changes[0].member, "blendHint");
  EXPECT_EQ(result.changes[0].before, "2");
  EXPECT_EQ(result.changes[0].after, "7");
  EXPECT_EQ(differing_bytes(result.bytes, bytes), 1u);
  const auto reread = fo4::hkx::decode_document(result.bytes);
  ASSERT_EQ(reread.status, HkxDecodeStatus::Ok);
  EXPECT_EQ(reread.document.objects.at(0).members.at("blendHint").integer, 7);
}

TEST(HkxConvertPackfile, RefusalsNameTheirReasonAndNeverReturnBytes) {
  // Constraint: detects a convert path that writes bytes for an unsupported pair, a rule gap, a rename the writer cannot place, a different target version, an unreadable file, or a 4-byte or tagfile input reported without its named reason (rows C32, L21).
  const auto bytes = stamped(binding_packfile(2, {5, -1, 300}));
  const auto expect_refused = [&](const HkxPackfileConversion& result, HkxPackfileConvertStatus status) {
    EXPECT_EQ(result.status, status) << result.reason;
    EXPECT_FALSE(result.reason.empty());
    EXPECT_TRUE(result.bytes.empty());
  };

  const auto pair = fo4::hkx::convert_packfile(bytes, "hk_2012.2.1-r1");
  expect_refused(pair, HkxPackfileConvertStatus::UnsupportedPair);
  EXPECT_NE(pair.reason.find("hk_2012.2.1-r1"), std::string::npos);
  EXPECT_NE(pair.reason.find("hk_2014.1.0-r1 -> hk_2014.1.0-r1"), std::string::npos);

  expect_refused(fo4::hkx::convert_packfile(binding_packfile(2, {5}), kFo4Version), HkxPackfileConvertStatus::UnknownVersion);
  expect_refused(fo4::hkx::convert_packfile(filled(64, std::byte{0}), kFo4Version), HkxPackfileConvertStatus::Unreadable);

  auto four_byte = bytes;
  four_byte[0x10] = std::byte{4};
  const auto narrow = fo4::hkx::convert_packfile(four_byte, kFo4Version);
  expect_refused(narrow, HkxPackfileConvertStatus::Unreadable);
  EXPECT_NE(narrow.reason.find("pointer size 4"), std::string::npos) << narrow.reason;

  std::vector<std::byte> tagfile(0x40, std::byte{0});
  patch_u32(tagfile, 0, 0x40);
  tagfile[4] = std::byte{'T'};
  tagfile[5] = std::byte{'A'};
  tagfile[6] = std::byte{'G'};
  tagfile[7] = std::byte{'0'};
  const auto tagged = fo4::hkx::convert_packfile(tagfile, kFo4Version);
  expect_refused(tagged, HkxPackfileConvertStatus::Unreadable);
  EXPECT_NE(tagged.reason.find("TAG0"), std::string::npos) << tagged.reason;

  const auto bad_signature = stamped(single_object_packfile("hkaAnimationBinding", kUnlistedSignature, filled(88, std::byte{0}), {}));
  const auto decode = fo4::hkx::convert_packfile(bad_signature, kFo4Version);
  expect_refused(decode, HkxPackfileConvertStatus::DecodeRefused);
  EXPECT_EQ(decode.block.class_name, "hkaAnimationBinding");

  const std::vector<HkxConversionMap> gap{blend_hint_map({{3, 4}})};
  const auto unmapped = fo4::hkx::convert_packfile(bytes, kFo4Version, gap);
  expect_refused(unmapped, HkxPackfileConvertStatus::ConversionRefused);
  EXPECT_EQ(unmapped.report.unsupported_enum_values, 1u);
  EXPECT_NE(unmapped.reason.find("enum value 2"), std::string::npos);

  auto renamed = blend_hint_map({{2, 2}});
  renamed.rules["hkaAnimationBinding"].renamed["blendHint"] = "blendMode";
  const std::vector<HkxConversionMap> rename{renamed};
  const auto moved = fo4::hkx::convert_packfile(bytes, kFo4Version, rename);
  expect_refused(moved, HkxPackfileConvertStatus::EmitRefused);
  EXPECT_EQ(moved.block.class_name, "hkaAnimationBinding");
  EXPECT_GE(moved.changed_total, 1u);

  const std::vector<HkxConversionMap> other_version{blend_hint_map({{2, 7}}, "hk_2016.2.0-r1")};
  const auto restamp = fo4::hkx::convert_packfile(bytes, "hk_2016.2.0-r1", other_version);
  expect_refused(restamp, HkxPackfileConvertStatus::EmitRefused);
  EXPECT_NE(restamp.reason.find("hk_2016.2.0-r1"), std::string::npos);
  EXPECT_EQ(restamp.changed_total, 1u);
}

TEST(HkxConvertPackfile, OpaqueClassesAreListedAndAReferenceIntoOneRefusesNamingTheClass) {
  // Constraint: detects a convert path that drops a reference to a class this build cannot decode and writes the file anyway, or one that hides which classes it left untouched.
  const auto only_unlisted = stamped(single_object_packfile("hkaNotAFallout4Class", kUnlistedSignature, filled(32, std::byte{0}), {}));
  const auto kept = fo4::hkx::convert_packfile(only_unlisted, kFo4Version);
  ASSERT_EQ(kept.status, HkxPackfileConvertStatus::Ok) << kept.reason;
  EXPECT_EQ(kept.opaque_classes, std::vector<std::string>{"hkaNotAFallout4Class"});
  EXPECT_EQ(kept.object_count, 0u);
  EXPECT_TRUE(kept.bytes == only_unlisted);

  const auto linked = stamped(binding_referencing_an_unlisted_object());
  const auto refused = fo4::hkx::convert_packfile(linked, kFo4Version);
  EXPECT_EQ(refused.status, HkxPackfileConvertStatus::ConversionRefused) << refused.reason;
  EXPECT_EQ(refused.report.dropped_references, 1u);
  EXPECT_EQ(refused.opaque_classes, std::vector<std::string>{"hkaNotAFallout4Class"});
  EXPECT_NE(refused.reason.find("hkaNotAFallout4Class"), std::string::npos);
  EXPECT_TRUE(refused.bytes.empty());
}
