#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode_ragdoll.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include "hknp_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace fo4::test::hknp;

namespace {

using fo4::nif::HknpClassName;
using fo4::nif::HknpPackfile;
using fo4::nif::HknpSystem;

constexpr std::size_t k_root_size = 0x90;
constexpr std::size_t k_skeleton_size = 0x88;
constexpr std::size_t k_ragdoll_constraint_size = 416;

std::vector<HknpClassName> ragdoll_classes() {
  return {{3700367531U, "hknpRagdollData"},
          {1948160018U, "hknpSphereShape"},
          {3078430774U, "hkpRagdollConstraintData"},
          {1374314554U, "hkpLimitedHingeConstraintData"},
          {339596288U, "hkpPositionConstraintMotor"},
          {4274114267U, "hkaSkeleton"},
          {3288630727U, "hknpBreakableConstraintData"}};
}

// Constraint: bone k is mapped to body bodies - 1 - k, so bone and body orders differ, and body b takes material slot bodies - 1 - b.
HknpSystem ragdoll_kit(std::size_t bodies, std::size_t bones) {
  HknpSystem sys;
  sys.root_class = "hknpRagdollData";
  sys.packfile.classes = ragdoll_classes();
  sys.packfile.objects.push_back(make_object("hknpRagdollData", k_root_size));
  for (std::size_t b = 0; b < bodies; ++b) {
    sys.packfile.objects.push_back(make_sphere(0.1f + 0.05f * static_cast<float>(b)));
  }
  for (std::size_t k = 1; k < bones; ++k) {
    sys.packfile.objects.push_back(make_object("hkpRagdollConstraintData", k_ragdoll_constraint_size));
  }
  sys.skeleton_object = sys.packfile.objects.size();
  sys.packfile.objects.push_back(make_object("hkaSkeleton", k_skeleton_size));

  for (std::size_t b = 0; b < bodies; ++b) {
    fo4::nif::HknpBody body;
    body.shape_object = 1 + b;
    body.motion_index = static_cast<std::uint32_t>(b);
    body.material_slot = static_cast<std::uint16_t>(bodies - 1 - b);
    body.friction = 0.25f * static_cast<float>(1 + b % 3);
    body.static_friction = body.friction;
    body.restitution = 0.5f;
    body.packed_filter = 0x00080000U + static_cast<std::uint32_t>(b);
    body.flags = 0x80U;
    body.position = {static_cast<float>(b), 2.0f * static_cast<float>(b), 3.0f};
    body.position_w = 0.5f * static_cast<float>(b);
    sys.bodies.push_back(body);

    fo4::nif::HknpDynMotion motion;
    motion.gravity_factor = 1.0f;
    motion.max_linear_velocity = 104.375f;
    motion.max_angular_velocity = 31.5703125f;
    motion.linear_damping = 0.125f;
    motion.angular_damping = 0.0625f;
    sys.motions.push_back(motion);

    fo4::nif::HknpDynInertia inertia;
    inertia.inverse_mass = 0.5f;
    inertia.density = 2.0f;
    inertia.inverse_inertia = {1.0f, 2.0f, 3.0f};
    inertia.inverse_inertia_w = 1.0f;
    inertia.center = {0.25f, 0.5f, 0.75f};
    inertia.center_w = 0.0f;
    inertia.motion_properties_id = static_cast<std::uint16_t>(b);
    sys.inertias.push_back(inertia);
  }

  for (std::size_t k = 0; k < bones; ++k) {
    fo4::nif::HknpBone bone;
    bone.parent = k == 0 ? -1 : static_cast<int>(k) - 1;
    bone.translation = {0.5f * static_cast<float>(k), 0.0f, 0.0f};
    bone.translation_w = 0.25f * static_cast<float>(k);
    bone.scale_w = k % 2 == 0 ? 1.0f : 0.0f;
    bone.lock_translation = k % 2 == 1;
    sys.bones.push_back(bone);
    sys.bone_to_body.push_back(static_cast<std::int32_t>(bodies - 1 - k));
  }

  for (std::size_t k = 1; k < bones; ++k) {
    fo4::nif::HknpConstraint constraint;
    constraint.kind = "hkpRagdollConstraintData";
    constraint.object = bodies + k;
    constraint.binding_object = constraint.object;
    constraint.child_body = sys.bone_to_body[k];
    constraint.parent_body = sys.bone_to_body[k - 1];
    constraint.friction = 0.5f;
    constraint.twist = {true, -0.5f, 0.5f, 0.25f};
    sys.constraints.push_back(constraint);
  }

  for (std::size_t b = 0; b < bodies; ++b) sys.referenced_objects.push_back(1 + (bodies - 1 - b));
  return sys;
}

std::vector<std::byte> written(const HknpSystem& sys) {
  return fo4::nif::write_hknp_packfile(fo4::nif::encode_hknp_ragdoll(sys));
}

HknpSystem decoded(const std::vector<std::byte>& bytes) {
  return fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));
}

void expect_kit_round_trips(std::size_t bodies, std::size_t bones) {
  const auto sys = ragdoll_kit(bodies, bones);
  const auto bytes = written(sys);
  const auto back = decoded(bytes);
  ASSERT_EQ(back.bodies.size(), bodies);
  ASSERT_EQ(back.bones.size(), bones);
  ASSERT_EQ(back.constraints.size(), bones - 1);
  EXPECT_EQ(back.bone_to_body, sys.bone_to_body);
  EXPECT_EQ(back.referenced_objects, sys.referenced_objects);
  EXPECT_EQ(back.motions.size(), bodies);
  EXPECT_EQ(back.inertias.size(), bodies);
  for (std::size_t b = 0; b < bodies; ++b) {
    EXPECT_EQ(back.bodies[b].material_slot, sys.bodies[b].material_slot) << "body " << b;
    EXPECT_FLOAT_EQ(back.bodies[b].friction, sys.bodies[b].friction) << "body " << b;
    EXPECT_FLOAT_EQ(back.bodies[b].static_friction, sys.bodies[b].static_friction) << "body " << b;
    EXPECT_FLOAT_EQ(back.bodies[b].position_w, sys.bodies[b].position_w) << "body " << b;
    EXPECT_EQ(back.bodies[b].motion_index, static_cast<std::uint32_t>(b)) << "body " << b;
  }
  for (std::size_t k = 0; k < bones; ++k) {
    EXPECT_EQ(back.bones[k].parent, sys.bones[k].parent) << "bone " << k;
    EXPECT_FLOAT_EQ(back.bones[k].translation_w, sys.bones[k].translation_w) << "bone " << k;
    EXPECT_FLOAT_EQ(back.bones[k].scale_w, sys.bones[k].scale_w) << "bone " << k;
    EXPECT_EQ(back.bones[k].lock_translation, sys.bones[k].lock_translation) << "bone " << k;
  }
  for (std::size_t k = 0; k + 1 < bones; ++k) {
    EXPECT_EQ(back.constraints[k].child_body, sys.constraints[k].child_body) << "constraint " << k;
    EXPECT_EQ(back.constraints[k].parent_body, sys.constraints[k].parent_body) << "constraint " << k;
  }
  EXPECT_EQ(fo4::nif::write_hknp_packfile(fo4::nif::encode_hknp_ragdoll(back)), bytes);
}

}  // namespace

// Detects: a ragdoll writer that sizes bodies and bones from one count, so the 48-body SkeletonRef kit with 11 bones reads back with the wrong body, bone or link counts.
TEST(HknpRagdollEncode, SkeletonRefKitWithMoreBodiesThanBonesRoundTrips) {
  expect_kit_round_trips(48, 11);
}

// Detects: the same count confusion on the 24-body skeletonSentryBodyPart kit with 9 bones, where a bone-to-body map of the wrong length leaves bones unmapped.
TEST(HknpRagdollEncode, SentryBodyPartKitWithMoreBodiesThanBonesRoundTrips) {
  expect_kit_round_trips(24, 9);
}

// Detects: the same count confusion on the 5-body TurretMountedSkeleton kit with 4 bones, where the constraint count or the bone-to-body links are written from the body count.
TEST(HknpRagdollEncode, TurretMountedKitWithMoreBodiesThanBonesRoundTrips) {
  expect_kit_round_trips(5, 4);
}

// Detects: a writer that sizes the motion array from the inertia array or drops a 0xFFFF motion id, so a turret whose static body has an inertia but no motion reads back with the wrong counts.
TEST(HknpRagdollEncode, StaticTurretBodyWithAnInertiaButNoMotionRoundTrips) {
  auto sys = ragdoll_kit(2, 2);
  sys.motions.resize(1);
  sys.inertias[0].motion_properties_id = 0xFFFFU;
  sys.inertias[0].inverse_mass = 0.0f;
  sys.inertias[0].inverse_inertia = {0.0f, 0.0f, 0.0f};
  sys.inertias[0].inverse_inertia_w = 0.0f;
  sys.inertias[0].center = {0.0f, 0.0f, 0.0f};
  sys.inertias[0].center_w = 0.0f;
  sys.inertias[1].motion_properties_id = 0U;
  const auto back = decoded(written(sys));
  ASSERT_EQ(back.motions.size(), 1U);
  ASSERT_EQ(back.inertias.size(), 2U);
  EXPECT_EQ(back.inertias[0].motion_properties_id, 0xFFFFU);
  EXPECT_FLOAT_EQ(back.inertias[0].inverse_inertia_w, 0.0f);
  EXPECT_EQ(back.inertias[1].motion_properties_id, 0U);
  EXPECT_FLOAT_EQ(back.inertias[1].inverse_mass, 0.5f);
}

// Detects: a binding written to the unwrapped data object instead of its breakable wrapper, which drops the wrapper and its threshold from the rebuilt root.
TEST(HknpRagdollEncode, BreakableBindingKeepsItsWrapperAndThreshold) {
  auto sys = ragdoll_kit(2, 2);
  auto wrapper = make_object("hknpBreakableConstraintData", 48);
  put_global(wrapper, 0x18, 3);
  sys.packfile.objects.push_back(std::move(wrapper));
  sys.constraints[0].breakable = true;
  sys.constraints[0].break_threshold = 0.25f;
  sys.constraints[0].binding_object = sys.packfile.objects.size() - 1;
  const auto back = decoded(written(sys));
  ASSERT_EQ(back.constraints.size(), 1U);
  EXPECT_TRUE(back.constraints[0].breakable);
  EXPECT_FLOAT_EQ(back.constraints[0].break_threshold, 0.25f);
  EXPECT_EQ(back.constraints[0].object, 3U);
  EXPECT_EQ(back.constraints[0].binding_object, 5U);
  EXPECT_EQ(back.constraints[0].kind, "hkpRagdollConstraintData");
}

// Detects: a friction stored by truncation, which reads back one step low; 0.4 must come back as the nearest bfloat16, 0.400390625.
TEST(HknpRagdollEncode, FrictionIsRoundedToTheNearestBfloat16) {
  auto sys = ragdoll_kit(1, 1);
  sys.bodies[0].friction = 0.4f;
  sys.bodies[0].static_friction = 0.4f;
  const auto back = decoded(written(sys));
  ASSERT_EQ(back.bodies.size(), 1U);
  EXPECT_FLOAT_EQ(back.bodies[0].friction, 0.400390625f);
  EXPECT_TRUE(back.constraints.empty());
}

// Detects: two bodies sharing one material slot, which would write one body's friction over the other's and lose it on the rebuild.
TEST(HknpRagdollEncode, RefusesMaterialIdsThatAreNotAPermutationOfTheBodies) {
  auto sys = ragdoll_kit(3, 2);
  sys.bodies[1].material_slot = sys.bodies[0].material_slot;
  EXPECT_THROW(fo4::nif::encode_hknp_ragdoll(sys), std::invalid_argument);
}

// Detects: a bone-to-body map shorter than the bone list, which would leave a bone without its body entry in the rebuilt root.
TEST(HknpRagdollEncode, RefusesABoneToBodyMapThatDoesNotMatchTheBones) {
  auto sys = ragdoll_kit(3, 2);
  sys.bone_to_body.pop_back();
  EXPECT_THROW(fo4::nif::encode_hknp_ragdoll(sys), std::invalid_argument);
}
