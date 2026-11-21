#include "fo4/previs/shared_geom_extra.hpp"

#include "fo4/binary/cursor.hpp"

#include <cmath>
#include <cstring>
#include <utility>

namespace fo4::previs {
namespace {

constexpr std::size_t k_ni_extra_name_bytes = 4;
constexpr std::size_t k_shared_header_bytes = 28;
constexpr std::size_t k_object_lead = 8;
constexpr std::size_t k_object_core = 4 + 28;
constexpr std::size_t k_object_suffix = 8;
constexpr std::size_t k_combined_record_bytes = 72;
// Constraint: combined record, measured on the vanilla oracles: f32 grayscale-to-palette scale, row-major 3x3 rotation, translation, scale, then the instance's bounding sphere (centre, radius).
constexpr std::size_t k_record_rotation = 4;
constexpr std::size_t k_record_bound = 56;

std::uint32_t read_u32(std::span<const std::byte> block, std::size_t offset) {
  binary::Cursor cursor(block.subspan(offset));
  return cursor.read_u32();
}

void write_f32(std::span<std::byte> block, std::size_t offset, float value) {
  std::uint32_t bits{};
  static_assert(sizeof(float) == 4);
  std::memcpy(&bits, &value, sizeof(value));
  block[offset] = static_cast<std::byte>(bits & 0xFFu);
  block[offset + 1] = static_cast<std::byte>((bits >> 8) & 0xFFu);
  block[offset + 2] = static_cast<std::byte>((bits >> 16) & 0xFFu);
  block[offset + 3] = static_cast<std::byte>((bits >> 24) & 0xFFu);
}

void put_u32_at(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

void put_f32_at(std::vector<std::byte>& out, std::size_t at, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(value));
  put_u32_at(out, at, bits);
}

std::size_t compact_record_size(std::size_t combined_count) {
  return k_object_lead + k_object_core + sizeof(std::uint32_t) + k_combined_record_bytes * combined_count +
         k_object_suffix;
}

std::size_t table_record_size(std::size_t combined_count) {
  return k_object_core + sizeof(std::uint32_t) + k_combined_record_bytes * combined_count + k_object_suffix;
}

bool walk_records(std::span<const std::byte> block, std::size_t object_count, std::size_t pos, bool compact_lead);

bool is_compact_layout(std::span<const std::byte> block, std::size_t object_count) {
  return walk_records(block, object_count, k_shared_header_bytes, true);
}

bool is_table_layout(std::span<const std::byte> block, std::size_t object_count) {
  return walk_records(block, object_count, k_shared_header_bytes + 8 * object_count, false);
}

bool walk_records(std::span<const std::byte> block, std::size_t object_count, std::size_t pos, bool compact_lead) {
  const auto lead = compact_lead ? k_object_lead : 0;
  for (std::size_t object = 0; object < object_count; ++object) {
    if (pos + lead + k_object_core + sizeof(std::uint32_t) > block.size()) return false;
    pos += lead + k_object_core;
    const auto combined = static_cast<std::size_t>(read_u32(block, pos));
    pos += sizeof(std::uint32_t);
    // Constraint: a block ships up to 630 instances on one object (measured), so the count is bounded by the bytes left.
    if (combined == 0 || combined > (block.size() - pos) / k_combined_record_bytes) return false;
    pos += k_combined_record_bytes * combined + k_object_suffix;
    if (pos > block.size()) return false;
  }
  return pos == block.size();
}

std::optional<SharedGeomExtraLayout> detect_layout(std::span<const std::byte> block) {
  if (block.size() < k_shared_header_bytes) return std::nullopt;
  const auto object_count = static_cast<std::size_t>(read_u32(block, 24));
  if (object_count == 0) return std::nullopt;
  const bool compact = is_compact_layout(block, object_count);
  const bool table = !compact && is_table_layout(block, object_count);
  if (!compact && !table) return std::nullopt;
  const auto lead = compact ? k_object_lead : 0;
  std::size_t pos = compact ? k_shared_header_bytes : k_shared_header_bytes + 8 * object_count;
  if (pos + lead + k_object_core + sizeof(std::uint32_t) > block.size()) return std::nullopt;
  pos += lead + k_object_core;
  const auto combined = static_cast<std::size_t>(read_u32(block, pos));
  if (combined == 0 || combined > (block.size() - pos) / k_combined_record_bytes) return std::nullopt;
  return SharedGeomExtraLayout{.object_count = object_count, .combined_per_object = combined};
}

struct ResolvedGeom {
  std::size_t prefix{0};
  std::span<const std::byte> body;
};

bool header_walkable(std::span<const std::byte> body) {
  if (body.size() < k_shared_header_bytes) return false;
  const auto object_count = static_cast<std::size_t>(read_u32(body, 24));
  if (object_count == 0 || object_count > k_shared_geom_max_objects) return false;
  return is_compact_layout(body, object_count) || is_table_layout(body, object_count);
}

ResolvedGeom resolve_geom(std::span<const std::byte> block) {
  if (block.size() > k_ni_extra_name_bytes) {
    const auto tail = block.subspan(k_ni_extra_name_bytes);
    if (header_walkable(tail)) return {.prefix = k_ni_extra_name_bytes, .body = tail};
  }
  if (header_walkable(block)) return {.prefix = 0, .body = block};
  return {.prefix = 0, .body = block};
}

std::optional<std::size_t> combined_record_offset_body(std::span<const std::byte> block,
                                                         std::size_t object_index,
                                                         std::size_t combined_index) {
  if (block.size() < k_shared_header_bytes) return std::nullopt;
  const auto object_count = static_cast<std::size_t>(read_u32(block, 24));
  if (object_index >= object_count) return std::nullopt;
  const bool compact = walk_records(block, object_count, k_shared_header_bytes, true);
  const bool table =
      !compact && walk_records(block, object_count, k_shared_header_bytes + 8 * object_count, false);
  if (!compact && !table) return std::nullopt;
  const auto lead = compact ? k_object_lead : 0;
  std::size_t pos = compact ? k_shared_header_bytes : k_shared_header_bytes + 8 * object_count;
  for (std::size_t object = 0; object < object_count; ++object) {
    if (pos + lead + k_object_core + sizeof(std::uint32_t) > block.size()) return std::nullopt;
    pos += lead + k_object_core;
    const auto combined_count = static_cast<std::size_t>(read_u32(block, pos));
    pos += sizeof(std::uint32_t);
    if (object == object_index) {
      if (combined_index >= combined_count) return std::nullopt;
      return pos + k_combined_record_bytes * combined_index;
    }
    pos += k_combined_record_bytes * combined_count + k_object_suffix;
  }
  return std::nullopt;
}

}

std::optional<SharedGeomExtraLayout> detect_shared_geom_layout(std::span<const std::byte> block) {
  return detect_layout(resolve_geom(block).body);
}

std::size_t shared_combined_geom_extra_size(std::size_t object_count, std::size_t combined_per_object) {
  if (object_count == 0 || combined_per_object == 0) return 0;
  return k_shared_header_bytes + object_count * compact_record_size(combined_per_object);
}

bool measure_shared_combined_geom_extra(std::span<const std::byte> block, SharedGeomExtraLayout& out) {
  out = {};
  block = resolve_geom(block).body;
  if (block.size() < k_shared_header_bytes) return false;
  const auto object_count = static_cast<std::size_t>(read_u32(block, 24));
  if (object_count == 0) return false;
  const auto payload = block.size() - k_shared_header_bytes;
  for (std::size_t combined = 1; combined < 32; ++combined) {
    const auto per_object = compact_record_size(combined);
    if (payload == object_count * per_object && is_compact_layout(block, object_count)) {
      out.object_count = object_count;
      out.combined_per_object = combined;
      return true;
    }
    const auto table_payload = 8 * object_count + object_count * table_record_size(combined);
    if (payload == table_payload && is_table_layout(block, object_count)) {
      out.object_count = object_count;
      out.combined_per_object = combined;
      return true;
    }
  }
  return false;
}

std::size_t shared_combined_object_slots(std::span<const std::byte> block) {
  const auto body = resolve_geom(block).body;
  if (body.size() < k_shared_header_bytes) return 0;
  return static_cast<std::size_t>(read_u32(body, 24));
}

std::optional<std::size_t> shared_combined_record_offset(std::span<const std::byte> block,
                                                           std::size_t object_index,
                                                           std::size_t combined_index) {
  return combined_record_offset_body(resolve_geom(block).body, object_index, combined_index);
}

bool read_shared_combined_record_xform(std::span<const std::byte> block,
                                       std::size_t object_index,
                                       std::size_t combined_index,
                                       SharedCombinedRecordXform& out) {
  const auto resolved = resolve_geom(block);
  const auto offset = combined_record_offset_body(resolved.body, object_index, combined_index);
  if (!offset.has_value() || *offset + k_combined_record_bytes > resolved.body.size()) return false;
  binary::Cursor cursor(resolved.body.subspan(*offset + k_record_rotation));
  for (float& value : out.rotation) value = cursor.read_f32();
  for (float& value : out.translation) value = cursor.read_f32();
  out.scale = cursor.read_f32();
  // Constraint: the stored sphere sits after the transform, so a caller reading a record must see the bound the writer placed there instead of a zeroed default.
  for (float& value : out.bound_center) value = cursor.read_f32();
  out.bound_radius = cursor.read_f32();
  return true;
}

bool write_shared_combined_record_xform(std::span<std::byte> block,
                                        std::size_t object_index,
                                        std::size_t combined_index,
                                        const SharedCombinedRecordXform& xform) {
  const auto resolved = resolve_geom(std::span<const std::byte>(block));
  auto body = block.subspan(resolved.prefix);
  const auto offset = combined_record_offset_body(resolved.body, object_index, combined_index);
  if (!offset.has_value() || *offset + k_combined_record_bytes > body.size()) return false;
  // Constraint: the stored sphere is the mesh's local sphere under the record's own transform, so the local sphere is recovered from the old transform and re-placed under the new one: centre = T + s * R * local.
  SharedCombinedRecordXform old{};
  float old_centre[3]{};
  {
    binary::Cursor cursor(std::span<const std::byte>(body).subspan(*offset + k_record_rotation));
    for (float& value : old.rotation) value = cursor.read_f32();
    for (float& value : old.translation) value = cursor.read_f32();
    old.scale = cursor.read_f32();
    for (float& value : old_centre) value = cursor.read_f32();
  }
  float old_radius{};
  {
    binary::Cursor cursor(std::span<const std::byte>(body).subspan(*offset + k_record_bound + 12));
    old_radius = cursor.read_f32();
  }
  const bool rescale = old.scale != 0.0f && std::isfinite(old.scale);
  float local[3]{};
  if (rescale) {
    float delta[3]{};
    for (std::size_t i = 0; i < 3; ++i) delta[i] = (old_centre[i] - old.translation[i]) / old.scale;
    // Constraint: the rotation is orthonormal, so its inverse is its transpose.
    for (std::size_t i = 0; i < 3; ++i) {
      local[i] = old.rotation[i] * delta[0] + old.rotation[3 + i] * delta[1] + old.rotation[6 + i] * delta[2];
    }
  }

  std::size_t pos = *offset + k_record_rotation;
  for (float value : xform.rotation) {
    write_f32(body, pos, value);
    pos += 4;
  }
  for (float value : xform.translation) {
    write_f32(body, pos, value);
    pos += 4;
  }
  write_f32(body, pos, xform.scale);
  if (rescale) {
    pos = *offset + k_record_bound;
    for (std::size_t i = 0; i < 3; ++i) {
      const float rotated = xform.rotation[3 * i] * local[0] + xform.rotation[3 * i + 1] * local[1] +
                            xform.rotation[3 * i + 2] * local[2];
      write_f32(body, pos, xform.translation[i] + xform.scale * rotated);
      pos += 4;
    }
    write_f32(body, pos, old_radius / old.scale * xform.scale);
  }
  return true;
}

std::optional<SharedGeomHeader> read_shared_geom_header(std::span<const std::byte> block) {
  const auto body = resolve_geom(block).body;
  if (body.size() < k_shared_header_bytes) return std::nullopt;
  const auto object_count = static_cast<std::size_t>(read_u32(body, 24));
  if (object_count == 0 || !is_table_layout(body, object_count)) return std::nullopt;
  SharedGeomHeader out;
  out.vertex_desc = std::uint64_t{read_u32(body, 0)} | (std::uint64_t{read_u32(body, 4)} << 32);
  out.vertex_count = read_u32(body, 8);
  out.triangle_count = read_u32(body, 12);
  out.objects.resize(object_count);
  std::size_t pos = k_shared_header_bytes + 8 * object_count;
  for (std::size_t index = 0; index < object_count; ++index) {
    auto& object = out.objects[index];
    object.file_hash = read_u32(body, k_shared_header_bytes + 8 * index);
    object.data_offset = read_u32(body, k_shared_header_bytes + 8 * index + 4);
    object.vertex_count = read_u32(body, pos);
    object.lod_levels = read_u32(body, pos + 4);
    for (std::size_t lod = 0; lod < 3; ++lod) {
      object.triangle_count[lod] = read_u32(body, pos + 8 + 8 * lod);
      object.triangle_offset[lod] = read_u32(body, pos + 12 + 8 * lod);
    }
    pos += k_object_core;
    object.combined_count = read_u32(body, pos);
    pos += sizeof(std::uint32_t) + k_combined_record_bytes * object.combined_count;
    object.vertex_desc = std::uint64_t{read_u32(body, pos)} | (std::uint64_t{read_u32(body, pos + 4)} << 32);
    pos += k_object_suffix;
  }
  return out;
}

bool walk_shared_combined_geom_extra(std::span<const std::byte> block) {
  block = resolve_geom(block).body;
  if (block.size() < k_shared_header_bytes) return false;
  const auto object_count = static_cast<std::size_t>(read_u32(block, 24));
  if (object_count == 0) return false;
  if (walk_records(block, object_count, k_shared_header_bytes, true)) return true;
  const auto with_table = k_shared_header_bytes + 8 * object_count;
  return walk_records(block, object_count, with_table, false);
}

std::optional<std::vector<std::byte>> rebuild_compact_shared_geom_extra(
    std::span<const std::byte> template_block,
    std::size_t object_count,
    std::span<const SharedCombinedRecordXform> xforms) {
  if (object_count == 0 || object_count > k_shared_geom_max_objects || xforms.size() != object_count) return std::nullopt;
  const auto resolved = resolve_geom(template_block);
  const auto layout = detect_layout(resolved.body);
  if (!layout.has_value() || layout->combined_per_object != 1) return std::nullopt;
  const auto& body = resolved.body;
  const bool compact = is_compact_layout(body, layout->object_count);
  const bool table = !compact && is_table_layout(body, layout->object_count);
  if (!compact && !table) return std::nullopt;
  const std::size_t lead = compact ? k_object_lead : 0;
  const std::size_t object_bytes = lead + table_record_size(1);
  std::size_t pos = k_shared_header_bytes + (compact ? 0 : 8 * layout->object_count);
  std::vector<std::size_t> object_begin;
  for (std::size_t object = 0; object < layout->object_count; ++object) {
    object_begin.push_back(pos);
    if (read_u32(body, pos + lead + k_object_core) != 1) return std::nullopt;
    pos += object_bytes;
  }

  std::vector<std::byte> rebuilt(resolved.prefix + shared_combined_geom_extra_size(object_count, 1));
  for (std::size_t i = 0; i < k_shared_header_bytes; ++i) rebuilt[resolved.prefix + i] = body[i];
  std::size_t table_out = resolved.prefix + k_shared_header_bytes;
  std::size_t out_pos = compact ? table_out : table_out + 8 * object_count;
  std::uint64_t vertices = 0;
  std::uint64_t triangles = 0;
  for (std::size_t object = 0; object < object_count; ++object) {
    // Constraint: an object past the template's count is a copy of object 0; every other object keeps its own bytes.
    const std::size_t source = object < layout->object_count ? object : 0;
    if (!compact) {
      for (std::size_t i = 0; i < 8; ++i) rebuilt[table_out + i] = body[k_shared_header_bytes + 8 * source + i];
      table_out += 8;
    }
    const std::size_t begin = object_begin[source];
    for (std::size_t i = 0; i < object_bytes; ++i) rebuilt[out_pos + i] = body[begin + i];
    out_pos += object_bytes;
    const std::size_t core = begin + lead;
    vertices += read_u32(body, core);
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      triangles += read_u32(body, core + 8 + 8 * level);
    }
  }
  if (vertices > 0xFFFFFFFFULL || triangles > 0xFFFFFFFFULL) return std::nullopt;
  put_u32_at(rebuilt, resolved.prefix + 8, static_cast<std::uint32_t>(vertices));
  put_u32_at(rebuilt, resolved.prefix + 12, static_cast<std::uint32_t>(triangles));
  put_u32_at(rebuilt, resolved.prefix + 24, static_cast<std::uint32_t>(object_count));
  for (std::size_t object = 0; object < object_count; ++object) {
    if (!write_shared_combined_record_xform(rebuilt, object, 0, xforms[object])) return std::nullopt;
  }
  if (!walk_shared_combined_geom_extra(rebuilt)) return std::nullopt;
  const auto detected = detect_layout(resolve_geom(rebuilt).body);
  if (!detected.has_value() || detected->object_count != object_count || detected->combined_per_object != 1) {
    return std::nullopt;
  }
  if (resolved.prefix > 0) {
    for (std::size_t i = 0; i < resolved.prefix; ++i) {
      rebuilt[i] = template_block[i];
    }
  }
  return rebuilt;
}

std::optional<std::vector<std::byte>> build_shared_geom_extra(std::span<const std::byte> template_block,
                                                              std::uint32_t file_hash,
                                                              std::span<const SharedGeomObjectWrite> objects) {
  auto built = build_shared_geom_extra_checked(template_block, file_hash, objects);
  if (!built.has_value()) return std::nullopt;
  return std::move(*built);
}

std::expected<std::vector<std::byte>, SharedGeomBuildError> build_shared_geom_extra_checked(
    std::span<const std::byte> template_block,
    std::uint32_t file_hash,
    std::span<const SharedGeomObjectWrite> objects) {
  using Error = SharedGeomBuildError;
  if (objects.empty()) return std::unexpected(Error::no_objects);
  if (objects.size() > k_shared_geom_max_objects) return std::unexpected(Error::too_many_objects);
  const auto resolved = resolve_geom(template_block);
  const auto& body = resolved.body;
  const auto layout = detect_layout(body);
  if (!layout.has_value() || layout->combined_per_object != 1) return std::unexpected(Error::template_unsupported);
  if (!is_table_layout(body, layout->object_count)) return std::unexpected(Error::template_unsupported);
  const std::size_t object0 = k_shared_header_bytes + 8 * layout->object_count;
  const std::size_t record0 = object0 + k_object_core + sizeof(std::uint32_t);
  if (record0 + k_combined_record_bytes + k_object_suffix > body.size()) {
    return std::unexpected(Error::template_unsupported);
  }

  std::size_t body_size = k_shared_header_bytes + 8 * objects.size();
  std::uint64_t vertices = 0;
  std::uint64_t triangles = 0;
  for (const auto& object : objects) {
    if (object.records.empty()) return std::unexpected(Error::object_without_records);
    if (object.vertex_count > k_shared_geom_max_object_vertices) return std::unexpected(Error::vertex_count_exceeds_u16);
    body_size += k_object_core + sizeof(std::uint32_t) + k_combined_record_bytes * object.records.size() +
                 k_object_suffix;
    vertices += std::uint64_t{object.vertex_count} * object.records.size();
    std::uint64_t object_triangles = 0;
    for (const auto count : object.triangle_count) object_triangles += count;
    triangles += object_triangles * object.records.size();
  }
  if (vertices > 0xFFFFFFFFULL || triangles > 0xFFFFFFFFULL) return std::unexpected(Error::totals_exceed_u32);

  std::vector<std::byte> rebuilt(resolved.prefix + body_size);
  for (std::size_t i = 0; i < resolved.prefix; ++i) rebuilt[i] = template_block[i];
  for (std::size_t i = 0; i < k_shared_header_bytes; ++i) rebuilt[resolved.prefix + i] = body[i];
  put_u32_at(rebuilt, resolved.prefix + 8, static_cast<std::uint32_t>(vertices));
  put_u32_at(rebuilt, resolved.prefix + 12, static_cast<std::uint32_t>(triangles));
  put_u32_at(rebuilt, resolved.prefix + 24, static_cast<std::uint32_t>(objects.size()));

  const auto prototype_core = body.subspan(object0, k_object_core);
  const auto prototype_record = body.subspan(record0, k_combined_record_bytes);
  std::size_t table = resolved.prefix + k_shared_header_bytes;
  std::size_t pos = table + 8 * objects.size();
  for (const auto& object : objects) {
    put_u32_at(rebuilt, table, file_hash);
    put_u32_at(rebuilt, table + 4, object.data_offset);
    table += 8;
    for (std::size_t i = 0; i < k_object_core; ++i) rebuilt[pos + i] = prototype_core[i];
    put_u32_at(rebuilt, pos, object.vertex_count);
    put_u32_at(rebuilt, pos + 4, object.lod_levels);
    // Constraint: a level's offset counts the words below it up to the last level that holds triangles, measured on 303,835 objects.
    std::size_t last_level = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      if (object.triangle_count[level] != 0) last_level = level;
    }
    std::uint32_t next_word = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      const auto count = object.triangle_count[level];
      put_u32_at(rebuilt, pos + 8 + 8 * level, count);
      put_u32_at(rebuilt, pos + 12 + 8 * level, level <= last_level ? next_word : 0u);
      next_word += 3u * count;
    }
    pos += k_object_core;
    put_u32_at(rebuilt, pos, static_cast<std::uint32_t>(object.records.size()));
    pos += sizeof(std::uint32_t);
    for (const auto& record : object.records) {
      for (std::size_t i = 0; i < 4; ++i) rebuilt[pos + i] = prototype_record[i];
      std::size_t at = pos + k_record_rotation;
      for (const float value : record.rotation) {
        put_f32_at(rebuilt, at, value);
        at += 4;
      }
      for (const float value : record.translation) {
        put_f32_at(rebuilt, at, value);
        at += 4;
      }
      put_f32_at(rebuilt, at, record.scale);
      at = pos + k_record_bound;
      for (const float value : record.bound_center) {
        put_f32_at(rebuilt, at, value);
        at += 4;
      }
      put_f32_at(rebuilt, at, record.bound_radius);
      pos += k_combined_record_bytes;
    }
    for (std::size_t i = 0; i < k_object_suffix; ++i) {
      rebuilt[pos + i] = static_cast<std::byte>((object.vertex_desc >> (8 * i)) & 0xFFu);
    }
    pos += k_object_suffix;
  }
  if (pos != rebuilt.size()) return std::unexpected(Error::result_unreadable);
  if (!walk_shared_combined_geom_extra(rebuilt)) return std::unexpected(Error::result_unreadable);
  return rebuilt;
}

}

