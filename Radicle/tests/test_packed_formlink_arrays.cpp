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
#include <vector>

namespace {

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
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
        std::tuple{"HAZD", "DNAM", std::size_t{48}}}) {
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
