#include "fo4/environment/world.hpp"

#include "default_water.hpp"
#include "record_reader.hpp"

#include <cstring>
#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature k_wrld{'W', 'R', 'L', 'D'};
constexpr Signature k_cell{'C', 'E', 'L', 'L'};
constexpr Signature k_wnam{'W', 'N', 'A', 'M'};
constexpr Signature k_pnam{'P', 'N', 'A', 'M'};
constexpr Signature k_cnam{'C', 'N', 'A', 'M'};
constexpr Signature k_nam2{'N', 'A', 'M', '2'};
constexpr Signature k_nam3{'N', 'A', 'M', '3'};
constexpr Signature k_nam4{'N', 'A', 'M', '4'};
constexpr Signature k_ltmp{'L', 'T', 'M', 'P'};
constexpr Signature k_dnam{'D', 'N', 'A', 'M'};
constexpr Signature k_nam0{'N', 'A', 'M', '0'};
constexpr Signature k_nam9{'N', 'A', 'M', '9'};
constexpr Signature k_data{'D', 'A', 'T', 'A'};
constexpr Signature k_xclc{'X', 'C', 'L', 'C'};
constexpr Signature k_xcll{'X', 'C', 'L', 'L'};
constexpr Signature k_xclw{'X', 'C', 'L', 'W'};
constexpr Signature k_xcwt{'X', 'C', 'W', 'T'};
constexpr Signature k_xcim{'X', 'C', 'I', 'M'};
constexpr Signature k_xezn{'X', 'E', 'Z', 'N'};
constexpr Signature k_xlcn{'X', 'L', 'C', 'N'};

// xEdit wbAmbientColors: six directional colors, plus specular and fresnel power from form version 30.
constexpr std::uint16_t k_ambient_specular_form_version = 30;

std::optional<std::array<float, 2>> point2(const Reader& reader, Signature signature) {
  const auto* subrecord = reader.sized(signature, 8);
  if (subrecord == nullptr) return std::nullopt;
  return std::array<float, 2>{Reader::f32(*subrecord, 0), Reader::f32(*subrecord, 4)};
}

std::optional<CellLighting> decode_lighting(const Reader& reader, Signature signature, bool has_inherits) {
  const auto* xcll = reader.unique(signature);
  if (xcll == nullptr) return std::nullopt;
  const std::size_t ambient_size =
      reader.ref().record->header.form_version >= k_ambient_specular_form_version ? 32 : 24;
  const std::size_t tail = 40 + ambient_size;
  const auto size = xcll->data.size();
  if (size < tail) reader.fail(signature, "is shorter than its required members");

  CellLighting lighting;
  lighting.ambient = Reader::color(*xcll, 0);
  lighting.directional = Reader::color(*xcll, 4);
  lighting.fog_near_color = Reader::color(*xcll, 8);
  lighting.fog_near = Reader::f32(*xcll, 12);
  lighting.fog_far = Reader::f32(*xcll, 16);
  lighting.directional_rotation_xy = Reader::i32(*xcll, 20);
  lighting.directional_rotation_z = Reader::i32(*xcll, 24);
  lighting.directional_fade = Reader::f32(*xcll, 28);
  lighting.fog_clip_distance = Reader::f32(*xcll, 32);
  lighting.fog_power = Reader::f32(*xcll, 36);
  if (size >= tail + 4) lighting.fog_far_color = Reader::color(*xcll, tail);
  if (size >= tail + 8) lighting.fog_max = Reader::f32(*xcll, tail + 4);
  if (size >= tail + 12) lighting.light_fade_begin = Reader::f32(*xcll, tail + 8);
  if (size >= tail + 16) lighting.light_fade_end = Reader::f32(*xcll, tail + 12);
  if (has_inherits && size >= tail + 20) lighting.inherits = Reader::u32(*xcll, tail + 16);
  return lighting;
}

}  // namespace

WorldspaceData decode_worldspace(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, k_wrld);
  WorldspaceData data;
  data.parent = reader.link(k_wnam);
  if (const auto* pnam = reader.sized(k_pnam, 2)) data.parent_flags = Reader::u16(*pnam, 0);
  data.climate = reader.link(k_cnam);
  data.water = reader.link(k_nam2);
  data.lighting_template = reader.link(k_ltmp);
  if (const auto* dnam = reader.sized(k_dnam, 8)) {
    data.default_land_height = Reader::f32(*dnam, 0);
    data.default_water_height = Reader::f32(*dnam, 4);
  }
  data.bounds_min = point2(reader, k_nam0);
  data.bounds_max = point2(reader, k_nam9);
  if (const auto* flags = reader.sized(k_data, 1)) {
    data.flags = std::to_integer<std::uint8_t>(flags->data[0]);
  }
  data.lod_water = reader.link(k_nam3);
  data.lod_water_height = reader.f32(k_nam4);
  return data;
}

LightingTemplateData decode_lighting_template(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, Signature::from_string("LGTM"));
  LightingTemplateData data;
  data.lighting = decode_lighting(reader, k_data, false);
  data.god_rays = reader.link(Signature::from_string("WGDR"));
  return data;
}

CellData decode_cell(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, k_cell);
  CellData data;
  if (const auto* flags = reader.unique(k_data)) {
    if (flags->data.size() == 1) {
      data.flags = std::to_integer<std::uint8_t>(flags->data[0]);
    } else if (flags->data.size() == 2) {
      data.flags = Reader::u16(*flags, 0);
    } else {
      reader.fail(k_data, "must be 1 or 2 bytes");
    }
  }
  if (const auto* xclc = reader.unique(k_xclc)) {
    if (xclc->data.size() != 8 && xclc->data.size() != 12) reader.fail(k_xclc, "must be 8 or 12 bytes");
    CellGrid grid{Reader::i32(*xclc, 0), Reader::i32(*xclc, 4), std::nullopt};
    if (xclc->data.size() == 12) grid.land_flags = std::to_integer<std::uint8_t>(xclc->data[8]);
    data.grid = grid;
  }
  data.lighting = decode_lighting(reader, k_xcll, true);
  data.lighting_template = reader.link(k_ltmp);
  data.water_height = reader.f32(k_xclw);
  data.image_space = reader.link(k_xcim);
  data.encounter_zone = reader.link(k_xezn);
  data.location = reader.link(k_xlcn);
  data.worldspace = env.world_of(ref);
  data.water = reader.link(k_xcwt);
  return data;
}

namespace {

const RecordRef* winning_type(const Environment& env, const std::optional<FormKey>& key,
                              Signature signature) {
  if (!key || key->owner().empty()) return nullptr;
  const auto* ref = env.winner(*key);
  return ref && ref->record->header.signature == signature && !env.metadata(*ref).deleted
             ? ref : nullptr;
}

std::uint16_t effective_parent_flags(const Record& record) {
  std::uint16_t flags = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == k_wnam) flags = 0xFFFFu;
    if (sub.signature == k_pnam) flags = Reader::u16(sub, 0);
  }
  return flags;
}

WorldspaceData inherited_world(const Environment& env, const RecordRef& root, std::uint16_t bit) {
  const auto* current = &root;
  std::set<FormKey> visited;
  for (;;) {
    if (!visited.insert(current->record->form_key).second) {
      throw WorldDecodeError("cyclic worldspace water inheritance");
    }
    auto world = decode_worldspace(env, *current);
    const auto flags = effective_parent_flags(*current->record);
    const auto* parent = winning_type(env, world.parent, k_wrld);
    if (parent == nullptr || (flags & bit) == 0) return world;
    current = parent;
  }
}

float loaded_water_height(float raw) {
  constexpr float limit = 2147483648.0f;
  if (raw >= limit) return raw;
  if (!std::isfinite(raw) || raw < -limit) return -limit;
  const auto integer = static_cast<std::int32_t>(raw);
  return static_cast<float>(integer) + (raw - static_cast<float>(integer) >= 0.5f ? 1.0f : 0.0f);
}

}

WaterSelection resolve_water(const Environment& env, const RecordRef& ref) {
  WaterSelection result;
  const RecordRef* world_ref = nullptr;
  std::optional<FormKey> water;
  if (ref.record && ref.record->header.signature == k_wrld) {
    world_ref = env.winner(ref.record->form_key);
  } else {
    const auto* winner = ref.record ? env.winner(ref.record->form_key) : nullptr;
    const auto cell = decode_cell(env, winner ? *winner : ref);
    const auto flags = cell.flags.value_or(0);
    if (cell.water && !cell.water->owner().empty()) {
      water = cell.water;
      if (winning_type(env, water, Signature{'W', 'A', 'T', 'R'}) == nullptr) {
        water.reset();
      }
    }
    if ((flags & 1u) == 0) world_ref = winning_type(env, cell.worldspace, k_wrld);
    if ((flags & 3u) == 2u) {
      const auto height = loaded_water_height(cell.water_height.value_or(std::numeric_limits<float>::max()));
      if (height < 2147483648.0f) result.height = height;
      else if (world_ref) result.height = inherited_world(env, *world_ref, 1).default_water_height.value_or(0.0f);
    }
  }
  if (world_ref) {
    if (!water) water = inherited_world(env, *world_ref, 8).water;
    if (ref.record->header.signature == k_wrld) {
      result.height = inherited_world(env, *world_ref, 1).default_water_height.value_or(0.0f);
    }
  }
  result.record = winning_type(env, water, Signature{'W', 'A', 'T', 'R'});
  if (result.record) result.water = result.record->record->form_key;
  else {
    result.uses_default_water = true;
    result.water = FormKey{PluginKey::from_filename(k_default_water_plugin), LocalFormId{k_default_water_local}};
    result.record = winning_type(env, result.water, Signature{'W', 'A', 'T', 'R'});
  }
  return result;
}

}  // namespace fo4::env
