#include "fo4/environment/navmesh.hpp"

#include "record_reader.hpp"
#include "fo4/binary/cursor.hpp"
#include "fo4/schema/form_link_mixed.hpp"

namespace fo4::env {
namespace {

using binary::Cursor;
using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

FormKey link(const Reader& r, const Subrecord& sub, Cursor& c) {
  const auto offset = c.position();
  c.read_u32();
  return r.link_at(sub, offset);
}

NavmeshParent parent(const Reader& r, const Subrecord& sub, Cursor& c) {
  NavmeshParent p;
  p.crc = c.read_u32();
  const auto raw_world = Reader::u32(sub, c.position());
  p.world = link(r, sub, c);
  if (raw_world == 0) p.cell = link(r, sub, c);
  else {
    p.grid_y = static_cast<std::int16_t>(c.read_u16());
    p.grid_x = static_cast<std::int16_t>(c.read_u16());
  }
  return p;
}

std::array<float, 3> vec3(Cursor& c) { return {c.read_f32(), c.read_f32(), c.read_f32()}; }

void validate(const Reader& r, const Subrecord& sub) {
  if (!navmesh_mixed_formid_offsets(*r.ref().record, sub)) r.fail(sub.signature, "unsupported counted layout");
}

void require_navi_version(const Reader& r) {
  const auto* version = r.sized(sig("NVER"), 4);
  if (!version || Reader::u32(*version, 0) != 15) r.fail(sig("NVER"), "version 15 is required");
}

}

std::optional<NavmeshGeometry> decode_navmesh(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("NAVM"));
  const auto* sub = r.unique(sig("NVNM"));
  if (!sub) return std::nullopt;
  if (ref.record->header.form_version < 128) r.fail(sub->signature, "legacy form version is unsupported");
  validate(r, *sub);
  Cursor c(sub->data);
  NavmeshGeometry out;
  out.version = c.read_u32();
  if (out.version != 15) r.fail(sub->signature, "unsupported geometry version");
  out.parent = parent(r, *sub, c);
  auto count = c.read_u32();
  out.vertices.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) out.vertices.push_back(vec3(c));
  count = c.read_u32();
  out.triangles.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    NavmeshTriangle t;
    for (auto& v : t.vertices) v = c.read_u16();
    for (auto& e : t.edges) e = static_cast<std::int16_t>(c.read_u16());
    t.clearance = c.read_f32(); t.traversal = c.read_u8();
    t.flags = c.read_u32();
    out.triangles.push_back(t);
  }
  count = c.read_u32();
  out.edges.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    NavmeshEdgeLink e;
    e.type = c.read_u32(); e.navmesh = link(r, *sub, c);
    e.triangle = static_cast<std::int16_t>(c.read_u16()); e.edge = c.read_u8();
    out.edges.push_back(e);
  }
  for (const auto& t : out.triangles) {
    for (std::size_t i = 0; i < t.edges.size(); ++i) {
      const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(t.edges[i]));
      if (t.edge_is_extra(i) && index >= out.edges.size()) r.fail(sub->signature, "extra edge index is out of range");
    }
  }
  count = c.read_u32();
  out.doors.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    NavmeshDoorLink d;
    d.triangle = static_cast<std::int16_t>(c.read_u16()); d.crc = c.read_u32(); d.door = link(r, *sub, c);
    out.doors.push_back(d);
  }
  out.opaque_tail = c.read_bytes(c.remaining());
  return out;
}

std::vector<NavmeshInfo> decode_navmesh_info(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("NAVI"));
  require_navi_version(r);
  std::vector<NavmeshInfo> out;
  for (const auto& sub : ref.record->subrecords) {
    if (sub.signature != sig("NVMI")) continue;
    validate(r, sub);
    Cursor c(sub.data);
    NavmeshInfo info;
    info.navmesh = link(r, sub, c); info.flags = c.read_u32();
    info.location = vec3(c); info.preferred = c.read_f32();
    for (auto* keys : {&info.edges, &info.preferred_edges}) {
      const auto count = c.read_u32(); keys->reserve(count);
      for (std::uint32_t i = 0; i < count; ++i) keys->push_back(link(r, sub, c));
    }
    const auto count = c.read_u32(); info.doors.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      NaviDoorLink d;
      d.crc = c.read_u32(); d.door = link(r, sub, c); info.doors.push_back(d);
    }
    info.has_island = c.read_u8() != 0;
    if (info.has_island) {
      const auto start = c.position();
      c.read_bytes(24);
      auto n = c.read_u32(); c.read_bytes(static_cast<std::size_t>(n) * 6);
      n = c.read_u32(); c.read_bytes(static_cast<std::size_t>(n) * 12);
      info.island = std::span{sub.data}.subspan(start, c.position() - start);
    }
    info.parent = parent(r, sub, c);
    out.push_back(std::move(info));
  }
  return out;
}

NavmeshPathData decode_navmesh_paths(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("NAVI"));
  require_navi_version(r);
  NavmeshPathData out;
  const auto* sub = r.unique(sig("NVPP"));
  if (!sub) return out;
  validate(r, *sub);
  Cursor c(sub->data);
  out.paths.resize(c.read_u32());
  for (auto& path : out.paths) {
    path.resize(c.read_u32());
    for (auto& key : path) key = link(r, *sub, c);
  }
  out.anchors.resize(c.read_u32());
  for (auto& anchor : out.anchors) {
    anchor.first = link(r, *sub, c);
    anchor.second = c.read_u32();
  }
  return out;
}

}
