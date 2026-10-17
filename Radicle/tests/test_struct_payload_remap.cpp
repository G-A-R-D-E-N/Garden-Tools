#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Sub = std::pair<std::string_view, std::vector<std::byte>>;

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

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

std::vector<std::uint32_t> remapped_words(const fo4::Plugin& plugin, const char* key,
                                          std::string_view signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  std::vector<std::uint32_t> values;
  if (record == nullptr) return values;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != signature) continue;
    for (std::size_t offset = 0; offset + 4 <= sub.data.size(); offset += 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, sub.data.data() + offset, 4);
      values.push_back(value);
    }
  }
  return values;
}

}  // namespace

TEST(StructPayloadRemap, PackedArraysRemapAndKeepNeighbors) {
  auto out = plugin_with_masters();
  append_record(out, "OTFT", 0x02000100, {{"INAM", words({0x1234, 0x1235})}});
  append_record(out, "COLL", 0x02000101,
                {{"INTV", words({0x5151})}, {"CNAM", words({0x1236, 0x1237})}});
  append_record(out, "RACE", 0x02000102,
                {{"TINL", words({0x5252})}, {"HCLF", words({0x1250, 0x1251})}});
  append_record(out, "CELL", 0x02000103,
                {{"XCLW", words({0x41200000})}, {"XCLR", words({0x1238, 0x1239})}});
  append_record(out, "COBJ", 0x02000104,
                {{"INTV", words({0x00070001})}, {"FNAM", words({0x1240, 0x1241})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000100:Patch.esp", "INAM"),
            (std::vector<std::uint32_t>{0x01001234, 0x01001235}));
  EXPECT_EQ(remapped_words(reread, "000101:Patch.esp", "CNAM"),
            (std::vector<std::uint32_t>{0x01001236, 0x01001237}));
  EXPECT_EQ(remapped_words(reread, "000101:Patch.esp", "INTV"),
            (std::vector<std::uint32_t>{0x5151}));
  EXPECT_EQ(remapped_words(reread, "000102:Patch.esp", "HCLF"),
            (std::vector<std::uint32_t>{0x01001250, 0x01001251}));
  EXPECT_EQ(remapped_words(reread, "000102:Patch.esp", "TINL"),
            (std::vector<std::uint32_t>{0x5252}));
  EXPECT_EQ(remapped_words(reread, "000103:Patch.esp", "XCLR"),
            (std::vector<std::uint32_t>{0x01001238, 0x01001239}));
  EXPECT_EQ(remapped_words(reread, "000103:Patch.esp", "XCLW"),
            (std::vector<std::uint32_t>{0x41200000}));
  EXPECT_EQ(remapped_words(reread, "000104:Patch.esp", "FNAM"),
            (std::vector<std::uint32_t>{0x01001240, 0x01001241}));
  EXPECT_EQ(remapped_words(reread, "000104:Patch.esp", "INTV"),
            (std::vector<std::uint32_t>{0x00070001}));
}

TEST(StructPayloadRemap, SoundAndCategoryStructsRemapAndKeepScalars) {
  auto out = plugin_with_masters();
  append_record(out, "KSSM", 0x02000200,
                {{"RNAM", words({0x0A, 0x1300})}, {"RNAM", words({0x0B, 0x1301})}});
  append_record(out, "MGEF", 0x02000201, {{"SNDD", words({0x2, 0x1310, 0x5, 0x1311})}});
  append_record(out, "SCSN", 0x02000202,
                {{"CNAM", words({0x1320, 0x3F000000})}, {"CNAM", words({0x1321, 0x3F800000})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000200:Patch.esp", "RNAM"),
            (std::vector<std::uint32_t>{0x0A, 0x01001300, 0x0B, 0x01001301}));
  EXPECT_EQ(remapped_words(reread, "000201:Patch.esp", "SNDD"),
            (std::vector<std::uint32_t>{0x2, 0x01001310, 0x5, 0x01001311}));
  EXPECT_EQ(remapped_words(reread, "000202:Patch.esp", "CNAM"),
            (std::vector<std::uint32_t>{0x01001320, 0x3F000000, 0x01001321, 0x3F800000}));
}

TEST(StructPayloadRemap, ComponentPairsRemapAndKeepCounts) {
  auto out = plugin_with_masters();
  append_record(out, "COBJ", 0x02000300,
                {{"FVPA", words({0x1330, 3, 0x1331, 1})}});
  append_record(out, "MISC", 0x02000301, {{"CVPA", words({0x1340, 2})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000300:Patch.esp", "FVPA"),
            (std::vector<std::uint32_t>{0x01001330, 3, 0x01001331, 1}));
  EXPECT_EQ(remapped_words(reread, "000301:Patch.esp", "CVPA"),
            (std::vector<std::uint32_t>{0x01001340, 2}));
}

TEST(StructPayloadRemap, MarkerParamsKeywordRemapsByFormVersion) {
  auto out = plugin_with_masters();
  append_record(out, "FURN", 0x02000310,
                {{"SNAM", words({0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x1350, 0x55,
                                 0x66666666, 0x77777777, 0x88888888, 0x99999999, 0x1353, 0x0})}},
                131);
  append_record(out, "TERM", 0x02000311,
                {{"SNAM", words({0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x1351,
                                 0x55555555, 0x66666666, 0x77777777, 0x88888888, 0x1352})}},
                124);
  append_record(out, "TERM", 0x02000312,
                {{"SNAM", words({0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x1354,
                                 0x55555555})}},
                125);
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000310:Patch.esp", "SNAM"),
            (std::vector<std::uint32_t>{0x11111111, 0x22222222, 0x33333333, 0x44444444,
                                        0x01001350, 0x55, 0x66666666, 0x77777777, 0x88888888,
                                        0x99999999, 0x01001353, 0x0}));
  EXPECT_EQ(remapped_words(reread, "000311:Patch.esp", "SNAM"),
            (std::vector<std::uint32_t>{0x11111111, 0x22222222, 0x33333333, 0x44444444,
                                        0x01001351, 0x55555555, 0x66666666, 0x77777777,
                                        0x88888888, 0x01001352}));
  EXPECT_EQ(remapped_words(reread, "000312:Patch.esp", "SNAM"),
            (std::vector<std::uint32_t>{0x11111111, 0x22222222, 0x33333333, 0x44444444,
                                        0x01001354, 0x55555555}));
}

TEST(StructPayloadRemap, SingleSlotsAndAlchEnitRemapAndKeepScalars) {
  auto out = plugin_with_masters();
  append_record(out, "ALCH", 0x02000400,
                {{"ENIT", words({0x19, 0x2, 0x1360, 0x3F000000, 0x1361})}});
  fo4::test::ByteWriter radr;
  radr.u32(0x1370);
  radr.f32(1.0f);
  radr.f32(0.5f);
  radr.u8(1);
  radr.u8(0);
  append_record(out, "ACTI", 0x02000401, {{"RADR", radr.bytes}});
  append_record(out, "CELL", 0x02000402,
                {{"XILW", words({0x1380, 0x3F800000, 0x40000000, 0x40400000})}});
  append_record(out, "REFR", 0x02000403,
                {{"XTEL", words({0x1390, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x1, 0x1391})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000400:Patch.esp", "ENIT"),
            (std::vector<std::uint32_t>{0x19, 0x2, 0x01001360, 0x3F000000, 0x01001361}));
  EXPECT_EQ(remapped_words(reread, "000401:Patch.esp", "RADR"),
            (std::vector<std::uint32_t>{0x01001370, 0x3F800000, 0x3F000000}));
  EXPECT_EQ(remapped_words(reread, "000402:Patch.esp", "XILW"),
            (std::vector<std::uint32_t>{0x01001380, 0x3F800000, 0x40000000, 0x40400000}));
  EXPECT_EQ(remapped_words(reread, "000403:Patch.esp", "XTEL"),
            (std::vector<std::uint32_t>{0x01001390, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x1,
                                        0x01001391}));
}

TEST(StructPayloadRemap, WrongStrideSizeFailsClosedWithoutMutation) {
  auto out = plugin_with_masters();
  append_record(out, "KSSM", 0x02000500,
                {{"RNAM", words({0x0A, 0x1300, 0x0})}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, swapped_masters()), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
}
