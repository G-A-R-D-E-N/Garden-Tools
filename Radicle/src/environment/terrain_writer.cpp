#include "fo4/environment/terrain_writer.hpp"

#include "fo4/plugin/record_edit.hpp"

#include "fo4/land/heightmap.hpp"
#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace fo4 {
namespace {

using detail::locate_mutable_record;
using detail::mark_record_modified;

constexpr Signature k_land = Signature::from_string("LAND");
constexpr Signature k_vhgt = Signature::from_string("VHGT");
constexpr Signature k_vnml = Signature::from_string("VNML");
constexpr Signature k_vclr = Signature::from_string("VCLR");
constexpr std::size_t k_vertex_vector_bytes = fo4::env::k_land_vertices * 3;
constexpr Signature k_btxt = Signature::from_string("BTXT");
constexpr Signature k_atxt = Signature::from_string("ATXT");
constexpr Signature k_vtxt = Signature::from_string("VTXT");
constexpr Signature k_ltex = Signature::from_string("LTEX");
constexpr Signature k_vtex = Signature::from_string("VTEX");
constexpr std::size_t k_quadrants = 4;
constexpr std::size_t k_texture_layer_bytes = 8;
constexpr std::size_t k_alpha_cells = fo4::env::k_land_alpha_grid * fo4::env::k_land_alpha_grid;

void push_little(std::vector<std::byte>& out, std::uint32_t value, std::size_t width) {
  for (std::size_t byte = 0; byte < width; ++byte) {
    out.push_back(std::byte{static_cast<std::uint8_t>((value >> (8 * byte)) & 0xFFu)});
  }
}

void push_float(std::vector<std::byte>& out, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  push_little(out, bits, sizeof(bits));
}

std::optional<LandMutationStatus> texture_problem(const Plugin& plugin, const FormKey& texture) {
  if (texture.owner().empty()) return LandMutationStatus::texture_missing;
  if (texture.owner() == plugin.key) {
    const auto* local = find_record(plugin, texture);
    if (local == nullptr || local->header.signature != k_ltex) return LandMutationStatus::texture_mismatch;
  }
  return std::nullopt;
}

std::vector<std::byte> texture_layer_payload(const Plugin& plugin,
                                             const FormKey& texture,
                                             std::uint8_t quadrant,
                                             std::uint16_t layer) {
  std::vector<std::byte> out;
  out.reserve(k_texture_layer_bytes);
  push_little(out, static_cast<std::uint32_t>(encode_plugin_form_id(texture, plugin_form_id_context(plugin)).value()), 4);
  out.push_back(std::byte{quadrant});
  out.push_back(std::byte{0});
  push_little(out, layer, 2);
  return out;
}

std::vector<std::byte> alpha_entries(std::span<const float> alpha) {
  std::vector<std::byte> out;
  for (std::size_t cell = 0; cell < k_alpha_cells; ++cell) {
    if (alpha[cell] == 0.0f) continue;
    push_little(out, static_cast<std::uint32_t>(cell), 2);
    push_little(out, 0, 2);
    push_float(out, alpha[cell]);
  }
  return out;
}

bool alpha_values_valid(std::span<const float> alpha) {
  return std::all_of(alpha.begin(), alpha.end(), [](float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
  });
}

std::size_t count_subrecords(const std::vector<Subrecord>& subs, Signature signature, std::size_t end) {
  std::size_t count = 0;
  for (std::size_t at = 0; at < end; ++at) {
    if (subs[at].signature == signature) ++count;
  }
  return count;
}

std::size_t vtex_position(const std::vector<Subrecord>& subs) {
  for (std::size_t at = 0; at < subs.size(); ++at) {
    if (subs[at].signature == k_vtex) return at;
  }
  return subs.size();
}

std::size_t first_subrecord(const Record& record, Signature signature) {
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    if (record.subrecords[at].signature == signature) return at;
  }
  return record.subrecords.size();
}

LandMutationStatus land_record_status(Plugin& plugin, const FormKey& key, Record*& record) {
  record = locate_mutable_record(plugin, key);
  if (record == nullptr) return LandMutationStatus::record_missing;
  if (record->header.signature != k_land) return LandMutationStatus::record_mismatch;
  return LandMutationStatus::updated;
}

LandMutationStatus replace_vertex_vectors(Plugin& plugin,
                                          const FormKey& key,
                                          Signature signature,
                                          std::span<const std::byte> bytes) {
  Record* record = nullptr;
  if (const auto status = land_record_status(plugin, key, record);
      status != LandMutationStatus::updated) {
    return status;
  }
  if (bytes.size() != k_vertex_vector_bytes) return LandMutationStatus::size_mismatch;
  const std::size_t at = first_subrecord(*record, signature);
  if (at == record->subrecords.size()) return LandMutationStatus::subrecord_missing;
  std::vector<std::byte> payload(bytes.begin(), bytes.end());
  if (record->subrecords[at].data == payload) return LandMutationStatus::unchanged;
  record->subrecords[at].data = std::move(payload);
  mark_record_modified(plugin, *record);
  return LandMutationStatus::updated;
}

}  // namespace

LandMutationStatus set_land_heights(Plugin& plugin,
                                    const FormKey& key,
                                    std::span<const float> heights) {
  Record* record = nullptr;
  if (const auto status = land_record_status(plugin, key, record);
      status != LandMutationStatus::updated) {
    return status;
  }
  if (heights.size() != fo4::env::k_land_vertices) return LandMutationStatus::size_mismatch;
  for (const float height : heights) {
    if (!std::isfinite(height)) return LandMutationStatus::value_invalid;
  }
  const std::size_t at = first_subrecord(*record, k_vhgt);
  if (at == record->subrecords.size()) return LandMutationStatus::subrecord_missing;

  const auto& current = record->subrecords[at].data;
  const auto exact = [&](const std::optional<fo4::land::LandHeightmap>& candidate) {
    if (!candidate) return false;
    const auto decoded = fo4::land::land_heights(*candidate);
    for (std::size_t index = 0; index < decoded.size(); ++index) {
      if (decoded[index] != heights[index]) return false;
    }
    return true;
  };
  // Invariant: the record's own offset is tried first, so writing back the heights a record
  // already has reproduces its bytes; a fresh offset is derived only when that one cannot hold them.
  std::optional<fo4::land::LandHeightmap> heightmap;
  if (current.size() == fo4::land::k_vhgt_size) {
    heightmap = fo4::land::make_land_heightmap(heights, fo4::land::decode_land_heightmap(current));
  }
  if (!exact(heightmap)) {
    const float offset = std::round(heights[0] / fo4::land::k_land_height_scale);
    heightmap = fo4::land::make_land_heightmap(heights, offset);
    if (!heightmap) return LandMutationStatus::height_out_of_range;
    if (!exact(heightmap)) return LandMutationStatus::height_unrepresentable;
  }

  auto encoded = fo4::land::encode_land_heightmap(*heightmap);
  if (current.size() == fo4::land::k_vhgt_size) {
    std::copy(current.begin() + 4 + static_cast<std::ptrdiff_t>(fo4::land::k_heightmap_vertices),
              current.end(), encoded.begin() + 4 + static_cast<std::ptrdiff_t>(fo4::land::k_heightmap_vertices));
  }
  std::vector<std::byte> payload(encoded.begin(), encoded.end());
  if (current == payload) return LandMutationStatus::unchanged;
  record->subrecords[at].data = std::move(payload);
  mark_record_modified(plugin, *record);
  return LandMutationStatus::updated;
}

LandMutationStatus set_land_normals(Plugin& plugin,
                                    const FormKey& key,
                                    std::span<const std::byte> normals) {
  return replace_vertex_vectors(plugin, key, k_vnml, normals);
}

LandMutationStatus set_land_vertex_colors(Plugin& plugin,
                                          const FormKey& key,
                                          std::span<const std::byte> colors) {
  return replace_vertex_vectors(plugin, key, k_vclr, colors);
}

LandMutationStatus set_land_base_texture(Plugin& plugin,
                                         const FormKey& key,
                                         std::uint8_t quadrant,
                                         const FormKey& texture,
                                         std::uint16_t layer) {
  Record* record = nullptr;
  if (const auto status = land_record_status(plugin, key, record);
      status != LandMutationStatus::updated) {
    return status;
  }
  if (quadrant >= k_quadrants) return LandMutationStatus::quadrant_invalid;
  if (const auto problem = texture_problem(plugin, texture)) return *problem;
  const auto payload = texture_layer_payload(plugin, texture, quadrant, layer);

  auto next = record->subrecords;
  std::size_t at = next.size();
  for (std::size_t index = 0; index < next.size(); ++index) {
    const auto& sub = next[index];
    if (sub.signature == k_btxt && sub.data.size() == k_texture_layer_bytes &&
        sub.data[4] == std::byte{quadrant}) {
      at = index;
      break;
    }
  }
  if (at != next.size()) {
    if (next[at].data == payload) return LandMutationStatus::unchanged;
    next[at].data = payload;
  } else {
    std::size_t position = vtex_position(next);
    for (std::size_t index = 0; index < next.size(); ++index) {
      if (next[index].signature == k_atxt || next[index].signature == k_vtxt) {
        position = index;
        break;
      }
    }
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(position), Subrecord{k_btxt, payload, false});
  }
  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  return LandMutationStatus::updated;
}

LandMutationStatus add_land_alpha_layer(Plugin& plugin,
                                        const FormKey& key,
                                        std::uint8_t quadrant,
                                        const FormKey& texture,
                                        std::uint16_t layer,
                                        std::span<const float> alpha,
                                        std::size_t* out_index) {
  if (out_index != nullptr) *out_index = 0;
  Record* record = nullptr;
  if (const auto status = land_record_status(plugin, key, record);
      status != LandMutationStatus::updated) {
    return status;
  }
  if (quadrant >= k_quadrants) return LandMutationStatus::quadrant_invalid;
  if (alpha.size() != k_alpha_cells) return LandMutationStatus::size_mismatch;
  if (!alpha_values_valid(alpha)) return LandMutationStatus::value_invalid;
  // Constraint: a layer that covers no cell would write an empty VTXT, which no shipped LAND carries.
  if (std::all_of(alpha.begin(), alpha.end(), [](float value) { return value == 0.0f; })) {
    return LandMutationStatus::value_invalid;
  }
  if (const auto problem = texture_problem(plugin, texture)) return *problem;

  auto next = record->subrecords;
  std::size_t position = vtex_position(next);
  for (std::size_t index = 0; index < next.size(); ++index) {
    if (next[index].signature == k_atxt || next[index].signature == k_vtxt) position = index + 1;
  }
  const std::size_t index = count_subrecords(next, k_atxt, position);
  next.insert(next.begin() + static_cast<std::ptrdiff_t>(position),
              Subrecord{k_atxt, texture_layer_payload(plugin, texture, quadrant, layer), false});
  next.insert(next.begin() + static_cast<std::ptrdiff_t>(position + 1),
              Subrecord{k_vtxt, alpha_entries(alpha), false});
  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  if (out_index != nullptr) *out_index = index;
  return LandMutationStatus::updated;
}

LandMutationStatus remove_land_alpha_layer(Plugin& plugin, const FormKey& key, std::size_t index) {
  Record* record = nullptr;
  if (const auto status = land_record_status(plugin, key, record);
      status != LandMutationStatus::updated) {
    return status;
  }
  const std::size_t total = count_subrecords(record->subrecords, k_atxt, record->subrecords.size());
  if (total == 0) return LandMutationStatus::subrecord_missing;
  if (index >= total) return LandMutationStatus::index_out_of_range;

  auto next = record->subrecords;
  std::size_t seen = 0;
  std::size_t at = next.size();
  for (std::size_t position = 0; position < next.size(); ++position) {
    if (next[position].signature != k_atxt) continue;
    if (seen == index) {
      at = position;
      break;
    }
    ++seen;
  }
  std::size_t end = at + 1;
  if (end < next.size() && next[end].signature == k_vtxt) ++end;
  next.erase(next.begin() + static_cast<std::ptrdiff_t>(at), next.begin() + static_cast<std::ptrdiff_t>(end));
  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  return LandMutationStatus::updated;
}

}  // namespace fo4
