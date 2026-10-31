#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using nif_build::Block;
using nif_build::Writer;

Block collision_block(std::string type, const std::vector<std::uint8_t>& payload) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(payload.size()));
  for (const auto value : payload) w.u8(value);
  return Block{std::move(type), w.bytes()};
}

Block raw_block(std::string type, const std::vector<std::uint8_t>& body) {
  Writer w;
  for (const auto value : body) w.u8(value);
  return Block{std::move(type), w.bytes()};
}

std::vector<std::byte> blob_bytes(const std::vector<std::uint8_t>& values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const auto value : values) out.push_back(std::byte{value});
  return out;
}

Block collision_object_block(std::int32_t target,
                             std::int32_t data,
                             std::uint32_t body_id,
                             std::uint16_t flags = 0) {
  Writer w;
  w.i32(target);
  w.u16(flags);
  w.i32(data);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

}  // namespace

TEST(NifCollision, ReadsPhysicsAndRagdollBlobsInBlockOrder) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57}),
                              collision_block("bhkRagdollSystem", {0x01, 0x02, 0x03})},
                           {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 2U);
  EXPECT_EQ(blobs[0].block, 0U);
  EXPECT_EQ(blobs[0].type, "bhkPhysicsSystem");
  EXPECT_EQ(blobs[0].bytes, blob_bytes({0x57, 0xE0, 0xE0, 0x57}));
  EXPECT_EQ(blobs[1].block, 1U);
  EXPECT_EQ(blobs[1].type, "bhkRagdollSystem");
  EXPECT_EQ(blobs[1].bytes, blob_bytes({0x01, 0x02, 0x03}));
}

TEST(NifCollision, ReadsAZeroLengthPayloadAsAnEmptyBlob) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif({}, {collision_block("bhkPhysicsSystem", {})}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].block, 0U);
  EXPECT_TRUE(blobs[0].bytes.empty());
}

TEST(NifCollision, IgnoresTrailingBytesAfterTheDeclaredLength) {
  Writer w;
  w.u32(2);
  w.u8(0xAA);
  w.u8(0xBB);
  w.u8(0x00);
  w.u8(0x00);
  const auto nif = fo4::nif::read_nif(nif_build::build_nif({}, {Block{"bhkPhysicsSystem", w.bytes()}}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].bytes, blob_bytes({0xAA, 0xBB}));
}

TEST(NifCollision, IgnoresBlocksThatAreNotCollisionSystems) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("BSClothExtraData", {0x01, 0x02})}, {}));
  EXPECT_TRUE(fo4::nif::read_collision_blobs(nif).empty());
}

TEST(NifCollision, RejectsABlockShorterThanItsLengthPrefix) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {raw_block("bhkPhysicsSystem", {0x01, 0x00, 0x00})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision block is shorter than its length prefix");
}

TEST(NifCollision, RejectsADeclaredLengthPastTheEndOfItsBlock) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {raw_block("bhkPhysicsSystem", {0x09, 0x00, 0x00, 0x00, 0xAA, 0xBB})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision blob length runs past the end of its block");
}

TEST(NifCollision, AttachesTheOwningObjectFlagsSystemBodyIdAndTarget) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {"collision"},
      {nif_build::named_node_block("NiNode", 0, {}, {}),
       collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57}),
       collision_object_block(0, 1, 0x1234, 0x0003)},
      {0}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 2);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0x0003});
  EXPECT_EQ(blobs[0].system_ref, 1);
  EXPECT_EQ(blobs[0].body_id, 0x1234U);
  EXPECT_EQ(blobs[0].target, 0);
  ASSERT_EQ(blobs[0].target_node, 0);
  EXPECT_EQ(nif.nodes()[static_cast<std::size_t>(blobs[0].target_node)].name, "collision");
}

TEST(NifCollision, AttachesAnObjectThatPrecedesItsSystem) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {"node"}, {collision_object_block(2, 1, 5), collision_block("bhkPhysicsSystem", {0x01}),
           nif_build::node_block("NiNode", {}, {})},
      {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 0);
  EXPECT_EQ(blobs[0].body_id, 5U);
  EXPECT_EQ(blobs[0].target, 2);
  EXPECT_EQ(blobs[0].target_node, 0);
}

TEST(NifCollision, KeepsASystemWithoutAnObjectUnattributed) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("bhkRagdollSystem", {0x01})}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, -1);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0});
  EXPECT_EQ(blobs[0].system_ref, -1);
  EXPECT_EQ(blobs[0].body_id, 0U);
  EXPECT_EQ(blobs[0].target, -1);
  EXPECT_EQ(blobs[0].target_node, -1);
}

TEST(NifCollision, IgnoresAnObjectWhoseDataIsNotACollisionSystem) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({"node"}, {collision_block("bhkPhysicsSystem", {0x01}),
                                nif_build::node_block("NiNode", {}, {}), collision_object_block(1, 1, 9)},
                           {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, -1);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0});
  EXPECT_EQ(blobs[0].system_ref, -1);
  EXPECT_EQ(blobs[0].body_id, 0U);
}

TEST(NifCollision, KeepsTheFirstOfTwoObjectsNamingTheSameSystem) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {collision_block("bhkPhysicsSystem", {0x01}), collision_object_block(-1, 0, 7),
           collision_object_block(-1, 0, 9)},
      {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 1);
  EXPECT_EQ(blobs[0].system_ref, 0);
  EXPECT_EQ(blobs[0].body_id, 7U);
}

TEST(NifCollision, IgnoresATargetOutsideTheBlockTable) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {collision_block("bhkPhysicsSystem", {0x01}), collision_object_block(99, 0, 3)}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 1);
  EXPECT_EQ(blobs[0].body_id, 3U);
  EXPECT_EQ(blobs[0].target, -1);
  EXPECT_EQ(blobs[0].target_node, -1);
}

TEST(NifCollision, RejectsACollisionObjectShorterThanItsFields) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {raw_block("bhkNPCollisionObject", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision object is shorter than its fields");
}

namespace {

constexpr std::size_t k_data_section_header = 0x40 + 2 * 0x40;

fo4::nif::HknpPackfile two_object_packfile() {
  fo4::nif::HknpPackfile pack;
  pack.classes = {{0x00C0FFEEu, "hknpPhysicsSystemData"}, {0x0BADF00Du, "hkRefCountedProperties"}};
  fo4::nif::HknpPackObject root;
  root.class_name = "hknpPhysicsSystemData";
  root.bytes.assign(32, std::byte{0x11});
  root.local.push_back({0x10, 0x18});
  root.global.push_back({0x08, 1});
  fo4::nif::HknpPackObject props;
  props.class_name = "hkRefCountedProperties";
  props.bytes.assign(16, std::byte{0x22});
  pack.objects = {root, props};
  return pack;
}

std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
  }
  return value;
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

std::size_t data_start(const std::vector<std::byte>& bytes) {
  return read_u32(bytes, k_data_section_header + 0x14);
}

std::size_t data_table(const std::vector<std::byte>& bytes, std::size_t field) {
  return data_start(bytes) + read_u32(bytes, k_data_section_header + field);
}

}  // namespace

// Detects: a rebuild that drops, reorders or re-pads a class, object or fixup; read then write must return the same bytes.
TEST(HknpPackfile, RebuildsAStoredPackfileByteForByte) {
  const auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  const auto pack = fo4::nif::read_hknp_packfile(bytes);
  ASSERT_EQ(pack.objects.size(), 2U);
  EXPECT_EQ(pack.objects[0].class_name, "hknpPhysicsSystemData");
  ASSERT_EQ(pack.objects[0].local.size(), 1U);
  EXPECT_EQ(pack.objects[0].local[0].source, 0x10U);
  EXPECT_EQ(pack.objects[0].local[0].target, 0x18U);
  ASSERT_EQ(pack.objects[0].global.size(), 1U);
  EXPECT_EQ(pack.objects[0].global[0].object, 1U);
  EXPECT_EQ(pack.objects[1].class_name, "hkRefCountedProperties");
  EXPECT_EQ(pack.objects[1].bytes.size(), 16U);
  EXPECT_EQ(fo4::nif::write_hknp_packfile(pack), bytes);
}

// Detects: a rebuild that shrinks a global fixup table the file reserved past its entries; the reserved span must round-trip.
TEST(HknpPackfile, KeepsAReservedGlobalFixupSpan) {
  auto pack = two_object_packfile();
  pack.global_fixup_bytes = 48;
  const auto bytes = fo4::nif::write_hknp_packfile(pack);
  const auto back = fo4::nif::read_hknp_packfile(bytes);
  EXPECT_EQ(back.global_fixup_bytes, 48U);
  EXPECT_EQ(fo4::nif::write_hknp_packfile(back), bytes);
}

// Detects: an over-read of a truncated packfile; every proper prefix must raise fo4::parse_error instead of reading past its end.
TEST(HknpPackfile, RejectsEveryTruncatedPrefix) {
  const auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    const std::vector<std::byte> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(prefix); }, "");
  }
}

// Detects: a payload without the Havok magic parsed as section tables; the magic must be checked before any table is read.
TEST(HknpPackfile, RejectsAPayloadWithoutThePackfileMagic) {
  const std::vector<std::byte> bytes(0x100, std::byte{0});
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); }, "not a Havok binary packfile");
}

// Detects: a local fixup that points into the next object, which a rebuild would silently re-home; a local target must stay inside its own object.
TEST(HknpPackfile, RejectsALocalFixupTargetOutsideItsObject) {
  auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  write_u32(bytes, data_table(bytes, 0x18) + 4, 0x40U);
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); },
                                "local fixup target lies outside its object");
}

// Detects: a global fixup aimed inside an object, which a rebuild cannot express as an object index; the target must be an object start.
TEST(HknpPackfile, RejectsAGlobalFixupThatIsNotAnObjectStart) {
  auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  write_u32(bytes, data_table(bytes, 0x1C) + 8, 4U);
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); },
                                "global fixup target is not an object start");
}

// Detects: a virtual fixup naming a class the table does not hold, which a rebuild would write as a dangling name; the name must resolve.
TEST(HknpPackfile, RejectsAnObjectNamingNoClass) {
  auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  write_u32(bytes, data_table(bytes, 0x20) + 8, 0x7777u);
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); },
                                "object names no class in the class table");
}

// Detects: a section descriptor placing the fixup tables past the file, which a parser would read as a short slice; the ranges must stay inside the data section.
TEST(HknpPackfile, RejectsASectionRangePastTheEnd) {
  auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  write_u32(bytes, k_data_section_header + 0x20, 0xFFFFFFF0u);
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); },
                                "packfile data section ranges are out of order or past its end");
}

// Detects: stray bytes after a packfile, which a rebuild would silently drop; the size must equal the data section's end.
TEST(HknpPackfile, RejectsTrailingBytesAfterThePackfile) {
  auto bytes = fo4::nif::write_hknp_packfile(two_object_packfile());
  bytes.push_back(std::byte{0});
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); },
                                "packfile size does not match its data section");
}

// Detects: a class table naming one class twice, which the writer would rebuild with the second entry's hash lost; the reader must reject the duplicate name.
TEST(HknpPackfile, RejectsAClassTableThatNamesOneClassTwice) {
  auto pack = two_object_packfile();
  pack.classes.push_back({0x1234u, "hknpPhysicsSystemData"});
  const auto bytes = fo4::nif::write_hknp_packfile(pack);
  nif_build::expect_parse_error([&] { fo4::nif::read_hknp_packfile(bytes); }, "class table names a class twice");
}

namespace {

using fo4::nif::HknpGlobalFixup;
using fo4::nif::HknpLocalFixup;
using fo4::nif::HknpPackfile;
using fo4::nif::HknpPackObject;

HknpPackObject make_object(std::string class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

void put_le(HknpPackObject& object, std::size_t offset, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    object.bytes.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
  }
}

void put_u8(HknpPackObject& object, std::size_t offset, std::uint8_t value) {
  object.bytes.at(offset) = std::byte{value};
}

void put_u16(HknpPackObject& object, std::size_t offset, std::uint16_t value) {
  put_le(object, offset, value, 2);
}

void put_u32(HknpPackObject& object, std::size_t offset, std::uint32_t value) {
  put_le(object, offset, value, 4);
}

void put_f32(HknpPackObject& object, std::size_t offset, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(object, offset, bits);
}

void put_vec3(HknpPackObject& object, std::size_t offset, float x, float y, float z) {
  put_f32(object, offset, x);
  put_f32(object, offset + 4, y);
  put_f32(object, offset + 8, z);
}

void put_array(HknpPackObject& object, std::size_t field, std::uint16_t count, std::size_t payload) {
  put_u16(object, field, count);
  put_u16(object, field + 2, static_cast<std::uint16_t>(payload - field));
}

void put_local(HknpPackObject& object, std::size_t source, std::size_t target) {
  object.local.push_back(HknpLocalFixup{static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

void put_global(HknpPackObject& object, std::size_t source, std::size_t target) {
  object.global.push_back(HknpGlobalFixup{static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

// Constraint: objects[0] is the root, and its one body names objects[1], the first shape given here.
HknpPackfile system_with_body(std::vector<HknpPackObject> shapes) {
  HknpPackObject root = make_object("hknpPhysicsSystemData", 0x200);
  put_local(root, 0x40, 0x80);
  put_u32(root, 0x48, 1);
  put_global(root, 0x80, 1);
  put_u32(root, 0x8c, 0x7fffffffU);
  HknpPackfile pack;
  pack.objects.push_back(std::move(root));
  for (auto& shape : shapes) {
    pack.objects.push_back(std::move(shape));
  }
  return pack;
}

HknpPackObject make_sphere(float radius, float x = 0.0f, float y = 0.0f, float z = 0.0f) {
  HknpPackObject shape = make_object("hknpSphereShape", 0xA0);
  put_f32(shape, 0x14, radius);
  put_array(shape, 0x30, 4, 0x60);
  for (std::size_t v = 0; v < 4; ++v) {
    put_vec3(shape, 0x60 + 16 * v, x, y, z);
  }
  return shape;
}

}  // namespace

// Detects: a convex polytope whose vertex, plane or face arrays decode with the wrong counts or indices; the vertices, plane and face loop must match the stored ones.
TEST(HknpDecode, ConvexPolytopeKeepsItsVerticesPlanesAndFaces) {
  HknpPackObject shape = make_object("hknpConvexPolytopeShape", 0x100);
  put_f32(shape, 0x14, 0.04f);
  put_array(shape, 0x30, 4, 0x60);
  put_vec3(shape, 0x60, 0.0f, 0.0f, 0.0f);
  put_vec3(shape, 0x70, 1.0f, 0.0f, 0.0f);
  put_vec3(shape, 0x80, 0.0f, 1.0f, 0.0f);
  put_vec3(shape, 0x90, 0.0f, 0.0f, 1.0f);
  put_array(shape, 0x40, 1, 0xA0);
  put_vec3(shape, 0xA0, 0.0f, 0.0f, 1.0f);
  put_f32(shape, 0xAC, -0.5f);
  put_array(shape, 0x44, 1, 0xB0);
  put_u16(shape, 0xB0, 0);
  put_u8(shape, 0xB2, 3);
  put_array(shape, 0x48, 3, 0xC0);
  put_u8(shape, 0xC0, 0);
  put_u8(shape, 0xC1, 1);
  put_u8(shape, 0xC2, 2);
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({shape}));
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& decoded = sys.shapes[0];
  EXPECT_EQ(decoded.kind, fo4::nif::HknpShapeKind::convex);
  ASSERT_EQ(decoded.vertices.size(), 4U);
  EXPECT_EQ(decoded.vertices[3], (fo4::nif::HknpVec3{0.0f, 0.0f, 1.0f}));
  ASSERT_EQ(decoded.planes.size(), 1U);
  EXPECT_FLOAT_EQ(decoded.planes[0][3], -0.5f);
  ASSERT_EQ(decoded.faces.size(), 1U);
  EXPECT_EQ(decoded.faces[0], (std::vector<std::uint8_t>{0, 1, 2}));
  EXPECT_FLOAT_EQ(decoded.convex_radius, 0.04f);
}

// Detects: a sphere whose vertices all name one point decoding as a polytope; it must decode as a sphere with its centre and convex radius and keep no vertex list.
TEST(HknpDecode, SphereKeepsItsCentreAndRadius) {
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({make_sphere(0.5f, 2.0f, 3.0f, 4.0f)}));
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& shape = sys.shapes[0];
  EXPECT_EQ(shape.kind, fo4::nif::HknpShapeKind::sphere);
  EXPECT_EQ(shape.point_a, (fo4::nif::HknpVec3{2.0f, 3.0f, 4.0f}));
  EXPECT_FLOAT_EQ(shape.radius, 0.5f);
  EXPECT_TRUE(shape.vertices.empty());
}

// Detects: a capsule decoding with its end points swapped or its radius left at the stored convex radius; the end points must be the stored ones and the radius must be the convex radius divided by 0.9801.
TEST(HknpDecode, CapsuleKeepsItsEndPointsAndOuterRadius) {
  HknpPackObject shape = make_object("hknpCapsuleShape", 0x70);
  put_f32(shape, 0x14, 0.9801f);
  put_vec3(shape, 0x50, 0.0f, 0.0f, 0.0f);
  put_vec3(shape, 0x60, 0.0f, 0.0f, 2.0f);
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({shape}));
  ASSERT_EQ(sys.shapes.size(), 1U);
  EXPECT_EQ(sys.shapes[0].kind, fo4::nif::HknpShapeKind::capsule);
  EXPECT_EQ(sys.shapes[0].point_a, (fo4::nif::HknpVec3{0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(sys.shapes[0].point_b, (fo4::nif::HknpVec3{0.0f, 0.0f, 2.0f}));
  EXPECT_FLOAT_EQ(sys.shapes[0].radius, 1.0f);
}

// Detects: a compound whose unsupported child is dropped, or whose decode throws and loses its siblings; the unsupported child must be listed with its class and object index while the scaled sibling and its sphere still decode.
TEST(HknpDecode, CompoundKeepsAnUnsupportedChildAndItsSiblings) {
  HknpPackObject compound = make_object("hknpStaticCompoundShape", 0x180);
  put_local(compound, 0x60, 0x80);
  put_u32(compound, 0x68, 2);
  put_global(compound, 0xD0, 2);
  put_global(compound, 0x150, 3);
  HknpPackObject mesh = make_object("hknpCompressedMeshShape", 0x20);
  HknpPackObject scaled = make_object("hknpScaledConvexShape", 0x50);
  put_vec3(scaled, 0x40, 2.0f, 2.0f, 2.0f);
  put_global(scaled, 0x30, 4);
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({compound, mesh, scaled, make_sphere(0.25f)}));
  ASSERT_EQ(sys.unsupported.size(), 1U);
  EXPECT_EQ(sys.unsupported[0].class_name, "hknpCompressedMeshShape");
  EXPECT_EQ(sys.unsupported[0].object, 2U);
  ASSERT_EQ(sys.bodies.size(), 1U);
  const auto& shape = sys.shapes[static_cast<std::size_t>(sys.bodies[0].shape)];
  EXPECT_EQ(shape.kind, fo4::nif::HknpShapeKind::compound);
  ASSERT_EQ(shape.instances.size(), 2U);
  EXPECT_EQ(shape.instances[0].child_object, 2U);
  EXPECT_EQ(shape.instances[0].shape, -1);
  ASSERT_NE(shape.instances[1].shape, -1);
  const auto& wrapper = sys.shapes[static_cast<std::size_t>(shape.instances[1].shape)];
  EXPECT_EQ(wrapper.kind, fo4::nif::HknpShapeKind::scaled_convex);
  EXPECT_FLOAT_EQ(wrapper.scale[0], 2.0f);
  ASSERT_NE(wrapper.child, -1);
  EXPECT_EQ(sys.shapes[static_cast<std::size_t>(wrapper.child)].kind, fo4::nif::HknpShapeKind::sphere);
}

// Detects: a convex shape's mass properties read through the wrong fixup hop or with packed vectors at the wrong offset; the centre, inertia, volume and mass must match the stored ones.
TEST(HknpDecode, ConvexShapeCarriesItsMassProperties) {
  HknpPackObject shape = make_object("hknpConvexShape", 0x40);
  put_array(shape, 0x30, 0, 0x60);
  put_global(shape, 0x20, 2);
  HknpPackObject reference = make_object("hkRefCountedProperties", 0x20);
  put_global(reference, 0x10, 3);
  HknpPackObject mass = make_object("hknpShapeMassProperties", 0x30);
  put_u16(mass, 0x10, 0x4000);
  put_u16(mass, 0x12, 0);
  put_u16(mass, 0x14, 0);
  put_u16(mass, 0x16, 0x3000);
  put_u16(mass, 0x18, 0x4000);
  put_u16(mass, 0x1a, 0x4000);
  put_u16(mass, 0x1c, 0x4000);
  put_u16(mass, 0x1e, 0x3000);
  put_f32(mass, 0x28, 2.0f);
  put_f32(mass, 0x2c, 2.0f);
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({shape, reference, mass}));
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& decoded = sys.shapes[0];
  ASSERT_TRUE(decoded.has_mass_properties);
  EXPECT_FLOAT_EQ(decoded.mass_properties.center[0], 0.5f);
  EXPECT_FLOAT_EQ(decoded.mass_properties.inertia_raw[1], 0.5f);
  EXPECT_FLOAT_EQ(decoded.mass_properties.volume, 2.0f);
  EXPECT_FLOAT_EQ(decoded.mass_properties.mass, 2.0f);
}

// Detects: a body's motion, inertia or property record read at the wrong stride or field; the friction, restitution, flags, dynamic motion and inverse mass must match the stored values.
TEST(HknpDecode, BodyTakesItsMotionAndPropertyRecords) {
  auto pack = system_with_body({make_sphere(0.5f)});
  auto& root = pack.objects[0];
  put_u32(root, 0x8c, 0);
  put_local(root, 0x20, 0x100);
  put_u32(root, 0x28, 1);
  put_local(root, 0x30, 0x140);
  put_u32(root, 0x38, 1);
  put_local(root, 0x10, 0x180);
  put_f32(root, 0x108, 0.5f);
  put_f32(root, 0x144, 0.25f);
  put_f32(root, 0x148, 2.0f);
  put_vec3(root, 0x160, 0.1f, 0.2f, 0.3f);
  put_u16(root, 0x192, 0x3F00);
  put_u16(root, 0x196, 0x3E80);
  put_u32(root, 0x18c, 0x200000U);
  put_u8(root, 0x190, 2);
  const auto sys = fo4::nif::decode_hknp_system(pack);
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_TRUE(sys.bodies[0].dynamic);
  EXPECT_EQ(sys.bodies[0].motion_index, 0U);
  EXPECT_FLOAT_EQ(sys.bodies[0].friction, 0.5f);
  EXPECT_FLOAT_EQ(sys.bodies[0].restitution, 0.25f);
  EXPECT_EQ(sys.bodies[0].material_flags, 0x200000U);
  EXPECT_EQ(sys.bodies[0].trigger_type, 2U);
  ASSERT_EQ(sys.motions.size(), 1U);
  EXPECT_FLOAT_EQ(sys.motions[0].gravity_factor, 0.5f);
  ASSERT_EQ(sys.inertias.size(), 1U);
  EXPECT_FLOAT_EQ(sys.inertias[0].inverse_mass, 0.25f);
  EXPECT_FLOAT_EQ(sys.inertias[0].mass, 4.0f);
  EXPECT_FLOAT_EQ(sys.inertias[0].density, 2.0f);
  EXPECT_FLOAT_EQ(sys.inertias[0].inverse_inertia[2], 0.3f);
}

// Detects: a ragdoll binding resolving to the wrong constraint, a breakable wrapper left unopened, or an atom chain read at the wrong offset; the bodies, frames, hinge limit and break threshold must match the stored values.
TEST(HknpDecode, ConstraintBindingsDecodeFramesLimitsAndBreakableWrappers) {
  HknpPackObject root = make_object("hknpRagdollData", 0x200);
  put_local(root, 0x50, 0x100);
  put_u32(root, 0x58, 2);
  put_global(root, 0x100, 1);
  put_u32(root, 0x108, 1);
  put_u32(root, 0x10c, 0);
  put_global(root, 0x118, 2);
  put_u32(root, 0x120, 2);
  put_u32(root, 0x124, 0);
  HknpPackObject hinge = make_object("hkpLimitedHingeConstraintData", 0x100);
  put_u16(hinge, 0x20, 0x02);
  put_vec3(hinge, 0x30, 1.0f, 0.0f, 0.0f);
  put_vec3(hinge, 0x60, 0.0f, 0.0f, 0.0f);
  put_vec3(hinge, 0xA0, 0.0f, 0.0f, 0.5f);
  put_u16(hinge, 0xB0, 0x0e);
  put_f32(hinge, 0xB4, -0.5f);
  put_f32(hinge, 0xB8, 1.0f);
  put_f32(hinge, 0xBC, 1.0f);
  HknpPackObject wrapper = make_object("hknpBreakableConstraintData", 0x40);
  put_global(wrapper, 0x30, 1);
  put_f32(wrapper, 0x20, 0.01f);
  HknpPackfile pack;
  pack.objects = {root, hinge, wrapper};
  const auto sys = fo4::nif::decode_hknp_system(pack);
  ASSERT_EQ(sys.constraints.size(), 2U);
  const auto& plain = sys.constraints[0];
  EXPECT_EQ(plain.kind, "hkpLimitedHingeConstraintData");
  EXPECT_EQ(plain.child_body, 1);
  EXPECT_EQ(plain.parent_body, 0);
  EXPECT_TRUE(plain.has_frames);
  EXPECT_EQ(plain.rotation_a[0], (fo4::nif::HknpVec3{1.0f, 0.0f, 0.0f}));
  EXPECT_EQ(plain.pivot_b, (fo4::nif::HknpVec3{0.0f, 0.0f, 0.5f}));
  EXPECT_TRUE(plain.hinge.present);
  EXPECT_FLOAT_EQ(plain.hinge.min, -0.5f);
  EXPECT_FLOAT_EQ(plain.hinge.max, 1.0f);
  EXPECT_FALSE(plain.breakable);
  EXPECT_EQ(plain.unread_atom, 0U);
  const auto& broken = sys.constraints[1];
  EXPECT_TRUE(broken.breakable);
  EXPECT_FLOAT_EQ(broken.break_threshold, 0.01f);
  EXPECT_EQ(broken.object, 1U);
  EXPECT_EQ(broken.kind, "hkpLimitedHingeConstraintData");
  EXPECT_EQ(broken.child_body, 2);
  EXPECT_FLOAT_EQ(broken.hinge.min, -0.5f);
}

// Detects: a skeleton whose parent indices or reference pose are read at the wrong stride; each bone's parent, translation and rotation must match the stored values.
TEST(HknpDecode, SkeletonKeepsParentsAndReferencePose) {
  HknpPackObject root = make_object("hknpRagdollData", 0x200);
  HknpPackObject skeleton = make_object("hkaSkeleton", 0x100);
  put_local(skeleton, 0x18, 0x40);
  put_u32(skeleton, 0x20, 2);
  put_u16(skeleton, 0x40, 0xFFFF);
  put_u16(skeleton, 0x42, 0);
  put_local(skeleton, 0x38, 0x80);
  put_vec3(skeleton, 0x80, 1.0f, 2.0f, 3.0f);
  put_f32(skeleton, 0x9C, 1.0f);
  put_vec3(skeleton, 0xA0, 1.0f, 1.0f, 1.0f);
  put_vec3(skeleton, 0xB0, 4.0f, 5.0f, 6.0f);
  put_f32(skeleton, 0xCC, 1.0f);
  put_vec3(skeleton, 0xD0, 1.0f, 1.0f, 1.0f);
  HknpPackfile pack;
  pack.objects = {root, skeleton};
  const auto sys = fo4::nif::decode_hknp_system(pack);
  ASSERT_EQ(sys.bones.size(), 2U);
  EXPECT_EQ(sys.bones[0].parent, -1);
  EXPECT_EQ(sys.bones[1].parent, 0);
  EXPECT_EQ(sys.bones[0].translation, (fo4::nif::HknpVec3{1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(sys.bones[1].translation, (fo4::nif::HknpVec3{4.0f, 5.0f, 6.0f}));
  EXPECT_FLOAT_EQ(sys.bones[1].rotation[3], 1.0f);
}

// Detects: a shape object cut short of the vertex array it names, read past its end instead of raising fo4::parse_error; every truncation must raise.
TEST(HknpDecode, TruncatedShapeObjectRaisesParseError) {
  for (const std::size_t length : {std::size_t{0}, std::size_t{0x20}, std::size_t{0x40}, std::size_t{0x80},
                                   std::size_t{0x90}}) {
    HknpPackObject shape = make_sphere(0.5f);
    shape.bytes.resize(length);
    const auto pack = system_with_body({shape});
    EXPECT_THROW(fo4::nif::decode_hknp_system(pack), fo4::parse_error) << "truncated to " << length << " bytes";
  }
}

// Detects: a local fixup whose target lies past its object, or a global fixup naming no object of the packfile, followed into memory outside the packfile; both must raise fo4::parse_error.
TEST(HknpDecode, FixupsOutsideTheirObjectRaiseParseError) {
  auto local_past_end = system_with_body({make_sphere(0.5f)});
  local_past_end.objects[0].local[0].target = 0x300;
  EXPECT_THROW(fo4::nif::decode_hknp_system(local_past_end), fo4::parse_error);
  auto global_to_nothing = system_with_body({make_sphere(0.5f)});
  global_to_nothing.objects[0].global[0].object = 9;
  EXPECT_THROW(fo4::nif::decode_hknp_system(global_to_nothing), fo4::parse_error);
}

// Detects: a scaled-shape chain nested past the decoder's depth cap, which recurses until the stack overflows instead of raising fo4::parse_error; a 64-link chain must raise.
TEST(HknpDecode, ScaledShapeChainDeeperThanTheCapRaisesParseError) {
  std::vector<HknpPackObject> shapes;
  for (std::size_t k = 0; k < 64; ++k) {
    HknpPackObject scaled = make_object("hknpScaledConvexShape", 0x50);
    put_vec3(scaled, 0x40, 1.0f, 1.0f, 1.0f);
    put_global(scaled, 0x30, k + 2);
    shapes.push_back(std::move(scaled));
  }
  shapes.push_back(make_sphere(0.5f));
  EXPECT_THROW(fo4::nif::decode_hknp_system(system_with_body(std::move(shapes))), fo4::parse_error);
}

// Detects: a constraint atom type missing from the length table ending the walk with no trace, so a consumer reads the constraint as fully decoded; the type must be reported in unread_atom and the atom behind it must stay undecoded.
TEST(HknpDecode, UnknownAtomTypeIsReportedAndStopsTheWalk) {
  HknpPackObject root = make_object("hknpRagdollData", 0x200);
  put_local(root, 0x50, 0x100);
  put_u32(root, 0x58, 1);
  put_global(root, 0x100, 1);
  HknpPackObject socket = make_object("hkpBallAndSocketConstraintData", 0x80);
  put_u16(socket, 0x20, 0x03);
  put_u16(socket, 0x30, 0x0e);
  put_f32(socket, 0x34, -0.5f);
  put_f32(socket, 0x38, 1.0f);
  HknpPackfile pack;
  pack.objects = {root, socket};
  const auto sys = fo4::nif::decode_hknp_system(pack);
  ASSERT_EQ(sys.constraints.size(), 1U);
  EXPECT_EQ(sys.constraints[0].unread_atom, 0x03U);
  EXPECT_FALSE(sys.constraints[0].hinge.present);
}

namespace {

std::uint32_t u32_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(object.bytes.at(offset + i))) << (8 * i);
  }
  return value;
}

float f32_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  const std::uint32_t bits = u32_in_object(object, offset);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

fo4::nif::HknpVec3 vec3_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  return {f32_in_object(object, offset), f32_in_object(object, offset + 4), f32_in_object(object, offset + 8)};
}

float distance_to_segment(const fo4::nif::HknpVec3& p, const fo4::nif::HknpVec3& a, const fo4::nif::HknpVec3& b) {
  std::array<float, 3> ab{};
  std::array<float, 3> ap{};
  float len_sq = 0.0f;
  float ap_dot_ab = 0.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    ab[k] = b[k] - a[k];
    ap[k] = p[k] - a[k];
    len_sq += ab[k] * ab[k];
    ap_dot_ab += ap[k] * ab[k];
  }
  const float t = len_sq > 0.0f ? std::clamp(ap_dot_ab / len_sq, 0.0f, 1.0f) : 0.0f;
  float sum = 0.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    const float d = ap[k] - t * ab[k];
    sum += d * d;
  }
  return std::sqrt(sum);
}

fo4::nif::HknpSystem decoded_after_rebuild(const fo4::nif::HknpShape& shape) {
  const auto bytes = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}));
  return fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));
}

}  // namespace

TEST(HknpEncode, BoxAuthoredBytesSurviveReadThenWriteAndDecodeToItsMass) {
  const auto shape = fo4::nif::make_hknp_box({1.0f, 2.0f, 3.0f}, {2.0f, 1.0f, 0.5f}, 0x064003D4U);
  const auto bytes = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}));
  EXPECT_EQ(fo4::nif::write_hknp_packfile(fo4::nif::read_hknp_packfile(bytes)), bytes);
  const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_FALSE(sys.bodies[0].dynamic);
  EXPECT_EQ(sys.bodies[0].packed_filter, 1U);
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& decoded = sys.shapes[0];
  EXPECT_EQ(decoded.kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(decoded.vertices.size(), 8U);
  EXPECT_EQ(decoded.faces.size(), 6U);
  EXPECT_EQ(decoded.planes.size(), 8U);
  ASSERT_TRUE(decoded.has_mass_properties);
  EXPECT_FLOAT_EQ(decoded.mass_properties.volume, 8.0f);
  EXPECT_FLOAT_EQ(decoded.mass_properties.mass, 8.0f);
  EXPECT_NEAR(decoded.mass_properties.center[0], 1.0f, 1e-3f);
  EXPECT_NEAR(decoded.mass_properties.center[1], 2.0f, 1e-3f);
  EXPECT_NEAR(decoded.mass_properties.center[2], 3.0f, 1e-3f);
}

TEST(HknpEncode, BoxPlanesWindingAndRadiusAgreeForAnOffCentreBox) {
  const fo4::nif::HknpVec3 center{3.0f, -2.0f, 1.0f};
  const fo4::nif::HknpVec3 half{0.5f, 2.0f, 0.25f};
  const auto box = fo4::nif::make_hknp_box(center, half, 7U);
  ASSERT_EQ(box.faces.size(), 6U);
  ASSERT_EQ(box.planes.size(), 8U);
  EXPECT_FLOAT_EQ(box.convex_radius, 0.01f);
  for (std::size_t f = 0; f < box.faces.size(); ++f) {
    const auto& plane = box.planes[f];
    const auto& loop = box.faces[f];
    for (const auto index : loop) {
      const auto& v = box.vertices[index];
      EXPECT_NEAR(plane[0] * v[0] + plane[1] * v[1] + plane[2] * v[2] + plane[3], 0.0f, 1e-5f);
    }
    const auto& p0 = box.vertices[loop[0]];
    const auto& p1 = box.vertices[loop[1]];
    const auto& p2 = box.vertices[loop[2]];
    const fo4::nif::HknpVec3 e1{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const fo4::nif::HknpVec3 e2{p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
    const fo4::nif::HknpVec3 normal_of_winding{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                               e1[0] * e2[1] - e1[1] * e2[0]};
    EXPECT_GT(normal_of_winding[0] * plane[0] + normal_of_winding[1] * plane[1] + normal_of_winding[2] * plane[2],
              0.0f);
  }
  for (std::size_t k = 0; k < 3; ++k) {
    float high = box.vertices[0][k];
    float low = box.vertices[0][k];
    for (const auto& v : box.vertices) {
      high = std::max(high, v[k]);
      low = std::min(low, v[k]);
    }
    EXPECT_NEAR(high - center[k] + box.convex_radius, half[k], 1e-5f);
    EXPECT_NEAR(center[k] - low + box.convex_radius, half[k], 1e-5f);
  }
  EXPECT_FLOAT_EQ(fo4::nif::make_hknp_box({}, {0.005f, 0.005f, 0.005f}, 1U).convex_radius, 0.0025f);
  EXPECT_THROW(fo4::nif::make_hknp_box({}, {0.0f, 1.0f, 1.0f}, 1U), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_box({}, {1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f}, 1U),
               std::invalid_argument);
}

TEST(HknpEncode, SphereCapsuleAndConvexAuthoredValuesDecodeBack) {
  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  sphere.radius = 0.5f;
  sphere.point_a = {2.0f, 3.0f, 4.0f};
  const auto sphere_sys = decoded_after_rebuild(sphere);
  ASSERT_EQ(sphere_sys.shapes.size(), 1U);
  EXPECT_EQ(sphere_sys.shapes[0].kind, fo4::nif::HknpShapeKind::sphere);
  EXPECT_EQ(sphere_sys.shapes[0].point_a, (fo4::nif::HknpVec3{2.0f, 3.0f, 4.0f}));
  EXPECT_FLOAT_EQ(sphere_sys.shapes[0].radius, 0.5f);

  fo4::nif::HknpShape capsule;
  capsule.kind = fo4::nif::HknpShapeKind::capsule;
  capsule.class_name = "hknpCapsuleShape";
  capsule.radius = 0.5f;
  capsule.point_a = {0.0f, 0.0f, 0.0f};
  capsule.point_b = {0.0f, 0.0f, 2.0f};
  const auto capsule_sys = decoded_after_rebuild(capsule);
  ASSERT_EQ(capsule_sys.shapes.size(), 1U);
  EXPECT_EQ(capsule_sys.shapes[0].kind, fo4::nif::HknpShapeKind::capsule);
  EXPECT_EQ(capsule_sys.shapes[0].point_a, capsule.point_a);
  EXPECT_EQ(capsule_sys.shapes[0].point_b, capsule.point_b);
  EXPECT_NEAR(capsule_sys.shapes[0].radius, 0.5f, 1e-6f);
  const auto& capsule_object = capsule_sys.packfile.objects[capsule_sys.shapes[0].object];
  const float pad = f32_in_object(capsule_object, 0x14) / 99.0f;
  for (std::size_t i = 0; i < 8; ++i) {
    EXPECT_LE(distance_to_segment(vec3_in_object(capsule_object, 0x70 + 16 * i), capsule.point_a, capsule.point_b),
              2.0f * pad);
  }

  fo4::nif::HknpShape convex;
  convex.kind = fo4::nif::HknpShapeKind::convex;
  convex.class_name = "hknpConvexShape";
  convex.shape_flags = 0x01000143U;
  convex.convex_radius = 0.04f;
  convex.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const auto convex_sys = decoded_after_rebuild(convex);
  ASSERT_EQ(convex_sys.shapes.size(), 1U);
  EXPECT_EQ(convex_sys.shapes[0].kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(convex_sys.shapes[0].vertices, convex.vertices);
  EXPECT_FLOAT_EQ(convex_sys.shapes[0].convex_radius, 0.04f);
}

TEST(HknpEncode, AuthorRejectsCompoundKindAndNonFiniteGeometry) {
  fo4::nif::HknpShape compound;
  compound.kind = fo4::nif::HknpShapeKind::compound;
  compound.class_name = "hknpStaticCompoundShape";
  EXPECT_THROW(fo4::nif::author_hknp_static_body(compound, {}), std::invalid_argument);

  auto box = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  box.vertices[2][1] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::author_hknp_static_body(box, {}), std::invalid_argument);

  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  EXPECT_THROW(fo4::nif::author_hknp_static_body(sphere, {}), std::invalid_argument);
}

TEST(HknpEncode, ShapeObjectsCarryTheMeasuredLayoutBytes) {
  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  sphere.radius = 0.5f;
  sphere.point_a = {2.0f, 3.0f, 4.0f};
  const auto sphere_pack = fo4::nif::author_hknp_static_body(sphere, {});
  ASSERT_EQ(sphere_pack.objects[0].bytes.size(), 0x140U);
  EXPECT_EQ(u32_in_object(sphere_pack.objects[0], 0xe4), 1U);
  const auto& sphere_object = sphere_pack.objects[1];
  ASSERT_EQ(sphere_object.bytes.size(), 0x80U);
  EXPECT_EQ(u32_in_object(sphere_object, 0x10), 0x01000111U);
  EXPECT_EQ(u32_in_object(sphere_object, 0x30), 0x00100004U);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(vec3_in_object(sphere_object, 0x40 + 16 * i), sphere.point_a);
    EXPECT_EQ(u32_in_object(sphere_object, 0x4c + 16 * i), 0x3f000000U);
  }

  fo4::nif::HknpShape capsule;
  capsule.kind = fo4::nif::HknpShapeKind::capsule;
  capsule.class_name = "hknpCapsuleShape";
  capsule.radius = 0.5f;
  capsule.point_a = {0.0f, 0.0f, 0.0f};
  capsule.point_b = {0.0f, 0.0f, 2.0f};
  const auto capsule_pack = fo4::nif::author_hknp_static_body(capsule, {});
  const auto& capsule_object = capsule_pack.objects[1];
  ASSERT_EQ(capsule_object.bytes.size(), 0x1b0U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x10), 0x010001c3U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x15c), 0xff7fffeeU);
  EXPECT_EQ(u32_in_object(capsule_object, 0x16c), 0xff7fffeeU);
  EXPECT_EQ(u32_in_object(capsule_object, 0x174), 0x04040004U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x190), 0x03020607U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x1a5), 0x00000406U);

  const auto box = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  const auto box_pack = fo4::nif::author_hknp_static_body(box, {});
  const auto& box_object = box_pack.objects[1];
  ASSERT_EQ(box_object.bytes.size(), 0x190U);
  EXPECT_EQ(u32_in_object(box_object, 0x30), 0x00200008U);
  EXPECT_EQ(u32_in_object(box_object, 0x40), 0x00900008U);
  EXPECT_EQ(u32_in_object(box_object, 0x44), 0x010c0006U);
  EXPECT_EQ(u32_in_object(box_object, 0x48), 0x01280018U);
  EXPECT_EQ(u32_in_object(box_pack.objects[2], 24), 0xF100U);
}
