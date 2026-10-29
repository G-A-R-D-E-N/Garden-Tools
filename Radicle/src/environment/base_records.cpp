#include "fo4/environment/base_records.hpp"

#include "record_reader.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

constexpr std::size_t k_light_required_size = 48;
constexpr std::size_t k_distant_lod_slot = 260;
constexpr std::size_t k_water_visual_required_size = 188;
constexpr std::size_t k_water_silt_end = 200;
constexpr std::size_t k_water_visual_full_size = 201;
constexpr std::size_t k_weather_transition_offset = 3;

Record* find_record_to_edit(Plugin& plugin, const FormKey& key, const char (&type)[5]) {
  Record* record = nullptr;
  visit_records(plugin, [&](Record& candidate) {
    if (candidate.form_key == key) {
      if (record != nullptr) throw WorldDecodeError(std::string("duplicate ") + type + " record key");
      record = &candidate;
    }
  });
  if (record == nullptr) return nullptr;
  require_decodable(*record);
  if (record->header.signature != sig(type)) throw WorldDecodeError(std::string("expected a ") + type + " record");
  return record;
}

Subrecord& required_subrecord(Record& record, const char (&type)[5]) {
  Subrecord* slot = nullptr;
  for (auto& sub : record.subrecords) {
    if (sub.signature != sig(type)) continue;
    if (slot != nullptr) throw WorldDecodeError(std::string(type) + " is duplicated");
    slot = &sub;
  }
  if (slot == nullptr) throw WorldDecodeError(std::string(type) + " is missing");
  return *slot;
}

void mark_edited(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

std::uint32_t encode_link(const FormKey& key, const PluginFormIdContext& context) {
  try {
    return encode_plugin_form_id(key, context).value();
  } catch (const FormIdError& error) {
    throw WorldDecodeError(std::string("link encode failed: ") + error.what());
  }
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  std::array<std::byte, 4> bytes{};
  std::memcpy(bytes.data(), &value, 4);
  out.insert(out.end(), bytes.begin(), bytes.end());
}

std::optional<std::string_view> text(const Reader& reader, Signature signature) {
  const auto* sub = reader.unique(signature);
  if (sub == nullptr) return std::nullopt;
  const auto value = zstring_view(*sub);
  if (!value) reader.fail(signature, "is not a single NUL-terminated string");
  return value;
}

std::optional<std::array<float, 3>> vec3(const Reader& reader, Signature signature) {
  const auto* sub = reader.sized(signature, 12);
  if (sub == nullptr) return std::nullopt;
  return reader.vec3(*sub, 0);
}

}  // namespace

std::optional<ByteColor> WeatherColors::color(WeatherColorGroup group, std::size_t time) const noexcept {
  const auto index = static_cast<std::size_t>(group);
  if (index >= groups || time >= times) return std::nullopt;
  const auto offset = (index * times + time) * 4;
  return ByteColor{std::to_integer<std::uint8_t>(raw[offset]), std::to_integer<std::uint8_t>(raw[offset + 1]),
                   std::to_integer<std::uint8_t>(raw[offset + 2])};
}

LightRecordData decode_light(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("LIGH"));
  LightRecordData out;
  if (const auto* sub = reader.unique(sig("DATA"))) {
    const auto size = sub->data.size();
    if (size < k_light_required_size) reader.fail(sub->signature, "is shorter than its required members");
    if (size % 4 != 0 || size > 64) reader.fail(sub->signature, "ends inside a member or past Weight");
    LightData data;
    data.time = Reader::i32(*sub, 0);
    data.radius = Reader::u32(*sub, 4);
    data.color = Reader::color(*sub, 8);
    data.flags = Reader::u32(*sub, 12);
    data.falloff_exponent = Reader::f32(*sub, 16);
    data.fov = Reader::f32(*sub, 20);
    data.near_clip = Reader::f32(*sub, 24);
    data.flicker_period = Reader::f32(*sub, 28);
    data.flicker_intensity_amplitude = Reader::f32(*sub, 32);
    data.flicker_movement_amplitude = Reader::f32(*sub, 36);
    data.constant = Reader::f32(*sub, 40);
    data.scalar = Reader::f32(*sub, 44);
    if (size >= 52) data.exponent = Reader::f32(*sub, 48);
    if (size >= 56) data.god_rays_near_clip = Reader::f32(*sub, 52);
    if (size >= 60) data.value = Reader::u32(*sub, 56);
    if (size >= 64) data.weight = Reader::f32(*sub, 60);
    out.data = data;
  }
  out.fade = reader.f32(sig("FNAM"));
  out.gobo = text(reader, sig("NAM0"));
  out.lens = reader.link(sig("LNAM"));
  out.god_rays = reader.link(sig("WGDR"));
  return out;
}

BendableSplineData decode_bendable_spline(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("BNDS"));
  BendableSplineData out;
  if (const auto* sub = reader.unique(sig("DNAM"))) {
    const auto size = sub->data.size();
    if (size != 0 && size != 4 && size != 6 && size != 8 && size != 20 && size != 24 && size != 28) {
      reader.fail(sub->signature, "ends inside a member");
    }
    if (size >= 4) out.default_tiles = Reader::f32(*sub, 0);
    if (size >= 6) out.default_slices = Reader::u16(*sub, 4);
    if (size >= 8) out.tiles_relative_to_length = Reader::u16(*sub, 6) != 0;
    if (size >= 20) out.default_color = std::array<float, 3>{Reader::f32(*sub, 8), Reader::f32(*sub, 12),
                                                            Reader::f32(*sub, 16)};
    if (size >= 24) out.wind_sensibility = Reader::f32(*sub, 20);
    if (size >= 28) out.wind_flexibility = Reader::f32(*sub, 24);
  }
  out.texture = reader.link(sig("TNAM"));
  return out;
}

std::optional<FormKey> decode_layer_parent(const Environment& env, const RecordRef& ref) {
  return Reader(env, ref, sig("LAYR")).link(sig("PNAM"));
}

ClimateData decode_climate(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("CLMT"));
  ClimateData out;
  if (const auto* sub = reader.unique(sig("WLST"))) {
    if (sub->data.size() % 12 != 0) reader.fail(sub->signature, "must be a multiple of 12 bytes");
    for (std::size_t offset = 0; offset < sub->data.size(); offset += 12) {
      out.weathers.push_back({reader.link_at(*sub, offset), Reader::i32(*sub, offset + 4),
                              reader.link_at(*sub, offset + 8)});
    }
  }
  out.sun_texture = text(reader, sig("FNAM"));
  out.sun_glare_texture = text(reader, sig("GNAM"));
  if (const auto* sub = reader.sized(sig("TNAM"), 6)) {
    const auto byte = [&](std::size_t offset) { return std::to_integer<std::uint8_t>(sub->data[offset]); };
    out.timing = ClimateTiming{byte(0), byte(1), byte(2), byte(3), byte(4), byte(5)};
  }
  return out;
}

bool set_climate_timing(Plugin& plugin, const FormKey& key, const ClimateTiming& timing) {
  auto* record = find_record_to_edit(plugin, key, "CLMT");
  if (record == nullptr) return false;
  auto& slot = required_subrecord(*record, "TNAM");
  if (slot.data.size() != 6) throw WorldDecodeError("CLMT requires a six-byte TNAM");
  const std::array bytes{std::byte{timing.sunrise_begin}, std::byte{timing.sunrise_end},
                         std::byte{timing.sunset_begin}, std::byte{timing.sunset_end},
                         std::byte{timing.volatility}, std::byte{timing.moons_phase_length}};
  if (std::equal(bytes.begin(), bytes.end(), slot.data.begin())) return true;
  std::copy(bytes.begin(), bytes.end(), slot.data.begin());
  mark_edited(plugin, *record);
  return true;
}

bool set_climate_weathers(Plugin& plugin, const FormKey& key, std::span<const ClimateWeather> weathers) {
  auto* record = find_record_to_edit(plugin, key, "CLMT");
  if (record == nullptr) return false;
  const auto context = plugin_form_id_context(plugin);
  std::vector<std::byte> bytes;
  bytes.reserve(weathers.size() * 12);
  for (const auto& entry : weathers) {
    append_u32(bytes, encode_link(entry.weather, context));
    append_u32(bytes, static_cast<std::uint32_t>(entry.chance));
    append_u32(bytes, encode_link(entry.global, context));
  }
  const bool present = std::any_of(record->subrecords.begin(), record->subrecords.end(),
                                   [](const Subrecord& sub) { return sub.signature == sig("WLST"); });
  if (!present) {
    if (bytes.empty()) return true;
    Subrecord list;
    list.signature = sig("WLST");
    list.data = std::move(bytes);
    const auto at = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                 [](const Subrecord& sub) { return sub.signature != sig("EDID"); });
    record->subrecords.insert(at, std::move(list));
    mark_edited(plugin, *record);
    return true;
  }
  auto& list = required_subrecord(*record, "WLST");
  if (list.data.size() % 12 != 0) throw WorldDecodeError("CLMT WLST must be a multiple of 12 bytes");
  if (bytes == list.data) return true;
  list.data = std::move(bytes);
  mark_edited(plugin, *record);
  return true;
}

WeatherData decode_weather(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("WTHR"));
  WeatherData out;
  if (const auto* sub = reader.unique(sig("DATA"))) {
    if (sub->data.size() != forms::weather_data_size(ref.record->header.form_version)) {
      reader.fail(sub->signature, "does not match its form version");
    }
    const auto byte = [&](std::size_t offset) { return std::to_integer<std::uint8_t>(sub->data[offset]); };
    WeatherParameters p{byte(0), byte(3), byte(4), byte(5), byte(6), byte(7), byte(8), byte(9), byte(10),
                        byte(11), Reader::color(*sub, 12), byte(15), byte(16), byte(17), byte(18), std::nullopt};
    if (sub->data.size() == 20) p.wind_turbulence = byte(19);
    out.parameters = p;
  }
  if (const auto* sub = reader.unique(sig("NAM0"))) {
    const auto version = ref.record->header.form_version;
    if (sub->data.size() != forms::weather_nam0_size(version)) {
      reader.fail(sub->signature, "does not match its form version");
    }
    out.colors = WeatherColors{forms::weather_time_slots(version), forms::weather_color_groups(version), sub->data};
  }
  if (const auto* sub = reader.unique(sig("FNAM"))) {
    if (sub->data.size() != forms::weather_fnam_size(ref.record->header.form_version)) {
      reader.fail(sub->signature, "does not match its form version");
    }
    for (std::size_t offset = 0; offset < sub->data.size(); offset += 4) {
      out.fog_distances.push_back(Reader::f32(*sub, offset));
    }
  }
  return out;
}

bool set_weather_colors(Plugin& plugin, const FormKey& key, WeatherColorGroup group, std::span<const ByteColor> times) {
  auto* record = find_record_to_edit(plugin, key, "WTHR");
  if (record == nullptr) return false;
  const auto version = record->header.form_version;
  auto& nam0 = required_subrecord(*record, "NAM0");
  if (nam0.data.size() != forms::weather_nam0_size(version)) throw WorldDecodeError("NAM0 does not match its form version");
  const auto index = static_cast<std::size_t>(group);
  if (index >= forms::weather_color_groups(version)) throw WorldDecodeError("weather color group is not stored at this form version");
  const auto slots = forms::weather_time_slots(version);
  if (times.size() != slots) throw WorldDecodeError("NAM0 colour group needs one colour per time slot");
  bool changed = false;
  for (std::size_t time = 0; time < slots; ++time) {
    auto* out = nam0.data.data() + (index * slots + time) * 4;
    const std::array bytes{std::byte{times[time].red}, std::byte{times[time].green}, std::byte{times[time].blue}};
    if (std::equal(bytes.begin(), bytes.end(), out)) continue;
    std::copy(bytes.begin(), bytes.end(), out);
    changed = true;
  }
  if (changed) mark_edited(plugin, *record);
  return true;
}

bool set_weather_fog(Plugin& plugin, const FormKey& key, std::span<const float> distances) {
  auto* record = find_record_to_edit(plugin, key, "WTHR");
  if (record == nullptr) return false;
  auto& fnam = required_subrecord(*record, "FNAM");
  const auto size = forms::weather_fnam_size(record->header.form_version);
  if (fnam.data.size() != size || distances.size() * 4 != size) {
    throw WorldDecodeError("FNAM needs one float per stored fog distance");
  }
  std::vector<std::byte> bytes(size);
  std::memcpy(bytes.data(), distances.data(), size);
  if (bytes == fnam.data) return true;
  fnam.data = std::move(bytes);
  mark_edited(plugin, *record);
  return true;
}

bool set_weather_transition(Plugin& plugin, const FormKey& key, std::uint8_t transition_delta) {
  auto* record = find_record_to_edit(plugin, key, "WTHR");
  if (record == nullptr) return false;
  auto& data = required_subrecord(*record, "DATA");
  if (data.data.size() != forms::weather_data_size(record->header.form_version)) {
    throw WorldDecodeError("DATA does not match its form version");
  }
  auto& slot = data.data[k_weather_transition_offset];
  if (slot == std::byte{transition_delta}) return true;
  slot = std::byte{transition_delta};
  mark_edited(plugin, *record);
  return true;
}

std::vector<StaticCollectionPart> decode_static_collection(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("SCOL"));
  std::vector<StaticCollectionPart> parts;
  bool open = false;
  for (const auto& sub : ref.record->subrecords) {
    if (sub.signature == sig("ONAM")) {
      if (sub.data.size() != 4) reader.fail(sub.signature, "must be 4 bytes");
      parts.push_back({reader.link_at(sub, 0), {}});
      open = true;
    } else if (sub.signature == sig("DATA")) {
      if (!open) reader.fail(sub.signature, "does not follow an ONAM");
      if (sub.data.size() % 28 != 0) reader.fail(sub.signature, "must be a multiple of 28 bytes");
      for (std::size_t offset = 0; offset < sub.data.size(); offset += 28) {
        parts.back().placements.push_back({reader.vec3(sub, offset), reader.vec3(sub, offset + 12),
                                           Reader::f32(sub, offset + 24)});
      }
      open = false;
    }
  }
  return parts;
}

StaticDistantLod decode_static_distant_lod(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("STAT"));
  StaticDistantLod out;
  const auto* sub = reader.unique(sig("MNAM"));
  if (sub == nullptr) return out;
  const auto size = sub->data.size();
  if (size % k_distant_lod_slot != 0 || size > k_distant_lod_slot * k_distant_lod_levels) {
    reader.fail(sub->signature, "must be up to four 260-byte mesh slots");
  }
  out.levels = size / k_distant_lod_slot;
  for (std::size_t level = 0; level < out.levels; ++level) {
    const auto* begin = reinterpret_cast<const char*>(sub->data.data()) + level * k_distant_lod_slot;
    const auto* end = static_cast<const char*>(std::memchr(begin, 0, k_distant_lod_slot));
    if (end == nullptr) reader.fail(sub->signature, "has a mesh slot without a NUL terminator");
    if (end != begin) out.meshes[level] = std::string_view{begin, static_cast<std::size_t>(end - begin)};
  }
  return out;
}

std::optional<FormKey> decode_activator_water_type(const Environment& env, const RecordRef& ref) {
  return Reader(env, ref, sig("ACTI")).link(sig("WNAM"));
}

float transition_hours(std::uint8_t transition_delta, float min_hours, float max_hours) {
  return static_cast<float>(transition_delta) / 255.0f * (max_hours - min_hours) + min_hours;
}

WaterData decode_water(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("WATR"));
  WaterData out;
  if (const auto* sub = reader.sized(sig("FNAM"), 1)) out.flags = std::to_integer<std::uint8_t>(sub->data[0]);
  if (const auto* sub = reader.unique(sig("DNAM"))) {
    const auto size = sub->data.size();
    if (size < k_water_visual_required_size) reader.fail(sub->signature, "is shorter than its required members");
    if (size > k_water_visual_full_size) reader.fail(sub->signature, "ends past Screen Space Reflections");
    WaterVisual v;
    v.fog_depth_amount = Reader::f32(*sub, 0);
    v.fog_shallow_color = Reader::color(*sub, 4);
    v.fog_deep_color = Reader::color(*sub, 8);
    v.fog_color_shallow_range = Reader::f32(*sub, 12);
    v.fog_color_deep_range = Reader::f32(*sub, 16);
    v.fog_shallow_alpha = Reader::f32(*sub, 20);
    v.fog_deep_alpha = Reader::f32(*sub, 24);
    v.fog_alpha_shallow_range = Reader::f32(*sub, 28);
    v.fog_alpha_deep_range = Reader::f32(*sub, 32);
    v.fog_underwater_color = Reader::color(*sub, 36);
    v.fog_underwater_amount = Reader::f32(*sub, 40);
    v.fog_underwater_near = Reader::f32(*sub, 44);
    v.fog_underwater_far = Reader::f32(*sub, 48);
    v.normal_magnitude = Reader::f32(*sub, 52);
    v.shallow_normal_falloff = Reader::f32(*sub, 56);
    v.deep_normal_falloff = Reader::f32(*sub, 60);
    v.reflectivity_amount = Reader::f32(*sub, 64);
    v.fresnel_amount = Reader::f32(*sub, 68);
    v.surface_effect_falloff = Reader::f32(*sub, 72);
    v.displacement_force = Reader::f32(*sub, 76);
    v.displacement_velocity = Reader::f32(*sub, 80);
    v.displacement_falloff = Reader::f32(*sub, 84);
    v.displacement_dampener = Reader::f32(*sub, 88);
    v.displacement_starting_size = Reader::f32(*sub, 92);
    v.reflection_color = Reader::color(*sub, 96);
    v.sun_specular_power = Reader::f32(*sub, 100);
    v.sun_specular_magnitude = Reader::f32(*sub, 104);
    v.sun_sparkle_power = Reader::f32(*sub, 108);
    v.sun_sparkle_magnitude = Reader::f32(*sub, 112);
    v.interior_specular_radius = Reader::f32(*sub, 116);
    v.interior_specular_brightness = Reader::f32(*sub, 120);
    v.interior_specular_power = Reader::f32(*sub, 124);
    for (std::size_t layer = 0; layer < 3; ++layer) {
      auto& n = v.noise[layer];
      n.wind_direction = Reader::f32(*sub, 128 + layer * 4);
      n.wind_speed = Reader::f32(*sub, 140 + layer * 4);
      n.amplitude_scale = Reader::f32(*sub, 152 + layer * 4);
      n.uv_scale = Reader::f32(*sub, 164 + layer * 4);
      n.noise_falloff = Reader::f32(*sub, 176 + layer * 4);
    }
    if (size >= k_water_silt_end) {
      v.silt_amount = Reader::f32(*sub, 188);
      v.silt_light_color = Reader::color(*sub, 192);
      v.silt_dark_color = Reader::color(*sub, 196);
    }
    if (size >= k_water_visual_full_size) v.screen_space_reflections = sub->data[200] != std::byte{0};
    out.visual = v;
  }
  out.noise_textures = {text(reader, sig("NAM2")), text(reader, sig("NAM3")), text(reader, sig("NAM4"))};
  out.linear_velocity = vec3(reader, sig("NAM0"));
  out.angular_velocity = vec3(reader, sig("NAM1"));
  return out;
}

}  // namespace fo4::env
