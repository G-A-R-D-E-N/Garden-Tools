#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

fo4::test::ByteWriter plugin_header(std::uint32_t records = 1) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(records);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : {"Fallout4.esm", "Unused.esm", "Objects.esp"}) {
    fo4::test::ByteWriter name;
    name.raw(master);
    name.u8(0);
    fo4::test::write_subrecord(out, "MAST", name.bytes);
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  return out;
}

void append_record(fo4::test::ByteWriter& out, std::string_view signature,
                   std::uint32_t id,
                   const std::vector<std::vector<std::byte>>& dnams) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  for (const auto& dnam : dnams) fo4::test::write_subrecord(out, "DNAM", dnam);
  if (signature == "DOBJ") {
    fo4::test::write_subrecord(out, "ZZZZ", words({0x02000042, 0xDEADBEEF}));
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<fo4::PluginKey> reordered_masters() {
  return {fo4::PluginKey::from_filename("Objects.esp"),
          fo4::PluginKey::from_filename("Fallout4.esm")};
}

}

TEST(FormLink, DobjDnamPackedRowsRemapOnlyObjectsAndPreserveBytes) {
  auto out = plugin_header();
  const auto original = words({0x50535757, 0x02000042, 0x02000042, 0x00000099,
                               0, 0x02000043, 0xFFFFFFFF, 0});
  append_record(out, "DOBJ", 0x03000800, {original});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto* before = fo4::find_record(plugin, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(before, nullptr);
  const auto original_subs = before->subrecords;

  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), original_subs.size());
  for (std::size_t i = 0; i < original_subs.size(); ++i) {
    EXPECT_EQ(record->subrecords[i].signature, original_subs[i].signature);
    if (i != 1) {
      EXPECT_EQ(record->subrecords[i].data, original_subs[i].data);
    }
  }
  EXPECT_EQ(record->subrecords[1].data,
            words({0x50535757, 0x00000042, 0x02000042, 0x01000099,
                   0, 0x00000043, 0xFFFFFFFF, 0}));

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 4);
  const std::uint32_t expected[] = {0x00000042, 0x01000099, 0x00000043, 0};
  for (std::size_t i = 0; i < links.size(); ++i) {
    EXPECT_EQ(links[i].record, record->form_key);
    EXPECT_EQ(links[i].subrecord.to_string(), "DNAM");
    EXPECT_EQ(links[i].subrecord_ordinal, 0);
    EXPECT_EQ(links[i].offset, 4 + 8 * i);
    EXPECT_EQ(links[i].raw.value(), expected[i]);
  }
}

TEST(FormLink, DobjDnamTruncatedPayloadsFailClosedWithoutMutation) {
  for (const std::size_t size : {1, 4, 7, 9, 12, 15, 17}) {
    SCOPED_TRACE(size);
    auto malformed = words({0x02000042, 0x00000099, 0x02000043, 0x0000009A, 0});
    malformed.resize(size);
    auto out = plugin_header(2);
    append_record(out, "IPCT", 0x03000799, {words({0x02000042})});
    append_record(out, "DOBJ", 0x03000800, {malformed});
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, reordered_masters()),
                 fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
    EXPECT_THROW(fo4::rekey_form_links(plugin,
                     fo4::FormKey::parse("000042:Objects.esp"),
                     fo4::FormKey::parse("000043:Objects.esp")), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
    const auto links = fo4::collect_form_links(plugin);
    ASSERT_EQ(links.size(), 1);
    EXPECT_EQ(links[0].record, fo4::FormKey::parse("000799:Patch.esp"));
  }
}

TEST(FormLink, DobjDnamSeparateSubrecordsFailClosed) {
  auto out = plugin_header();
  append_record(out, "DOBJ", 0x03000800,
                {words({0x02000042, 0x00000099}), words({0x02000043, 0x0000009A})});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, reordered_masters()),
               fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
  EXPECT_TRUE(fo4::collect_form_links(plugin).empty());
}

TEST(FormLink, DobjDnamEmptyArrayRemainsEmpty) {
  auto out = plugin_header();
  append_record(out, "DOBJ", 0x03000800, {{}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 3);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "DNAM");
  EXPECT_TRUE(record->subrecords[1].data.empty());
  EXPECT_EQ(record->subrecords[2].data, words({0x02000042, 0xDEADBEEF}));
  EXPECT_TRUE(fo4::collect_form_links(reread).empty());
}

TEST(FormLink, DobjDnamCollisionControlsPreserveQuestAndRemapIpctAndInfo) {
  auto out = plugin_header(5);
  const auto quest_data = words({0x02000042, 0, 0});
  append_record(out, "QUST", 0x03000798, {words({0x02000042})});
  append_record(out, "QUST", 0x03000799, {quest_data});
  append_record(out, "DOBJ", 0x03000800, {words({0x02000042, 0x02000043})});
  append_record(out, "IPCT", 0x03000801, {words({0x02000042})});
  append_record(out, "INFO", 0x03000802, {words({0x02000044})});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* quest = fo4::find_record(reread, fo4::FormKey::parse("000799:Patch.esp"));
  const auto* short_quest = fo4::find_record(reread, fo4::FormKey::parse("000798:Patch.esp"));
  ASSERT_NE(short_quest, nullptr);
  ASSERT_EQ(short_quest->subrecords.size(), 2);
  EXPECT_EQ(short_quest->subrecords[1].data, words({0x02000042}));
  ASSERT_NE(quest, nullptr);
  ASSERT_EQ(quest->subrecords.size(), 2);
  EXPECT_EQ(quest->subrecords[1].data, quest_data);
  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 3);
  EXPECT_EQ(links[0].raw.value(), 0x00000043);
  EXPECT_EQ(links[0].offset, 4);
  EXPECT_EQ(links[1].record, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(links[1].raw.value(), 0x00000042);
  EXPECT_EQ(links[1].offset, 0);
  EXPECT_EQ(links[2].record, fo4::FormKey::parse("000802:Patch.esp"));
  EXPECT_EQ(links[2].raw.value(), 0x00000044);
  EXPECT_EQ(links[2].offset, 0);
}

TEST(FormLink, DobjDnamDirectRekeyPersistsAndPreservesAllOtherBytes) {
  auto out = plugin_header();
  append_record(out, "DOBJ", 0x03000800,
                {words({0x02000042, 0x02000042, 0x50535757, 0x00000099})});
  auto expected = plugin_header();
  append_record(expected, "DOBJ", 0x03000800,
                {words({0x02000042, 0x02000043, 0x50535757, 0x00000099})});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  ASSERT_FALSE(fo4::plugin_original_bytes(plugin).empty());
  fo4::rekey_form_links(plugin, fo4::FormKey::parse("000042:Objects.esp"),
                        fo4::FormKey::parse("000043:Objects.esp"));
  EXPECT_TRUE(fo4::plugin_original_bytes(plugin).empty());
  EXPECT_EQ(fo4::write_plugin(plugin), expected.bytes);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::write_plugin(reread), expected.bytes);
  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 2);
  EXPECT_EQ(links[0].raw.value(), 0x02000043);
  EXPECT_EQ(links[1].raw.value(), 0x00000099);
}

TEST(FormLink, DobjDnamCollectionPreservesDocumentOrder) {
  auto out = plugin_header(3);
  append_record(out, "IPCT", 0x03000799, {words({0x02000041})});
  append_record(out, "DOBJ", 0x03000800,
                {words({0x02000042, 0x02000042, 0x50535757, 0x00000099})});
  append_record(out, "INFO", 0x03000801, {words({0x02000043})});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto links = fo4::collect_form_links(plugin);
  ASSERT_EQ(links.size(), 4);
  const char* records[] = {"000799:Patch.esp", "000800:Patch.esp",
                           "000800:Patch.esp", "000801:Patch.esp"};
  const std::size_t offsets[] = {0, 4, 12, 0};
  const std::uint32_t values[] = {0x02000041, 0x02000042, 0x00000099, 0x02000043};
  for (std::size_t i = 0; i < links.size(); ++i) {
    EXPECT_EQ(links[i].record, fo4::FormKey::parse(records[i]));
    EXPECT_EQ(links[i].subrecord.to_string(), "DNAM");
    EXPECT_EQ(links[i].subrecord_ordinal, 0);
    EXPECT_EQ(links[i].offset, offsets[i]);
    EXPECT_EQ(links[i].raw.value(), values[i]);
  }
}
