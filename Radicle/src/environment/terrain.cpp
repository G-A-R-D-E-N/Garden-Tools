#include "fo4/environment/terrain.hpp"

#include "record_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

constexpr std::size_t k_vhgt_size = 4 + k_land_vertices + 3;
constexpr std::size_t k_vertex3_size = k_land_vertices * 3;
constexpr float k_land_height_scale = 8.0f;
constexpr std::size_t k_quadrant_span = (k_land_grid - 1) / 2;
constexpr std::size_t k_alpha_cells = k_land_alpha_grid * k_land_alpha_grid;

using AlphaGrid = std::array<float, k_alpha_cells>;
using VertexGrid = std::array<float, k_land_vertices>;
using TextureKey = std::optional<FormKey>;

std::optional<std::string_view> cstring(const Reader& reader, const Subrecord& sub) {
  const auto nul = std::find(sub.data.begin(), sub.data.end(), std::byte{0});
  if (nul == sub.data.end()) reader.fail(sub.signature, "is not NUL-terminated");
  const auto* begin = reinterpret_cast<const char*>(sub.data.data());
  return std::string_view{begin, static_cast<std::size_t>(nul - sub.data.begin())};
}

void require_signature(const RecordRef& ref, Signature expected) {
  if (ref.record == nullptr || ref.record->header.signature != expected) {
    throw WorldDecodeError("expected a " + expected.to_string() + " record");
  }
}

template <typename T>
T load_value(std::span<const std::byte> bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

std::optional<FormKey> present_texture(const FormKey& texture) {
  if (texture.owner().empty()) return std::nullopt;
  return texture;
}

std::optional<FormKey> first_base_texture(const LandscapeData& land, std::uint8_t quadrant) {
  for (const auto& layer : land.layers) {
    if (layer.base && layer.quadrant == quadrant) return present_texture(layer.texture);
  }
  return std::nullopt;
}

AlphaGrid decode_alpha_grid(std::span<const std::byte> vtxt) {
  AlphaGrid grid{};
  for (std::size_t off = 0; off + k_land_alpha_entry_size <= vtxt.size(); off += k_land_alpha_entry_size) {
    const auto position = load_value<std::uint16_t>(vtxt, off);
    const auto opacity = load_value<float>(vtxt, off + 4);
    const std::size_t row = position / k_land_alpha_grid;
    const std::size_t col = position % k_land_alpha_grid;
    if (row < k_land_alpha_grid) grid[row * k_land_alpha_grid + col] = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 0.0f;
  }
  return grid;
}

template <typename Grid>
Grid& slot(std::vector<std::pair<TextureKey, Grid>>& items, const TextureKey& key) {
  for (auto& item : items) {
    if (item.first == key) return item.second;
  }
  items.emplace_back(key, Grid{});
  return items.back().second;
}

std::vector<std::pair<TextureKey, AlphaGrid>> layered_quadrant(const LandscapeData& land, std::uint8_t quadrant) {
  std::vector<std::pair<TextureKey, AlphaGrid>> weights;
  for (const auto& layer : land.layers) {
    if (layer.base || layer.quadrant != quadrant || layer.alpha.empty()) continue;
    auto& own = slot(weights, present_texture(layer.texture));
    const auto alpha = decode_alpha_grid(layer.alpha);
    for (std::size_t i = 0; i < k_alpha_cells; ++i) own[i] += alpha[i];
  }
  auto& ground = slot(weights, first_base_texture(land, quadrant));
  for (std::size_t i = 0; i < k_alpha_cells; ++i) {
    float total = 0.0f;
    for (const auto& entry : weights) total += entry.second[i];
    ground[i] += std::max(1.0f - total, 0.0f);
    if (total <= 1.0f) continue;
    for (auto& entry : weights) entry.second[i] /= total;
  }
  return weights;
}

}  // namespace

LandscapeData decode_landscape(const Environment& env, const RecordRef& ref) {
  require_signature(ref, sig("LAND"));
  const Reader reader(env, ref);
  LandscapeData data;
  const auto once = [&](bool present, const Subrecord& sub) {
    if (present) reader.fail(sub.signature, "appears more than once");
  };
  const auto sized = [&](const Subrecord& sub, std::size_t size) {
    if (sub.data.size() != size) reader.fail(sub.signature, "must be " + std::to_string(size) + " bytes");
  };
  LandLayer* open_alpha = nullptr;
  for (const auto& sub : ref.record->subrecords) {
    const auto s = sub.signature;
    if (s != sig("VTXT")) open_alpha = nullptr;
    if (s == sig("DATA")) {
      once(data.flags.has_value(), sub);
      sized(sub, 4);
      data.flags = Reader::u32(sub, 0);
    } else if (s == sig("VHGT")) {
      once(data.height_offset.has_value(), sub);
      sized(sub, k_vhgt_size);
      data.height_offset = Reader::f32(sub, 0);
      data.heights = std::span<const std::byte>(sub.data).subspan(4, k_land_vertices);
    } else if (s == sig("VNML")) {
      once(!data.normals.empty(), sub);
      sized(sub, k_vertex3_size);
      data.normals = sub.data;
    } else if (s == sig("VCLR")) {
      once(!data.colors.empty(), sub);
      sized(sub, k_vertex3_size);
      data.colors = sub.data;
    } else if (s == sig("BTXT") || s == sig("ATXT")) {
      sized(sub, 8);
      LandLayer layer;
      layer.base = s == sig("BTXT");
      layer.texture = reader.link_at(sub, 0);
      layer.quadrant = std::to_integer<std::uint8_t>(sub.data[4]);
      layer.layer = Reader::u16(sub, 6);
      if (layer.quadrant > 3) reader.fail(s, "has a quadrant outside 0..3");
      data.layers.push_back(layer);
      if (!layer.base) open_alpha = &data.layers.back();
    } else if (s == sig("VTXT")) {
      if (open_alpha == nullptr || !open_alpha->alpha.empty()) {
        reader.fail(s, "does not follow an ATXT header");
      }
      if (sub.data.size() % k_land_alpha_entry_size != 0) {
        reader.fail(s, "must be a multiple of 8 bytes");
      }
      open_alpha->alpha = sub.data;
    }
  }
  return data;
}

std::optional<FormKey> decode_landscape_texture_set(const Environment& env, const RecordRef& ref) {
  require_signature(ref, sig("LTEX"));
  return Reader(env, ref).link(sig("TNAM"));
}

TextureSetData decode_texture_set(const Environment& env, const RecordRef& ref) {
  require_signature(ref, sig("TXST"));
  const Reader reader(env, ref);
  TextureSetData data;
  static constexpr std::array<Signature, 8> k_slots{
      sig("TX00"), sig("TX01"), sig("TX02"), sig("TX03"),
      sig("TX04"), sig("TX05"), sig("TX06"), sig("TX07"),
  };
  for (std::size_t slot = 0; slot < k_slots.size(); ++slot) {
    if (const auto* sub = reader.unique(k_slots[slot])) data.textures[slot] = cstring(reader, *sub);
  }
  if (const auto* sub = reader.unique(sig("MNAM"))) data.material = cstring(reader, *sub);
  if (const auto* sub = reader.sized(sig("DNAM"), 2)) data.flags = Reader::u16(*sub, 0);
  if (const auto* sub = reader.unique(sig("DODT"))) {
    if (sub->data.size() < 16) reader.fail(sub->signature, "is shorter than its width/height members");
    DecalData decal;
    std::array<std::optional<float>*, 6> members{&decal.min_width, &decal.max_width,
                                                 &decal.min_height, &decal.max_height,
                                                 &decal.depth, &decal.shininess};
    for (std::size_t i = 0; i < members.size() && (i + 1) * 4 <= sub->data.size(); ++i) {
      *members[i] = Reader::f32(*sub, i * 4);
    }
    data.decal = decal;
  }
  return data;
}

std::optional<std::string_view> landscape_texture_diffuse(const Environment& env, const FormKey& ltex) {
  const auto* land_texture = env.winner(ltex);
  if (land_texture == nullptr) return std::nullopt;
  const auto set = decode_landscape_texture_set(env, *land_texture);
  if (!set || set->owner().empty()) return std::nullopt;
  const auto* texture_set = env.winner(*set);
  if (texture_set == nullptr) return std::nullopt;
  const auto diffuse = decode_texture_set(env, *texture_set).textures[0];
  if (!diffuse || diffuse->empty()) return std::nullopt;
  return diffuse;
}

std::optional<LandHeightGrid> decode_land_heights(const LandscapeData& land) {
  if (!land.height_offset || land.heights.size() != k_land_vertices) return std::nullopt;
  LandHeightGrid grid;
  grid.base = *land.height_offset;
  float row_start = grid.base * k_land_height_scale;
  for (std::size_t y = 0; y < k_land_grid; ++y) {
    float current = row_start;
    for (std::size_t x = 0; x < k_land_grid; ++x) {
      const auto delta = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(land.heights[y * k_land_grid + x]));
      current += static_cast<float>(delta) * k_land_height_scale;
      grid.heights[y * k_land_grid + x] = current;
      if (x == 0) row_start = current;
    }
  }
  return grid;
}

std::vector<LandLayerAlpha> decode_layer_alpha(const LandscapeData& land) {
  std::vector<LandLayerAlpha> out;
  for (const auto& layer : land.layers) {
    if (layer.base || layer.alpha.empty()) continue;
    out.push_back({layer.quadrant, layer.layer, present_texture(layer.texture), decode_alpha_grid(layer.alpha)});
  }
  return out;
}

LandVertexWeights blend_cell_weights(const LandscapeData& land) {
  std::vector<std::pair<TextureKey, VertexGrid>> sums;
  std::array<std::uint32_t, k_land_vertices> counts{};
  for (std::uint8_t quadrant = 0; quadrant < 4; ++quadrant) {
    const std::size_t x0 = (quadrant & 1) * k_quadrant_span;
    const std::size_t y0 = (quadrant >> 1) * k_quadrant_span;
    for (const auto& [texture, weights] : layered_quadrant(land, quadrant)) {
      auto& sum = slot(sums, texture);
      for (std::size_t y = 0; y < k_land_alpha_grid; ++y) {
        for (std::size_t x = 0; x < k_land_alpha_grid; ++x) {
          sum[(y0 + y) * k_land_grid + x0 + x] += weights[y * k_land_alpha_grid + x];
        }
      }
    }
    for (std::size_t y = 0; y < k_land_alpha_grid; ++y) {
      for (std::size_t x = 0; x < k_land_alpha_grid; ++x) {
        ++counts[(y0 + y) * k_land_grid + x0 + x];
      }
    }
  }
  LandVertexWeights out;
  for (std::uint8_t quadrant = 0; quadrant < 4; ++quadrant) {
    out.base[quadrant] = first_base_texture(land, quadrant);
  }
  for (auto& [texture, sum] : sums) {
    for (std::size_t i = 0; i < k_land_vertices; ++i) sum[i] /= static_cast<float>(counts[i]);
    if (texture) out.layers.emplace_back(*texture, sum);
  }
  return out;
}

}  // namespace fo4::env
