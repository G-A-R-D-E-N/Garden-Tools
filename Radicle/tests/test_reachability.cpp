#include "fo4/graph/reachability.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

void top_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t id,
                std::initializer_list<std::pair<std::string_view, std::vector<std::byte>>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, id);
  for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> u32(std::uint32_t value) {
  fo4::test::ByteWriter w;
  w.u32(value);
  return w.bytes;
}

std::vector<std::byte> u16(std::uint16_t value) {
  fo4::test::ByteWriter w;
  w.u16(value);
  return w.bytes;
}

fo4::Plugin make_game() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(8);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  top_record(out, "KYWD", 0x800, {});
  top_record(out, "KYWD", 0x801, {});
  top_record(out, "NPC_", 0x802, {{"ACBS", u32(0x4)}, {"KSIZ", u32(1)}, {"KWDA", u32(0x800)}});
  top_record(out, "NPC_", 0x803, {{"ACBS", u32(0x0)}});
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x804);
  fo4::test::end_record(out, cell);
  const auto children = fo4::test::begin_group(out, "CELL", 6);
  const auto temporary = fo4::test::begin_group(out, "CELL", 9);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x805);
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cells);
  top_record(out, "QUST", 0x806, {{"DNAM", u16(0x1)}});
  top_record(out, "GMST", 0x807, {});
  return fo4::read_plugin(out.bytes, "Game.esm");
}

std::vector<std::string> unreachable(const fo4::Plugin& plugin) {
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  std::vector<std::string> out;
  for (const auto& item : fo4::find_unreachable(load_order, 0)) out.push_back(item.key.to_string());
  return out;
}

}  // namespace

TEST(Reachability, ReportsRecordsNoRootReaches) {
  EXPECT_EQ(unreachable(make_game()),
            (std::vector<std::string>{"000801:Game.esm", "000803:Game.esm", "000804:Game.esm",
                                      "000805:Game.esm"}));
}
