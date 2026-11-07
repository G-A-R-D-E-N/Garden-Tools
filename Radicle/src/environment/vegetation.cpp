#include "fo4/environment/vegetation.hpp"

#include "record_reader.hpp"

#include <algorithm>

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

void model(const Reader& r) {
  r.sized(sig("OBND"), 12);
  if (const auto* sub = r.unique(sig("MODL")); sub && !zstring_view(*sub)) r.fail(sub->signature, "invalid model path");
}

HarvestData harvest(const Reader& r) {
  HarvestData out;
  out.ingredient = r.link(sig("PFIG")); out.sound = r.link(sig("SNAM"));
  if (const auto* sub = r.sized(sig("PFPC"), 4)) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::to_integer<std::uint8_t>(sub->data[i]);
    out.seasons = bytes;
  }
  return out;
}

}

std::optional<GrassData> decode_grass(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("GRAS")); model(r);
  const auto* stored = r.unique(sig("DATA"));
  if (!stored) return std::nullopt;
  Subrecord prefix;
  prefix.data.resize(32);
  std::copy_n(stored->data.begin(), std::min(stored->data.size(), prefix.data.size()), prefix.data.begin());
  const auto* sub = &prefix;
  GrassData out;
  out.density = std::to_integer<std::uint8_t>(sub->data[0]);
  out.min_slope = std::to_integer<std::uint8_t>(sub->data[1]);
  out.max_slope = std::to_integer<std::uint8_t>(sub->data[2]);
  out.units_from_water = Reader::u16(*sub, 4); out.water_type = Reader::u32(*sub, 8);
  out.position_range = Reader::f32(*sub, 12); out.height_range = Reader::f32(*sub, 16);
  out.color_range = Reader::f32(*sub, 20); out.wave_period = Reader::f32(*sub, 24);
  out.flags = std::to_integer<std::uint8_t>(sub->data[28]);
  return out;
}

TreeData decode_tree(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("TREE")); model(r);
  TreeData out; out.harvest = harvest(r);
  if (const auto* sub = r.unique(sig("CNAM")); sub && sub->data.size() == 48) {
    std::array<float, 12> values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = Reader::f32(*sub, i * 4);
    out.wind = values;
  }
  return out;
}

FloraData decode_flora(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("FLOR")); model(r);
  FloraData out; out.harvest = harvest(r);
  if (const auto* sub = r.sized(sig("FNAM"), 2)) out.flags = Reader::u16(*sub, 0);
  return out;
}

std::vector<FormKey> decode_landscape_grasses(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("LTEX"));
  std::vector<FormKey> out;
  for (const auto& sub : ref.record->subrecords) {
    if (sub.signature != sig("GNAM")) continue;
    if (sub.data.size() != 4) r.fail(sub.signature, "grass link must be four bytes");
    out.push_back(r.link_at(sub, 0));
  }
  return out;
}

std::vector<FormKey> landscape_grasses_runtime_view(const Environment& env, const RecordRef& ref) {
  auto links = decode_landscape_grasses(env, ref);
  std::erase_if(links, [](const FormKey& key) { return key.owner().empty(); });
  return links;
}

}
