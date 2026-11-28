#include "fo4/environment/world.hpp"

#include "default_water.hpp"
#include "record_reader.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature k_rvis{'R', 'V', 'I', 'S'};

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
  if (size >= tail + 24) lighting.near_height_mid = Reader::f32(*xcll, tail + 20);
  if (size >= tail + 28) lighting.near_height_range = Reader::f32(*xcll, tail + 24);
  if (size >= tail + 32) lighting.fog_color_high_near = Reader::color(*xcll, tail + 28);
  if (size >= tail + 36) lighting.fog_color_high_far = Reader::color(*xcll, tail + 32);
  if (size >= tail + 40) lighting.high_density_scale = Reader::f32(*xcll, tail + 36);
  if (size >= tail + 44) lighting.fog_near_scale = Reader::f32(*xcll, tail + 40);
  if (size >= tail + 48) lighting.fog_far_scale = Reader::f32(*xcll, tail + 44);
  if (size >= tail + 52) lighting.fog_high_near_scale = Reader::f32(*xcll, tail + 48);
  if (size >= tail + 56) lighting.fog_high_far_scale = Reader::f32(*xcll, tail + 52);
  if (size >= tail + 60) lighting.far_height_mid = Reader::f32(*xcll, tail + 56);
  if (size >= tail + 64) lighting.far_height_range = Reader::f32(*xcll, tail + 60);
  return lighting;
}

std::vector<FormKey> link_list(const Reader& reader, Signature signature) {
  std::vector<FormKey> links;
  const auto* subrecord = reader.unique(signature);
  if (subrecord == nullptr) return links;
  if (subrecord->data.size() % 4 != 0) reader.fail(signature, "must be a multiple of 4 bytes");
  for (std::size_t offset = 0; offset < subrecord->data.size(); offset += 4) {
    links.push_back(reader.link_at(*subrecord, offset));
  }
  return links;
}

std::optional<std::string_view> stored_string(const Reader& reader, Signature signature) {
  const auto* subrecord = reader.unique(signature);
  if (subrecord == nullptr) return std::nullopt;
  const auto value = zstring_view(*subrecord);
  if (!value) reader.fail(signature, "is not a single NUL-terminated string");
  return *value;
}

// Tolerant link: a duplicate or non-4-byte RVIS must decode as absent instead of throwing.
std::optional<FormKey> first_link(const Reader& reader, Signature signature) {
  const Subrecord* found = nullptr;
  for (const auto& sub : reader.ref().record->subrecords) {
    if (sub.signature != signature) continue;
    if (found != nullptr || sub.data.size() != 4) return std::nullopt;
    found = &sub;
  }
  if (found == nullptr) return std::nullopt;
  return reader.link_at(*found, 0);
}

void decode_cell_extras(const Reader& reader, CellData& data) {
  if (const auto* visi = reader.sized(sig_visi, 2)) data.previs_timestamp = Reader::u16(*visi, 0);
  if (const auto* pcmb = reader.sized(sig_pcmb, 2)) data.precombined_timestamp = Reader::u16(*pcmb, 0);
  data.previs_root = first_link(reader, k_rvis);
  data.regions = link_list(reader, sig_xclr);
  data.previs_references = link_list(reader, sig_xpri);
  data.acoustic_space = reader.link(sig_xcas);
  data.music_type = reader.link(sig_xcmo);
  data.sky_weather_region = reader.link(sig_xccm);
  data.god_rays = reader.link(sig_xgdr);
  data.lock_list = reader.link(sig_xill);
  if (const auto* xown = reader.sized(sig_xown, 12)) {
    data.owner = reader.link_at(*xown, 0);
  }
  if (const auto* xrnk = reader.sized(sig_xrnk, 4)) data.owner_rank = Reader::i32(*xrnk, 0);
  if (const auto* xilw = reader.sized(sig_xilw, 16)) {
    data.exterior_lod = ExteriorLod{
        reader.link_at(*xilw, 0),
        {Reader::f32(*xilw, 4), Reader::f32(*xilw, 8), Reader::f32(*xilw, 12)}};
  }
  if (const auto* xwcn = reader.sized(sig_xwcn, 4)) data.water_current_count = Reader::u32(*xwcn, 0);
  if (const auto* xwcu = reader.unique(sig_xwcu)) {
    if (xwcu->data.size() % 16 != 0) reader.fail(sig_xwcu, "must be a multiple of 16 bytes");
    for (std::size_t offset = 0; offset < xwcu->data.size(); offset += 16) {
      data.water_currents.push_back(WaterCurrent{
          {Reader::f32(*xwcu, offset), Reader::f32(*xwcu, offset + 4), Reader::f32(*xwcu, offset + 8)},
          Reader::f32(*xwcu, offset + 12)});
    }
  }
  data.water_environment_map = stored_string(reader, sig_xwem);
}

}  // namespace

WorldspaceData decode_worldspace(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig_wrld);
  WorldspaceData data;
  data.parent = reader.link(sig_wnam);
  if (const auto* pnam = reader.sized(sig_pnam, 2)) data.parent_flags = Reader::u16(*pnam, 0);
  data.climate = reader.link(sig_cnam);
  data.water = reader.link(sig_nam2);
  data.lighting_template = reader.link(sig_ltmp);
  if (const auto* dnam = reader.sized(sig_dnam, 8)) {
    data.default_land_height = Reader::f32(*dnam, 0);
    data.default_water_height = Reader::f32(*dnam, 4);
  }
  data.bounds_min = point2(reader, sig_nam0);
  data.bounds_max = point2(reader, sig_nam9);
  if (const auto* flags = reader.sized(sig_data, 1)) {
    data.flags = std::to_integer<std::uint8_t>(flags->data[0]);
  }
  data.lod_water = reader.link(sig_nam3);
  data.lod_water_height = reader.f32(sig_nam4);
  return data;
}

LightingTemplateData decode_lighting_template(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, Signature::from_string("LGTM"));
  LightingTemplateData data;
  data.lighting = decode_lighting(reader, sig_data, false);
  data.god_rays = reader.link(Signature::from_string("WGDR"));
  return data;
}

CellData decode_cell(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig_cell);
  CellData data;
  if (const auto* flags = reader.unique(sig_data)) {
    if (flags->data.size() == 1) {
      data.flags = std::to_integer<std::uint8_t>(flags->data[0]);
    } else if (flags->data.size() == 2) {
      data.flags = Reader::u16(*flags, 0);
    } else {
      reader.fail(sig_data, "must be 1 or 2 bytes");
    }
  }
  if (const auto* xclc = reader.unique(sig_xclc)) {
    if (xclc->data.size() != 8 && xclc->data.size() != 12) reader.fail(sig_xclc, "must be 8 or 12 bytes");
    CellGrid grid{Reader::i32(*xclc, 0), Reader::i32(*xclc, 4), std::nullopt};
    if (xclc->data.size() == 12) grid.land_flags = std::to_integer<std::uint8_t>(xclc->data[8]);
    data.grid = grid;
  }
  data.lighting = decode_lighting(reader, sig_xcll, true);
  data.lighting_template = reader.link(sig_ltmp);
  data.water_height = reader.f32(sig_xclw);
  data.image_space = reader.link(sig_xcim);
  data.encounter_zone = reader.link(sig_xezn);
  data.location = reader.link(sig_xlcn);
  data.worldspace = env.world_of(ref);
  data.water = reader.link(sig_xcwt);
  decode_cell_extras(reader, data);
  return data;
}

PrevisRoot decode_previs_root(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig_cell);
  PrevisRoot out;
  out.root = first_link(reader, k_rvis);
  // Constraint: first_link is empty only when RVIS is absent or malformed.
  if (!out.root) {
    for (const auto& sub : ref.record->subrecords) {
      if (sub.signature == k_rvis) out.malformed = true;
    }
  }
  return out;
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
    if (sub.signature == sig_wnam) flags = 0xFFFFu;
    if (sub.signature == sig_pnam) flags = Reader::u16(sub, 0);
  }
  return flags;
}

WorldspaceData inherited_world(const Environment& env, const RecordRef& root, std::uint16_t bit) {
  const auto* current = &root;
  std::set<FormKey> visited;
  for (;;) {
    if (!visited.insert(current->record->form_key).second) {
      throw WorldDecodeError("cyclic worldspace parent inheritance");
    }
    auto world = decode_worldspace(env, *current);
    const auto flags = effective_parent_flags(*current->record);
    const auto* parent = winning_type(env, world.parent, sig_wrld);
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
  if (ref.record && ref.record->header.signature == sig_wrld) {
    world_ref = winning_type(env, ref.record->form_key, sig_wrld);
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
    if ((flags & 1u) == 0) world_ref = winning_type(env, cell.worldspace, sig_wrld);
    if ((flags & 3u) == 2u) {
      const auto height = loaded_water_height(cell.water_height.value_or(std::numeric_limits<float>::max()));
      if (height < 2147483648.0f) result.height = height;
      else if (world_ref) result.height = inherited_world(env, *world_ref, 1).default_water_height.value_or(0.0f);
    }
  }
  if (world_ref) {
    if (!water) water = inherited_world(env, *world_ref, 8).water;
    if (ref.record->header.signature == sig_wrld) {
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

const RecordRef* resolve_climate(const Environment& env, const RecordRef& ref) {
  if (!ref.record || ref.record->header.signature != sig_wrld) {
    throw WorldDecodeError("resolve_climate requires a WRLD record");
  }
  const auto* world_ref = winning_type(env, ref.record->form_key, sig_wrld);
  if (world_ref == nullptr) return nullptr;
  return winning_type(env, inherited_world(env, *world_ref, 0x10).climate,
                      Signature{'C', 'L', 'M', 'T'});
}

std::vector<const RecordRef*> worldspace_map_markers(const Environment& env, const FormKey& world) {
  constexpr auto xmrk = Signature::from_string("XMRK");
  std::vector<const RecordRef*> cells = env.worldspace_cells(world);
  if (const auto* top = env.worldspace_top_cell(world)) cells.insert(cells.begin(), top);
  std::vector<const RecordRef*> markers;
  for (const auto* cell : cells) {
    for (const auto& child : env.cell_children(cell->record->form_key)) {
      const auto& record = *child.ref->record;
      if (record.header.signature != sig_refr) continue;
      const auto has_marker = std::any_of(record.subrecords.begin(), record.subrecords.end(),
                                          [&](const Subrecord& sub) { return sub.signature == xmrk; });
      if (has_marker) markers.push_back(child.ref);
    }
  }
  return markers;
}

}  // namespace fo4::env
