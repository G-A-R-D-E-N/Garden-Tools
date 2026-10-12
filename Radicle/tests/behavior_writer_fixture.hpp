#pragma once

#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <string>
#include <utility>
#include <vector>

namespace behavior_writer_test {

using Bytes = std::vector<std::byte>;
using Subs = std::vector<std::pair<std::string, Bytes>>;
inline Bytes words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}
inline Bytes text(std::string_view value) {
  fo4::test::ByteWriter out; out.raw(value); out.u8(0); return out.bytes;
}
inline Bytes floats(std::size_t count) {
  fo4::test::ByteWriter out;
  for (std::size_t i = 0; i < count; ++i) out.f32(0.5f);
  return out.bytes;
}
inline fo4::FormKey key(std::uint32_t id = 0x800) {
  return {fo4::PluginKey::from_filename("Behavior.esp"), fo4::LocalFormId{id}};
}
inline Subs package() {
  return {{"EDID", text("Package")}, {"PKDT", Bytes(12)}, {"PSDT", Bytes(12)},
          {"CNAM", words({0x801})}, {"QNAM", words({0x806})}, {"PKCU", words({3, 0x800, 9})},
          {"ANAM", text("Integer")}, {"CNAM", words({0x804})},
          {"PLDT", words({6, 0x803, 0xFFFFFFFF})}, {"PLDT", words({4, 0x805, 2, 7})},
          {"PLDT", words({0, 0x804})}, {"PLDT", words({5, 0x804, 3})},
          {"PTDA", words({3, 0x803, 0xFFFFFFFF})}, {"PTDA", words({4, 0x804, 0})},
          {"XNAM", {}}, {"POBA", {}}, {"INAM", words({0x802})},
          {"POEA", {}}, {"INAM", words({0})}, {"POCA", {}}, {"INAM", words({0})}};
}
inline Subs style(std::size_t melee = 10, std::size_t range = 5) {
  auto close = floats(9);
  const auto max = words({5}); close.insert(close.end(), max.begin(), max.end());
  const auto variance = floats(1); close.insert(close.end(), variance.begin(), variance.end());
  return {{"EDID", text("Style")}, {"CSGD", floats(12)}, {"CSMD", words({0xDEADBEEF})},
          {"CSME", floats(melee)}, {"CSRA", floats(1)}, {"CSCR", close},
          {"CSLR", floats(range)}, {"CSCV", floats(1)}, {"CSFL", floats(8)}, {"DATA", words({0})}};
}
inline Subs idle() {
  return {{"EDID", text("Idle")}, {"DNAM", text("graph.hkx")}, {"ENAM", text("event")},
          {"ANAM", words({0x807, 0x802})}, {"DATA", Bytes(6)}, {"GNAM", text("animation.hkx")}};
}
inline Bytes fixture(Subs pack = package(), Subs combat = style()) {
  fo4::test::ByteWriter out;
  const auto header = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr; hedr.f32(1); hedr.u32(9); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes); fo4::test::end_record(out, header);
  const auto add = [&](std::string_view tag, std::uint32_t id, const Subs& subs) {
    const auto group = fo4::test::begin_group(out, tag, 0);
    const auto at = fo4::test::begin_record(out, tag, 0, id, 131);
    for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, at); fo4::test::end_group(out, group);
  };
  add("PACK", 0x800, pack); add("CSTY", 0x801, combat); add("IDLE", 0x802, idle());
  add("KYWD", 0x803, {}); add("REFR", 0x804, {}); add("MISC", 0x805, {});
  add("QUST", 0x806, {}); add("AACT", 0x807, {}); add("MISC", 0x808, {{"ZZZZ", words({0x804})}});
  return out.bytes;
}
inline fo4::Plugin load(Subs pack = package(), Subs combat = style()) {
  return fo4::read_plugin(fixture(std::move(pack), std::move(combat)), "Behavior.esp");
}

}
