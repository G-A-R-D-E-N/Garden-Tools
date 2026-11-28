#include "fo4/environment/region.hpp"

#include "record_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr std::size_t k_object_stride = 52;
constexpr std::size_t k_grass_stride = 8;
constexpr std::size_t k_weather_stride = 12;
constexpr std::size_t k_sound_stride = 12;

std::uint8_t byte_at(const Subrecord& subrecord, std::size_t offset) {
  return std::to_integer<std::uint8_t>(subrecord.data[offset]);
}

bool is_entry_payload(Signature signature) {
  return signature == sig("RDMO") || signature == sig("RDMP") || signature == sig("RDOT") ||
         signature == sig("RDGS") || signature == sig("RDWT") || signature == sig("RDSA");
}

bool payload_matches_type(std::uint32_t type, Signature signature) {
  switch (type) {
    case 2:
      return signature == sig("RDOT");
    case 3:
      return signature == sig("RDWT");
    case 4:
      return signature == sig("RDMP");
    case 6:
      return signature == sig("RDGS");
    case 7:
      return signature == sig("RDMO") || signature == sig("RDSA");
    default:
      return false;
  }
}

void require_stride(const Reader& reader, const Subrecord& subrecord, std::size_t stride) {
  if (subrecord.data.size() % stride != 0) {
    reader.fail(subrecord.signature, "must be a multiple of " + std::to_string(stride) + " bytes");
  }
}

RecordName stored_name(const Environment& env, const RecordRef& ref, const Reader& reader,
                       const Subrecord& subrecord) {
  if (env.plugins().at(ref.plugin).localized) {
    if (subrecord.data.size() != 4) reader.fail(subrecord.signature, "string ID must be four bytes");
    return {NameKind::string_id, {}, Reader::u32(subrecord, 0)};
  }
  const auto text = zstring_view(subrecord);
  if (!text) reader.fail(subrecord.signature, "invalid inline text");
  return {NameKind::inline_text, *text, 0};
}

RegionArea read_area(const Reader& reader, const Subrecord& subrecord) {
  if (subrecord.data.size() != 4) reader.fail(subrecord.signature, "edge fall-off must be four bytes");
  return RegionArea{Reader::u32(subrecord, 0), {}, std::nullopt};
}

void read_points(const Reader& reader, const Subrecord& subrecord, RegionArea& area) {
  require_stride(reader, subrecord, 8);
  for (std::size_t offset = 0; offset < subrecord.data.size(); offset += 8) {
    area.points.push_back(
        RegionPoint{Reader::f32(subrecord, offset), Reader::f32(subrecord, offset + 4)});
  }
}

RegionDataEntry read_entry_header(const Reader& reader, const Subrecord& subrecord) {
  if (subrecord.data.size() != 8) reader.fail(subrecord.signature, "data header must be eight bytes");
  RegionDataEntry entry;
  entry.type = Reader::u32(subrecord, 0);
  entry.override_value = byte_at(subrecord, 4);
  entry.priority = byte_at(subrecord, 5);
  return entry;
}

void read_objects(const Reader& reader, const Subrecord& subrecord, RegionDataEntry& entry) {
  require_stride(reader, subrecord, k_object_stride);
  for (std::size_t base = 0; base < subrecord.data.size(); base += k_object_stride) {
    RegionObject object;
    object.object = reader.link_at(subrecord, base);
    object.parent_index = Reader::u16(subrecord, base + 4);
    object.density = Reader::f32(subrecord, base + 8);
    object.clustering = byte_at(subrecord, base + 12);
    object.min_slope = byte_at(subrecord, base + 13);
    object.max_slope = byte_at(subrecord, base + 14);
    object.flags = byte_at(subrecord, base + 15);
    object.radius_with_parent = Reader::u16(subrecord, base + 16);
    object.radius = Reader::u16(subrecord, base + 18);
    object.min_height = Reader::f32(subrecord, base + 20);
    object.max_height = Reader::f32(subrecord, base + 24);
    object.sink = Reader::f32(subrecord, base + 28);
    object.sink_variance = Reader::f32(subrecord, base + 32);
    object.size_variance = Reader::f32(subrecord, base + 36);
    object.angle_variance = {Reader::u16(subrecord, base + 40), Reader::u16(subrecord, base + 42),
                             Reader::u16(subrecord, base + 44)};
    entry.objects.push_back(object);
  }
}

void read_grasses(const Reader& reader, const Subrecord& subrecord, RegionDataEntry& entry) {
  require_stride(reader, subrecord, k_grass_stride);
  for (std::size_t base = 0; base < subrecord.data.size(); base += k_grass_stride) {
    entry.grasses.push_back(reader.link_at(subrecord, base));
  }
}

void read_weathers(const Reader& reader, const Subrecord& subrecord, RegionDataEntry& entry) {
  require_stride(reader, subrecord, k_weather_stride);
  for (std::size_t base = 0; base < subrecord.data.size(); base += k_weather_stride) {
    RegionWeather weather;
    weather.weather = reader.link_at(subrecord, base);
    weather.chance = Reader::u32(subrecord, base + 4);
    if (Reader::u32(subrecord, base + 8) != 0) weather.global = reader.link_at(subrecord, base + 8);
    entry.weathers.push_back(weather);
  }
}

void read_sounds(const Reader& reader, const Subrecord& subrecord, RegionDataEntry& entry) {
  require_stride(reader, subrecord, k_sound_stride);
  for (std::size_t base = 0; base < subrecord.data.size(); base += k_sound_stride) {
    entry.sounds.push_back(RegionSound{reader.link_at(subrecord, base),
                                       Reader::u32(subrecord, base + 4),
                                       Reader::f32(subrecord, base + 8)});
  }
}

bool on_segment(const RegionPoint& a, const RegionPoint& b, double x, double y) {
  const double cross = (static_cast<double>(b.x) - a.x) * (y - a.y) - (static_cast<double>(b.y) - a.y) * (x - a.x);
  return cross == 0.0 && x >= std::min(a.x, b.x) && x <= std::max(a.x, b.x) &&
         y >= std::min(a.y, b.y) && y <= std::max(a.y, b.y);
}

bool area_contains(const RegionArea& area, double x, double y) {
  const auto& pts = area.points;
  if (pts.size() < 3) return false;
  bool inside = false;
  for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
    if (on_segment(pts[j], pts[i], x, y)) return true;
    if ((pts[i].y > y) != (pts[j].y > y) &&
        x < (static_cast<double>(pts[j].x) - pts[i].x) * (y - pts[i].y) / (static_cast<double>(pts[j].y) - pts[i].y) + pts[i].x) {
      inside = !inside;
    }
  }
  return inside;
}

int weather_priority(const RegionData& data) {
  for (const auto& entry : data.entries) {
    if (entry.type == 3) return entry.priority;
  }
  return -1;
}

}  // namespace

RegionData decode_region(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("REGN"));
  RegionData data;
  if (const auto* rclr = reader.sized(sig("RCLR"), 4)) data.map_color = Reader::color(*rclr, 0);
  data.worldspace = reader.link(sig("WNAM"));

  RegionArea* area = nullptr;
  RegionDataEntry* entry = nullptr;
  for (const auto& sub : ref.record->subrecords) {
    if (sub.signature == sig("RPLI")) {
      data.areas.push_back(read_area(reader, sub));
      area = &data.areas.back();
      entry = nullptr;
      continue;
    }
    if (sub.signature == sig("RPLD")) {
      if (area == nullptr) reader.fail(sub.signature, "appears before any RPLI edge fall-off");
      read_points(reader, sub, *area);
      continue;
    }
    if (sub.signature == sig("RDAT")) {
      data.entries.push_back(read_entry_header(reader, sub));
      entry = &data.entries.back();
      continue;
    }
    if (sub.signature == sig("RLDM")) {
      if (entry == nullptr) reader.fail(sub.signature, "appears outside an open RDAT data entry");
      if (sub.data.size() != 4) reader.fail(sub.signature, "must be four bytes");
      entry->lod_display_distance_multiplier = Reader::f32(sub, 0);
      continue;
    }
    if (sub.signature == sig("ANAM")) {
      if (sub.data.size() != 4) reader.fail(sub.signature, "must be four bytes");
      if (entry != nullptr) {
        entry->occlusion_accuracy_dist = Reader::f32(sub, 0);
      } else if (area != nullptr) {
        area->unknown_anam = Reader::f32(sub, 0);
      } else {
        reader.fail(sub.signature, "appears before any RPLI edge fall-off");
      }
      continue;
    }
    if (!is_entry_payload(sub.signature)) continue;
    if (entry == nullptr) reader.fail(sub.signature, "appears outside an open RDAT data entry");
    if (!payload_matches_type(entry->type, sub.signature)) {
      reader.fail(sub.signature,
                  "does not belong to region data type " + std::to_string(entry->type));
    }
    if (sub.signature == sig("RDMO")) {
      if (sub.data.size() != 4) reader.fail(sub.signature, "music link must be four bytes");
      entry->music = reader.link_at(sub, 0);
    } else if (sub.signature == sig("RDMP")) {
      entry->map_name = stored_name(env, ref, reader, sub);
    } else if (sub.signature == sig("RDOT")) {
      read_objects(reader, sub, *entry);
    } else if (sub.signature == sig("RDGS")) {
      read_grasses(reader, sub, *entry);
    } else if (sub.signature == sig("RDWT")) {
      read_weathers(reader, sub, *entry);
    } else if (sub.signature == sig("RDSA")) {
      read_sounds(reader, sub, *entry);
    }
  }
  return data;
}

std::vector<FormKey> regions_at(const Environment& env, const FormKey& world, float x, float y) {
  std::vector<std::pair<int, FormKey>> hits;
  for (const auto* ref : env.winners_of(sig("REGN"))) {
    const auto data = decode_region(env, *ref);
    if (data.worldspace != world) continue;
    const bool inside = std::any_of(data.areas.begin(), data.areas.end(),
                                    [&](const RegionArea& area) { return area_contains(area, x, y); });
    if (inside) hits.emplace_back(weather_priority(data), env.metadata(*ref).form_key);
  }
  std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<FormKey> keys;
  for (const auto& hit : hits) keys.push_back(hit.second);
  return keys;
}

}  // namespace fo4::env
