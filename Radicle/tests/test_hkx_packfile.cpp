#include "fo4/hkx/behavior.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/object_view.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/project_index.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_document.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

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
                                         std::size_t pose_count) {
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
