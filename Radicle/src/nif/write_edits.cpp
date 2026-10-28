#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;
using write_detail::Model;

constexpr std::string_view k_shader_type = "BSLightingShaderProperty";
constexpr std::string_view k_texture_set_type = "BSShaderTextureSet";

struct FloatField {
  std::string_view key;
  bool shader;
  std::size_t fixed;
  std::size_t count;
};

constexpr std::array<FloatField, 10> k_float_fields{{
    {"translation", false, 16, 3},
    {"scale", false, 64, 1},
    {"uvOffset", true, 24, 2},
    {"uvScale", true, 32, 2},
    {"emissiveColor", true, 44, 3},
    {"emissiveMultiple", true, 56, 1},
    {"glossiness", true, 76, 1},
    {"alpha", true, 68, 1},
    {"specularColor", true, 80, 3},
    {"specularStrength", true, 92, 1},
}};

[[noreturn]] void fail(const NifFile& nif, std::uint32_t block, const std::string& message) {
  throw parse_error(nif.blocks()[block].offset, "edit on block " + std::to_string(block) + ": " + message);
}

bool is_shape_type(std::string_view type) {
  return type == "BSTriShape" || type == "BSSubIndexTriShape" || type == "BSMeshLODTriShape" ||
         type == "BSDynamicTriShape";
}

bool is_av_block(const NifFile& nif, std::uint32_t block) {
  return nif.node_for_block(block) >= 0 || is_shape_type(nif.blocks()[block].type);
}

void check_field(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t offset, std::size_t width) {
  const auto size = model.blocks[block].size();
  if (offset > size || width > size - offset) fail(nif, block, "block is too short for this edit");
}

std::size_t extra_offset(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t count_at,
                         std::size_t fixed) {
  check_field(nif, model, block, count_at, 4);
  return fixed + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(model.blocks[block], count_at));
}

const std::string& text_value(const NifFile& nif, const Edit& edit) {
  const auto* text = std::get_if<std::string>(&edit.value);
  if (text == nullptr) fail(nif, edit.id, "key '" + edit.key + "' expects a string");
  return *text;
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
    fail(nif, edit.id, "key '" + edit.key + "' expects " + std::to_string(count) + " finite numbers");
  }
  return *values;
}

void write_floats(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                  const std::vector<float>& values) {
  check_field(nif, model, block, offset, 4 * values.size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    write_detail::write_int(model.blocks[block], offset + 4 * i, std::bit_cast<std::uint32_t>(values[i]));
  }
}

void set_name(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, const std::string& text) {
  check_field(nif, model, block, offset, 4);
  write_detail::write_int(model.blocks[block], offset, write_detail::intern_string(model, text));
}

std::uint32_t shader_block(const NifFile& nif, const Model& model, std::uint32_t block) {
  const auto type = write_detail::block_type(model, block);
  if (type == k_shader_type) return block;
  if (!is_shape_type(type)) fail(nif, block, "key needs a shape or shader block");
  const auto ref_at = extra_offset(nif, model, block, 4, 92);
  check_field(nif, model, block, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[block], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size() ||
      write_detail::block_type(model, static_cast<std::uint32_t>(ref)) != k_shader_type) {
    fail(nif, block, "shape has no BSLightingShaderProperty");
  }
  return static_cast<std::uint32_t>(ref);
}

void apply_name(const NifFile& nif, Model& model, const Edit& edit) {
  const auto& text = text_value(nif, edit);
  if (edit.key == "material") {
    set_name(nif, model, shader_block(nif, model, edit.id), 4, text);
    return;
  }
  const bool shader = write_detail::block_type(model, edit.id) == k_shader_type;
  if (!shader && !is_av_block(nif, edit.id)) fail(nif, edit.id, "key 'name' does not apply to this block type");
  set_name(nif, model, edit.id, shader ? 4 : 0, text);
}

void apply_float_field(const NifFile& nif, Model& model, const Edit& edit, const FloatField& field) {
  const auto block = field.shader ? shader_block(nif, model, edit.id) : edit.id;
  if (!field.shader && !is_av_block(nif, block)) {
    fail(nif, block, "key '" + edit.key + "' does not apply to this block type");
  }
  const auto offset = extra_offset(nif, model, block, field.shader ? 8 : 4, field.fixed);
  const auto values =
      field.count == 1 ? std::vector<float>{number_value(nif, edit)} : vector_value(nif, edit, field.count);
  write_floats(nif, model, block, offset, values);
}

std::size_t texture_set_users(const Model& model, std::uint32_t texture_set) {
  std::size_t users = 0;
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    const auto& bytes = model.blocks[block];
    if (write_detail::block_type(model, block) != k_shader_type || bytes.size() < 12) continue;
    const auto ref_at = 40 + 4 * static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(bytes, 8));
    if (ref_at + 4 > bytes.size()) continue;
    if (write_detail::read_int<std::int32_t>(bytes, ref_at) == static_cast<std::int32_t>(texture_set)) ++users;
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

void apply_texture(const NifFile& nif, Model& model, const Edit& edit, std::size_t slot) {
  const auto& text = text_value(nif, edit);
  const auto shader = shader_block(nif, model, edit.id);
  const auto ref_at = extra_offset(nif, model, shader, 8, 40);
  check_field(nif, model, shader, ref_at, 4);
  const auto ref = write_detail::read_int<std::int32_t>(model.blocks[shader], ref_at);
  if (ref < 0 || static_cast<std::size_t>(ref) >= model.blocks.size()) fail(nif, shader, "shader has no texture set");
  auto texture_set = static_cast<std::uint32_t>(ref);
  if (write_detail::block_type(model, texture_set) != k_texture_set_type) {
    fail(nif, shader, "shader's texture set reference is not a BSShaderTextureSet");
  }
  if (texture_set_users(model, texture_set) > 1) {
    const Bytes copy = model.blocks[texture_set];
    const auto type_index = model.block_type_index[texture_set];
    model.blocks.push_back(copy);
    model.block_type_index.push_back(type_index);
    texture_set = static_cast<std::uint32_t>(model.blocks.size() - 1);
    write_detail::write_int(model.blocks[shader], ref_at, static_cast<std::int32_t>(texture_set));
  }
  auto slots = texture_slots(nif, model, texture_set);
  if (slot >= slots.size()) slots.resize(slot + 1);
  slots[slot] = text;
  model.blocks[texture_set] = texture_set_bytes(slots);
}

void apply_edit(const NifFile& nif, Model& model, const Edit& edit) {
  if (edit.id >= nif.blocks().size()) {
    throw parse_error(0, "edit targets block " + std::to_string(edit.id) + ", which is not in the file");
  }
  if (edit.key == "name" || edit.key == "material") return apply_name(nif, model, edit);
  if (edit.key == "verts" || edit.key == "tris" || edit.key == "shaderType") {
    fail(nif, edit.id, "key '" + edit.key + "' is read-only");
  }
  if (edit.key.size() == 4 && edit.key.starts_with("tex") && edit.key[3] >= '0' && edit.key[3] <= '7') {
    return apply_texture(nif, model, edit, static_cast<std::size_t>(edit.key[3] - '0'));
  }
  for (const auto& field : k_float_fields) {
    if (field.key == edit.key) return apply_float_field(nif, model, edit, field);
  }
  fail(nif, edit.id, "key '" + edit.key + "' is not supported by the NIF writer");
}

}  // namespace

std::vector<std::byte> write_nif(const NifFile& nif) {
  return write_detail::serialize(write_detail::load_model(nif));
}

std::vector<std::byte> apply_edits(const NifFile& nif, std::span<const Edit> edits) {
  auto model = write_detail::load_model(nif);
  for (const auto& edit : edits) apply_edit(nif, model, edit);
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
