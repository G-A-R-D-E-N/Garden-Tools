#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::uint16_t read_u16(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DropMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"KeepMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NAVM", 0);
  const auto record = fo4::test::begin_record(out, "NAVM", 0, 0x03000800);

  fo4::test::ByteWriter onam;
  onam.u32(0x00000007u);
  onam.u32(0x02000812u);
  onam.u32(0x02000813u);
  fo4::test::write_subrecord(out, "ONAM", onam.bytes);

  fo4::test::ByteWriter pnam;
  pnam.u16(0x0002u);
  pnam.u16(0x0200u);
  pnam.u16(0x0812u);
  pnam.u16(0x1234u);
  fo4::test::write_subrecord(out, "PNAM", pnam.bytes);

  fo4::test::ByteWriter nnam;
  nnam.u16(0x0003u);
  nnam.u16(0x0200u);
  nnam.u16(0x0813u);
  nnam.u16(0x5678u);
  fo4::test::write_subrecord(out, "NNAM", nnam.bytes);

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(FormLink, NavmBaseObjectOnamArrayRemapsWithoutTouchingVertexIndexes) {
  auto plugin = fo4::read_plugin(make_plugin(), "NavmeshPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(plugin), "NavmeshPatch.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:NavmeshPatch.esp"));
  ASSERT_NE(record, nullptr);

  const auto* onam = find_sub(*record, "ONAM");
  const auto* pnam = find_sub(*record, "PNAM");
  const auto* nnam = find_sub(*record, "NNAM");
  ASSERT_NE(onam, nullptr);
  ASSERT_NE(pnam, nullptr);
  ASSERT_NE(nnam, nullptr);

  ASSERT_EQ(onam->data.size(), 12u);
  EXPECT_EQ(read_u32(onam->data, 0), 0x00000007u);
  EXPECT_EQ(read_u32(onam->data, 4), 0x01000812u);
  EXPECT_EQ(read_u32(onam->data, 8), 0x01000813u);

  ASSERT_EQ(pnam->data.size(), 8u);
  EXPECT_EQ(read_u16(pnam->data, 0), 0x0002u);
  EXPECT_EQ(read_u16(pnam->data, 2), 0x0200u);
  EXPECT_EQ(read_u16(pnam->data, 4), 0x0812u);
  EXPECT_EQ(read_u16(pnam->data, 6), 0x1234u);

  ASSERT_EQ(nnam->data.size(), 8u);
  EXPECT_EQ(read_u16(nnam->data, 0), 0x0003u);
  EXPECT_EQ(read_u16(nnam->data, 2), 0x0200u);
  EXPECT_EQ(read_u16(nnam->data, 4), 0x0813u);
  EXPECT_EQ(read_u16(nnam->data, 6), 0x5678u);
}

TEST(FormLink, NavmCollectionReportsOnlyOnamBaseObjectsFromThisSlice) {
  const auto plugin = fo4::read_plugin(make_plugin(), "NavmeshPatch.esp");
  const auto hits = fo4::collect_form_links(plugin);

  std::vector<fo4::RawFormId> onamHits;
  for (const auto& hit : hits) {
    if (hit.record == fo4::FormKey::parse("000800:NavmeshPatch.esp") &&
        hit.subrecord.to_string() == "ONAM") {
      onamHits.push_back(hit.raw);
    }
    EXPECT_NE(hit.subrecord.to_string(), "PNAM");
    EXPECT_NE(hit.subrecord.to_string(), "NNAM");
  }

  ASSERT_EQ(onamHits.size(), 3u);
  EXPECT_EQ(onamHits[0], fo4::RawFormId{0x00000007u});
  EXPECT_EQ(onamHits[1], fo4::RawFormId{0x02000812u});
  EXPECT_EQ(onamHits[2], fo4::RawFormId{0x02000813u});
}
