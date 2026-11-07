#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode_constraints.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::HknpConstraint;
using fo4::nif::HknpPackfile;
using fo4::nif::HknpPackObject;
using fo4::nif::HknpVec4;

HknpPackObject make_object(std::string class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

void put_u8(HknpPackObject& object, std::size_t at, std::uint8_t value) {
  object.bytes[at] = std::byte{value};
}

void put_u16(HknpPackObject& object, std::size_t at, std::uint16_t value) {
  object.bytes[at] = std::byte{static_cast<std::uint8_t>(value & 0xFFu)};
  object.bytes[at + 1] = std::byte{static_cast<std::uint8_t>(value >> 8)};
}

void put_u32(HknpPackObject& object, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) object.bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

void put_f32(HknpPackObject& object, std::size_t at, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(object, at, bits);
}

void put_vec4(HknpPackObject& object, std::size_t at, const HknpVec4& value) {
  for (std::size_t k = 0; k < 4; ++k) put_f32(object, at + 4 * k, value[k]);
}

void put_local(HknpPackObject& object, std::size_t source, std::size_t target) {
  object.local.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

void put_global(HknpPackObject& object, std::size_t source, std::size_t target_object) {
  object.global.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target_object)});
}

// Constraint: objects[1] is the constraint binding's data, and the root names it with one binding between two bodies.
HknpPackfile binding_system(std::vector<HknpPackObject> objects) {
  HknpPackObject root = make_object("hknpRagdollData", 0x200);
  put_local(root, 0x50, 0x100);
  put_u32(root, 0x58, 1);
  put_global(root, 0x100, 1);
  put_u32(root, 0x108, 1);
  put_u32(root, 0x10c, 0);
  HknpPackfile pack;
  pack.objects.push_back(std::move(root));
  for (auto& object : objects) pack.objects.push_back(std::move(object));
  return pack;
}

HknpConstraint decode_binding(std::vector<HknpPackObject> objects) {
  return fo4::nif::decode_hknp_system(binding_system(std::move(objects))).constraints.at(0);
}

HknpPackObject object_from_bytes(std::string class_name, const std::vector<std::byte>& bytes) {
  HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes = bytes;
  return object;
}

float f32_in(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < 4; ++i) bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

}  // namespace

// Detects: a linear-limit or linear-motor atom read at the wrong offset, or an atom skipped by a wrong length in the walk; the limit, motor flag and full atom chain must match the stored ones.
TEST(HknpConstraintAtoms, PrismaticChainKeepsItsLinearLimitMotorAndAtomOrder) {
  HknpPackObject prismatic = make_object("hkpPrismaticConstraintData", 288);
  put_u16(prismatic, 0x20, 0x02);
  put_u16(prismatic, 0xB0, 0x0b);
  put_u8(prismatic, 0xB2, 1);
  put_u16(prismatic, 0xC8, 0x0a);
  put_u16(prismatic, 0xD8, 0x0d);
  put_u16(prismatic, 0xE8, 0x07);
  put_u16(prismatic, 0xF8, 0x07);
  put_u16(prismatic, 0x108, 0x09);
  put_f32(prismatic, 0x10C, -0.5f);
  put_f32(prismatic, 0x110, 1.5f);
  const auto decoded = decode_binding({prismatic});
  EXPECT_EQ(decoded.atom_types, (std::vector<std::uint16_t>{0x02, 0x0b, 0x0a, 0x0d, 0x07, 0x07, 0x09}));
  EXPECT_TRUE(decoded.linear_limit.present);
  EXPECT_FLOAT_EQ(decoded.linear_limit.min, -0.5f);
  EXPECT_FLOAT_EQ(decoded.linear_limit.max, 1.5f);
  EXPECT_TRUE(decoded.linear_motor_enabled);
  EXPECT_EQ(decoded.unread_atom, 0U);
}

// Detects: a translations atom read at the wrong offset, which moves the ball-socket pivots; the four stored words of each translation must come back.
TEST(HknpConstraintAtoms, BallAndSocketTranslationsKeepTheirFourWords) {
  HknpPackObject socket = make_object("hkpBallAndSocketConstraintData", 112);
  put_u16(socket, 0x20, 0x03);
  put_vec4(socket, 0x30, HknpVec4{1.0f, 2.0f, 3.0f, 0.5f});
  put_vec4(socket, 0x40, HknpVec4{-1.0f, 0.0f, 0.25f, -2.0f});
  put_u16(socket, 0x50, 0x17);
  put_u16(socket, 0x60, 0x05);
  put_u8(socket, 0x62, 0);
  const auto decoded = decode_binding({socket});
  EXPECT_EQ(decoded.atom_types, (std::vector<std::uint16_t>{0x03, 0x17, 0x05}));
  EXPECT_EQ(decoded.translation_a, (HknpVec4{1.0f, 2.0f, 3.0f, 0.5f}));
  EXPECT_EQ(decoded.translation_b, (HknpVec4{-1.0f, 0.0f, 0.25f, -2.0f}));
  EXPECT_EQ(decoded.ball_socket_method, 0);
}

// Detects: an atom type outside the length table, which the walk must report in unread_atom and stop on instead of reading the atoms behind it as known.
TEST(HknpConstraintAtoms, UnknownAtomTypeStopsTheWalkAndIsReported) {
  HknpPackObject socket = make_object("hkpBallAndSocketConstraintData", 112);
  put_u16(socket, 0x20, 0x1d);
  const auto decoded = decode_binding({socket});
  EXPECT_EQ(decoded.unread_atom, 0x1dU);
  EXPECT_TRUE(decoded.atom_types.empty());
}

// Detects: a ragdoll writer whose frame, limit, friction or flag offsets disagree with the decoder; each decoded field must survive a write and a read.
TEST(HknpConstraintWriters, RagdollFieldsSurviveWriteThenDecode) {
  HknpConstraint fields;
  fields.frame_a = {HknpVec4{1.0f, 0.0f, 0.0f, 0.5f}, HknpVec4{0.0f, 1.0f, 0.0f, -0.25f},
                    HknpVec4{0.0f, 0.0f, 1.0f, 0.75f}, HknpVec4{0.5f, -1.0f, 2.0f, 0.125f}};
  fields.frame_b = {HknpVec4{0.0f, 0.0f, 1.0f, 0.0f}, HknpVec4{1.0f, 0.0f, 0.0f, 0.0f},
                    HknpVec4{0.0f, 1.0f, 0.0f, 0.0f}, HknpVec4{-3.0f, 4.0f, -5.0f, 0.5f}};
  fields.friction = 0.5f;
  fields.motor_enabled = true;
  fields.setup_stabilization = true;
  fields.ball_socket_method = 0;
  fields.twist = {true, -0.5f, 0.5f, 0.25f};
  fields.cone = {true, -1.0f, 1.0f, 0.75f};
  fields.plane = {true, -0.125f, 0.25f, 1.0f};
  const auto written = fo4::nif::encode_ragdoll_constraint(fields);
  ASSERT_EQ(written.size(), 416U);
  const auto decoded = decode_binding({object_from_bytes("hkpRagdollConstraintData", written)});
  EXPECT_EQ(decoded.frame_a, fields.frame_a);
  EXPECT_EQ(decoded.frame_b, fields.frame_b);
  EXPECT_FLOAT_EQ(decoded.friction, 0.5f);
  EXPECT_TRUE(decoded.motor_enabled);
  EXPECT_TRUE(decoded.setup_stabilization);
  EXPECT_EQ(decoded.ball_socket_method, 0);
  EXPECT_FLOAT_EQ(decoded.twist.min, -0.5f);
  EXPECT_FLOAT_EQ(decoded.twist.max, 0.5f);
  EXPECT_FLOAT_EQ(decoded.twist.tau, 0.25f);
  EXPECT_FLOAT_EQ(decoded.cone.max, 1.0f);
  EXPECT_FLOAT_EQ(decoded.plane.min, -0.125f);
  EXPECT_FLOAT_EQ(decoded.plane.tau, 1.0f);
  EXPECT_EQ(decoded.atom_types, (std::vector<std::uint16_t>{0x02, 0x17, 0x13, 0x11, 0x0f, 0x10, 0x10, 0x05}));
  EXPECT_EQ(decoded.unread_atom, 0U);
}

// Detects: a limited-hinge writer that places the hinge limit or the angular motor flag at the ragdoll offsets, so a hinge reads back with the wrong limit or motor state.
TEST(HknpConstraintWriters, LimitedHingeFieldsSurviveWriteThenDecode) {
  HknpConstraint fields;
  fields.frame_a[3] = HknpVec4{0.0f, 0.0f, 0.5f, 0.0f};
  fields.friction = 0.25f;
  fields.motor_enabled = true;
  fields.hinge = {true, -0.5f, 1.0f, 0.5f};
  fields.ball_socket_method = 1;
  const auto written = fo4::nif::encode_limited_hinge_constraint(fields);
  ASSERT_EQ(written.size(), 304U);
  const auto decoded = decode_binding({object_from_bytes("hkpLimitedHingeConstraintData", written)});
  EXPECT_EQ(decoded.frame_a[3], fields.frame_a[3]);
  EXPECT_FLOAT_EQ(decoded.friction, 0.25f);
  EXPECT_TRUE(decoded.motor_enabled);
  EXPECT_FLOAT_EQ(decoded.hinge.min, -0.5f);
  EXPECT_FLOAT_EQ(decoded.hinge.max, 1.0f);
  EXPECT_FLOAT_EQ(decoded.hinge.tau, 0.5f);
  EXPECT_EQ(decoded.atom_types, (std::vector<std::uint16_t>{0x02, 0x17, 0x12, 0x11, 0x0e, 0x0c, 0x05}));
}

// Detects: a ball-socket or prismatic writer that drops a translation word, the linear limit or the motor flag; each must read back as it was written.
TEST(HknpConstraintWriters, BallAndSocketAndPrismaticFieldsSurviveWriteThenDecode) {
  HknpConstraint socket;
  socket.translation_a = HknpVec4{1.0f, 2.0f, 3.0f, 0.5f};
  socket.translation_b = HknpVec4{-1.0f, 0.0f, 0.25f, -2.0f};
  const auto socket_bytes = fo4::nif::encode_ball_and_socket_constraint(socket);
  ASSERT_EQ(socket_bytes.size(), 112U);
  const auto socket_read = decode_binding({object_from_bytes("hkpBallAndSocketConstraintData", socket_bytes)});
  EXPECT_EQ(socket_read.translation_a, socket.translation_a);
  EXPECT_EQ(socket_read.translation_b, socket.translation_b);

  HknpConstraint prismatic;
  prismatic.linear_motor_enabled = true;
  prismatic.linear_limit = {true, -0.25f, 2.0f, 0.0f};
  const auto prismatic_bytes = fo4::nif::encode_prismatic_constraint(prismatic);
  ASSERT_EQ(prismatic_bytes.size(), 288U);
  const auto prismatic_read = decode_binding({object_from_bytes("hkpPrismaticConstraintData", prismatic_bytes)});
  EXPECT_TRUE(prismatic_read.linear_motor_enabled);
  EXPECT_FLOAT_EQ(prismatic_read.linear_limit.min, -0.25f);
  EXPECT_FLOAT_EQ(prismatic_read.linear_limit.max, 2.0f);
}

// Detects: a breakable writer that puts the threshold outside the wrapper's field, or a wrapper whose threshold no longer reaches the decoded constraint.
TEST(HknpConstraintWriters, BreakableThresholdAndPositionMotorFloatsRoundTrip) {
  HknpConstraint breakable;
  breakable.break_threshold = 0.25f;
  HknpPackObject wrapper = object_from_bytes("hknpBreakableConstraintData", fo4::nif::encode_breakable_constraint(breakable));
  put_global(wrapper, 0x30, 2);
  ASSERT_EQ(wrapper.bytes.size(), 48U);
  HknpPackObject socket = object_from_bytes("hkpBallAndSocketConstraintData", fo4::nif::encode_ball_and_socket_constraint({}));
  const auto decoded = decode_binding({wrapper, socket});
  EXPECT_TRUE(decoded.breakable);
  EXPECT_FLOAT_EQ(decoded.break_threshold, 0.25f);
  EXPECT_EQ(decoded.kind, "hkpBallAndSocketConstraintData");

  const fo4::nif::HknpPositionMotor motor{};
  const auto motor_bytes = fo4::nif::encode_position_motor(motor);
  ASSERT_EQ(motor_bytes.size(), 48U);
  EXPECT_FLOAT_EQ(f32_in(motor_bytes, 32), 0.8f);
  EXPECT_FLOAT_EQ(f32_in(motor_bytes, 36), 1.0f);
  EXPECT_FLOAT_EQ(f32_in(motor_bytes, 40), 5.0f);
  EXPECT_FLOAT_EQ(f32_in(motor_bytes, 44), 0.2f);
}
