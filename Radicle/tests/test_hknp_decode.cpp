#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include "hknp_test_support.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using fo4::nif::HknpPackfile;
using fo4::nif::HknpPackObject;
using namespace fo4::test::hknp;

namespace {

constexpr std::size_t k_data_section_header = 0x40 + 2 * 0x40;

HknpPackfile two_object_packfile() {
  HknpPackfile pack;
  pack.classes = {{0x00C0FFEEu, "hknpPhysicsSystemData"}, {0x0BADF00Du, "hkRefCountedProperties"}};
  HknpPackObject root;
  root.class_name = "hknpPhysicsSystemData";
  root.bytes.assign(32, std::byte{0x11});
  root.local.push_back({0x10, 0x18});
  root.global.push_back({0x08, 1});
  HknpPackObject props;
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
  HknpPackObject height_field = make_object("hknpHeightFieldShape", 0x20);
  HknpPackObject scaled = make_object("hknpScaledConvexShape", 0x50);
  put_vec3(scaled, 0x40, 2.0f, 2.0f, 2.0f);
  put_global(scaled, 0x30, 4);
  const auto sys = fo4::nif::decode_hknp_system(system_with_body({compound, height_field, scaled, make_sphere(0.25f)}));
  ASSERT_EQ(sys.unsupported.size(), 1U);
  EXPECT_EQ(sys.unsupported[0].class_name, "hknpHeightFieldShape");
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
  put_u32(root, 0x18, 1);
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
