#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<std::byte> race_sraf_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto keywords = fo4::test::begin_group(out, "KYWD", 0);
  const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000800, 131);
  fo4::test::end_record(out, keyword);
  fo4::test::end_group(out, keywords);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000801, 131);
  fo4::test::ByteWriter link;
  link.u32(0x00000800);
  fo4::test::write_subrecord(out, "SAKD", link.bytes);
  const std::vector<std::byte> flags{
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
  fo4::test::write_subrecord(out, "SRAF", flags);
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);
  return out.bytes;
}

std::vector<std::byte> info_sraf_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto cameras = fo4::test::begin_group(out, "CPTH", 0);
  const auto camera = fo4::test::begin_record(out, "CPTH", 0, 0x00000800, 131);
  fo4::test::end_record(out, camera);
  fo4::test::end_group(out, cameras);

  const auto infos = fo4::test::begin_group(out, "INFO", 0);
  const auto info = fo4::test::begin_record(out, "INFO", 0, 0x00000801, 131);
  fo4::test::ByteWriter camera_link;
  camera_link.u32(0x00000800);
  fo4::test::write_subrecord(out, "SRAF", camera_link.bytes);
  fo4::test::end_record(out, info);
  fo4::test::end_group(out, infos);
  return out.bytes;
}

std::uint32_t sraf_raw(const fo4::Record& record) {
  const auto signature = fo4::Signature::from_string("SRAF");
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    EXPECT_EQ(subrecord.data.size(), 4u);
    if (subrecord.data.size() != 4) return 0;
    std::uint32_t raw = 0;
    std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
    return raw;
  }
  ADD_FAILURE() << "missing SRAF";
  return 0;
}

}

TEST(SrafFormLinks, RaceSubgraphFlagsAreNotCollectedOrRekeyed) {
  auto plugin = fo4::read_plugin(race_sraf_plugin(), "Sraf.esp");
  const auto race_key = fo4::FormKey::parse("000801:Sraf.esp");
  const auto keyword_key = fo4::FormKey::parse("000800:Sraf.esp");

  const auto links = fo4::collect_form_links(plugin);
  std::size_t sakd_hits = 0;
  std::size_t sraf_hits = 0;
  for (const auto& link : links) {
    if (link.record != race_key) continue;
    if (link.subrecord.to_string() == "SAKD" && link.key == keyword_key) ++sakd_hits;
    if (link.subrecord.to_string() == "SRAF") ++sraf_hits;
  }
  EXPECT_EQ(sakd_hits, 1u);
  EXPECT_EQ(sraf_hits, 0u);

  const auto fake_old = fo4::FormKey::parse("000001:Sraf.esp");
  const auto fake_new = fo4::FormKey::parse("000002:Sraf.esp");
  fo4::rekey_form_links(plugin, fake_old, fake_new);
  const auto* race = fo4::find_record(plugin, race_key);
  ASSERT_NE(race, nullptr);
  EXPECT_EQ(sraf_raw(*race), 1u);
}

TEST(SrafFormLinks, InfoCameraPathRemainsAFormLink) {
  auto plugin = fo4::read_plugin(info_sraf_plugin(), "Sraf.esp");
  const auto info_key = fo4::FormKey::parse("000801:Sraf.esp");
  const auto camera_key = fo4::FormKey::parse("000800:Sraf.esp");

  const auto links = fo4::collect_form_links(plugin);
  std::size_t camera_hits = 0;
  for (const auto& link : links) {
    if (link.record == info_key && link.subrecord.to_string() == "SRAF" &&
        link.key == camera_key) {
      ++camera_hits;
    }
  }
  EXPECT_EQ(camera_hits, 1u);
}

TEST(InfoFormLinks, UnknownFieldsFailOnlyWhenContextsDiffer) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("INFO");
  record.subrecords.push_back({
      fo4::Signature::from_string("ZZZZ"),
      {std::byte{0x10}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}},
      false});
  fo4::PluginFormIdContext context;
  context.current_plugin = fo4::PluginKey::from_filename("Same.esp");
  const auto other = fo4::PluginKey::from_filename("Other.esp");
  auto changed = context;
  changed.current_plugin = other;

  const auto before = record.subrecords[0].data;
  fo4::remap_record_form_links(record, context, context);
  EXPECT_EQ(record.subrecords[0].data, before);
  EXPECT_THROW(
      fo4::remap_record_form_links(record, context, changed),
      fo4::FormIdError);
}
