#pragma once

#include "plugin_bytes.hpp"
#include "fo4/plugin/reader.hpp"

namespace fo4::test {

inline Plugin navmesh_writer_fixture(std::uint16_t stored_edge0 = 0xFFFF, std::uint16_t stored_edge1 = 0xFFFF) {
  ByteWriter geometry;
  geometry.u32(15); geometry.u32(0x12345678); geometry.u32(0); geometry.u32(0x810);
  geometry.u32(3);
  for (int i = 0; i < 9; ++i) geometry.f32(static_cast<float>(i));
  geometry.u32(1);
  for (std::uint16_t i = 0; i < 3; ++i) geometry.u16(i);
  geometry.u16(stored_edge0); geometry.u16(stored_edge1); geometry.u16(0xFFFF);
  geometry.f32(4.5f); geometry.u8(0xAA); geometry.u32(0xABCD8012);
  geometry.u32(0); geometry.u32(1); geometry.u16(0); geometry.u32(0xABCDE); geometry.u32(0x811);
  for (int i = 0; i < 12; ++i) geometry.u32(0);
  ByteWriter out;
  auto record = begin_record(out, "TES4", 0, 0);
  ByteWriter header; header.f32(1); header.u32(10); header.u32(0x900);
  write_subrecord(out, "HEDR", header.bytes); end_record(out, record);
  auto group = begin_group(out, "NAVM", 0);
  record = begin_record(out, "NAVM", 0, 0x800, 131);
  write_subrecord(out, "NVNM", geometry.bytes);
  ByteWriter sentinel; sentinel.u32(0x12345678); write_subrecord(out, "ZZZZ", sentinel.bytes);
  end_record(out, record); end_group(out, group);
  for (const auto id : {0x811u, 0x812u}) {
    group = begin_group(out, "REFR", 0); record = begin_record(out, "REFR", 0, id);
    write_subrecord(out, "ZZZZ", sentinel.bytes); end_record(out, record); end_group(out, group);
  }
  group = begin_group(out, "MISC", 0); record = begin_record(out, "MISC", 0, 0x813);
  write_subrecord(out, "ZZZZ", sentinel.bytes); end_record(out, record); end_group(out, group);
  group = begin_group(out, "NAVI", 0); record = begin_record(out, "NAVI", 0, 0x801);
  write_subrecord(out, "ZZZZ", sentinel.bytes); end_record(out, record); end_group(out, group);
  return read_plugin(out.bytes, "Navigation.esp");
}

inline FormKey navmesh_key(std::uint32_t id = 0x800) {
  return {PluginKey::from_filename("Navigation.esp"), LocalFormId{id}};
}

}
