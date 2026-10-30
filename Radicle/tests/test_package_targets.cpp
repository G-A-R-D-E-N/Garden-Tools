#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> tagged_target(std::uint32_t type, std::uint32_t data) {
  fo4::test::ByteWriter out;
  out.u32(type);
  out.u32(data);
  return out.bytes;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::vector<const fo4::Subrecord*> find_subs(const fo4::Record& record,
                                              std::string_view signature) {
  std::vector<const fo4::Subrecord*> out;
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) out.push_back(&sub);
  }
  return out;
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

  const auto group = fo4::test::begin_group(out, "PACK", 0);
  const auto record = fo4::test::begin_record(out, "PACK", 0, 0x03000800);

  fo4::test::write_subrecord(out, "PTDT", tagged_target(0, 0x02000812));
  fo4::test::write_subrecord(out, "PTDT", tagged_target(1, 0x02000813));
  fo4::test::write_subrecord(out, "PTDT", tagged_target(2, 0x02000814));
  fo4::test::write_subrecord(out, "PTDT", tagged_target(3, 0x02000815));

  fo4::test::write_subrecord(out, "PDTO", tagged_target(0, 0x02000816));
  fo4::test::write_subrecord(out, "PDTO", tagged_target(1, 0x02000817));

  fo4::test::ByteWriter shortTarget;
  shortTarget.u32(0);
  fo4::test::write_subrecord(out, "PTDT", shortTarget.bytes);
  fo4::test::write_subrecord(out, "PDTO", shortTarget.bytes);

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(FormLink, PackageTargetsRemapOnlyTypeTaggedFormIds) {
  auto plugin = fo4::read_plugin(make_plugin(), "PackagePatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "PackagePatch.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:PackagePatch.esp"));
  ASSERT_NE(record, nullptr);

  const auto ptdt = find_subs(*record, "PTDT");
  ASSERT_EQ(ptdt.size(), 5u);
  EXPECT_EQ(read_u32(ptdt[0]->data, 4), 0x01000812u);
  EXPECT_EQ(read_u32(ptdt[1]->data, 4), 0x01000813u);
  EXPECT_EQ(read_u32(ptdt[2]->data, 4), 0x02000814u);
  EXPECT_EQ(read_u32(ptdt[3]->data, 4), 0x02000815u);
  ASSERT_EQ(ptdt[4]->data.size(), 4u);
  EXPECT_EQ(read_u32(ptdt[4]->data, 0), 0u);

  const auto pdto = find_subs(*record, "PDTO");
  ASSERT_EQ(pdto.size(), 3u);
  EXPECT_EQ(read_u32(pdto[0]->data, 4), 0x01000816u);
  EXPECT_EQ(read_u32(pdto[1]->data, 4), 0x02000817u);
  ASSERT_EQ(pdto[2]->data.size(), 4u);
  EXPECT_EQ(read_u32(pdto[2]->data, 0), 0u);
}

TEST(FormLink, PackageTargetCollectionReportsOnlyFormIdVariants) {
  auto plugin = fo4::read_plugin(make_plugin(), "PackagePatch.esp");
  const auto hits = fo4::collect_form_links(plugin);

  std::size_t ptdtHits = 0;
  std::size_t pdtoHits = 0;
  for (const auto& hit : hits) {
    if (hit.subrecord.to_string() == "PTDT") {
      ++ptdtHits;
      EXPECT_EQ(hit.offset, 4u);
      EXPECT_TRUE(hit.raw.value() == 0x02000812u || hit.raw.value() == 0x02000813u);
    }
    if (hit.subrecord.to_string() == "PDTO") {
      ++pdtoHits;
      EXPECT_EQ(hit.offset, 4u);
      EXPECT_EQ(hit.raw.value(), 0x02000816u);
    }
  }

  EXPECT_EQ(ptdtHits, 2u);
  EXPECT_EQ(pdtoHits, 1u);
}
