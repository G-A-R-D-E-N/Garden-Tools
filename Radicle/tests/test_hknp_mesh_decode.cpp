#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::HknpGlobalFixup;
using fo4::nif::HknpLocalFixup;
using fo4::nif::HknpPackfile;
using fo4::nif::HknpPackObject;
using fo4::nif::HknpShapeKind;

constexpr std::size_t k_data_sections = 0xD0;
constexpr std::size_t k_data_primitives = 0x130;
constexpr std::size_t k_data_shared_index = 0x138;
constexpr std::size_t k_data_packed = 0x140;
constexpr std::size_t k_data_shared = 0x150;
constexpr std::size_t k_data_runs = 0x158;
constexpr std::size_t k_data_size = 0x15C;
constexpr std::uint32_t k_run_material_crc = 0x12345678U;
constexpr std::uint32_t k_shape_material_crc = 0x0BADF00DU;

HknpPackObject make_object(std::string class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

void put_le(HknpPackObject& object, std::size_t offset, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    object.bytes.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
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

void put_u64(HknpPackObject& object, std::size_t offset, std::uint64_t value) {
  put_u32(object, offset, static_cast<std::uint32_t>(value));
  put_u32(object, offset + 4, static_cast<std::uint32_t>(value >> 32U));
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

void put_array(HknpPackObject& object, std::size_t field, std::uint32_t count, std::size_t payload) {
  put_u32(object, field + 8, count);
  object.local.push_back(HknpLocalFixup{static_cast<std::uint32_t>(field), static_cast<std::uint32_t>(payload)});
}

void put_global(HknpPackObject& object, std::size_t source, std::size_t target) {
  object.global.push_back(HknpGlobalFixup{static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

// Constraint: objects[0] is the root, and its one body names objects[1], the shape given here.
HknpPackfile system_with_shape(std::vector<HknpPackObject> shapes) {
  HknpPackObject root = make_object("hknpPhysicsSystemData", 0x200);
  put_array(root, 0x40, 1, 0x80);
  put_global(root, 0x80, 1);
  put_u32(root, 0x8c, 0x7fffffffU);
  HknpPackfile pack;
  pack.objects.push_back(std::move(root));
  for (auto& shape : shapes) {
    pack.objects.push_back(std::move(shape));
  }
  return pack;
}

// Constraint: object 1 is the compressed mesh shape, 2 its shape data reference, 3 its material table, and 4 the mesh data.
HknpPackfile single_section_system() {
  HknpPackObject shape = make_object("hknpCompressedMeshShape", 0xA0);
  put_u32(shape, 0x18, k_shape_material_crc);
  put_global(shape, 0x20, 2);
  put_global(shape, 0x60, 4);
  HknpPackObject reference = make_object("hkReferencedObject", 0x20);
  put_global(reference, 0x10, 3);
  HknpPackObject materials = make_object("hknpBSMaterialProperties", 0x38);
  put_array(materials, 0x10, 1, 0x20);
  put_u32(materials, 0x34, k_run_material_crc);
  HknpPackObject data = make_object("hknpCompressedMeshShapeData", k_data_size);
  put_vec3(data, 0x20, 0.0f, 0.0f, 0.0f);
  put_vec3(data, 0x30, 4.0f, 4.0f, 4.0f);
  put_array(data, 0x50, 1, k_data_sections);
  put_array(data, 0x60, 2, k_data_primitives);
  put_array(data, 0x70, 1, k_data_shared_index);
  put_array(data, 0x80, 4, k_data_packed);
  put_array(data, 0x90, 1, k_data_shared);
  put_array(data, 0xA0, 1, k_data_runs);
  const std::size_t section = k_data_sections;
  put_vec3(data, section + 0x30, 1.0f, 2.0f, 3.0f);
  put_vec3(data, section + 0x3c, 0.5f, 0.25f, 0.125f);
  put_u32(data, section + 0x50, 2U);
  put_u32(data, section + 0x54, 1U);
  put_u8(data, section + 0x58, 4);
  put_u8(data, k_data_primitives + 0, 0);
  put_u8(data, k_data_primitives + 1, 1);
  put_u8(data, k_data_primitives + 2, 2);
  put_u8(data, k_data_primitives + 3, 3);
  put_u8(data, k_data_primitives + 4, 0);
  put_u8(data, k_data_primitives + 5, 1);
  put_u8(data, k_data_primitives + 6, 4);
  put_u8(data, k_data_primitives + 7, 4);
  put_u16(data, k_data_shared_index, 0);
  put_u32(data, k_data_packed + 0, 2U | (4U << 11U) | (8U << 22U));
  put_u32(data, k_data_packed + 4, 0U);
  put_u32(data, k_data_packed + 8, 4U);
  put_u32(data, k_data_packed + 12, 8U << 11U);
  put_u64(data, k_data_shared, 0x1FFFFFULL | (0x3FFFFFULL << 42U));
  put_u8(data, k_data_runs + 0, 0);
  put_u8(data, k_data_runs + 2, 0);
  put_u8(data, k_data_runs + 3, 2);
  return system_with_shape({std::move(shape), std::move(reference), std::move(materials), std::move(data)});
}

}  // namespace

// Detects: a quad or triangle decoded from the wrong codec bits, the wrong vertex order, or a shared vertex resolved to the packed range; the five vertices, the quad's four indices, the triangle's shared index and the run's material must match the hand-built values.
TEST(HknpMeshDecode, SingleSectionDecodesCodecValuesQuadTriangleAndSharedVertex) {
  const auto sys = fo4::nif::decode_hknp_system(single_section_system());
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& shape = sys.shapes[0];
  EXPECT_EQ(shape.kind, HknpShapeKind::compressed_mesh);
  EXPECT_EQ(shape.aabb_max, (fo4::nif::HknpVec3{4.0f, 4.0f, 4.0f}));
  ASSERT_EQ(shape.mesh_vertices.size(), 5U);
  EXPECT_EQ(shape.mesh_vertices[0], (fo4::nif::HknpVec3{2.0f, 3.0f, 4.0f}));
  EXPECT_EQ(shape.mesh_vertices[1], (fo4::nif::HknpVec3{1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(shape.mesh_vertices[2], (fo4::nif::HknpVec3{3.0f, 2.0f, 3.0f}));
  EXPECT_EQ(shape.mesh_vertices[3], (fo4::nif::HknpVec3{1.0f, 4.0f, 3.0f}));
  EXPECT_EQ(shape.mesh_vertices[4], (fo4::nif::HknpVec3{4.0f, 0.0f, 4.0f}));
  ASSERT_EQ(shape.mesh_triangles.size(), 2U);
  EXPECT_TRUE(shape.mesh_triangles[0].quad);
  EXPECT_EQ(shape.mesh_triangles[0].indices, (std::array<std::uint32_t, 4>{0, 1, 2, 3}));
  EXPECT_FALSE(shape.mesh_triangles[1].quad);
  EXPECT_EQ(shape.mesh_triangles[1].indices, (std::array<std::uint32_t, 4>{0, 1, 4, 0}));
  EXPECT_EQ(shape.mesh_triangles[0].material_crc, k_run_material_crc);
  EXPECT_EQ(shape.mesh_triangles[1].material_crc, k_run_material_crc);
  EXPECT_EQ(shape.mesh_material_table, (std::vector<std::uint32_t>{k_run_material_crc}));
  EXPECT_EQ(shape.mesh_section_counts, (std::vector<std::uint32_t>{2U}));
}

// Detects: a compressed mesh whose data pointer is missing being decoded from garbage or throwing, instead of being listed as unsupported with its siblings kept.
TEST(HknpMeshDecode, CompressedMeshWithoutDataIsListedAsUnsupported) {
  HknpPackObject shape = make_object("hknpCompressedMeshShape", 0xA0);
  const auto sys = fo4::nif::decode_hknp_system(system_with_shape({shape}));
  EXPECT_TRUE(sys.shapes.empty());
  ASSERT_EQ(sys.unsupported.size(), 1U);
  EXPECT_EQ(sys.unsupported[0].class_name, "hknpCompressedMeshShape");
  EXPECT_EQ(sys.unsupported[0].object, 1U);
}

// Detects: a mesh data object cut short anywhere in its header, arrays or run table being read past its end instead of raising fo4::parse_error.
TEST(HknpMeshDecode, TruncatedMeshDataRaisesParseError) {
  for (const std::size_t length : {std::size_t{0x20}, std::size_t{0x90}, k_data_sections, k_data_primitives,
                                   k_data_runs}) {
    auto pack = single_section_system();
    pack.objects[4].bytes.resize(length);
    EXPECT_THROW(fo4::nif::decode_hknp_system(pack), fo4::parse_error) << "truncated to " << length << " bytes";
  }
}

// Detects: a section whose packed vertex range runs past the packed array, read from the bytes that follow instead of raising fo4::parse_error.
TEST(HknpMeshDecode, SectionPackedRangePastTheArrayRaisesParseError) {
  auto out_of_range_section = single_section_system();
  put_u8(out_of_range_section.objects[4], k_data_sections + 0x58, 5);
  EXPECT_THROW(fo4::nif::decode_hknp_system(out_of_range_section), fo4::parse_error);
}

// Detects: a body that drops its quality id or material slot, or resolves a CRC other than its shape's, so a body reports the wrong material.
TEST(HknpMeshDecode, BodyKeepsItsQualityAndSlotAndTakesTheShapeCrc) {
  auto pack = single_section_system();
  put_u8(pack.objects[0], 0x90, 4);
  put_u16(pack.objects[0], 0x92, 0x0102);
  const auto sys = fo4::nif::decode_hknp_system(pack);
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_EQ(sys.bodies[0].quality, 4);
  EXPECT_EQ(sys.bodies[0].material_slot, 0x0102);
  EXPECT_EQ(sys.bodies[0].resolved_material_crc, k_shape_material_crc);
}

// Detects: a shared index value past the shared-vertex array throwing, which would fail every vanilla mesh that carries such a reference; the shape must be listed as unsupported with no shape kept.
TEST(HknpMeshDecode, UnresolvedSharedValueListsTheShapeAsUnsupported) {
  auto unresolved = single_section_system();
  put_u16(unresolved.objects[4], k_data_shared_index, 7);
  const auto sys = fo4::nif::decode_hknp_system(unresolved);
  EXPECT_TRUE(sys.shapes.empty());
  ASSERT_EQ(sys.unsupported.size(), 1U);
  EXPECT_EQ(sys.unsupported[0].class_name, "hknpCompressedMeshShape");
}

// Detects: a material run naming a table entry that does not exist, or runs that stop short of the section's primitives, so some triangles keep a guessed material; both must raise fo4::parse_error.
TEST(HknpMeshDecode, BadMaterialRunsRaiseParseError) {
  auto bad_material = single_section_system();
  put_u8(bad_material.objects[4], k_data_runs, 1);
  EXPECT_THROW(fo4::nif::decode_hknp_system(bad_material), fo4::parse_error);
  auto short_run = single_section_system();
  put_u8(short_run.objects[4], k_data_runs + 3, 1);
  EXPECT_THROW(fo4::nif::decode_hknp_system(short_run), fo4::parse_error);
}
