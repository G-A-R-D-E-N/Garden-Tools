#include "hknp_decode_internal.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace fo4::nif::hknp_detail {
namespace {

constexpr std::size_t k_data_pointer = 0x60;
constexpr std::size_t k_material_ref = 0x20;
constexpr std::size_t k_material_props = 0x10;
constexpr std::size_t k_material_stride = 0x18;
constexpr std::size_t k_section_stride = 0x60;
constexpr std::size_t k_prim_bytes = 4;
constexpr std::size_t k_run_bytes = 4;
constexpr std::size_t k_packed_bytes = 4;
constexpr std::size_t k_shared_bytes = 8;
constexpr std::size_t k_shared_index_bytes = 2;
constexpr std::uint64_t k_shared_key = std::uint64_t{1} << 32;
constexpr float k_shared_x_max = 2097151.0f;
constexpr float k_shared_y_max = 2097151.0f;
constexpr float k_shared_z_max = 4194303.0f;

struct MeshArrays {
  ArrayRef sections;
  ArrayRef primitives;
  ArrayRef shared_index;
  ArrayRef packed;
  ArrayRef shared;
  ArrayRef runs;
};

struct Section {
  HknpVec3 codec_min{};
  HknpVec3 codec_step{};
  std::uint32_t first_packed{0};
  std::uint32_t packed_count{0};
  std::uint32_t first_shared{0};
  std::uint32_t first_prim{0};
  std::uint32_t prim_count{0};
  std::uint32_t first_run{0};
  std::uint32_t run_count{0};
};

struct UnresolvedSharedVertex {};

struct MeshContext {
  const HknpPackObject& data;
  MeshArrays arrays;
  HknpShape& shape;
  std::map<std::uint64_t, std::uint32_t> vertex_of_key;
};

HknpVec3 dequantize(const HknpVec3& codec_min, const HknpVec3& step, std::uint32_t x, std::uint32_t y,
                    std::uint32_t z) {
  return {codec_min[0] + static_cast<float>(x) * step[0], codec_min[1] + static_cast<float>(y) * step[1],
          codec_min[2] + static_cast<float>(z) * step[2]};
}

ArrayRef mesh_array(const HknpPackObject& data, std::size_t field) {
  if (const auto array = array_at(data, field)) {
    return *array;
  }
  if (u32_at(data, field + 8) != 0) {
    throw fo4::parse_error(field, "compressed mesh array has a count but no payload");
  }
  return ArrayRef{};
}

std::uint64_t u64_at(const HknpPackObject& object, std::size_t offset) {
  return std::uint64_t{u32_at(object, offset)} | (std::uint64_t{u32_at(object, offset + 4)} << 32);
}

Section read_section(const HknpPackObject& data, std::size_t at) {
  Section section;
  section.codec_min = vec3_at(data, at + 0x30);
  section.codec_step = vec3_at(data, at + 0x3c);
  section.first_packed = u32_at(data, at + 0x48);
  section.first_shared = u32_at(data, at + 0x4c) >> 8;
  const std::uint32_t prims = u32_at(data, at + 0x50);
  section.first_prim = prims >> 8;
  section.prim_count = prims & 0xFFU;
  const std::uint32_t runs = u32_at(data, at + 0x54);
  section.first_run = runs >> 8;
  section.run_count = runs & 0xFFU;
  section.packed_count = u8_at(data, at + 0x58);
  return section;
}

std::vector<std::uint32_t> material_table(const HknpPackfile& pack, const HknpPackObject& shape) {
  std::vector<std::uint32_t> table;
  const auto reference = global_target(pack, shape, k_material_ref);
  if (!reference) {
    return table;
  }
  const auto props = global_target(pack, pack.objects[*reference], k_material_props);
  if (!props || pack.objects[*props].class_name != "hknpBSMaterialProperties") {
    return table;
  }
  const HknpPackObject& materials = pack.objects[*props];
  const auto payload = local_target(materials, k_material_props);
  if (!payload) {
    return table;
  }
  const std::uint32_t count = u32_at(materials, k_material_props + 8);
  for (std::uint32_t i = 0; i < count; ++i) {
    table.push_back(u32_at(materials, *payload + k_material_stride * i + 0x14));
  }
  return table;
}

std::vector<std::uint32_t> primitive_materials(const HknpPackObject& data, const MeshArrays& arrays,
                                               const Section& section, const std::vector<std::uint32_t>& table,
                                               std::uint32_t fallback) {
  std::vector<std::uint32_t> materials(section.prim_count, fallback);
  if (section.run_count == 0) {
    return materials;
  }
  std::uint32_t next = 0;
  for (std::uint32_t k = 0; k < section.run_count; ++k) {
    const std::uint32_t run = section.first_run + k;
    if (run >= arrays.runs.count) {
      throw fo4::parse_error(arrays.runs.offset, "compressed mesh run index runs past the table");
    }
    const std::size_t at = arrays.runs.offset + k_run_bytes * run;
    const std::uint32_t material = u8_at(data, at);
    const std::uint32_t start = u8_at(data, at + 2);
    const std::uint32_t count = u8_at(data, at + 3);
    if (material >= table.size() || start != next || start + count > section.prim_count) {
      throw fo4::parse_error(at, "material run does not tile its section");
    }
    for (std::uint32_t p = start; p < start + count; ++p) {
      materials[p] = table[material];
    }
    next += count;
  }
  if (next != section.prim_count) {
    throw fo4::parse_error(arrays.runs.offset, "material runs do not cover their section");
  }
  return materials;
}

std::uint32_t intern_vertex(MeshContext& mesh, std::uint64_t key, const HknpVec3& position) {
  const auto found = mesh.vertex_of_key.find(key);
  if (found != mesh.vertex_of_key.end()) {
    return found->second;
  }
  const auto index = static_cast<std::uint32_t>(mesh.shape.mesh_vertices.size());
  mesh.vertex_of_key.emplace(key, index);
  mesh.shape.mesh_vertices.push_back(position);
  return index;
}

std::uint32_t section_vertex(MeshContext& mesh, const Section& section, std::uint32_t local) {
  if (local < section.packed_count) {
    const std::uint32_t index = section.first_packed + local;
    if (index >= mesh.arrays.packed.count) {
      throw fo4::parse_error(mesh.arrays.packed.offset, "packed vertex index runs past the array");
    }
    const std::uint32_t word = u32_at(mesh.data, mesh.arrays.packed.offset + k_packed_bytes * index);
    const HknpVec3 position = dequantize(section.codec_min, section.codec_step, word & 0x7FFU,
                                         (word >> 11U) & 0x7FFU, (word >> 22U) & 0x3FFU);
    return intern_vertex(mesh, index, position);
  }
  const std::uint32_t slot = section.first_shared + (local - section.packed_count);
  if (slot >= mesh.arrays.shared_index.count) {
    throw UnresolvedSharedVertex{};
  }
  const std::uint32_t shared = u16_at(mesh.data, mesh.arrays.shared_index.offset + k_shared_index_bytes * slot);
  if (shared >= mesh.arrays.shared.count) {
    throw UnresolvedSharedVertex{};
  }
  const std::uint64_t word = u64_at(mesh.data, mesh.arrays.shared.offset + k_shared_bytes * shared);
  const HknpVec3& domain_min = mesh.shape.aabb_min;
  const HknpVec3& domain_max = mesh.shape.aabb_max;
  const HknpVec3 position{
      domain_min[0] + static_cast<float>(word & 0x1FFFFFU) / k_shared_x_max * (domain_max[0] - domain_min[0]),
      domain_min[1] + static_cast<float>((word >> 21U) & 0x1FFFFFU) / k_shared_y_max * (domain_max[1] - domain_min[1]),
      domain_min[2] + static_cast<float>((word >> 42U) & 0x3FFFFFU) / k_shared_z_max * (domain_max[2] - domain_min[2])};
  return intern_vertex(mesh, k_shared_key | shared, position);
}

void decode_primitives(MeshContext& mesh, const Section& section, const std::vector<std::uint32_t>& materials) {
  for (std::uint32_t t = 0; t < section.prim_count; ++t) {
    const std::size_t at = mesh.arrays.primitives.offset + k_prim_bytes * (section.first_prim + t);
    const std::uint32_t a = u8_at(mesh.data, at);
    const std::uint32_t b = u8_at(mesh.data, at + 1);
    const std::uint32_t c = u8_at(mesh.data, at + 2);
    const std::uint32_t d = u8_at(mesh.data, at + 3);
    HknpMeshTriangle triangle;
    triangle.indices = {section_vertex(mesh, section, a), section_vertex(mesh, section, b),
                        section_vertex(mesh, section, c), 0U};
    triangle.quad = d != c;
    if (triangle.quad) {
      triangle.indices[3] = section_vertex(mesh, section, d);
    }
    triangle.material_crc = materials[t];
    mesh.shape.mesh_triangles.push_back(triangle);
  }
}

}  // namespace

int decode_compressed_mesh(DecodeContext& ctx, std::size_t object) {
  const HknpPackObject& source = ctx.pack.objects[object];
  const auto data_object = global_target(ctx.pack, source, k_data_pointer);
  if (!data_object) {
    ctx.sys.unsupported.push_back(HknpUnsupportedShape{source.class_name, object});
    return k_shape_unsupported;
  }
  const HknpPackObject& data = ctx.pack.objects[*data_object];
  if (data.class_name != "hknpCompressedMeshShapeData") {
    throw fo4::parse_error(k_data_pointer, "compressed mesh data pointer names another class");
  }
  HknpShape shape;
  shape.kind = HknpShapeKind::compressed_mesh;
  shape.class_name = source.class_name;
  shape.object = object;
  shape.shape_flags = u32_at(source, 0x10);
  shape.convex_radius = f32_at(source, 0x14);
  shape.material_crc = u32_at(source, 0x18);
  shape.aabb_min = vec3_at(data, 0x20);
  shape.aabb_max = vec3_at(data, 0x30);
  shape.mesh_material_table = material_table(ctx.pack, source);
  const MeshArrays arrays{mesh_array(data, 0x50), mesh_array(data, 0x60), mesh_array(data, 0x70),
                          mesh_array(data, 0x80), mesh_array(data, 0x90), mesh_array(data, 0xa0)};
  MeshContext mesh{data, arrays, shape, {}};
  try {
    for (std::uint32_t s = 0; s < arrays.sections.count; ++s) {
      const Section section = read_section(data, arrays.sections.offset + k_section_stride * s);
      if (std::uint64_t{section.first_packed} + section.packed_count > arrays.packed.count ||
          std::uint64_t{section.first_prim} + section.prim_count > arrays.primitives.count) {
        throw fo4::parse_error(arrays.sections.offset + k_section_stride * s,
                               "compressed mesh section runs past its arrays");
      }
      const auto materials = primitive_materials(data, arrays, section, shape.mesh_material_table, shape.material_crc);
      shape.mesh_section_counts.push_back(section.prim_count);
      decode_primitives(mesh, section, materials);
    }
  } catch (const UnresolvedSharedVertex&) {
    ctx.sys.unsupported.push_back(HknpUnsupportedShape{source.class_name, object});
    return k_shape_unsupported;
  }
  ctx.sys.shapes.push_back(std::move(shape));
  return static_cast<int>(ctx.sys.shapes.size()) - 1;
}

}  // namespace fo4::nif::hknp_detail
