#include "fo4/error.hpp"
#include "fo4/nif/hcl_cloth.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace {

using fo4::nif::HknpPackfile;
using fo4::nif::HknpPackObject;

HknpPackObject make_object(std::string class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

void put_u32(HknpPackObject& object, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) object.bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

void put_f32(HknpPackObject& object, std::size_t at, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(object, at, bits);
}

void put_text(HknpPackObject& object, std::size_t at, const std::string& text) {
  for (std::size_t i = 0; i < text.size(); ++i) object.bytes[at + i] = static_cast<std::byte>(text[i]);
}

void put_local(HknpPackObject& object, std::size_t source, std::size_t target) {
  object.local.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

void put_global(HknpPackObject& object, std::size_t source, std::size_t target_object) {
  object.global.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target_object)});
}

}  // namespace

// Detects: a collidable whose name, transform or shape pointer is read at the wrong offset, which loses the capsule it names; the shape must resolve to object 1.
TEST(HclCloth, CollidableKeepsItsNameTransformAndShapeObject) {
  HknpPackObject collidable = make_object("hclCollidable", 176);
  put_local(collidable, 16, 0x90);
  put_text(collidable, 0x90, "Cape_Front");
  put_f32(collidable, 32, 1.0f);
  put_f32(collidable, 80, 5.0f);
  put_f32(collidable, 132, 0.25f);
  put_global(collidable, 136, 1);
  HknpPackfile pack;
  pack.objects = {collidable, make_object("hclCapsuleShape", 96)};
  const auto cloth = fo4::nif::decode_hcl_cloth(pack);
  ASSERT_EQ(cloth.collidables.size(), 1U);
  EXPECT_EQ(cloth.collidables[0].name, "Cape_Front");
  EXPECT_FLOAT_EQ(cloth.collidables[0].transform[0][0], 1.0f);
  EXPECT_FLOAT_EQ(cloth.collidables[0].transform[3][0], 5.0f);
  EXPECT_FLOAT_EQ(cloth.collidables[0].pinch_detection_radius, 0.25f);
  EXPECT_EQ(cloth.collidables[0].shape_object, 1U);
}

// Detects: capsule or tapered-capsule fields read from the wrong offsets, so a radius or an axis reports another shape's value.
TEST(HclCloth, CapsuleAndTaperedShapesKeepTheirFields) {
  HknpPackObject capsule = make_object("hclCapsuleShape", 96);
  put_f32(capsule, 32, 1.0f);
  put_f32(capsule, 48, 2.0f);
  put_f32(capsule, 64 + 8, 1.0f);
  put_f32(capsule, 80, 0.5f);
  put_f32(capsule, 84, 0.75f);
  HknpPackObject tapered = make_object("hclTaperedCapsuleShape", 176);
  put_f32(tapered, 144, 0.1f);
  put_f32(tapered, 148, 0.4f);
  put_f32(tapered, 152, 2.0f);
  put_f32(tapered, 156, 3.0f);
  HknpPackfile pack;
  pack.objects = {capsule, tapered};
  const auto cloth = fo4::nif::decode_hcl_cloth(pack);
  ASSERT_EQ(cloth.capsules.size(), 1U);
  ASSERT_EQ(cloth.tapered_capsules.size(), 1U);
  EXPECT_FLOAT_EQ(cloth.capsules[0].start[0], 1.0f);
  EXPECT_FLOAT_EQ(cloth.capsules[0].end[0], 2.0f);
  EXPECT_FLOAT_EQ(cloth.capsules[0].dir[2], 1.0f);
  EXPECT_FLOAT_EQ(cloth.capsules[0].radius, 0.5f);
  EXPECT_FLOAT_EQ(cloth.capsules[0].cap_len_sqr_inv, 0.75f);
  EXPECT_EQ(cloth.capsules[0].object, 0U);
  EXPECT_FLOAT_EQ(cloth.tapered_capsules[0].small_radius, 0.1f);
  EXPECT_FLOAT_EQ(cloth.tapered_capsules[0].big_radius, 0.4f);
  EXPECT_FLOAT_EQ(cloth.tapered_capsules[0].length, 2.0f);
  EXPECT_FLOAT_EQ(cloth.tapered_capsules[0].depth, 3.0f);
  EXPECT_EQ(cloth.tapered_capsules[0].object, 1U);
}

// Detects: bone names read at the wrong stride, or a nameless bone dropping out of the list, which shifts every later name off its bone.
TEST(HclCloth, SkeletonBoneNamesFollowTheBoneArray) {
  HknpPackObject skeleton = make_object("hkaSkeleton", 0x100);
  put_local(skeleton, 0x28, 0x80);
  put_u32(skeleton, 0x30, 3);
  put_local(skeleton, 0x80, 0xC0);
  put_local(skeleton, 0xA0, 0xD0);
  put_text(skeleton, 0xC0, "hip");
  put_text(skeleton, 0xD0, "spine");
  HknpPackfile pack;
  pack.objects = {skeleton};
  const auto cloth = fo4::nif::decode_hcl_cloth(pack);
  ASSERT_EQ(cloth.bone_names.size(), 3U);
  EXPECT_EQ(cloth.bone_names[0], "hip");
  EXPECT_EQ(cloth.bone_names[1], "");
  EXPECT_EQ(cloth.bone_names[2], "spine");
}

// Detects: a capsule object cut short of its class size, read past its end instead of raising fo4::parse_error.
TEST(HclCloth, TruncatedCapsuleRaisesParseError) {
  HknpPackfile pack;
  pack.objects = {make_object("hclCapsuleShape", 0x40)};
  EXPECT_THROW(fo4::nif::decode_hcl_cloth(pack), fo4::parse_error);
}
