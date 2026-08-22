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

std::vector<std::byte> epft(std::uint8_t type) {
  return {std::byte{type}};
}

std::vector<std::byte> u32_payload(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> two_u32_payload(std::uint32_t first, std::uint32_t second) {
  fo4::test::ByteWriter out;
  out.u32(first);
  out.u32(second);
  return out.bytes;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset = 0) {
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

  const auto group = fo4::test::begin_group(out, "PERK", 0);
  const auto record = fo4::test::begin_record(out, "PERK", 0, 0x03000800);

  // EPFT 1 = Float. The bit pattern must remain opaque.
  fo4::test::write_subrecord(out, "EPFT", epft(1));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000811));

  // EPFT 2 = Float/AV,Float. Both 32-bit words remain opaque.
  fo4::test::write_subrecord(out, "EPFT", epft(2));
  fo4::test::write_subrecord(out, "EPFD", two_u32_payload(0x02000812, 0x02000813));

  // EPFT 3 = LVLI FormID.
  fo4::test::write_subrecord(out, "EPFT", epft(3));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000814));

  // EPFT 4 = Add Activate Choice: SPEL FormID. EPF2/EPF3 are separate
  // label/flags data and deliberately not included in FormLink scanning.
  fo4::test::write_subrecord(out, "EPFT", epft(4));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000815));
  fo4::test::write_subrecord(out, "EPF2", std::string_view{"Activate\0", 9});
  fo4::test::ByteWriter flags;
  flags.u32(0x02000899);
  fo4::test::write_subrecord(out, "EPF3", flags.bytes);

  // EPFT 5 = Select Spell: SPEL FormID.
  fo4::test::write_subrecord(out, "EPFT", epft(5));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000816));

  // EPFT 6 = string; FormID-looking bytes remain untouched.
  fo4::test::write_subrecord(out, "EPFT", epft(6));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000817));

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(FormLink, PerkEntryPointRemapsOnlyFormIdEpfdVariants) {
  auto plugin = fo4::read_plugin(make_plugin(), "PerkPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "PerkPatch.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:PerkPatch.esp"));
  ASSERT_NE(record, nullptr);

  const auto epfd = find_subs(*record, "EPFD");
  ASSERT_EQ(epfd.size(), 6u);
  EXPECT_EQ(read_u32(epfd[0]->data), 0x02000811u);
  ASSERT_EQ(epfd[1]->data.size(), 8u);
  EXPECT_EQ(read_u32(epfd[1]->data, 0), 0x02000812u);
  EXPECT_EQ(read_u32(epfd[1]->data, 4), 0x02000813u);
  EXPECT_EQ(read_u32(epfd[2]->data), 0x01000814u);
  EXPECT_EQ(read_u32(epfd[3]->data), 0x01000815u);
  EXPECT_EQ(read_u32(epfd[4]->data), 0x01000816u);
  EXPECT_EQ(read_u32(epfd[5]->data), 0x02000817u);

  const auto epf3 = find_subs(*record, "EPF3");
  ASSERT_EQ(epf3.size(), 1u);
  EXPECT_EQ(read_u32(epf3[0]->data), 0x02000899u);
}

TEST(FormLink, PerkEntryPointCollectionReportsOnlyFormIdEpfdVariants) {
  const auto plugin = fo4::read_plugin(make_plugin(), "PerkPatch.esp");
  const auto links = fo4::collect_form_links(plugin);

  std::size_t epfdHits = 0;
  for (const auto& link : links) {
    if (link.subrecord.to_string() != "EPFD") continue;
    ++epfdHits;
    EXPECT_EQ(link.offset, 0u);
    EXPECT_TRUE(link.raw.value() == 0x02000814u ||
                link.raw.value() == 0x02000815u ||
                link.raw.value() == 0x02000816u);
    EXPECT_EQ(link.key.owner().filename(), "KeepMe.esm");
  }
  EXPECT_EQ(epfdHits, 3u);
}
