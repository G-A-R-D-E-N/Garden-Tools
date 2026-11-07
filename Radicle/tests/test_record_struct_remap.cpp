#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Sub = std::pair<std::string_view, std::vector<std::byte>>;
using Patch = std::pair<std::size_t, std::uint32_t>;

fo4::test::ByteWriter plugin_with_masters() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm", "DLC01.esm"});
  return out;
}

void append_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t id,
                   const std::vector<Sub>& subs, std::uint16_t form_version = 131) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, id, form_version);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<fo4::PluginKey> swapped_masters() {
  return {fo4::PluginKey::from_filename("DLC01.esm"),
          fo4::PluginKey::from_filename("Fallout4.esm")};
}

fo4::Plugin reread_swapped(const fo4::test::ByteWriter& out) {
  auto plugin = fo4::remap_plugin_masters(fo4::read_plugin(out.bytes, "Patch.esp"),
                                          swapped_masters());
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

std::vector<std::byte> subrecord_data(const fo4::Plugin& plugin, const char* key,
                                      std::string_view signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) return sub.data;
  }
  return {};
}

std::vector<std::byte> payload(std::size_t size, std::initializer_list<Patch> values) {
  std::vector<std::byte> data(size, std::byte{0});
  for (const auto& [offset, value] : values) std::memcpy(data.data() + offset, &value, 4);
  return data;
}

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

struct LctnCase {
  std::string_view signature;
  std::size_t stride;
  std::vector<std::size_t> slots;
};

std::vector<std::byte> lctn_words(std::size_t size, const std::vector<std::size_t>& slots,
                                  std::uint32_t mask) {
  std::vector<std::byte> data(size);
  for (std::size_t offset = 0; offset < size; offset += 4) {
    auto value = static_cast<std::uint32_t>(0x1100 + offset / 4);
    if (std::find(slots.begin(), slots.end(), offset) != slots.end()) value |= mask;
    std::memcpy(data.data() + offset, &value, 4);
  }
  return data;
}

fo4::test::ByteWriter header_with_tnam(const std::vector<std::byte>& tnam) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const std::string_view master : {"Fallout4.esm", "DLC01.esm"}) {
    std::string name{master};
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::write_subrecord(out, "TNAM", tnam);
  fo4::test::end_record(out, tes4);
  return out;
}

}  // namespace

TEST(RecordStructRemap, LctnEntriesRemapFormIdSlotsOnly) {
  const std::vector<LctnCase> cases = {
      {"ACPR", 12, {0, 4}}, {"LCPR", 12, {0, 4}}, {"RCPR", 4, {0}},
      {"ACUN", 12, {0, 4, 8}}, {"LCUN", 12, {0, 4, 8}}, {"RCUN", 4, {0}},
      {"ACSR", 16, {0, 4, 8}}, {"LCSR", 16, {0, 4, 8}}, {"RCSR", 4, {0}},
      {"ACEC", 12, {0}}, {"LCEC", 12, {0}}, {"RCEC", 12, {0}},
      {"ACID", 4, {0}}, {"LCID", 4, {0}},
      {"ACEP", 12, {0, 4}}, {"LCEP", 12, {0, 4}},
  };
  std::vector<Sub> subs;
  for (const auto& entry : cases) {
    subs.emplace_back(entry.signature, lctn_words(entry.stride, entry.slots, 0));
  }
  auto out = plugin_with_masters();
  append_record(out, "LCTN", 0x02000100, subs);
  const auto reread = reread_swapped(out);
  for (const auto& entry : cases) {
    EXPECT_EQ(subrecord_data(reread, "000100:Patch.esp", entry.signature),
              lctn_words(entry.stride, entry.slots, 0x01000000))
        << entry.signature;
  }
}

TEST(RecordStructRemap, BodyPartNodeRemapsElevenSlots) {
  auto out = plugin_with_masters();
  append_record(out, "BPTD", 0x02000200,
                {{"BPND", payload(101, {{0, 0x3F800000}, {4, 0x1200}, {8, 0x1201},
                                        {12, 0x3F000000}, {16, 0x1202}, {20, 0x1203},
                                        {24, 0x3F000000}, {52, 0x1204}, {56, 0x1205},
                                        {60, 0x3F000000}, {67, 0x1206}, {79, 0x1207},
                                        {83, 0x1208}, {87, 0x1209}, {91, 0x120A},
                                        {95, 0x3F800000}})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000200:Patch.esp", "BPND"),
            payload(101, {{0, 0x3F800000}, {4, 0x01001200}, {8, 0x01001201},
                          {12, 0x3F000000}, {16, 0x01001202}, {20, 0x01001203},
                          {24, 0x3F000000}, {52, 0x01001204}, {56, 0x01001205},
                          {60, 0x3F000000}, {67, 0x01001206}, {79, 0x01001207},
                          {83, 0x01001208}, {87, 0x01001209}, {91, 0x0100120A},
                          {95, 0x3F800000}}));
}

TEST(RecordStructRemap, CellPrevisAndCombinedIndexRemap) {
  auto out = plugin_with_masters();
  append_record(out, "CELL", 0x02000300,
                {{"XPRI", words({0x1300, 0x1301})},
                 {"XCRI", words({1, 2, 0x55, 0x1302, 0x56})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000300:Patch.esp", "XPRI"),
            words({0x01001300, 0x01001301}));
  EXPECT_EQ(subrecord_data(reread, "000300:Patch.esp", "XCRI"),
            words({1, 2, 0x55, 0x01001302, 0x56}));
}

TEST(RecordStructRemap, IdleMarkerAndLandLayersRemap) {
  auto out = plugin_with_masters();
  append_record(out, "IDLM", 0x02000400, {{"IDLA", words({0x1400, 0x1401})}});
  append_record(out, "LAND", 0x02000401,
                {{"BTXT", words({0x1402, 0x00000001})}, {"ATXT", words({0x1403, 0x00000002})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000400:Patch.esp", "IDLA"),
            words({0x01001400, 0x01001401}));
  EXPECT_EQ(subrecord_data(reread, "000401:Patch.esp", "BTXT"),
            words({0x01001402, 0x00000001}));
  EXPECT_EQ(subrecord_data(reread, "000401:Patch.esp", "ATXT"),
            words({0x01001403, 0x00000002}));
}

TEST(RecordStructRemap, RaceDataAndTintTemplatesRemapByFormVersion) {
  auto out = plugin_with_masters();
  append_record(out, "RACE", 0x02000500,
                {{"DATA", payload(200, {{0, 0x3F800000}, {36, 0x3F000000}, {146, 0x1500},
                                        {150, 0x1501}, {154, 0x1502}, {158, 0x1503},
                                        {162, 0x1504}, {166, 0x1505}, {170, 0x3F800000},
                                        {176, 0x1506}, {180, 0x1507}, {184, 0x1508},
                                        {188, 0x1509}, {192, 0x3F000000}, {196, 0x3F800000}})},
                 {"TTEC", payload(28, {{0, 0x1520}, {4, 0x3F800000}, {8, 0x00000001},
                                       {10, 0x00000002}, {14, 0x1521}, {18, 0x3F800000},
                                       {22, 0x00000001}, {24, 0x00000002}})}},
                131);
  append_record(out, "RACE", 0x02000501,
                {{"DATA", payload(164, {{0, 0x3F800000}, {118, 0x1510}, {122, 0x1511},
                                        {126, 0x1512}, {130, 0x1513}, {134, 0x1514},
                                        {138, 0x1515}, {148, 0x1516}, {152, 0x1517},
                                        {156, 0x1518}, {160, 0x3F000000}})}},
                100);
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000500:Patch.esp", "DATA"),
            payload(200, {{0, 0x3F800000}, {36, 0x3F000000}, {146, 0x01001500},
                          {150, 0x01001501}, {154, 0x01001502}, {158, 0x01001503},
                          {162, 0x01001504}, {166, 0x01001505}, {170, 0x3F800000},
                          {176, 0x01001506}, {180, 0x01001507}, {184, 0x01001508},
                          {188, 0x01001509}, {192, 0x3F000000}, {196, 0x3F800000}}));
  EXPECT_EQ(subrecord_data(reread, "000500:Patch.esp", "TTEC"),
            payload(28, {{0, 0x01001520}, {4, 0x3F800000}, {8, 0x00000001},
                         {10, 0x00000002}, {14, 0x01001521}, {18, 0x3F800000},
                         {22, 0x00000001}, {24, 0x00000002}}));
  EXPECT_EQ(subrecord_data(reread, "000501:Patch.esp", "DATA"),
            payload(164, {{0, 0x3F800000}, {118, 0x01001510}, {122, 0x01001511},
                          {126, 0x01001512}, {130, 0x01001513}, {134, 0x01001514},
                          {138, 0x01001515}, {148, 0x01001516}, {152, 0x01001517},
                          {156, 0x01001518}, {160, 0x3F000000}}));
}

TEST(RecordStructRemap, RegionEntriesAndStagTnamRemap) {
  auto out = plugin_with_masters();
  append_record(out, "REGN", 0x02000600,
                {{"RDOT", payload(52, {{0, 0x1600}, {8, 0x3F800000}, {12, 0x00000002}})},
                 {"RDGS", payload(8, {{0, 0x1601}, {4, 0x00000003}})},
                 {"RDWT", payload(12, {{0, 0x1602}, {4, 0x00000004}, {8, 0x1603}})},
                 {"RDSA", payload(12, {{0, 0x1604}, {4, 0x00000005}, {8, 0x3F800000}})}});
  fo4::test::ByteWriter tnam;
  tnam.u32(0x1610);
  tnam.u8('F');
  tnam.u8('o');
  tnam.u8('o');
  tnam.u8(0);
  append_record(out, "STAG", 0x02000601, {{"TNAM", tnam.bytes}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000600:Patch.esp", "RDOT"),
            payload(52, {{0, 0x01001600}, {8, 0x3F800000}, {12, 0x00000002}}));
  EXPECT_EQ(subrecord_data(reread, "000600:Patch.esp", "RDGS"),
            payload(8, {{0, 0x01001601}, {4, 0x00000003}}));
  EXPECT_EQ(subrecord_data(reread, "000600:Patch.esp", "RDWT"),
            payload(12, {{0, 0x01001602}, {4, 0x00000004}, {8, 0x01001603}}));
  EXPECT_EQ(subrecord_data(reread, "000600:Patch.esp", "RDSA"),
            payload(12, {{0, 0x01001604}, {4, 0x00000005}, {8, 0x3F800000}}));
  fo4::test::ByteWriter expected_tnam;
  expected_tnam.u32(0x01001610);
  expected_tnam.u8('F');
  expected_tnam.u8('o');
  expected_tnam.u8('o');
  expected_tnam.u8(0);
  EXPECT_EQ(subrecord_data(reread, "000601:Patch.esp", "TNAM"), expected_tnam.bytes);
}

TEST(RecordStructRemap, WorldLargeRefsAndNvnmOutsideNavmesh) {
  auto out = plugin_with_masters();
  fo4::test::ByteWriter rnam;
  rnam.u16(1);
  rnam.u16(2);
  rnam.u32(2);
  rnam.u32(0x1700);
  rnam.u16(3);
  rnam.u16(4);
  rnam.u32(0x1701);
  rnam.u16(5);
  rnam.u16(6);
  append_record(out, "WRLD", 0x02000700, {{"RNAM", rnam.bytes}});

  fo4::test::ByteWriter nvnm;
  nvnm.u32(15);
  nvnm.u32(0x11111111);
  nvnm.u32(0x1710);
  nvnm.u16(2);
  nvnm.u16(3);
  nvnm.u32(0);
  nvnm.u32(0);
  nvnm.u32(1);
  nvnm.u32(0x22);
  nvnm.u32(0x1711);
  nvnm.u8(1);
  nvnm.u8(2);
  nvnm.u8(3);
  nvnm.u32(0);
  nvnm.u32(0);
  nvnm.u32(0);
  nvnm.u32(0);
  for (int index = 0; index < 9; ++index) nvnm.u32(0);
  append_record(out, "STAT", 0x02000701, {{"NVNM", nvnm.bytes}});
  append_record(out, "ACTI", 0x02000702, {{"NVNM", nvnm.bytes}});
  append_record(out, "FURN", 0x02000703, {{"NVNM", nvnm.bytes}});

  const auto reread = reread_swapped(out);
  fo4::test::ByteWriter expected_rnam;
  expected_rnam.u16(1);
  expected_rnam.u16(2);
  expected_rnam.u32(2);
  expected_rnam.u32(0x01001700);
  expected_rnam.u16(3);
  expected_rnam.u16(4);
  expected_rnam.u32(0x01001701);
  expected_rnam.u16(5);
  expected_rnam.u16(6);
  EXPECT_EQ(subrecord_data(reread, "000700:Patch.esp", "RNAM"), expected_rnam.bytes);

  auto expected_nvnm = nvnm.bytes;
  const auto patch = [&expected_nvnm](std::size_t offset, std::uint32_t value) {
    std::memcpy(expected_nvnm.data() + offset, &value, 4);
  };
  patch(8, 0x01001710);
  patch(32, 0x01001711);
  EXPECT_EQ(subrecord_data(reread, "000701:Patch.esp", "NVNM"), expected_nvnm);
  EXPECT_EQ(subrecord_data(reread, "000702:Patch.esp", "NVNM"), expected_nvnm);
  EXPECT_EQ(subrecord_data(reread, "000703:Patch.esp", "NVNM"), expected_nvnm);
}

TEST(RecordStructRemap, MusicTrackListRemaps) {
  auto out = plugin_with_masters();
  append_record(out, "MUST", 0x02000100, {{"SNAM", words({0x00000200, 0x01000300})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000100:Patch.esp", "SNAM"),
            words({0x01000200, 0x00000300}));
}

TEST(RecordStructRemap, HeaderTransientTypesRemapReferencesNotFormType) {
  const auto out = header_with_tnam(words({78, 0x00001234, 0x01005678}));
  const auto reread = reread_swapped(out);
  std::vector<std::byte> tnam;
  for (const auto& sub : reread.tes4.subrecords) {
    if (sub.signature.to_string() == "TNAM") tnam = sub.data;
  }
  EXPECT_EQ(tnam, words({78, 0x01001234, 0x00005678}));

  const auto malformed = header_with_tnam(payload(6, {{0, 78}}));
  auto plugin = fo4::read_plugin(malformed.bytes, "Patch.esp");
  EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, swapped_masters()), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), malformed.bytes);
}

TEST(RecordStructRemap, CollectReportsUnclaimedListedPayloadsOutsideTheAllowList) {
  auto out = plugin_with_masters();
  append_record(out, "ZZZZ", 0x02000100,
                {{"NAME", payload(6, {})}, {"NAME", payload(4, {{0, 0x01000200}})}});
  append_record(out, "ACTI", 0x02000101, {{"FNAM", payload(2, {})}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto collection = fo4::collect_form_links_detailed(plugin);
  ASSERT_EQ(collection.unclaimed_link_payloads.size(), 1u);
  const auto& unclaimed = collection.unclaimed_link_payloads.front();
  EXPECT_EQ(unclaimed.record, fo4::FormKey::parse("000100:Patch.esp"));
  EXPECT_EQ(unclaimed.subrecord.to_string(), "NAME");
  EXPECT_EQ(unclaimed.subrecord_ordinal, 0u);
  EXPECT_EQ(unclaimed.size, 6u);
}

TEST(RecordStructRemap, MalformedSizesFailClosedWithoutMutation) {
  struct Case {
    std::string_view record;
    std::string_view signature;
    std::vector<std::byte> data;
  };
  const std::vector<Case> cases = {
      {"BPTD", "BPND", payload(100, {})},
      {"RACE", "DATA", payload(199, {})},
      {"RACE", "TTEC", payload(15, {})},
      {"LCTN", "ACPR", payload(13, {})},
      {"LCTN", "ACEC", payload(10, {})},
      {"REGN", "RDOT", payload(51, {})},
      {"WRLD", "RNAM", payload(16, {{4, 2}})},
      {"CELL", "XCRI", words({1, 3, 0x55, 0x1800, 0x56})},
      {"STAG", "TNAM", payload(3, {})},
      {"MUST", "SNAM", payload(6, {})},
      {"STAT", "NVNM", payload(16, {{0, 14}})},
  };
  for (const auto& test_case : cases) {
    auto out = plugin_with_masters();
    append_record(out, test_case.record, 0x02000800, {{test_case.signature, test_case.data}});
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, swapped_masters()), fo4::FormIdError)
        << test_case.record << " " << test_case.signature;
    EXPECT_EQ(fo4::write_plugin(plugin), out.bytes)
        << test_case.record << " " << test_case.signature;
  }
}
