#include "fo4/environment/base_records.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
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

fo4::test::ByteWriter plugin_header() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  write_master(out, "Fallout4.esm");
  write_master(out, "DLC01.esm");
  write_master(out, "RadiationOverhaul.esp");
  fo4::test::end_record(out, tes4);
  return out;
}

void append_record(fo4::test::ByteWriter& out,
                   std::string_view signature,
                   std::uint32_t form_id,
                   std::vector<std::byte> wlst) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::write_subrecord(out, "WLST", std::move(wlst));
  if (signature == "CLMT") {
    const std::array timing{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    fo4::test::write_subrecord(out, "TNAM", timing);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void append_refr_record(fo4::test::ByteWriter& out,
                        std::uint32_t form_id,
                        std::uint32_t target_form_id) {
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Ref\0", 4});
  fo4::test::ByteWriter name;
  name.u32(target_form_id);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

fo4::test::ByteWriter make_plugin(std::vector<std::byte> clmt_wlst,
                                  std::vector<std::byte> control_wlst = {}) {
  auto out = plugin_header();
  append_record(out, "CLMT", 0x03000800, std::move(clmt_wlst));
  if (!control_wlst.empty()) {
    append_record(out, "MISC", 0x03000801, std::move(control_wlst));
  }
  return out;
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

}

TEST(FormLink, ClmtTimingEditAndMasterRemapPreserveWeatherChanceLanes) {
  fo4::test::ByteWriter wlst;
  wlst.u32(0x02000042);
  wlst.u32(static_cast<std::uint32_t>(-7));
  wlst.u32(0x00000099);
  wlst.u32(0x02000043);
  wlst.u32(42);
  wlst.u32(0x0000009A);
  const auto original_wlst = wlst.bytes;

  fo4::test::ByteWriter control;
  control.u32(0x02000052);
  control.u32(0x11223344);
  control.u32(0x0000009B);

  auto plugin = fo4::read_plugin(make_plugin(wlst.bytes, control.bytes).bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  ASSERT_TRUE(fo4::env::set_climate_timing(plugin, key, {128, 255, 36, 114, 99, 195}));
  ASSERT_NE(fo4::find_record(plugin, key), nullptr);
  EXPECT_EQ(find_sub(*fo4::find_record(plugin, key), "WLST")->data, original_wlst);
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* clmt = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(clmt, nullptr);
  const auto* clmt_wlst = find_sub(*clmt, "WLST");
  ASSERT_NE(clmt_wlst, nullptr);
  auto expected = original_wlst;
  write_u32(expected, 0, 0x00000042);
  write_u32(expected, 8, 0x01000099);
  write_u32(expected, 12, 0x00000043);
  write_u32(expected, 20, 0x0100009A);
  EXPECT_EQ(clmt_wlst->data, expected);
  const auto* timing = find_sub(*clmt, "TNAM");
  ASSERT_NE(timing, nullptr);
  const std::vector wanted{std::byte{128}, std::byte{255}, std::byte{36}, std::byte{114}, std::byte{99}, std::byte{195}};
  EXPECT_EQ(timing->data, wanted);

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 4);
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[1].offset, 8);
  EXPECT_EQ(links[2].offset, 12);
  EXPECT_EQ(links[3].offset, 20);

  const auto* misc = fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp"));
  ASSERT_NE(misc, nullptr);
  const auto* misc_wlst = find_sub(*misc, "WLST");
  ASSERT_NE(misc_wlst, nullptr);
  EXPECT_EQ(misc_wlst->data, control.bytes);
}

TEST(FormLink, ClmtWlstMalformedPayloadFailsClosedWithoutMutation) {
  std::vector<std::byte> malformed(13);
  for (std::size_t i = 0; i < malformed.size(); ++i) {
    malformed[i] = std::byte{static_cast<std::uint8_t>(i)};
  }
  auto plugin = fo4::read_plugin(make_plugin(std::move(malformed)).bytes, "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      (void)fo4::remap_plugin_masters(plugin, reordered_masters()),
      fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(FormLink, ClmtWlstDirectRekeyPersistsThroughWriter) {
  fo4::test::ByteWriter wlst;
  wlst.u32(0x02000042);
  wlst.u32(static_cast<std::uint32_t>(-7));
  wlst.u32(0x00000099);

  auto plugin = fo4::read_plugin(make_plugin(wlst.bytes).bytes, "Patch.esp");
  const auto old_key = fo4::FormKey::parse("000042:RadiationOverhaul.esp");
  const auto new_key = fo4::FormKey::parse("000043:RadiationOverhaul.esp");

  fo4::rekey_form_links(plugin, old_key, new_key);
  EXPECT_TRUE(fo4::plugin_original_bytes(plugin).empty());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* clmt = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(clmt, nullptr);
  const auto* clmt_wlst = find_sub(*clmt, "WLST");
  ASSERT_NE(clmt_wlst, nullptr);
  std::uint32_t rekeyed_weather = 0;
  std::memcpy(&rekeyed_weather, clmt_wlst->data.data(), sizeof(rekeyed_weather));
  EXPECT_EQ(rekeyed_weather, 0x02000043);
}

TEST(FormLink, ClmtWlstCollectionPreservesTraversalOrder) {
  fo4::test::ByteWriter wlst;
  wlst.u32(0x02000042);
  wlst.u32(static_cast<std::uint32_t>(-7));
  wlst.u32(0x00000099);

  auto out = plugin_header();
  append_refr_record(out, 0x03000799, 0x00000042);
  append_record(out, "CLMT", 0x03000800, std::move(wlst.bytes));
  append_refr_record(out, 0x03000802, 0x00000043);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto links = fo4::collect_form_links(plugin);
  ASSERT_EQ(links.size(), 4);

  EXPECT_EQ(links[0].record, fo4::FormKey::parse("000799:Patch.esp"));
  EXPECT_EQ(links[0].subrecord.to_string(), "NAME");

  EXPECT_EQ(links[1].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[1].subrecord.to_string(), "WLST");
  EXPECT_EQ(links[1].offset, 0);

  EXPECT_EQ(links[2].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[2].subrecord.to_string(), "WLST");
  EXPECT_EQ(links[2].offset, 8);

  EXPECT_EQ(links[3].record, fo4::FormKey::parse("000802:Patch.esp"));
  EXPECT_EQ(links[3].subrecord.to_string(), "NAME");
}
