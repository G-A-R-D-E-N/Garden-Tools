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

std::vector<std::byte> bytes_of(const std::vector<std::uint32_t>& values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::uint32_t> attack_words(std::uint32_t spell) {
  return {0x3F800000, 0x3F000000, spell, 0x4, 0x40000000, 0x40400000,
          0x40800000, 0x40A00000, 0x40C00000, 0x40E00000, 0x2};
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

TEST(GeneratorGapRemap, GlobalSignaturesRemapEveryFourByteSlot) {
  auto out = plugin_with_masters();
  append_record(out, "ARMO", 0x02000100, {{"EITM", words({0x1400})}});
  append_record(out, "EXPL", 0x02000101, {{"EITM", words({0x1401})}});
  append_record(out, "NPC_", 0x02000102,
                {{"CS2K", words({0x1402})}, {"CS2D", words({0x1403})},
                 {"CS2K", words({0x1404})}, {"CS2D", words({0x1405})}});
  append_record(out, "RACE", 0x02000103, {{"HEAD", words({0x1406})}, {"HEAD", words({0x1407})}});
  append_record(out, "PLYR", 0x02000104, {{"PLYR", words({0x1408})}});
  append_record(out, "MGEF", 0x02000105, {{"MDOB", words({0x1409})}, {"DATA", words({0x1410})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000100:Patch.esp", "EITM"),
            (std::vector<std::uint32_t>{0x01001400}));
  EXPECT_EQ(remapped_words(reread, "000101:Patch.esp", "EITM"),
            (std::vector<std::uint32_t>{0x01001401}));
  EXPECT_EQ(remapped_words(reread, "000102:Patch.esp", "CS2K"),
            (std::vector<std::uint32_t>{0x01001402, 0x01001404}));
  EXPECT_EQ(remapped_words(reread, "000102:Patch.esp", "CS2D"),
            (std::vector<std::uint32_t>{0x01001403, 0x01001405}));
  EXPECT_EQ(remapped_words(reread, "000103:Patch.esp", "HEAD"),
            (std::vector<std::uint32_t>{0x01001406, 0x01001407}));
  EXPECT_EQ(remapped_words(reread, "000104:Patch.esp", "PLYR"),
            (std::vector<std::uint32_t>{0x01001408}));
  EXPECT_EQ(remapped_words(reread, "000105:Patch.esp", "MDOB"),
            (std::vector<std::uint32_t>{0x01001409}));
  EXPECT_EQ(remapped_words(reread, "000105:Patch.esp", "DATA"),
            (std::vector<std::uint32_t>{0x1410}));
}

TEST(GeneratorGapRemap, ArmorKeywordArrayAndAddonModelsRemapRecordQualified) {
  auto out = plugin_with_masters();
  append_record(out, "ARMO", 0x02000110,
                {{"APPR", words({0x1420, 0x1421})}, {"MODL", words({0x1422})},
                 {"MODL", words({0x1423})}});
  append_record(out, "ARMA", 0x02000111, {{"MODL", words({0x1424})}, {"MODL", words({0x1425})}});
  append_record(out, "STAT", 0x02000112, {{"MODL", words({0x1426})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000110:Patch.esp", "APPR"),
            (std::vector<std::uint32_t>{0x01001420, 0x01001421}));
  EXPECT_EQ(remapped_words(reread, "000110:Patch.esp", "MODL"),
            (std::vector<std::uint32_t>{0x01001422, 0x01001423}));
  EXPECT_EQ(remapped_words(reread, "000111:Patch.esp", "MODL"),
            (std::vector<std::uint32_t>{0x01001424, 0x01001425}));
  EXPECT_EQ(remapped_words(reread, "000112:Patch.esp", "MODL"),
            (std::vector<std::uint32_t>{0x1426}));
}

TEST(GeneratorGapRemap, NpcTemplateAttacksAndOvisIndexRemapRecordQualified) {
  auto out = plugin_with_masters();
  append_record(out, "NPC_", 0x02000120,
                {{"TPTA", words({0x1430, 0x1431, 0x1432, 0x1433, 0x1434, 0x0, 0x1436, 0x1437,
                                 0x1438, 0x1439, 0x143A, 0x143B, 0x143C})}});
  append_record(out, "NPC_", 0x02000121,
                {{"ATKD", bytes_of(attack_words(0x1450))},
                 {"ATKD", bytes_of(attack_words(0x1451))}});
  append_record(out, "RACE", 0x02000122, {{"ATKD", bytes_of(attack_words(0x1452))}});
  append_record(out, "OVIS", 0x02000123, {{"INDX", words({0x1460})}});
  append_record(out, "NOCM", 0x02000124, {{"INDX", words({0x1461})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(remapped_words(reread, "000120:Patch.esp", "TPTA"),
            (std::vector<std::uint32_t>{0x01001430, 0x01001431, 0x01001432, 0x01001433,
                                        0x01001434, 0x0, 0x01001436, 0x01001437, 0x01001438,
                                        0x01001439, 0x0100143A, 0x0100143B, 0x0100143C}));
  auto attacks = attack_words(0x01001450);
  const auto second = attack_words(0x01001451);
  attacks.insert(attacks.end(), second.begin(), second.end());
  EXPECT_EQ(remapped_words(reread, "000121:Patch.esp", "ATKD"), attacks);
  EXPECT_EQ(remapped_words(reread, "000122:Patch.esp", "ATKD"), attack_words(0x01001452));
  EXPECT_EQ(remapped_words(reread, "000123:Patch.esp", "INDX"),
            (std::vector<std::uint32_t>{0x01001460}));
  EXPECT_EQ(remapped_words(reread, "000124:Patch.esp", "INDX"),
            (std::vector<std::uint32_t>{0x1461}));
}
