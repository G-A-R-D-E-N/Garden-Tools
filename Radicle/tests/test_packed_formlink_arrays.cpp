#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> u32_then_u8(std::uint32_t value, std::uint8_t tail) {
  fo4::test::ByteWriter out;
  out.u32(value);
  out.u8(tail);
  return out.bytes;
}

void write_master(fo4::test::ByteWriter& out, std::string_view name) {
  std::vector<std::byte> bytes;
  bytes.reserve(name.size() + 1);
  for (const char ch : name) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "MAST", bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

fo4::test::ByteWriter plugin_header(std::uint32_t records = 1) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(records);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  write_master(out, "Fallout4.esm");
  write_master(out, "DLC01.esm");
  write_master(out, "RadiationOverhaul.esp");
  fo4::test::end_record(out, tes4);
  return out;
}

void append_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                   std::string_view sub_sig, std::vector<std::byte> payload) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::write_subrecord(out, sub_sig, std::move(payload));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

std::vector<fo4::PluginKey> reordered_masters() {
  return {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
          fo4::PluginKey::from_filename("Fallout4.esm")};
}

std::uint32_t u32_at(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::vector<std::byte> filled_with_u32(
    std::size_t size, std::initializer_list<std::pair<std::size_t, std::uint32_t>> fields) {
  std::vector<std::byte> data(size, std::byte{0x11});
  for (const auto& [offset, value] : fields) std::memcpy(data.data() + offset, &value, 4);
  return data;
}

}

TEST(FormLink, PackedOmodMnamAndFnamRemapEveryLane) {
  auto out = plugin_header(1);
  const auto group = fo4::test::begin_group(out, "OMOD", 0);
  const auto record = fo4::test::begin_record(out, "OMOD", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::write_subrecord(out, "MNAM", words({0x02000042, 0x00000099, 0x02000043}));
  fo4::test::write_subrecord(out, "FNAM", words({0x02000044, 0x0000009A}));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* omod = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(omod, nullptr);
  const auto* mnam = find_sub(*omod, "MNAM");
  const auto* fnam = find_sub(*omod, "FNAM");
  ASSERT_NE(mnam, nullptr);
  ASSERT_NE(fnam, nullptr);
  EXPECT_EQ(mnam->data, words({0x00000042, 0x01000099, 0x00000043}));
  EXPECT_EQ(fnam->data, words({0x00000044, 0x0100009A}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 5);
  EXPECT_EQ(links[0].subrecord.to_string(), "MNAM");
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[2].offset, 8);
  EXPECT_EQ(links[3].subrecord.to_string(), "FNAM");
  EXPECT_EQ(links[4].offset, 4);
}

TEST(FormLink, PackedEqupPnamAndMuscTnamRemapEveryLane) {
  auto out = plugin_header(2);
  append_record(out, "EQUP", 0x03000800, "PNAM", words({0x02000042, 0x00000099, 0x02000043}));
  append_record(out, "MUSC", 0x03000801, "TNAM", words({0x02000044, 0x0000009A}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* equp = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  const auto* musc = fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp"));
  ASSERT_NE(equp, nullptr);
  ASSERT_NE(musc, nullptr);
  ASSERT_NE(find_sub(*equp, "PNAM"), nullptr);
  ASSERT_NE(find_sub(*musc, "TNAM"), nullptr);
  EXPECT_EQ(find_sub(*equp, "PNAM")->data, words({0x00000042, 0x01000099, 0x00000043}));
  EXPECT_EQ(find_sub(*musc, "TNAM")->data, words({0x00000044, 0x0100009A}));
}

TEST(FormLink, PackedArrayMalformedPayloadFailsClosedWithoutMutation) {
  for (const auto& [sig, sub, form_id] :
       {std::tuple{"OMOD", "MNAM", 0x03000800u}, {"EQUP", "PNAM", 0x03000800u},
        {"MUSC", "TNAM", 0x03000800u}}) {
    SCOPED_TRACE(sig);
    std::vector<std::byte> malformed(5, std::byte{0x11});
    auto out = plugin_header();
    append_record(out, sig, form_id, sub, malformed);
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    const auto before = fo4::write_plugin(plugin);
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, reordered_masters()), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
    EXPECT_THROW(fo4::rekey_form_links(plugin, fo4::FormKey::parse("000011:RadiationOverhaul.esp"),
                                       fo4::FormKey::parse("000012:RadiationOverhaul.esp")),
                 fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(FormLink, StructuredPayloadMalformedFailsClosedWithoutMutation) {
  for (const auto& [sig, sub, size] :
       {std::tuple{"EXPL", "DATA", std::size_t{20}},
        std::tuple{"PROJ", "DNAM", std::size_t{92}},
        std::tuple{"HAZD", "DNAM", std::size_t{48}},
        std::tuple{"ACTI", "PRPS", std::size_t{20}},
        std::tuple{"ACTI", "DAMC", std::size_t{20}},
        std::tuple{"ARMO", "DAMA", std::size_t{20}}}) {
    SCOPED_TRACE(sig);
    std::vector<std::byte> malformed(size, std::byte{0x11});
    auto out = plugin_header();
    append_record(out, sig, 0x03000800, sub, malformed);
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    const auto before = fo4::write_plugin(plugin);
    EXPECT_TRUE(fo4::collect_form_links(plugin).empty());
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, reordered_masters()), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
    EXPECT_THROW(fo4::rekey_form_links(plugin, fo4::FormKey::parse("000011:RadiationOverhaul.esp"),
                                       fo4::FormKey::parse("000012:RadiationOverhaul.esp")),
                 fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(FormLink, PackedArrayEmptyPayloadRemainsEmpty) {
  auto out = plugin_header();
  append_record(out, "OMOD", 0x03000800, "MNAM", {});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* omod = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(omod, nullptr);
  const auto* mnam = find_sub(*omod, "MNAM");
  ASSERT_NE(mnam, nullptr);
  EXPECT_TRUE(mnam->data.empty());
  EXPECT_TRUE(fo4::collect_form_links(reread).empty());
}

TEST(FormLink, PackedArrayCollisionControlsPreserveForeignLayouts) {
  auto out = plugin_header(5);
  fo4::test::ByteWriter must_fnam;
  must_fnam.f32(1.5f);
  must_fnam.f32(2.5f);
  const auto must_bytes = must_fnam.bytes;
  fo4::test::ByteWriter navm_pnam;
  navm_pnam.u16(0x0042);
  navm_pnam.u16(0x0043);
  navm_pnam.u16(0x0044);
  navm_pnam.u16(0x0045);
  const auto navm_bytes = navm_pnam.bytes;
  append_record(out, "MUST", 0x03000798, "FNAM", must_bytes);
  append_record(out, "NAVM", 0x03000799, "PNAM", navm_bytes);
  append_record(out, "QUST", 0x0300079A, "FNAM", words({0x02000042}));
  append_record(out, "OMOD", 0x03000800, "MNAM", words({0x02000043, 0x00000099}));
  append_record(out, "NPC_", 0x03000801, "PNAM", words({0x02000044}));

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("000798:Patch.esp")), "FNAM")->data,
            must_bytes);
  EXPECT_EQ(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("000799:Patch.esp")), "PNAM")->data,
            navm_bytes);
  EXPECT_EQ(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("00079A:Patch.esp")), "FNAM")->data,
            words({0x02000042}));
  EXPECT_EQ(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp")), "MNAM")->data,
            words({0x00000043, 0x01000099}));
  EXPECT_EQ(u32_at(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp")), "PNAM")->data,
                   0),
            0x00000044);
}

TEST(FormLink, PackedArrayDirectRekeyPersistsThroughWriter) {
  auto out = plugin_header();
  append_record(out, "OMOD", 0x03000800, "MNAM", words({0x02000042, 0x00000099}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  ASSERT_FALSE(fo4::plugin_original_bytes(plugin).empty());
  fo4::rekey_form_links(plugin, fo4::FormKey::parse("000042:RadiationOverhaul.esp"),
                        fo4::FormKey::parse("000043:RadiationOverhaul.esp"));
  EXPECT_TRUE(fo4::plugin_original_bytes(plugin).empty());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(find_sub(*fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp")), "MNAM")->data,
            words({0x02000043, 0x00000099}));
}

TEST(FormLink, PackedArrayCollectionPreservesDocumentOrder) {
  auto out = plugin_header(3);
  append_record(out, "NPC_", 0x03000799, "PNAM", words({0x02000041}));
  append_record(out, "OMOD", 0x03000800, "MNAM", words({0x02000042, 0x00000099}));
  append_record(out, "MUSC", 0x03000801, "TNAM", words({0x02000043}));
  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto links = fo4::collect_form_links(plugin);
  ASSERT_EQ(links.size(), 4);
  EXPECT_EQ(links[0].record, fo4::FormKey::parse("000799:Patch.esp"));
  EXPECT_EQ(links[1].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[1].offset, 0);
  EXPECT_EQ(links[2].offset, 4);
  EXPECT_EQ(links[3].record, fo4::FormKey::parse("000801:Patch.esp"));
}

TEST(FormLink, FixedStrideStructArraysRemapFormIdsAndKeepValues) {
  auto out = plugin_header(2);
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::ByteWriter prps;
  prps.u32(0x02000042);
  prps.f32(1.5f);
  prps.u32(0x00000099);
  prps.f32(-2.25f);
  fo4::test::write_subrecord(out, "PRPS", prps.bytes);
  fo4::test::ByteWriter damc;
  damc.u32(0x02000043);
  damc.u32(0x02000044);
  damc.u32(0x00000099);
  damc.u32(7);
  fo4::test::write_subrecord(out, "DAMC", damc.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  append_record(out, "ARMO", 0x03000801, "DAMA", words({0x02000045, 3, 0x00000099, 4}));

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* acti = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(acti, nullptr);
  const auto* prps_after = find_sub(*acti, "PRPS");
  const auto* damc_after = find_sub(*acti, "DAMC");
  ASSERT_NE(prps_after, nullptr);
  ASSERT_NE(damc_after, nullptr);
  fo4::test::ByteWriter prps_expected;
  prps_expected.u32(0x00000042);
  prps_expected.f32(1.5f);
  prps_expected.u32(0x01000099);
  prps_expected.f32(-2.25f);
  EXPECT_EQ(prps_after->data, prps_expected.bytes);
  fo4::test::ByteWriter damc_expected;
  damc_expected.u32(0x00000043);
  damc_expected.u32(0x02000044);
  damc_expected.u32(0x01000099);
  damc_expected.u32(7);
  EXPECT_EQ(damc_after->data, damc_expected.bytes);
  const auto* armo = fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp"));
  ASSERT_NE(armo, nullptr);
  ASSERT_NE(find_sub(*armo, "DAMA"), nullptr);
  EXPECT_EQ(find_sub(*armo, "DAMA")->data, words({0x00000045, 3, 0x01000099, 4}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 6);
  EXPECT_EQ(links[0].subrecord.to_string(), "PRPS");
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[1].offset, 8);
  EXPECT_EQ(links[2].subrecord.to_string(), "DAMC");
  EXPECT_EQ(links[2].offset, 0);
  EXPECT_EQ(links[3].offset, 8);
  EXPECT_EQ(links[4].subrecord.to_string(), "DAMA");
  EXPECT_EQ(links[4].record, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(links[5].offset, 8);
}

TEST(FormLink, FactionXnamRelationFormIdRemapsAndKeepsModifierAndReaction) {
  auto out = plugin_header(1);
  append_record(out, "FACT", 0x03000800, "XNAM", words({0x00000099, 0x00000099, 2}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* fact = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(fact, nullptr);
  ASSERT_NE(find_sub(*fact, "XNAM"), nullptr);
  EXPECT_EQ(find_sub(*fact, "XNAM")->data, words({0x01000099, 0x00000099, 2}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "XNAM");
  EXPECT_EQ(links[0].offset, 0);
}

TEST(FormLink, WrongSizedFixedLayoutPayloadsFailClosedUnderStrictRemap) {
  const struct {
    const char* record;
    const char* subrecord;
    std::size_t size;
  } rows[] = {{"FACT", "XNAM", 13}, {"NPC_", "SNAM", 6}, {"NPC_", "PRKR", 4},
              {"LVLN", "LLKC", 12}, {"WEAP", "DNAM", 131}};
  for (const auto& row : rows) {
    SCOPED_TRACE(std::string(row.record) + " " + row.subrecord);
    auto out = plugin_header();
    append_record(out, row.record, 0x03000800, row.subrecord,
                  std::vector<std::byte>(row.size, std::byte{0x11}));
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    const auto before = fo4::write_plugin(plugin);
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, reordered_masters()), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(FormLink, DamaCurveTableElementsRemapAtFormVersion152) {
  auto out = plugin_header(1);
  const auto group = fo4::test::begin_group(out, "ARMO", 0);
  const auto record = fo4::test::begin_record(out, "ARMO", 0, 0x03000800, 152);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::write_subrecord(out, "DAMA",
                             words({0x02000045, 0x02000044, 0x02000046, 0x00000099, 4, 0x00000047}));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* armo = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(armo, nullptr);
  ASSERT_NE(find_sub(*armo, "DAMA"), nullptr);
  EXPECT_EQ(find_sub(*armo, "DAMA")->data,
            words({0x00000045, 0x02000044, 0x00000046, 0x01000099, 4, 0x01000047}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 4);
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[1].offset, 8);
  EXPECT_EQ(links[2].offset, 12);
  EXPECT_EQ(links[3].offset, 20);
}

TEST(FormLink, NpcFactionSnamRemapsFormIdAndKeepsRank) {
  auto out = plugin_header(1);
  append_record(out, "NPC_", 0x03000800, "SNAM", u32_then_u8(0x00000099, 0xFE));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* npc = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(npc, nullptr);
  ASSERT_NE(find_sub(*npc, "SNAM"), nullptr);
  EXPECT_EQ(find_sub(*npc, "SNAM")->data, u32_then_u8(0x01000099, 0xFE));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "SNAM");
  EXPECT_EQ(links[0].offset, 0);
}

TEST(FormLink, NpcPerkPrkrRemapsFormIdAndKeepsRank) {
  auto out = plugin_header(1);
  append_record(out, "NPC_", 0x03000800, "PRKR", u32_then_u8(0x02000044, 3));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* npc = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(npc, nullptr);
  ASSERT_NE(find_sub(*npc, "PRKR"), nullptr);
  EXPECT_EQ(find_sub(*npc, "PRKR")->data, u32_then_u8(0x00000044, 3));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "PRKR");
  EXPECT_EQ(links[0].offset, 0);
}

TEST(FormLink, LeveledListLlkcKeywordsRemapAndKeepChances) {
  auto out = plugin_header(1);
  append_record(out, "LVLI", 0x03000800, "LLKC",
                words({0x00000099, 50, 0x02000044, 0x00000005}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* lvli = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(lvli, nullptr);
  ASSERT_NE(find_sub(*lvli, "LLKC"), nullptr);
  EXPECT_EQ(find_sub(*lvli, "LLKC")->data, words({0x01000099, 50, 0x00000044, 0x00000005}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 2);
  EXPECT_EQ(links[0].subrecord.to_string(), "LLKC");
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[1].offset, 8);
}

TEST(FormLink, WeaponDnamFormIdSlotsRemapAndKeepOtherBytes) {
  auto out = plugin_header(1);
  append_record(out, "WEAP", 0x03000800, "DNAM",
                filled_with_u32(132, {{0, 0x00000099}, {40, 0x02000044}, {44, 0x00000099},
                                      {73, 0x00000099}, {77, 0}, {81, 0}, {85, 0}, {89, 0},
                                      {93, 0}, {97, 0}, {101, 0}}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(weap, nullptr);
  ASSERT_NE(find_sub(*weap, "DNAM"), nullptr);
  EXPECT_EQ(find_sub(*weap, "DNAM")->data,
            filled_with_u32(132, {{0, 0x01000099}, {40, 0x00000044}, {44, 0x01000099},
                                  {73, 0x01000099}, {77, 0}, {81, 0}, {85, 0}, {89, 0},
                                  {93, 0}, {97, 0}, {101, 0}}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 11);
  EXPECT_EQ(links[0].subrecord.to_string(), "DNAM");
  constexpr std::size_t expected[] = {0, 40, 44, 73, 77, 81, 85, 89, 93, 97, 101};
  for (std::size_t i = 0; i < links.size(); ++i) {
    EXPECT_EQ(links[i].offset, expected[i]) << i;
  }
}

TEST(FormLink, WeaponFnamOverrideProjectileRemapsAndKeepsOtherBytes) {
  auto out = plugin_header(1);
  append_record(out, "WEAP", 0x03000800, "FNAM", filled_with_u32(41, {{29, 0x00000099}}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(weap, nullptr);
  ASSERT_NE(find_sub(*weap, "FNAM"), nullptr);
  EXPECT_EQ(find_sub(*weap, "FNAM")->data, filled_with_u32(41, {{29, 0x01000099}}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "FNAM");
  EXPECT_EQ(links[0].offset, 29);
}

TEST(FormLink, WeaponCrdtCritEffectRemapsAndKeepsMultipliers) {
  auto out = plugin_header(1);
  append_record(out, "WEAP", 0x03000800, "CRDT", words({0x40000000, 0x3F800000, 0x02000044}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(weap, nullptr);
  ASSERT_NE(find_sub(*weap, "CRDT"), nullptr);
  EXPECT_EQ(find_sub(*weap, "CRDT")->data, words({0x40000000, 0x3F800000, 0x00000044}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "CRDT");
  EXPECT_EQ(links[0].offset, 8);
}

TEST(FormLink, FactionPlvdFormIdTypeRemapsAndKeepsRadius) {
  auto out = plugin_header(1);
  append_record(out, "FACT", 0x03000800, "PLVD", words({4, 0x02000044, 7, 2}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* fact = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(fact, nullptr);
  ASSERT_NE(find_sub(*fact, "PLVD"), nullptr);
  EXPECT_EQ(find_sub(*fact, "PLVD")->data, words({4, 0x00000044, 7, 2}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 1);
  EXPECT_EQ(links[0].subrecord.to_string(), "PLVD");
  EXPECT_EQ(links[0].offset, 4);
}

TEST(FormLink, FactionPlvdNonFormIdTypeStaysUnchanged) {
  auto out = plugin_header(1);
  append_record(out, "FACT", 0x03000800, "PLVD", words({5, 0x02000044, 7}));
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* fact = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(fact, nullptr);
  ASSERT_NE(find_sub(*fact, "PLVD"), nullptr);
  EXPECT_EQ(find_sub(*fact, "PLVD")->data, words({5, 0x02000044, 7}));
  EXPECT_TRUE(fo4::collect_form_links(reread).empty());
}
