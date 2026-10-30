#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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

Bytes texture_set_bytes(const std::vector<std::string>& slots) {
  Bytes bytes;
  write_detail::append_int(bytes, static_cast<std::uint32_t>(slots.size()));
  for (const auto& text : slots) write_detail::append_sized(bytes, text);
  return bytes;
}

std::uint32_t share_or_copy(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t ref_at,
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

void apply_alpha_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  const auto alpha = read_alpha(nif, block);
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
    fail(nif, block, "key '" + edit.key + "' is read-only");
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

void apply_shader_name(const NifFile& nif, Model& model, std::uint32_t shader, const std::string& text) {
  const auto type = write_detail::block_type(model, shader);
  if (type == k_lighting_type) return set_name(nif, model, shader, read_lighting_shader(nif, shader).offsets.name, text);
  if (type == k_effect_type) return set_name(nif, model, shader, read_effect_shader(nif, shader).offsets.name, text);
  fail(nif, shader, "key 'material' needs a lighting or effect shader property");
}

// Offset of the NiAVObject transform is 16 plus four bytes per extra-data entry.
void apply_node_edit(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit) {
  if (edit.key == "name") return set_name(nif, model, block, 0, text_value(nif, edit));
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
  if (edit.key == "material") return apply_shader_name(nif, model, shader_block(nif, model, block), text_value(nif, edit));
  std::size_t slot = 0;
  if (texture_key(edit.key, slot)) {
    return write_texture_slot(nif, model, shape_texture_set(nif, model, block), slot, text_value(nif, edit));
  }
  const auto transform = extra_offset(nif, model, block, 4, k_av_transform_at);
  if (apply_scalar_refs(nif, model, block, edit,
                        {{"translation", transform, 3}, {"scale", transform + 48, 1}})) {
    return;
  }
  if (edit.key == "alphaBlend" || edit.key == "alphaTest" || edit.key == "alphaThreshold" ||
      edit.key == "threshold") {
    return apply_alpha_edit(nif, model, alpha_block(nif, model, block), edit);
  }
  if (edit.key == "verts" || edit.key == "tris" || edit.key == "shaderType" || edit.key == "skin" ||
      edit.key == "vertexSize" || edit.key == "vertexAttributes" || edit.key == "fullPrecision") {
    fail(nif, block, "key '" + edit.key + "' is read-only");
  }
  const auto shader = shader_block(nif, model, block);
  if (write_detail::block_type(model, shader) == k_effect_type) {
    return apply_effect_edit(nif, model, shader, edit);
  }
  apply_lighting_edit(nif, model, shader, edit);
}

void apply_edit(const NifFile& nif, Model& model, const Edit& edit) {
  if (edit.id >= nif.blocks().size()) {
    throw parse_error(0, "edit targets block " + std::to_string(edit.id) + ", which is not in the file");
  }
  const auto type = write_detail::block_type(model, edit.id);
  if (type == k_lighting_type) return apply_lighting_edit(nif, model, edit.id, edit);
  if (type == k_effect_type) return apply_effect_edit(nif, model, edit.id, edit);
  if (type == k_alpha_type) return apply_alpha_edit(nif, model, edit.id, edit);
  if (type == k_bsx_type) return apply_bsx_edit(nif, model, edit.id, edit);
  if (type == k_collision_object_type) return apply_collision_edit(nif, model, edit.id, edit);
  if (type == k_texture_set_type) {
    std::size_t slot = 0;
    if (texture_key(edit.key, slot)) return write_texture_slot(nif, model, edit.id, slot, text_value(nif, edit));
    fail(nif, edit.id, "key '" + edit.key + "' is not supported by the NIF writer");
  }
  if (is_shape_type(type)) return apply_shape_edit(nif, model, edit.id, edit);
  if (nif.node_for_block(edit.id) >= 0) return apply_node_edit(nif, model, edit.id, edit);
  fail(nif, edit.id, "key '" + edit.key + "' is not supported by the NIF writer");
}

}  // namespace

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

bool is_shape_type(std::string_view type) {
  return type == "BSTriShape" || type == "BSSubIndexTriShape" || type == "BSMeshLODTriShape" ||
         type == "BSDynamicTriShape";
}

std::size_t extra_offset(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t count_at,
                         std::size_t fixed) {
  check_field(nif, model, block, count_at, 4);
  return fixed + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(model.blocks[block], count_at));
}

std::uint32_t shader_block(const NifFile& nif, Model& model, std::uint32_t block) {
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
  if (shader_users(model, shader) <= 1) return shader;
  const auto type_index = model.block_type_index[shader];
  const Bytes bytes = model.blocks[shader];
  model.block_type_index.push_back(type_index);
  model.blocks.push_back(bytes);
  const auto fresh = static_cast<std::uint32_t>(model.blocks.size() - 1);
  write_detail::write_int(model.blocks[block], ref_at, static_cast<std::int32_t>(fresh));
  return fresh;
}

std::uint32_t alpha_block(const NifFile& nif, Model& model, std::uint32_t block) {
  if (write_detail::block_type(model, block) == k_alpha_type) return block;
  const auto ref_at = extra_offset(nif, model, block, 4, 96);
  check_field(nif, model, block, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[block], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size() ||
      write_detail::block_type(model, static_cast<std::uint32_t>(ref)) != k_alpha_type) {
    fail(nif, block, "shape has no NiAlphaProperty");
  }
  return share_or_copy(nif, model, block, ref_at, alpha_users(model, static_cast<std::uint32_t>(ref)));
}

std::uint32_t shape_texture_set(const NifFile& nif, Model& model, std::uint32_t block) {
  const auto shader = shader_block(nif, model, block);
  if (write_detail::block_type(model, shader) != k_lighting_type) {
    fail(nif, shader, "an effect shader stores its textures itself");
  }
  const auto shader_offsets = read_lighting_shader(nif, shader).offsets;
  check_field(nif, model, shader, shader_offsets.texture_set, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[shader], shader_offsets.texture_set);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size() ||
      write_detail::block_type(model, static_cast<std::uint32_t>(ref)) != k_texture_set_type) {
    fail(nif, shader, "shader has no BSShaderTextureSet");
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

void write_texture_slot(const NifFile& nif, Model& model, std::uint32_t block, std::size_t slot,
                        const std::string& text) {
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
  auto bytes = write_detail::serialize(write_detail::load_model(nif));
  for (const auto& edit : edits) {
    const auto fresh = read_nif(bytes);
    auto model = write_detail::load_model(fresh);
    edit_detail::apply_edit(fresh, model, edit);
    bytes = write_detail::serialize(model);
  }
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
