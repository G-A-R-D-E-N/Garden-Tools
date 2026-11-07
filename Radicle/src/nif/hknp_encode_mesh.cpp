#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "hknp_encode_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace fo4::nif::hknp_detail {
namespace {

// Constraint: these offsets match the vanilla hknpCompressedMeshShapeData layout that the decoder reads.
constexpr std::size_t k_mesh_header = 0xd0;
constexpr std::size_t k_section_stride = 0x60;
constexpr std::size_t k_material_stride = 0x18;
constexpr std::uint32_t k_full_x = 2047U;
constexpr std::uint32_t k_full_z = 1023U;
constexpr std::uint32_t k_array_flag = 0x80000000U;
constexpr std::uint8_t k_section_leaf = 0;
constexpr std::uint8_t k_section_internal = 0x80;
// Constraint: a leaf stores twice its primitive index in one byte, so a section holds at most 128 primitives and 255 vertices.
constexpr std::size_t k_max_prims_per_section = 128;
constexpr std::size_t k_max_verts_per_section = 255;
constexpr std::size_t k_max_runs_per_section = 255;
// Constraint: the section tree's left leaf count is one byte, so a mesh holds at most 511 sections.
constexpr std::size_t k_max_sections = 511;
constexpr std::size_t k_max_materials = 256;
// Constraint: a bound nibble scales by sqrt(226), with the measured scale constant below.
constexpr float k_nibble_divisor = 226.0f;
constexpr float k_nibble_scale = 15.0332963783729f;
constexpr std::uint32_t k_tail_positive = 0x7f7fffeeU;
constexpr std::uint32_t k_tail_negative = 0xff7fffeeU;
constexpr std::uint32_t k_mesh_properties_tag = 0xF601U;
constexpr std::uint32_t k_max_first_index = 0xFFFFFFU;

using TreeBox = std::array<HknpVec3, 2>;
using Prim = std::array<std::uint8_t, 4>;
using Run = std::array<std::uint8_t, 4>;

struct Group {
  std::vector<std::uint32_t> globals;
  std::vector<Prim> prims;
  std::vector<std::uint32_t> materials;
};

struct Section {
  std::vector<HknpVec3> stored;
  std::vector<std::uint32_t> words;
  std::vector<Prim> prims;
  std::vector<Run> runs;
  HknpVec3 codec_min{};
  HknpVec3 step{};
  TreeBox box{};
  std::vector<TreeBox> prim_boxes;
  std::vector<std::byte> tree;
};

std::uint32_t bit_length(std::uint32_t value) {
  std::uint32_t bits = 0;
  while (value != 0) {
    ++bits;
    value >>= 1;
  }
  return bits;
}

void put_word(std::vector<std::byte>& out, std::uint32_t word) {
  for (std::size_t byte = 0; byte < 4; ++byte) {
    out.push_back(std::byte{static_cast<std::uint8_t>((word >> (8 * byte)) & 0xFFU)});
  }
}

void pad_to_align(std::vector<std::byte>& out) {
  while (out.size() % k_pack_align != 0) out.push_back(std::byte{0});
}

// Constraint: a parent axis with no span stores 15 in both nibbles.
std::uint8_t inset_nibble(float inset, float span) {
  if (!(span > 0.0f)) return 15;
  if (!(inset > 0.0f)) return 0;
  const float fraction = std::min(inset / span, 1.0f);
  const int scaled = static_cast<int>(std::sqrt(fraction) * k_nibble_scale + 1.0e-6f);
  return static_cast<std::uint8_t>(std::clamp(scaled, 0, 15));
}

// Constraint: each bound byte is (loInset << 4) | hiInset, with nibbles from sqrt(inset / parentSpan) * sqrt(226).
std::array<std::uint8_t, 3> encode_bounds(TreeBox& box, const TreeBox& parent) {
  std::array<std::uint8_t, 3> out{};
  for (std::size_t a = 0; a < 3; ++a) {
    const float span = parent[1][a] - parent[0][a];
    const auto lo = inset_nibble(box[0][a] - parent[0][a], span);
    const auto hi = inset_nibble(parent[1][a] - box[1][a], span);
    out[a] = static_cast<std::uint8_t>((lo << 4) | hi);
    const float lo_fraction = static_cast<float>(lo * lo) / k_nibble_divisor;
    const float hi_fraction = static_cast<float>(hi * hi) / k_nibble_divisor;
    box[0][a] = parent[0][a] + lo_fraction * span;
    box[1][a] = parent[1][a] - hi_fraction * span;
  }
  return out;
}

void union_box(TreeBox& into, const TreeBox& from) {
  for (std::size_t a = 0; a < 3; ++a) {
    into[0][a] = std::min(into[0][a], from[0][a]);
    into[1][a] = std::max(into[1][a], from[1][a]);
  }
}

std::size_t widest_axis(const TreeBox& box) {
  std::size_t axis = 0;
  for (std::size_t a = 1; a < 3; ++a) {
    if (box[1][a] - box[0][a] > box[1][axis] - box[0][axis]) axis = a;
  }
  return axis;
}

TreeBox point_box(const HknpVec3& point) { return TreeBox{point, point}; }

// Constraint: a leaf names its primitive as twice the index; an internal node sets (leftLeafCount << 1) | 1.
void append_mesh_tree(std::vector<std::byte>& out, std::vector<int>& prims, const std::vector<TreeBox>& boxes,
                      int first, int count, const TreeBox& parent) {
  TreeBox own = boxes[static_cast<std::size_t>(prims[static_cast<std::size_t>(first)])];
  for (int i = first + 1; i < first + count; ++i) {
    union_box(own, boxes[static_cast<std::size_t>(prims[static_cast<std::size_t>(i)])]);
  }
  const auto xyz = encode_bounds(own, parent);
  out.push_back(std::byte{xyz[0]});
  out.push_back(std::byte{xyz[1]});
  out.push_back(std::byte{xyz[2]});
  if (count == 1) {
    out.push_back(std::byte{static_cast<std::uint8_t>(prims[static_cast<std::size_t>(first)] * 2)});
    return;
  }
  const std::size_t axis = widest_axis(own);
  std::stable_sort(prims.begin() + first, prims.begin() + first + count, [&boxes, axis](int a, int b) {
    return boxes[static_cast<std::size_t>(a)][0][axis] + boxes[static_cast<std::size_t>(a)][1][axis] <
           boxes[static_cast<std::size_t>(b)][0][axis] + boxes[static_cast<std::size_t>(b)][1][axis];
  });
  const int left = count / 2;
  out.push_back(std::byte{static_cast<std::uint8_t>((left * 2) | 1)});
  append_mesh_tree(out, prims, boxes, first, left, own);
  append_mesh_tree(out, prims, boxes, first + left, count - left, own);
}

// Constraint: a section-tree node is five bytes; a leaf names its section at +4, an internal node sets 0x80 at +3.
void append_section_tree(std::vector<std::byte>& out, std::vector<int>& sections, const std::vector<TreeBox>& boxes,
                         int first, int count, const TreeBox& parent, std::vector<std::uint32_t>& leaf_node) {
  const auto node_index = static_cast<std::uint32_t>(out.size() / 5);
  TreeBox own = boxes[static_cast<std::size_t>(sections[static_cast<std::size_t>(first)])];
  for (int i = first + 1; i < first + count; ++i) {
    union_box(own, boxes[static_cast<std::size_t>(sections[static_cast<std::size_t>(i)])]);
  }
  const auto xyz = encode_bounds(own, parent);
  out.push_back(std::byte{xyz[0]});
  out.push_back(std::byte{xyz[1]});
  out.push_back(std::byte{xyz[2]});
  if (count == 1) {
    const auto section = sections[static_cast<std::size_t>(first)];
    leaf_node[static_cast<std::size_t>(section)] = node_index;
    out.push_back(std::byte{k_section_leaf});
    // Constraint: the leaf's section byte wraps modulo 256.
    out.push_back(std::byte{static_cast<std::uint8_t>(section)});
    return;
  }
  const std::size_t axis = widest_axis(own);
  std::stable_sort(sections.begin() + first, sections.begin() + first + count, [&boxes, axis](int a, int b) {
    return boxes[static_cast<std::size_t>(a)][0][axis] + boxes[static_cast<std::size_t>(a)][1][axis] <
           boxes[static_cast<std::size_t>(b)][0][axis] + boxes[static_cast<std::size_t>(b)][1][axis];
  });
  const int left = count / 2;
  out.push_back(std::byte{k_section_internal});
  out.push_back(std::byte{static_cast<std::uint8_t>(left)});
  append_section_tree(out, sections, boxes, first, left, own, leaf_node);
  append_section_tree(out, sections, boxes, first + left, count - left, own, leaf_node);
}

TreeBox vertex_box(const std::vector<HknpVec3>& vertices) {
  TreeBox box{vertices.front(), vertices.front()};
  for (const auto& vertex : vertices) {
    for (std::size_t a = 0; a < 3; ++a) {
      box[0][a] = std::min(box[0][a], vertex[a]);
      box[1][a] = std::max(box[1][a], vertex[a]);
    }
  }
  return box;
}

// Constraint: the index is clamped in double before integer conversion, because long is 32 bits on MSVC.
std::uint32_t quantize(float value, float base, float step, std::uint32_t full) {
  if (!(step > 0.0f)) return 0;
  const double index = std::floor((static_cast<double>(value) - base) / static_cast<double>(step) + 0.5);
  if (!(index > 0.0)) return 0;
  if (index >= static_cast<double>(full)) return full;
  return static_cast<std::uint32_t>(index);
}

void write_box(HknpPackObject& object, std::size_t offset, const TreeBox& box) {
  put_vec3(object, offset, box[0]);
  put_f32(object, offset + 12, 1.0f);
  put_vec3(object, offset + 16, box[1]);
  put_f32(object, offset + 28, 1.0f);
}

// Constraint: vertices are grouped in spatial order, so each section's tree boxes stay small.
std::vector<Group> partition(const HknpShape& shape, std::vector<std::uint32_t>& table) {
  const auto& vertices = shape.mesh_vertices;
  const auto& triangles = shape.mesh_triangles;
  if (triangles.empty()) throw std::invalid_argument("a compressed mesh needs at least one triangle");
  std::vector<std::uint32_t> material(triangles.size());
  std::map<std::uint32_t, std::uint32_t> index_of;
  for (std::size_t t = 0; t < triangles.size(); ++t) {
    const auto& tri = triangles[t];
    const std::size_t used = tri.quad ? 4 : 3;
    for (std::size_t k = 0; k < used; ++k) {
      if (tri.indices[k] >= vertices.size()) {
        throw std::invalid_argument("a mesh triangle names a vertex outside the vertex list");
      }
    }
    if (tri.quad && tri.indices[3] == tri.indices[2]) {
      throw std::invalid_argument("a mesh quad needs a fourth vertex distinct from its third");
    }
    const auto found = index_of.find(tri.material_crc);
    if (found != index_of.end()) {
      material[t] = found->second;
      continue;
    }
    if (table.size() >= k_max_materials) throw std::invalid_argument("a compressed mesh holds at most 256 materials");
    index_of.emplace(tri.material_crc, static_cast<std::uint32_t>(table.size()));
    material[t] = static_cast<std::uint32_t>(table.size());
    table.push_back(tri.material_crc);
  }

  std::array<double, 3> low{};
  std::array<double, 3> high{};
  std::vector<std::array<double, 3>> centroid(triangles.size());
  for (std::size_t t = 0; t < triangles.size(); ++t) {
    const auto& tri = triangles[t];
    const std::size_t used = tri.quad ? 4 : 3;
    std::array<double, 3> sum{};
    for (std::size_t k = 0; k < used; ++k) {
      for (std::size_t a = 0; a < 3; ++a) sum[a] += vertices[tri.indices[k]][a];
    }
    for (std::size_t a = 0; a < 3; ++a) centroid[t][a] = sum[a] / static_cast<double>(used);
    for (std::size_t a = 0; a < 3; ++a) {
      low[a] = t == 0 ? centroid[t][a] : std::min(low[a], centroid[t][a]);
      high[a] = t == 0 ? centroid[t][a] : std::max(high[a], centroid[t][a]);
    }
  }
  std::size_t axis = 0;
  for (std::size_t a = 1; a < 3; ++a) {
    if (high[a] - low[a] > high[axis] - low[axis]) axis = a;
  }
  std::vector<std::size_t> order(triangles.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&centroid, axis](std::size_t a, std::size_t b) { return centroid[a][axis] < centroid[b][axis]; });

  std::vector<Group> groups;
  Group current;
  std::vector<std::int64_t> local(vertices.size(), -1);
  const auto close = [&] {
    for (const std::uint32_t g : current.globals) local[g] = -1;
    if (!current.prims.empty()) groups.push_back(std::move(current));
    current = Group{};
  };
  const auto fresh_count = [&local](const HknpMeshTriangle& tri) {
    const std::size_t used = tri.quad ? 4 : 3;
    std::size_t count = 0;
    for (std::size_t k = 0; k < used; ++k) {
      const std::uint32_t index = tri.indices[k];
      if (local[index] >= 0) continue;
      bool seen = false;
      for (std::size_t j = 0; j < k; ++j) {
        if (tri.indices[j] == index) seen = true;
      }
      if (!seen) ++count;
    }
    return count;
  };
  for (const std::size_t t : order) {
    const auto& tri = triangles[t];
    if (current.globals.size() + fresh_count(tri) > k_max_verts_per_section ||
        current.prims.size() + 1 > k_max_prims_per_section) {
      close();
    }
    const std::size_t used = tri.quad ? 4 : 3;
    std::array<std::uint8_t, 4> slot{};
    for (std::size_t k = 0; k < used; ++k) {
      const std::uint32_t index = tri.indices[k];
      if (local[index] < 0) {
        local[index] = static_cast<std::int64_t>(current.globals.size());
        current.globals.push_back(index);
      }
      slot[k] = static_cast<std::uint8_t>(local[index]);
    }
    if (!tri.quad) slot[3] = slot[2];
    current.prims.push_back(Prim{slot[0], slot[1], slot[2], slot[3]});
    current.materials.push_back(material[t]);
  }
  close();
  return groups;
}

Section build_section(const std::vector<HknpVec3>& vertices, const Group& group) {
  Section section;
  std::vector<HknpVec3> raw;
  raw.reserve(group.globals.size());
  for (const std::uint32_t g : group.globals) raw.push_back(vertices[g]);
  const TreeBox box = vertex_box(raw);
  section.box = box;
  section.codec_min = box[0];
  for (std::size_t a = 0; a < 3; ++a) {
    const float extent = box[1][a] - box[0][a];
    const auto full = static_cast<float>(a < 2 ? k_full_x : k_full_z);
    section.step[a] = extent > 0.0f ? extent / full : 0.0f;
  }
  section.stored.reserve(raw.size());
  for (const auto& vertex : raw) {
    const std::uint32_t x = quantize(vertex[0], section.codec_min[0], section.step[0], k_full_x);
    const std::uint32_t y = quantize(vertex[1], section.codec_min[1], section.step[1], k_full_x);
    const std::uint32_t z = quantize(vertex[2], section.codec_min[2], section.step[2], k_full_z);
    section.words.push_back(x | (y << 11) | (z << 22));
    section.stored.push_back({section.codec_min[0] + static_cast<float>(x) * section.step[0],
                              section.codec_min[1] + static_cast<float>(y) * section.step[1],
                              section.codec_min[2] + static_cast<float>(z) * section.step[2]});
  }

  std::vector<std::size_t> perm(group.prims.size());
  std::iota(perm.begin(), perm.end(), std::size_t{0});
  std::stable_sort(perm.begin(), perm.end(),
                   [&group](std::size_t a, std::size_t b) { return group.materials[a] < group.materials[b]; });
  std::vector<std::uint32_t> materials;
  for (const std::size_t p : perm) {
    section.prims.push_back(group.prims[p]);
    materials.push_back(group.materials[p]);
  }
  for (std::size_t i = 0; i < materials.size();) {
    std::size_t end = i;
    while (end < materials.size() && materials[end] == materials[i]) ++end;
    section.runs.push_back(Run{static_cast<std::uint8_t>(materials[i]), 0, static_cast<std::uint8_t>(i),
                               static_cast<std::uint8_t>(end - i)});
    i = end;
  }

  for (const auto& prim : section.prims) {
    TreeBox prim_box = point_box(section.stored[prim[0]]);
    for (std::size_t corner = 1; corner < 4; ++corner) union_box(prim_box, point_box(section.stored[prim[corner]]));
    section.prim_boxes.push_back(prim_box);
  }
  std::vector<int> order(section.prim_boxes.size());
  std::iota(order.begin(), order.end(), 0);
  append_mesh_tree(section.tree, order, section.prim_boxes, 0, static_cast<int>(order.size()), section.box);
  return section;
}

}  // namespace

// Constraint: the flat bit field sits at 0xa0 and the interior one at the next 16-byte boundary.
std::vector<HknpPackObject> encode_mesh_objects(const HknpShape& shape, std::uint32_t first_object) {
  std::vector<std::uint32_t> table;
  const std::vector<Group> groups = partition(shape, table);
  const std::size_t section_count = groups.size();
  // Constraint: the section tree's left leaf count is one byte, so a mesh holds at most 511 sections.
  if (section_count > k_max_sections) throw std::invalid_argument("a mesh shape holds at most 511 sections");

  std::vector<Section> prepared;
  prepared.reserve(section_count);
  for (const Group& group : groups) prepared.push_back(build_section(shape.mesh_vertices, group));
  for (const auto& section : prepared) {
    if (section.runs.empty()) throw std::invalid_argument("a mesh section needs at least one material run");
    if (section.runs.size() > k_max_runs_per_section) {
      throw std::invalid_argument("a mesh section holds at most 255 material runs");
    }
  }

  std::uint32_t triangles = 0;
  std::uint32_t max_prims = 0;
  for (const auto& section : prepared) {
    max_prims = std::max(max_prims, static_cast<std::uint32_t>(section.prims.size()));
    for (const auto& prim : section.prims) triangles += prim[3] != prim[2] ? 2U : 1U;
  }
  const std::uint32_t prim_bits = bit_length(2 * max_prims - 1);
  const auto& last = prepared.back();
  const auto last_prims = static_cast<std::uint32_t>(last.prims.size());
  const bool last_is_quad = last.prims.back()[3] != last.prims.back()[2];
  const std::uint32_t max_key = ((static_cast<std::uint32_t>(section_count - 1)) << prim_bits) |
                                (2 * (last_prims - 1U) + (last_is_quad ? 1U : 0U));
  const std::uint32_t interior_bits = max_key + 1U;
  const std::uint32_t bits_per_key = bit_length(interior_bits);
  const std::uint32_t flat_bits = (interior_bits + 1U) / 2U;
  const std::uint32_t flat_words = (flat_bits + 31U) / 32U;
  const std::uint32_t interior_words = (interior_bits + 31U) / 32U;

  std::vector<std::byte> section_tree;
  TreeBox domain = prepared.front().box;
  std::vector<TreeBox> section_boxes;
  section_boxes.reserve(section_count);
  for (const auto& one : prepared) {
    union_box(domain, one.box);
    section_boxes.push_back(one.box);
  }
  std::vector<int> section_order(section_count);
  std::iota(section_order.begin(), section_order.end(), 0);
  std::vector<std::uint32_t> section_leaf(section_count, 0);
  append_section_tree(section_tree, section_order, section_boxes, 0, static_cast<int>(section_count), domain,
                      section_leaf);

  std::vector<std::byte> payload(section_tree);
  pad_to_align(payload);
  std::vector<std::uint32_t> section_at;
  std::uint32_t first_prim = 0;
  std::uint32_t first_run = 0;
  std::uint32_t first_packed = 0;
  for (std::size_t s = 0; s < section_count; ++s) {
    const auto& one = prepared[s];
    if (first_prim > k_max_first_index || first_run > k_max_first_index) {
      throw std::invalid_argument("a compressed mesh holds at most 16777215 primitives and runs");
    }
    section_at.push_back(static_cast<std::uint32_t>(payload.size()));
    HknpPackObject record = make_object("section", k_section_stride);
    const std::uint32_t nodes = static_cast<std::uint32_t>(2 * one.prim_boxes.size() - 1);
    put_u32(record, 0x08, nodes);
    put_u32(record, 0x0c, k_array_flag | nodes);
    write_box(record, 0x10, one.box);
    put_vec3(record, 0x30, one.codec_min);
    put_f32(record, 0x3c, one.step[0]);
    put_f32(record, 0x40, one.step[1]);
    put_f32(record, 0x44, one.step[2]);
    put_u32(record, 0x48, first_packed);
    put_u32(record, 0x50, (first_prim << 8) | static_cast<std::uint32_t>(one.prims.size()));
    put_u32(record, 0x54, (first_run << 8) | static_cast<std::uint32_t>(one.runs.size()));
    put_u8(record, 0x58, static_cast<std::uint8_t>(one.words.size()));
    // Constraint: the byte at +0x5a is the section leaf's preorder node index, kept as one byte.
    put_u8(record, 0x5a, static_cast<std::uint8_t>(section_leaf[s]));
    payload.insert(payload.end(), record.bytes.begin(), record.bytes.end());
    first_prim += static_cast<std::uint32_t>(one.prims.size());
    first_run += static_cast<std::uint32_t>(one.runs.size());
    first_packed += static_cast<std::uint32_t>(one.words.size());
  }
  pad_to_align(payload);
  // Constraint: the mesh trees follow the section records.
  std::vector<std::uint32_t> tree_at;
  for (const auto& one : prepared) {
    pad_to_align(payload);
    tree_at.push_back(static_cast<std::uint32_t>(payload.size()));
    payload.insert(payload.end(), one.tree.begin(), one.tree.end());
  }
  pad_to_align(payload);
  const std::uint32_t prims_at = static_cast<std::uint32_t>(payload.size());
  for (const auto& one : prepared) {
    for (const auto& prim : one.prims) {
      payload.push_back(std::byte{prim[0]});
      payload.push_back(std::byte{prim[1]});
      payload.push_back(std::byte{prim[2]});
      payload.push_back(std::byte{prim[3]});
    }
  }
  pad_to_align(payload);
  const std::uint32_t packed_at = static_cast<std::uint32_t>(payload.size());
  for (const auto& one : prepared) {
    for (const std::uint32_t word : one.words) put_word(payload, word);
  }
  pad_to_align(payload);
  const std::uint32_t runs_at = static_cast<std::uint32_t>(payload.size());
  for (const auto& one : prepared) {
    for (const auto& run : one.runs) {
      payload.push_back(std::byte{run[0]});
      payload.push_back(std::byte{run[1]});
      payload.push_back(std::byte{run[2]});
      payload.push_back(std::byte{run[3]});
    }
  }
  pad_to_align(payload);
  const std::uint32_t tail_at = static_cast<std::uint32_t>(payload.size());
  // Constraint: the tail is two 0x70 elements of six planes plus four zero words each.
  for (std::size_t element = 0; element < 2; ++element) {
    for (std::size_t plane = 0; plane < 6; ++plane) {
      const std::uint32_t word = (plane % 2) == 0 ? k_tail_positive : k_tail_negative;
      for (std::size_t lane = 0; lane < 4; ++lane) put_word(payload, word);
    }
    for (std::size_t zero = 0; zero < 4; ++zero) put_word(payload, 0);
  }

  HknpPackObject data = make_object("hknpCompressedMeshShapeData", k_mesh_header);
  const std::uint32_t tree_nodes = static_cast<std::uint32_t>(2 * section_count - 1);
  put_u32(data, 0x18, tree_nodes);
  put_u32(data, 0x1c, k_array_flag | tree_nodes);
  write_box(data, 0x20, domain);
  put_u32(data, 0x40, triangles);
  put_u32(data, 0x44, bits_per_key);
  put_u32(data, 0x48, max_key);
  put_u32(data, 0x58, static_cast<std::uint32_t>(section_count));
  put_u32(data, 0x5c, k_array_flag | static_cast<std::uint32_t>(section_count));
  put_u32(data, 0x68, first_prim);
  put_u32(data, 0x6c, k_array_flag | first_prim);
  put_u32(data, 0x7c, k_array_flag);
  put_u32(data, 0x88, first_packed);
  put_u32(data, 0x8c, k_array_flag | first_packed);
  put_u32(data, 0x9c, k_array_flag);
  put_u32(data, 0xa8, first_run);
  put_u32(data, 0xac, k_array_flag | first_run);
  put_u32(data, 0xc0, 2);
  put_u32(data, 0xc4, k_array_flag | 2U);
  data.bytes.insert(data.bytes.end(), payload.begin(), payload.end());
  const auto at = [](std::uint32_t payload_offset) { return payload_offset + static_cast<std::uint32_t>(k_mesh_header); };
  add_local(data, 0x10, static_cast<std::uint32_t>(k_mesh_header));
  add_local(data, 0x50, at(section_at.front()));
  for (std::size_t s = 0; s < section_count; ++s) {
    data.local.push_back(HknpLocalFixup{at(section_at[s]), at(tree_at[s])});
  }
  add_local(data, 0x60, at(prims_at));
  add_local(data, 0x80, at(packed_at));
  add_local(data, 0xa0, at(runs_at));
  add_local(data, 0xb8, at(tail_at));

  constexpr std::uint32_t k_flat_field = 0xA0;
  const std::uint32_t flat_at = k_flat_field;
  const std::uint32_t interior_at =
      static_cast<std::uint32_t>(align16(std::size_t{k_flat_field} + 4 * std::size_t{flat_words}));
  HknpPackObject mesh_shape =
      make_object("hknpCompressedMeshShape", align16(interior_at + 4 * std::size_t{interior_words}));
  put_u8(mesh_shape, 0x10, 4);
  put_u8(mesh_shape, 0x11, 2);
  put_u8(mesh_shape, 0x12, static_cast<std::uint8_t>(bits_per_key));
  put_u8(mesh_shape, 0x13, 2);
  put_u32(mesh_shape, 0x18, shape.material_crc);
  put_u32(mesh_shape, 0x30, 0xffffffffU);
  put_u32(mesh_shape, 0x44, k_array_flag);
  put_u32(mesh_shape, 0x54, k_array_flag);
  put_u32(mesh_shape, 0x58, 0xffffffffU);
  put_u32(mesh_shape, 0x70, flat_words);
  put_u32(mesh_shape, 0x74, k_array_flag | flat_words);
  put_u32(mesh_shape, 0x78, flat_bits);
  put_u32(mesh_shape, 0x88, interior_words);
  put_u32(mesh_shape, 0x8c, k_array_flag | interior_words);
  put_u32(mesh_shape, 0x90, interior_bits);
  add_local(mesh_shape, 0x68, flat_at);
  add_local(mesh_shape, 0x80, interior_at);
  add_global(mesh_shape, 0x20, first_object + 1);
  add_global(mesh_shape, 0x60, first_object + 3);

  HknpPackObject properties = make_object("hkRefCountedProperties", 0x20);
  put_u32(properties, 8, 1);
  put_u32(properties, 12, k_array_flag | 1U);
  put_u32(properties, 24, k_mesh_properties_tag);
  add_local(properties, 0, 16);
  add_global(properties, 16, first_object + 2);

  const std::size_t materials = table.size();
  HknpPackObject material_table =
      make_object("hknpBSMaterialProperties", align16(0x20 + materials * k_material_stride));
  put_u32(material_table, 0x18, static_cast<std::uint32_t>(materials));
  put_u32(material_table, 0x1c, k_array_flag | static_cast<std::uint32_t>(materials));
  for (std::size_t i = 0; i < materials; ++i) {
    put_u32(material_table, 0x20 + i * k_material_stride + 0x10, 1);
    put_u32(material_table, 0x20 + i * k_material_stride + 0x14, table[i]);
  }
  add_local(material_table, 0x10, 0x20);

  std::vector<HknpPackObject> objects;
  objects.push_back(std::move(mesh_shape));
  objects.push_back(std::move(properties));
  objects.push_back(std::move(material_table));
  objects.push_back(std::move(data));
  return objects;
}

}  // namespace fo4::nif::hknp_detail
