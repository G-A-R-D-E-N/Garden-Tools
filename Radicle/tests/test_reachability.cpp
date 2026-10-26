#include "fo4/graph/reachability.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
  return fo4::read_plugin(out.bytes, "Fallout4.esm");
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
            (std::vector<std::string>{"000801:Fallout4.esm", "000803:Fallout4.esm", "000804:Fallout4.esm",
                                      "000805:Fallout4.esm"}));
}

TEST(Reachability, LowIdsInACustomFirstMasterAreNotEngineRoots) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  top_record(out, "KYWD", 0x014, {});
  auto plugin = fo4::read_plugin(out.bytes, "CustomMaster.esm");
  EXPECT_EQ(unreachable(plugin), std::vector<std::string>{"000014:CustomMaster.esm"});
}

TEST(Reachability, NoReachLinksDoNotReachTheirTarget) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto ctda = [](std::uint8_t type, std::uint32_t compare, std::uint32_t param1) {
    fo4::test::ByteWriter w;
    w.u32(type);
    w.u32(compare);
    w.u16(560);
    w.u16(0);
    w.u32(param1);
    w.u32(0);
    w.u32(0);
    w.u32(0);
    w.u32(0xFFFFFFFFu);
    return w.bytes;
  };
  top_record(out, "KYWD", 0x810, {});
  top_record(out, "QUST", 0x811, {{"DNAM", u16(0x0)}});
  top_record(out, "DIAL", 0x812, {{"QNAM", u32(0x811)}});
  top_record(out, "KYWD", 0x814, {});
  top_record(out, "GLOB", 0x815, {});
  top_record(out, "COBJ", 0x813,
             {{"CNAM", u32(0x812)}, {"BNAM", u32(0x810)}, {"CTDA", ctda(0x04, 0x815, 0x814)}});
  auto plugin = fo4::read_plugin(out.bytes, "Fallout4.esm");
  const auto links = fo4::collect_form_links(plugin);
  EXPECT_TRUE(std::ranges::any_of(links, [](const fo4::FormLinkHit& hit) {
    return hit.subrecord.to_string() == "CTDA" && hit.offset == 12 && hit.key.local().value() == 0x814;
  }));
  EXPECT_EQ(unreachable(plugin), (std::vector<std::string>{"000810:Fallout4.esm", "000811:Fallout4.esm",
                                                           "000814:Fallout4.esm"}));
}

TEST(Reachability, AComplexRecordFollowsTheLinksOfEveryVersion) {
  const auto dnam = [](std::uint32_t object) {
    fo4::test::ByteWriter w;
    w.u32(0);
    w.u32(object);
    return w.bytes;
  };
  fo4::test::ByteWriter base;
  const auto base_tes4 = fo4::test::begin_record(base, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x900);
  fo4::test::write_subrecord(base, "HEDR", hedr.bytes);
  fo4::test::end_record(base, base_tes4);
  top_record(base, "KYWD", 0x821, {});
  top_record(base, "KYWD", 0x822, {});
  top_record(base, "DOBJ", 0x820, {{"DNAM", dnam(0x821)}});
  const auto master = fo4::read_plugin(base.bytes, "Fallout4.esm");

  fo4::test::ByteWriter patch;
  const auto patch_tes4 = fo4::test::begin_record(patch, "TES4", 0, 0);
  fo4::test::write_subrecord(patch, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(patch, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(patch, "DATA", data.bytes);
  fo4::test::end_record(patch, patch_tes4);
  top_record(patch, "DOBJ", 0x820, {{"DNAM", dnam(0x822)}});
  const auto override_plugin = fo4::read_plugin(patch.bytes, "Patch.esp");

  const std::vector<const fo4::Plugin*> load_order{&master, &override_plugin};
  EXPECT_TRUE(fo4::find_unreachable(load_order, 0).empty());
}
