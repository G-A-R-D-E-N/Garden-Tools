#include "fo4/environment/base_records.hpp"

#include "record_reader.hpp"

#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

constexpr std::uint16_t k_weather_eight_times_form_version = 111;
constexpr std::size_t k_light_required_size = 48;

std::optional<std::string_view> text(const Reader& reader, Signature signature) {
  const auto* sub = reader.unique(signature);
  if (sub == nullptr) return std::nullopt;
  const auto value = zstring_view(*sub);
  if (!value) reader.fail(signature, "is not a single NUL-terminated string");
  return value;
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
  return out;
}

WeatherData decode_weather(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("WTHR"));
  WeatherData out;
  if (const auto* sub = reader.unique(sig("NAM0"))) {
    const std::size_t times = ref.record->header.form_version >= k_weather_eight_times_form_version ? 8 : 4;
    const auto stride = times * 4;
    const auto groups = sub->data.size() / stride;
    if (sub->data.size() % stride != 0 || groups < 9) {
      reader.fail(sub->signature, "is not a whole number of color groups");
    }
    out.colors = WeatherColors{times, groups, sub->data};
  }
  if (const auto* sub = reader.unique(sig("FNAM"))) {
    if (sub->data.size() % 4 != 0 || sub->data.size() < 16) reader.fail(sub->signature, "is not a float array");
    for (std::size_t offset = 0; offset < sub->data.size(); offset += 4) {
      out.fog_distances.push_back(Reader::f32(*sub, offset));
    }
  }
  return out;
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

}  // namespace fo4::env
