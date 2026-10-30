#pragma once

#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::detail {

inline TreeField text_field(std::string key, std::string label, std::string value, std::string_view type = "string") {
  return TreeField{std::move(key), std::move(label), std::string(type), std::move(value)};
}

inline TreeField readonly_field(std::string key, std::string label, std::string value) {
  return text_field(std::move(key), std::move(label), std::move(value), "readonly");
}

inline TreeField tex_field(std::string key, std::string label, std::string value) {
  return text_field(std::move(key), std::move(label), std::move(value), "tex");
}

inline TreeField number_field(std::string key, std::string label, float value, std::string_view type = "float") {
  return TreeField{std::move(key), std::move(label), std::string(type), value};
}

inline TreeField numbers_field(std::string key, std::string label, std::vector<float> values, std::string_view type) {
  return TreeField{std::move(key), std::move(label), std::string(type), std::move(values)};
}

inline TreeField bool_field(std::string key, std::string label, bool value) {
  return TreeField{std::move(key), std::move(label), "bool", value};
}

inline TreeField flags_field(std::string key, std::string label, std::vector<TreeFlag> flags) {
  return TreeField{std::move(key), std::move(label), "flags", std::move(flags)};
}

// Invariant: an empty reference reads as "none", so a missing field never looks like block 0.
inline std::string block_ref_text(std::int32_t ref) {
  return ref < 0 ? std::string("none") : "block " + std::to_string(ref);
}

std::vector<TreeField> lighting_shader_fields(const NifFile& nif, std::uint32_t block);
std::vector<TreeField> effect_shader_fields(const NifFile& nif, std::uint32_t block);
std::vector<TreeField> alpha_property_fields(const NifFile& nif, std::uint32_t block);
std::vector<TreeField> texture_set_fields(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif::detail
