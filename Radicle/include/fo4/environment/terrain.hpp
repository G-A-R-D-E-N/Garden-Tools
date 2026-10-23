#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::env {

inline constexpr std::size_t k_land_grid = 33;
inline constexpr std::size_t k_land_vertices = k_land_grid * k_land_grid;
inline constexpr std::size_t k_land_alpha_entry_size = 8;
inline constexpr std::size_t k_land_alpha_grid = 17;

struct LandLayer {
  bool base{false};
  FormKey texture;
  std::uint8_t quadrant{0};
  std::uint16_t layer{0};
  std::span<const std::byte> alpha;
};

struct LandscapeData {
  std::optional<std::uint32_t> flags;
  std::optional<float> height_offset;
  std::span<const std::byte> heights;
  std::span<const std::byte> normals;
  std::span<const std::byte> colors;
  std::vector<LandLayer> layers;
};

struct LandHeightGrid {
  float base{0.0f};
  std::array<float, k_land_vertices> heights{};
};

struct LandLayerAlpha {
  std::uint8_t quadrant{0};
  std::uint16_t layer{0};
  std::optional<FormKey> texture;
  std::array<float, k_land_alpha_grid * k_land_alpha_grid> alpha{};
};

struct LandVertexWeights {
  std::optional<FormKey> base[4];
  std::vector<std::pair<FormKey, std::array<float, k_land_vertices>>> layers;
};

struct DecalData {
  std::optional<float> min_width;
  std::optional<float> max_width;
  std::optional<float> min_height;
  std::optional<float> max_height;
  std::optional<float> depth;
  std::optional<float> shininess;
};

struct TextureSetData {
  std::array<std::optional<std::string_view>, 8> textures{};
  std::optional<std::string_view> material;
  std::optional<std::uint16_t> flags;
  std::optional<DecalData> decal;
};

LandscapeData decode_landscape(const Environment& env, const RecordRef& ref);
std::optional<FormKey> decode_landscape_texture_set(const Environment& env, const RecordRef& ref);
TextureSetData decode_texture_set(const Environment& env, const RecordRef& ref);

std::optional<std::string_view> landscape_texture_diffuse(const Environment& env, const FormKey& ltex);

std::optional<LandHeightGrid> decode_land_heights(const LandscapeData& land);
std::vector<LandLayerAlpha> decode_layer_alpha(const LandscapeData& land);
LandVertexWeights blend_cell_weights(const LandscapeData& land);

}  // namespace fo4::env
