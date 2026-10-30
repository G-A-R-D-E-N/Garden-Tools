#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::edit_detail {

using write_detail::Bytes;
using write_detail::Model;

constexpr std::string_view k_lighting_type = "BSLightingShaderProperty";
constexpr std::string_view k_effect_type = "BSEffectShaderProperty";
constexpr std::string_view k_alpha_type = "NiAlphaProperty";
constexpr std::string_view k_texture_set_type = "BSShaderTextureSet";
constexpr std::string_view k_bsx_type = "BSXFlags";
constexpr std::string_view k_collision_object_type = "bhkNPCollisionObject";
constexpr std::size_t k_texture_slots = 8;

[[noreturn]] void fail(const NifFile& nif, std::uint32_t block, const std::string& message);

// Requires offset + width to lie inside the block.
void check_field(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t offset, std::size_t width);

const std::string& text_value(const NifFile& nif, const Edit& edit);
const std::vector<std::string>& names_value(const NifFile& nif, const Edit& edit);
float number_value(const NifFile& nif, const Edit& edit);
std::vector<float> vector_value(const NifFile& nif, const Edit& edit, std::size_t count);
bool bool_value(const NifFile& nif, const Edit& edit);
std::uint32_t whole_number(const NifFile& nif, const Edit& edit, float max);
std::uint32_t word_value(const NifFile& nif, const Edit& edit);

void write_floats(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                  const std::vector<float>& values);
void write_word(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint32_t value);
void write_u16(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint16_t value);
void write_byte(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, std::uint8_t value);
void set_name(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset, const std::string& text);
void set_sized_string(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                      const std::string& text);

bool is_shape_type(std::string_view type);
std::size_t extra_offset(const NifFile& nif, const Model& model, std::uint32_t block, std::size_t count_at,
                         std::size_t fixed);

// Invariant: an edited shader, texture set or alpha block has exactly one user.
std::uint32_t shader_source_index(const NifFile& nif, Model& model, std::uint32_t block);
std::uint32_t shader_block(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source);
std::uint32_t alpha_source_index(const NifFile& nif, Model& model, std::uint32_t block);
std::uint32_t alpha_block(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source);
std::uint32_t shader_texture_set(const NifFile& nif, Model& model, std::uint32_t shader, std::uint32_t source);
bool texture_key(const std::string& key, std::size_t& slot);
void write_texture_slot(const NifFile& nif, Model& model, std::uint32_t block, std::size_t slot,
                        const std::string& text);

struct ScalarRef {
  std::string_view key;
  std::size_t offset;
  std::size_t count;
};

inline bool apply_scalar_refs(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit,
                              std::initializer_list<ScalarRef> refs) {
  for (const auto& ref : refs) {
    if (ref.key != edit.key) continue;
    const auto values =
        ref.count == 1 ? std::vector<float>{number_value(nif, edit)} : vector_value(nif, edit, ref.count);
    write_floats(nif, model, block, ref.offset, values);
    return true;
  }
  return false;
}

// Constraint: source must be the same block in nif, which a freshly copied block is not.
void apply_lighting_edit(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source,
                         const Edit& edit);
void apply_effect_edit(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source,
                       const Edit& edit);

}  // namespace fo4::nif::edit_detail
