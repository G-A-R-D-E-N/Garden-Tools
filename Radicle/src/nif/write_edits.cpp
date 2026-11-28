#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/text/encoding.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/sidecars.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "nif_internal.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4::nif::edit_detail {
namespace {

constexpr std::uint16_t k_alpha_blend_bit = 1U << 0U;
constexpr std::uint16_t k_alpha_test_bit = 1U << 9U;
constexpr std::size_t k_bsx_flags_at = 4;
constexpr std::size_t k_collision_flags_at = 4;
constexpr std::size_t k_collision_body_id_at = 10;
constexpr std::size_t k_av_transform_at = 16;
constexpr float k_byte_max = 255.0f;
constexpr float k_u16_max = 65535.0f;
constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr float k_max_half = 65504.0f;
constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_max_vertex_count = 65535;
constexpr std::string_view k_plain_shape = "BSTriShape";

void refuse_count_changing_derived_shape(const NifFile& nif, const Model& model, std::uint32_t block) {
  const auto type = write_detail::block_type(model, block);
  if (type != k_plain_shape) {
    fail(nif, block, "only BSTriShape is supported; " + std::string(type) +
                     " keeps data after its triangle list that a changed count would invalidate");
  }
}

void set_bits(std::uint32_t& word, std::uint32_t bit, bool on) {
  if (on) {
    word |= bit;
  } else {
    word &= ~bit;
  }
}

std::size_t shader_users(const Model& model, std::uint32_t shader) {
  std::size_t users = 0;
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    const auto& bytes = model.blocks[block];
    if (!is_shape_type(write_detail::block_type(model, block)) || bytes.size() < 8) continue;
    const auto ref_at = 92 + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, 4));
    if (ref_at + 4 > bytes.size()) continue;
    if (write_detail::read_int<std::int32_t>(bytes, ref_at) == static_cast<std::int32_t>(shader)) ++users;
  }
  return users;
}

std::size_t texture_set_users(const Model& model, std::uint32_t texture_set) {
  std::size_t users = 0;
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    const auto& bytes = model.blocks[block];
    if (write_detail::block_type(model, block) != k_lighting_type || bytes.size() < 12) continue;
    const auto ref_at = 40 + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, 8));
    if (ref_at + 4 > bytes.size()) continue;
    if (write_detail::read_int<std::int32_t>(bytes, ref_at) == static_cast<std::int32_t>(texture_set)) ++users;
  }
  return users;
}

std::size_t alpha_users(const Model& model, std::uint32_t alpha) {
  std::size_t users = 0;
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    const auto& bytes = model.blocks[block];
    if (!is_shape_type(write_detail::block_type(model, block)) || bytes.size() < 12) continue;
    const auto ref_at = 96 + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, 4));
    if (ref_at + 4 > bytes.size()) continue;
    if (write_detail::read_int<std::int32_t>(bytes, ref_at) == static_cast<std::int32_t>(alpha)) ++users;
  }
  return users;
}

std::vector<std::string> texture_slots(const NifFile& nif, const Model& model, std::uint32_t block) {
  const auto& bytes = model.blocks[block];
  check_field(nif, model, block, 0, 4);
  const auto count = write_detail::read_int<std::uint32_t>(bytes, 0);
  std::vector<std::string> slots;
  std::size_t at = 4;
  for (std::uint32_t i = 0; i < count; ++i) {
    check_field(nif, model, block, at, 4);
    const auto length = write_detail::read_int<std::uint32_t>(bytes, at);
    at += 4;
    check_field(nif, model, block, at, length);
    slots.emplace_back(reinterpret_cast<const char*>(bytes.data() + at), length);
    at += length;
  }
  if (at != bytes.size()) fail(nif, block, "texture set has bytes after its slots");
  return slots;
}

// Constraint: the source blend lives in flags bits 1-4, the destination blend in bits 5-8 and the test function in bits 10-12, so a mode edit masks only its own field and keeps the enable bits, the threshold and every neighbour.
void apply_alpha_mode(const NifFile& nif, Model& model, std::uint32_t block, std::uint16_t flags,
                      std::size_t flags_at, const Edit& edit) {
  const bool is_test = edit.key == "testFunction";
  const bool is_source = edit.key == "sourceBlend";
  const std::uint32_t shift = is_test ? 10U : (is_source ? 1U : 5U);
  const std::uint32_t max = is_test ? 7U : 10U;
  std::uint32_t mode = 0;
  if (const auto* text = std::get_if<std::string>(&edit.value)) {
    if (is_test) {
      const auto function = parse_alpha_test(*text);
      if (!function)
        fail(nif, edit.id,
             "key 'testFunction' takes Always, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual or Never");
      mode = static_cast<std::uint32_t>(*function);
    } else {
      const auto blend = parse_alpha_blend(*text);
      if (!blend)
        fail(nif, edit.id,
             "key '" + edit.key +
                 "' takes One, Zero, SourceColor, InverseSourceColor, DestinationColor, "
                 "InverseDestinationColor, SourceAlpha, InverseSourceAlpha, DestinationAlpha, "
                 "InverseDestinationAlpha or SourceAlphaSaturate");
      mode = static_cast<std::uint32_t>(*blend);
    }
  } else {
    mode = whole_number(nif, edit, static_cast<float>(max));
  }
  const std::uint32_t field = is_test ? 0x7U : 0xFU;
  const auto mask = static_cast<std::uint32_t>(field << shift);
  const auto next = (static_cast<std::uint32_t>(flags) & ~mask) | ((mode << shift) & mask);
  return write_u16(nif, model, block, flags_at, static_cast<std::uint16_t>(next));
}

void apply_alpha_edit(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source,
                      const Edit& edit) {
  const auto alpha = read_alpha(nif, source);
  if (edit.key == "name") return set_name(nif, model, block, 0, text_value(nif, edit));
  if (edit.key == "threshold" || edit.key == "alphaThreshold") {
    return write_byte(nif, model, block, alpha.threshold_at,
                      static_cast<std::uint8_t>(whole_number(nif, edit, k_byte_max)));
  }
  if (edit.key == "alphaBlend" || edit.key == "alphaTest") {
    const auto bit = edit.key == "alphaBlend" ? k_alpha_blend_bit : k_alpha_test_bit;
    const auto word = static_cast<std::uint32_t>(alpha.flags);
    const auto next = static_cast<std::uint32_t>(bool_value(nif, edit) ? word | bit : word & ~bit);
    return write_u16(nif, model, block, alpha.flags_at, static_cast<std::uint16_t>(next));
  }
  if (edit.key == "sourceBlend" || edit.key == "destinationBlend" || edit.key == "testFunction") {
    return apply_alpha_mode(nif, model, block, alpha.flags, alpha.flags_at, edit);
  }
  if (edit.key == "flags") {
    return write_u16(nif, model, block, alpha.flags_at,
                     static_cast<std::uint16_t>(whole_number(nif, edit, k_u16_max)));
  }
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

void apply_bsx_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  if (edit.key == "name") return set_name(nif, model, block, 0, text_value(nif, edit));
  check_field(nif, model, block, k_bsx_flags_at, 4);
  if (edit.key == "flags") return write_word(nif, model, block, k_bsx_flags_at, word_value(nif, edit));
  if (edit.key == "editorMarker") {
    auto word = read_bsx_flags(nif, block);
    set_bits(word, k_bsx_editor_marker_bit, bool_value(nif, edit));
    return write_word(nif, model, block, k_bsx_flags_at, word);
  }
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

void apply_collision_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  read_collision_object(nif, block);
  if (edit.key == "flags") {
    return write_u16(nif, model, block, k_collision_flags_at,
                     static_cast<std::uint16_t>(whole_number(nif, edit, k_u16_max)));
  }
  if (edit.key == "bodyId") return write_word(nif, model, block, k_collision_body_id_at, word_value(nif, edit));
  if (edit.key == "target" || edit.key == "system") fail(nif, block, "key '" + edit.key + "' is read-only");
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

void apply_shader_name(const NifFile& nif, Model& model, std::uint32_t shader, std::uint32_t source,
                       const std::string& text) {
  const auto type = write_detail::block_type(model, shader);
  if (type == k_lighting_type) return set_name(nif, model, shader, read_lighting_shader(nif, source).offsets.name, text);
  if (type == k_effect_type) return set_name(nif, model, shader, read_effect_shader(nif, source).offsets.name, text);
  fail(nif, shader, "key 'material' needs a lighting or effect shader property");
}

// Offset of the NiAVObject transform is 16 plus four bytes per extra-data entry.
std::vector<float> uv_value(const NifFile& nif, const Edit& edit, std::size_t count) {
  const auto* values = std::get_if<std::vector<float>>(&edit.value);
  if (values == nullptr || values->size() != count) {
    fail(nif, edit.id, "key 'uvs' expects " + std::to_string(count) + " numbers, u and v per vertex");
  }
  for (const float value : *values) {
    if (!std::isfinite(value) || std::fabs(value) > k_max_half) {
      fail(nif, edit.id, "key 'uvs' expects finite numbers within +-65504");
    }
  }
  return *values;
}

void apply_uvs(const NifFile& nif, Model& model, const Edit& edit) {
  const auto block = edit.id;
  if (!is_shape_type(write_detail::block_type(model, block))) fail(nif, block, "key 'uvs' needs a shape block");
  const auto descriptor_at = extra_offset(nif, model, block, 4, 100);
  check_field(nif, model, block, descriptor_at, 18);
  const auto& bytes = model.blocks[block];
  const auto descriptor = write_detail::read_int<std::uint64_t>(bytes, descriptor_at);
  const auto vertex_count =
      static_cast<std::size_t>(write_detail::read_int<std::uint16_t>(bytes, descriptor_at + 12));
  const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  if ((flags & k_has_uv) == 0) fail(nif, block, "shape has no UV channel");
  if (const auto fault = vertex_layout_fault(VertexDesc{descriptor}); !fault.empty()) {
    fail(nif, block, "vertex descriptor is inconsistent: " + fault);
  }
  if (stride == 0) fail(nif, block, "shape has no vertex data");
  const std::size_t uv_at = (flags & k_full_precision) != 0 ? 16 : 8;
  if (stride < uv_at + 4) fail(nif, block, "vertex stride has no room for UVs");
  const auto data_size = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, descriptor_at + 14));
  if (data_size < vertex_count * stride) fail(nif, block, "vertex data size is smaller than its vertices");
  const auto data_at = descriptor_at + 18;
  check_field(nif, model, block, data_at, vertex_count * stride);
  const auto values = uv_value(nif, edit, 2 * vertex_count);
  for (std::size_t v = 0; v < vertex_count; ++v) {
    const auto at = data_at + v * stride + uv_at;
    write_detail::write_int<std::uint16_t>(model.blocks[block], at, write_detail::half_bits(values[2 * v]));
    write_detail::write_int<std::uint16_t>(model.blocks[block], at + 2,
                                            write_detail::half_bits(values[2 * v + 1]));
  }
}

void apply_uvs2(const NifFile& nif, Model& model, const Edit& edit) {
  const auto block = edit.id;
  if (!is_shape_type(write_detail::block_type(model, block))) fail(nif, block, "key 'uvs2' needs a shape block");
  const auto descriptor_at = extra_offset(nif, model, block, 4, 100);
  check_field(nif, model, block, descriptor_at, 18);
  const auto& bytes = model.blocks[block];
  const auto descriptor = write_detail::read_int<std::uint64_t>(bytes, descriptor_at);
  const auto vertex_count = static_cast<std::size_t>(write_detail::read_int<std::uint16_t>(bytes, descriptor_at + 12));
  const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  if ((flags & k_has_uv2) == 0) fail(nif, block, "shape has no second UV channel");
  if (const auto fault = vertex_layout_fault(VertexDesc{descriptor}); !fault.empty()) {
    fail(nif, block, "vertex descriptor is inconsistent: " + fault);
  }
  if (stride == 0) fail(nif, block, "shape has no vertex data");
  const std::size_t uv2_at = ((flags & k_full_precision) != 0 ? 16 : 8) + ((flags & k_has_uv) != 0 ? 4 : 0);
  if (stride < uv2_at + 4) fail(nif, block, "vertex stride has no room for a second UV channel");
  const auto data_size = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, descriptor_at + 14));
  if (data_size < vertex_count * stride) fail(nif, block, "vertex data size is smaller than its vertices");
  const auto data_at = descriptor_at + 18;
  check_field(nif, model, block, data_at, vertex_count * stride);
  const auto* values = std::get_if<std::vector<float>>(&edit.value);
  if (values == nullptr || values->size() != 2 * vertex_count) {
    fail(nif, block, "key 'uvs2' expects " + std::to_string(2 * vertex_count) + " numbers, u and v per vertex");
  }
  for (const float value : *values) {
    if (!std::isfinite(value) || std::fabs(value) > k_max_half) {
      fail(nif, block, "key 'uvs2' expects finite numbers within +-65504");
    }
  }
  for (std::size_t v = 0; v < vertex_count; ++v) {
    const auto at = data_at + v * stride + uv2_at;
    write_detail::write_int<std::uint16_t>(model.blocks[block], at, write_detail::half_bits((*values)[2 * v]));
    write_detail::write_int<std::uint16_t>(model.blocks[block], at + 2,
                                            write_detail::half_bits((*values)[2 * v + 1]));
  }
}

std::size_t split_number(const NifFile& nif, std::uint32_t block, float value, std::size_t bound) {
  if (!std::isfinite(value) || value < 0.0f || value >= static_cast<float>(bound) || std::floor(value) != value) {
    fail(nif, block, "key 'split' expects whole non-negative numbers that fit the shape");
  }
  return static_cast<std::size_t>(value);
}

void apply_split(const NifFile& nif, Model& model, const Edit& edit) {
  const auto block = edit.id;
  if (!is_shape_type(write_detail::block_type(model, block))) fail(nif, block, "key 'split' needs a shape block");
  refuse_count_changing_derived_shape(nif, model, block);
  const auto* values = std::get_if<std::vector<float>>(&edit.value);
  if (values == nullptr || values->empty()) {
    fail(nif, block, "key 'split' expects a vertex count, then a source list and an index list");
  }
  const auto descriptor_at = extra_offset(nif, model, block, 4, 100);
  check_field(nif, model, block, descriptor_at, 18);
  const auto& bytes = model.blocks[block];
  const auto descriptor = write_detail::read_int<std::uint64_t>(bytes, descriptor_at);
  const auto vertex_count = static_cast<std::size_t>(write_detail::read_int<std::uint16_t>(bytes, descriptor_at + 12));
  const auto data_size = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, descriptor_at + 14));
  const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  if (stride == 0) fail(nif, block, "shape has no vertex data to split");
  const auto vertex_bytes = vertex_count * stride;
  if (data_size < vertex_bytes) fail(nif, block, "vertex data size is smaller than its vertices");
  const auto triangle_bytes = data_size - vertex_bytes;
  if (triangle_bytes % k_triangle_bytes != 0) fail(nif, block, "shape has bytes after its triangle list");
  const auto triangle_count = triangle_bytes / k_triangle_bytes;
  const auto data_at = descriptor_at + 18;
  check_field(nif, model, block, data_at, data_size);
  const auto added = split_number(nif, block, values->at(0), k_max_vertex_count + 1);
  if (vertex_count + added > k_max_vertex_count) {
    fail(nif, block, "key 'split' would grow the shape past 65535 vertices");
  }
  if (values->size() != 1 + added + 3 * triangle_count) {
    fail(nif, block, "key 'split' expects one source index per added vertex and three indices per triangle");
  }
  std::vector<std::size_t> sources(added);
  for (std::size_t i = 0; i < added; ++i) sources[i] = split_number(nif, block, values->at(1 + i), vertex_count);
  const auto total = vertex_count + added;
  Bytes out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(data_at));
  const auto* vertex_at = bytes.data() + data_at;
  out.insert(out.end(), vertex_at, vertex_at + vertex_bytes);
  for (const auto source : sources) {
    out.insert(out.end(), vertex_at + source * stride, vertex_at + (source + 1) * stride);
  }
  for (std::size_t i = 0; i < 3 * triangle_count; ++i) {
    const auto index = split_number(nif, block, values->at(1 + added + i), total);
    write_detail::append_int(out, static_cast<std::uint16_t>(index));
  }
  out.insert(out.end(), bytes.data() + data_at + data_size, bytes.data() + bytes.size());
  write_detail::write_int<std::uint16_t>(out, data_at - 6, static_cast<std::uint16_t>(total));
  write_detail::write_int<std::uint32_t>(
      out, data_at - 4, static_cast<std::uint32_t>(vertex_bytes + added * stride + triangle_bytes));
  model.blocks[block] = std::move(out);
}

// Invariant: a connect point names its parent by string, so a node that a connect point names keeps its name.
void require_connect_parent_keeps_name(const NifFile& nif, std::uint32_t block, const std::string& text) {
  const auto& current = nif.nodes().at(static_cast<std::size_t>(nif.node_for_block(block))).name;
  if (current.empty() || text == current) return;
  for (const auto& point : read_connect_parents(nif)) {
    if (point.parent == current) {
      fail(nif, block, "connect point parent '" + current + "' names this node, so renaming it is refused");
    }
  }
}

void apply_node_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  if (edit.key == "name") {
    const auto& text = text_value(nif, edit);
    require_connect_parent_keeps_name(nif, block, text);
    return set_name(nif, model, block, 0, text);
  }
  const auto transform = extra_offset(nif, model, block, 4, k_av_transform_at);
  if (apply_scalar_refs(nif, model, block, edit,
                        {{"translation", transform, 3}, {"scale", transform + 48, 1}})) {
    return;
  }
  if (edit.key == "parent") fail(nif, block, "key 'parent' is read-only");
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

void apply_shape_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  if (edit.key == "name") return set_name(nif, model, block, 0, text_value(nif, edit));
  const auto shader_source = [&] { return shader_source_index(nif, model, block); };
  if (edit.key == "material") {
    const auto source = shader_source();
    return apply_shader_name(nif, model, shader_block(nif, model, block, source), source, text_value(nif, edit));
  }
  std::size_t slot = 0;
  if (texture_key(edit.key, slot)) {
    const auto source = shader_source();
    const auto shader = shader_block(nif, model, block, source);
    if (write_detail::block_type(model, shader) == k_effect_type) {
      return apply_effect_edit(nif, model, shader, source, edit);
    }
    return write_texture_slot(nif, model, shader_texture_set(nif, model, shader, source), slot,
                              text_value(nif, edit));
  }
  const auto transform = extra_offset(nif, model, block, 4, k_av_transform_at);
  if (apply_scalar_refs(nif, model, block, edit,
                        {{"translation", transform, 3}, {"scale", transform + 48, 1}})) {
    return;
  }
  if (edit.key == "alphaBlend" || edit.key == "alphaTest" || edit.key == "alphaThreshold" ||
      edit.key == "threshold" || edit.key == "sourceBlend" || edit.key == "destinationBlend" ||
      edit.key == "testFunction") {
    const auto alpha_source = alpha_source_index(nif, model, block);
    return apply_alpha_edit(nif, model, alpha_block(nif, model, block, alpha_source), alpha_source, edit);
  }
  if (edit.key == "verts" || edit.key == "tris" || edit.key == "shaderType" || edit.key == "skin" ||
      edit.key == "vertexSize" || edit.key == "vertexAttributes" || edit.key == "fullPrecision") {
    fail(nif, block, "key '" + edit.key + "' is read-only");
  }
  const auto source = shader_source();
  const auto shader = shader_block(nif, model, block, source);
  if (write_detail::block_type(model, shader) == k_effect_type) {
    return apply_effect_edit(nif, model, shader, source, edit);
  }
  apply_lighting_edit(nif, model, shader, source, edit);
}

constexpr std::string_view k_bone_data_type = "BSSkin::BoneData";
constexpr std::size_t k_bone_center_at = 0;
constexpr std::size_t k_bone_radius_at = 12;
constexpr std::size_t k_bone_rotation_at = 16;
constexpr std::size_t k_bone_translation_at = 52;
constexpr std::size_t k_bone_scale_at = 64;

struct BoneField {
  std::size_t at{0};
  std::size_t count{0};
};

bool bone_key(const std::string& key, std::size_t& bone, BoneField& field) {
  constexpr std::string_view prefix = "bone";
  if (!key.starts_with(prefix)) return false;
  std::size_t at = prefix.size();
  const auto digits = at;
  std::size_t index = 0;
  while (at < key.size() && key[at] >= '0' && key[at] <= '9') {
    index = index * 10 + static_cast<std::size_t>(key[at] - '0');
    if (index > 0xFFFFU) return false;
    ++at;
  }
  if (at == digits) return false;
  const auto name = std::string_view{key}.substr(at);
  if (name == "Center") {
    field = BoneField{k_bone_center_at, 3};
  } else if (name == "Radius") {
    field = BoneField{k_bone_radius_at, 1};
  } else if (name == "Rotation") {
    field = BoneField{k_bone_rotation_at, 9};
  } else if (name == "Translation") {
    field = BoneField{k_bone_translation_at, 3};
  } else if (name == "Scale") {
    field = BoneField{k_bone_scale_at, 1};
  } else {
    return false;
  }
  bone = index;
  return true;
}

void apply_bone_data_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  std::size_t bone = 0;
  BoneField field;
  if (!bone_key(edit.key, bone, field)) {
    fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
  }
  check_field(nif, model, block, 0, 4);
  const auto count = write_detail::read_int<std::uint32_t>(model.blocks[block], 0);
  if (bone >= count) {
    fail(nif, block, "edit names bone " + std::to_string(bone) + " of " + std::to_string(count));
  }
  const auto at = 4 + bone * detail::k_bone_transform_bytes + field.at;
  check_field(nif, model, block, at, field.count * 4);
  const auto values =
      field.count == 1 ? std::vector<float>{number_value(nif, edit)} : vector_value(nif, edit, field.count);
  if ((field.at == k_bone_radius_at || field.at == k_bone_scale_at) && !(values.front() > 0.0f)) {
    fail(nif, block, "key '" + edit.key + "' must be above zero");
  }
  write_floats(nif, model, block, at, values);
}

// Start indices count u16 indices, three per triangle; primitives count triangles.
void check_segment_span(const NifFile& nif, std::uint32_t block, std::size_t triangles, std::uint32_t start,
                        std::uint32_t primitives) {
  const auto end = std::uint64_t{start} + std::uint64_t{primitives} * 3U;
  if (end > std::uint64_t{triangles} * 3U) {
    fail(nif, block, "segment runs past the shape's " + std::to_string(triangles) + " triangles");
  }
}

Bytes segment_table_bytes(const NifFile& nif, std::uint32_t block, const SegmentTable& table) {
  const auto triangles = read_shape(nif, block).tris.size() / 3;
  if (table.segments.size() > std::numeric_limits<std::uint16_t>::max()) {
    fail(nif, block, "too many segments to encode in an .ssf id");
  }
  Bytes bytes;
  write_detail::append_int(bytes, table.primitives);
  write_detail::append_int(bytes, static_cast<std::uint32_t>(table.segments.size()));
  write_detail::append_int(bytes, table.total_segments);
  for (const auto& segment : table.segments) {
    check_segment_span(nif, block, triangles, segment.start_index, segment.primitives);
    write_detail::append_int(bytes, segment.start_index);
    write_detail::append_int(bytes, segment.primitives);
    write_detail::append_int(bytes, segment.parent_array_index);
    if (segment.sub_segments.size() > k_max_ssf_sub_segments) {
      fail(nif, block, "too many sub-segments to encode in an .ssf id");
    }
    write_detail::append_int(bytes, static_cast<std::uint32_t>(segment.sub_segments.size()));
    for (const auto& sub : segment.sub_segments) {
      check_segment_span(nif, block, triangles, sub.start_index, sub.primitives);
      write_detail::append_int(bytes, sub.start_index);
      write_detail::append_int(bytes, sub.primitives);
      write_detail::append_int(bytes, sub.parent_array_index);
      write_detail::append_int(bytes, sub.unused);
    }
  }
  if (table.segments.size() >= table.total_segments) {
    if (!table.segment_starts.empty() || !table.user_data.empty() || !table.ssf_file.empty()) {
      fail(nif, block, "segment table has shared data, but Total Segments equals Num Segments, which leaves none");
    }
    return bytes;
  }
  if (table.ssf_file.size() > std::numeric_limits<std::uint16_t>::max()) {
    fail(nif, block, "SSF File is longer than its 16-bit length can hold");
  }
  const bool has_sub_segments = std::any_of(table.segments.begin(), table.segments.end(),
                                            [](const Segment& segment) { return !segment.sub_segments.empty(); });
  if (has_sub_segments) {
    if (table.segment_starts.size() != table.segments.size()) {
      fail(nif, block, "segment table has shared data, but its segment starts are not one per segment");
    }
    if (table.user_data.size() != table.total_segments) {
      fail(nif, block, "segment table has shared data, but its user data is not one entry per total segment");
    }
    for (std::size_t i = 0; i < table.segments.size(); ++i) {
      for (std::size_t k = 0; k < table.segments[i].sub_segments.size(); ++k) {
        if (static_cast<std::size_t>(table.segment_starts[i]) + 1 + k >= table.user_data.size()) {
          fail(nif, block, "segment table has shared data, but a sub-segment names user data past its last entry");
        }
      }
    }
  }
  write_detail::append_int(bytes, static_cast<std::uint32_t>(table.segment_starts.size()));
  write_detail::append_int(bytes, static_cast<std::uint32_t>(table.user_data.size()));
  for (const auto start : table.segment_starts) write_detail::append_int(bytes, start);
  for (const auto& user : table.user_data) {
    write_detail::append_int(bytes, user.user_index);
    write_detail::append_int(bytes, user.bone_id);
    write_detail::append_int(bytes, static_cast<std::uint32_t>(user.cut_offsets.size()));
    for (const float offset : user.cut_offsets) {
      if (!std::isfinite(offset)) fail(nif, block, "segment cut offsets must be finite numbers");
      write_detail::append_int(bytes, std::bit_cast<std::uint32_t>(offset));
    }
  }
  write_detail::append_int(bytes, static_cast<std::uint16_t>(table.ssf_file.size()));
  write_detail::append_text(bytes, table.ssf_file);
  return bytes;
}

void apply_segments(const NifFile& nif, Model& model, const Edit& edit) {
  const auto* table = std::get_if<SegmentTable>(&edit.value);
  if (table == nullptr) fail(nif, edit.id, "key 'segments' expects a segment table");
  const auto offset = detail::segment_table_offset(nif, edit.id);
  if (!offset) fail(nif, edit.id, "key 'segments' needs a BSSubIndexTriShape with segment data");
  check_field(nif, model, edit.id, 0, *offset);
  const auto bytes = segment_table_bytes(nif, edit.id, *table);
  auto& block = model.blocks[edit.id];
  block.resize(*offset);
  block.insert(block.end(), bytes.begin(), bytes.end());
}

bool in_memory_edit(const Model& model, const Edit& edit) {
  std::size_t slot = 0;
  return edit.id < model.blocks.size() && write_detail::block_type(model, edit.id) == k_texture_set_type &&
         texture_key(edit.key, slot);
}

bool patch_view(NifFile& view, const Model& model) {
  const auto& infos = view.blocks();
  if (model.blocks.size() != infos.size() || model.strings != view.strings()) return false;
  for (std::size_t block = 0; block < infos.size(); ++block) {
    if (model.blocks[block].size() != infos[block].size) return false;
  }
  for (std::size_t block = 0; block < infos.size(); ++block) {
    const auto& bytes = model.blocks[block];
    const auto at = view.bytes().begin() + static_cast<std::ptrdiff_t>(infos[block].offset);
    if (std::equal(bytes.begin(), bytes.end(), at)) continue;
    if (view.node_for_block(static_cast<std::uint32_t>(block)) >= 0) return false;
    detail::overwrite_block_bytes(view, static_cast<std::uint32_t>(block), bytes);
  }
  return true;
}

void apply_edit(const NifFile& nif, Model& model, const Edit& edit) {
  if (edit.id >= nif.blocks().size()) {
    throw parse_error(0, "edit targets block " + std::to_string(edit.id) + ", which is not in the file");
  }
  if (edit.key == "segments") return apply_segments(nif, model, edit);
  if (edit.key == "split") return apply_split(nif, model, edit);
  if (edit.key == "uvs") return apply_uvs(nif, model, edit);
  if (edit.key == "uvs2") return apply_uvs2(nif, model, edit);
  const auto type = write_detail::block_type(model, edit.id);
  if (type == k_lighting_type) return apply_lighting_edit(nif, model, edit.id, edit.id, edit);
  if (type == k_effect_type) return apply_effect_edit(nif, model, edit.id, edit.id, edit);
  if (type == k_alpha_type) return apply_alpha_edit(nif, model, edit.id, edit.id, edit);
  if (type == k_bsx_type) return apply_bsx_edit(nif, model, edit.id, edit);
  if (type == k_collision_object_type) return apply_collision_edit(nif, model, edit.id, edit);
  if (type == k_bone_data_type) return apply_bone_data_edit(nif, model, edit.id, edit);
  if (type == k_texture_set_type) {
    std::size_t slot = 0;
    if (texture_key(edit.key, slot)) return write_texture_slot(nif, model, edit.id, slot, text_value(nif, edit));
    if (is_texture_slot_name(edit.key)) {
      fail(nif, edit.id, "key '" + edit.key + "' names a texture slot this block does not store");
    }
    fail(nif, edit.id, "key '" + edit.key + "' is not supported by the NIF writer");
  }
  if (is_shape_type(type)) return apply_shape_edit(nif, model, edit.id, edit);
  if (nif.node_for_block(edit.id) >= 0) return apply_node_edit(nif, model, edit.id, edit);
  fail(nif, edit.id, "key '" + edit.key + "' is not supported by the NIF writer");
}

}  // namespace

Bytes texture_set_bytes(const std::vector<std::string>& slots) {
  Bytes bytes;
  write_detail::append_int(bytes, static_cast<std::uint32_t>(slots.size()));
  for (const auto& text : slots) write_detail::append_sized(bytes, text);
  return bytes;
}

std::uint32_t share_or_copy(const NifFile& nif, Model& model, std::uint32_t block, std::size_t ref_at,
                            std::size_t users) {
  check_field(nif, model, block, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[block], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size()) fail(nif, block, "reference is unset");
  if (users <= 1) return static_cast<std::uint32_t>(ref);
  const auto copy = static_cast<std::uint32_t>(ref);
  const auto type_index = model.block_type_index[copy];
  const Bytes bytes = model.blocks[copy];
  model.block_type_index.push_back(type_index);
  model.blocks.push_back(bytes);
  const auto fresh = static_cast<std::uint32_t>(model.blocks.size() - 1);
  write_detail::write_int(model.blocks[block], ref_at, static_cast<std::int32_t>(fresh));
  return fresh;
}

[[noreturn]] void fail(const NifFile& nif, std::uint32_t block, const std::string& message) {
  const auto offset = block < nif.blocks().size() ? nif.blocks()[block].offset : std::size_t{0};
  throw parse_error(offset, "edit on block " + std::to_string(block) + ": " + message);
}

void check_field(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t offset, std::size_t width) {
  const auto size = model.blocks[block].size();
  if (offset > size || width > size - offset) fail(nif, block, "block is too short for this edit");
}

const std::string& text_value(const NifFile& nif, const Edit& edit) {
  const auto* text = std::get_if<std::string>(&edit.value);
  if (text == nullptr) fail(nif, edit.id, "key '" + edit.key + "' expects a string");
  return *text;
}

const std::vector<std::string>& names_value(const NifFile& nif, const Edit& edit) {
  const auto* names = std::get_if<std::vector<std::string>>(&edit.value);
  if (names == nullptr) fail(nif, edit.id, "key '" + edit.key + "' expects a list of shader flag names");
  return *names;
}

float number_value(const NifFile& nif, const Edit& edit) {
  if (const auto* word = std::get_if<std::uint32_t>(&edit.value)) {
    const auto number = static_cast<float>(*word);
    if (static_cast<double>(number) != static_cast<double>(*word)) {
      fail(nif, edit.id, "key '" + edit.key + "' expects a number a 32-bit float stores exactly");
    }
    return number;
  }
  const auto* number = std::get_if<float>(&edit.value);
  if (number == nullptr || !std::isfinite(*number)) {
    fail(nif, edit.id, "key '" + edit.key + "' expects one finite number");
  }
  return *number;
}

std::vector<float> vector_value(const NifFile& nif, const Edit& edit, std::size_t count) {
  const auto* values = std::get_if<std::vector<float>>(&edit.value);
  if (values == nullptr || values->size() != count ||
      !std::all_of(values->begin(), values->end(), [](float value) { return std::isfinite(value); })) {
    if (const auto* names = std::get_if<std::vector<std::string>>(&edit.value); names != nullptr) {
      fail(nif, edit.id, "key '" + edit.key + "' expects " + std::to_string(count) + " numbers, not names");
    }
    fail(nif, edit.id, "key '" + edit.key + "' expects " + std::to_string(count) + " finite numbers");
  }
  return *values;
}

bool bool_value(const NifFile& nif, const Edit& edit) { return number_value(nif, edit) != 0.0f; }

std::uint32_t whole_number(const NifFile& nif, const Edit& edit, float max) {
  const auto value = number_value(nif, edit);
  if (value < 0.0f || value > max || value != std::floor(value)) {
    fail(nif, edit.id, "key '" + edit.key + "' expects a whole number in range");
  }
  return static_cast<std::uint32_t>(value);
}

std::uint32_t word_value(const NifFile& nif, const Edit& edit) {
  if (const auto* word = std::get_if<std::uint32_t>(&edit.value)) return *word;
  const auto value = number_value(nif, edit);
  if (value < 0.0f || value != std::floor(value) || static_cast<double>(value) >= 4294967296.0) {
    fail(nif, edit.id, "key '" + edit.key + "' expects a whole number from 0 to 4294967295");
  }
  return static_cast<std::uint32_t>(value);
}

void write_floats(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                  const std::vector<float>& values) {
  check_field(nif, model, block, offset, 4 * values.size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    write_detail::write_int(model.blocks[block], offset + 4 * i, std::bit_cast<std::uint32_t>(values[i]));
  }
}

void write_word(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint32_t value) {
  check_field(nif, model, block, offset, 4);
  write_detail::write_int(model.blocks[block], offset, value);
}

void write_u16(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint16_t value) {
  check_field(nif, model, block, offset, 2);
  write_detail::write_int(model.blocks[block], offset, value);
}

void write_byte(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint8_t value) {
  check_field(nif, model, block, offset, 1);
  model.blocks[block][offset] = static_cast<std::byte>(value);
}

void set_name(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, const std::string& text) {
  check_field(nif, model, block, offset, 4);
  write_detail::write_int(model.blocks[block], offset, write_detail::intern_string(model, text));
}

// Offset of every field after a sized string moves with that string's length.
void set_sized_string(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                      const std::string& text) {
  check_field(nif, model, block, offset, 4);
  auto& bytes = model.blocks[block];
  const auto length = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, offset));
  check_field(nif, model, block, offset + 4, length);
  Bytes rebuilt;
  rebuilt.insert(rebuilt.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
  write_detail::append_sized(rebuilt, text);
  rebuilt.insert(rebuilt.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4 + length), bytes.end());
  bytes = std::move(rebuilt);
}

bool is_shape_type(std::string_view type) { return is_shape_block_type(type); }

std::size_t extra_offset(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t count_at,
                         std::size_t fixed) {
  check_field(nif, model, block, count_at, 4);
  return fixed + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(model.blocks[block], count_at));
}

std::uint32_t shader_source_index(const NifFile& nif, Model& model, std::uint32_t block) {
  const auto type = write_detail::block_type(model, block);
  if (type == k_lighting_type || type == k_effect_type) return block;
  if (!is_shape_type(type)) fail(nif, block, "key needs a shape or shader block");
  const auto ref_at = extra_offset(nif, model, block, 4, 92);
  check_field(nif, model, block, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[block], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size()) {
    fail(nif, block, "shape has no shader property");
  }
  const auto shader = static_cast<std::uint32_t>(ref);
  const auto shader_type = write_detail::block_type(model, shader);
  if (shader_type != k_lighting_type && shader_type != k_effect_type) {
    fail(nif, block, "shape has no lighting or effect shader property");
  }
  return shader;
}

std::uint32_t shader_block(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source) {
  const auto type = write_detail::block_type(model, block);
  if (type == k_lighting_type || type == k_effect_type) return block;
  const auto ref_at = extra_offset(nif, model, block, 4, 92);
  if (shader_users(model, source) <= 1) return source;
  const auto type_index = model.block_type_index[source];
  const Bytes bytes = model.blocks[source];
  model.block_type_index.push_back(type_index);
  model.blocks.push_back(bytes);
  const auto fresh = static_cast<std::uint32_t>(model.blocks.size() - 1);
  write_detail::write_int(model.blocks[block], ref_at, static_cast<std::int32_t>(fresh));
  return fresh;
}

std::uint32_t alpha_source_index(const NifFile& nif, Model& model, std::uint32_t block) {
  if (write_detail::block_type(model, block) == k_alpha_type) return block;
  const auto ref_at = extra_offset(nif, model, block, 4, 96);
  check_field(nif, model, block, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[block], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size() ||
      write_detail::block_type(model, static_cast<std::uint32_t>(ref)) != k_alpha_type) {
    fail(nif, block, "shape has no NiAlphaProperty");
  }
  return static_cast<std::uint32_t>(ref);
}

std::uint32_t alpha_block(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source) {
  if (write_detail::block_type(model, block) == k_alpha_type) return block;
  const auto ref_at = extra_offset(nif, model, block, 4, 96);
  return share_or_copy(nif, model, block, ref_at, alpha_users(model, source));
}

std::uint32_t shader_texture_set(const NifFile& nif, Model& model, std::uint32_t shader, std::uint32_t source) {
  if (write_detail::block_type(model, shader) != k_lighting_type) {
    fail(nif, source, "an effect shader stores its textures itself");
  }
  const auto shader_offsets = read_lighting_shader(nif, source).offsets;
  check_field(nif, model, shader, shader_offsets.texture_set, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[shader], shader_offsets.texture_set);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size() ||
      write_detail::block_type(model, static_cast<std::uint32_t>(ref)) != k_texture_set_type) {
    fail(nif, source, "shader has no BSShaderTextureSet");
  }
  return share_or_copy(nif, model, shader, shader_offsets.texture_set,
                       texture_set_users(model, static_cast<std::uint32_t>(ref)));
}

bool texture_key(const std::string& key, std::size_t& slot) {
  if (key.size() != 4 || !key.starts_with("tex") || key[3] < '0' ||
      key[3] >= '0' + static_cast<char>(k_texture_slots)) {
    return false;
  }
  slot = static_cast<std::size_t>(key[3] - '0');
  return true;
}

bool is_texture_slot_name(const std::string& key) {
  if (key.size() != 4 || !key.starts_with("tex") || key[3] < '0' || key[3] > '9') return false;
  std::size_t slot = 0;
  return !texture_key(key, slot);
}

void check_texture_path(const NifFile& nif, std::uint32_t block, std::string_view text) {
  constexpr std::size_t k_max_path_bytes = 260;
  if (text.size() > k_max_path_bytes) fail(nif, block, "texture path is longer than 260 bytes");
  for (const unsigned char ch : text) {
    if (ch < 0x20 || ch == 0x7F) fail(nif, block, "texture path must not contain control characters");
  }
  if (!fo4::text::is_valid_utf8(text)) fail(nif, block, "texture path must be valid UTF-8");
}

void write_texture_slot(const NifFile& nif, Model& model, std::uint32_t block, std::size_t slot,
                        const std::string& text) {
  check_texture_path(nif, block, text);
  auto slots = texture_slots(nif, model, block);
  if (slot >= slots.size()) slots.resize(slot + 1);
  slots[slot] = text;
  model.blocks[block] = texture_set_bytes(slots);
}

}  // namespace fo4::nif::edit_detail

namespace fo4::nif {

std::vector<std::byte> write_nif(const NifFile& nif) {
  return write_detail::serialize(write_detail::load_model(nif));
}

std::vector<std::byte> apply_edits(const NifFile& nif, std::span<const Edit> edits) {
  auto model = write_detail::load_model(nif);
  NifFile view = nif;
  bool current = true;
  for (const auto& edit : edits) {
    if (!current && !edit_detail::in_memory_edit(model, edit)) {
      view = read_nif(write_detail::serialize(model));
    }
    edit_detail::apply_edit(view, model, edit);
    current = edit_detail::patch_view(view, model);
  }
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
