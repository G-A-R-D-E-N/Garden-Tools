#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace fo4::env {

struct MaterialSubstitution {
  std::string_view original_material;
  std::optional<std::string_view> replacement_material;
  std::optional<float> color_remapping_index;
};

std::vector<MaterialSubstitution> decode_material_swap(const Environment& env, const RecordRef& ref);

std::optional<FormKey> placed_material_swap(const Environment& env, const RecordRef& ref);

std::optional<FormKey> primary_model_material_swap(const Environment& env, const RecordRef& ref);

std::optional<float> primary_model_color_remapping_index(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
