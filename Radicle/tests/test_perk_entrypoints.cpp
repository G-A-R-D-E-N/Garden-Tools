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

std::vector<std::byte> prke(std::uint8_t type) {
  return {std::byte{type}, std::byte{1}, std::byte{0}};
}

std::vector<std::byte> entry_point(std::uint8_t function) {
  return {std::byte{0}, std::byte{function}, std::byte{0}};
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

  fo4::test::write_subrecord(out, "DATA",
                             std::vector<std::byte>{std::byte{0x50}, std::byte{0x08},
                                                    std::byte{0x00}, std::byte{0x02},
                                                    std::byte{0x00}});

  fo4::test::write_subrecord(out, "EPFT", epft(1));
  fo4::test::write_subrecord(out, "EPFD", u32_payload(0x02000811));

  fo4::test::write_subrecord(out, "PRKE", prke(2));
  fo4::test::write_subrecord(out, "DATA", entry_point(4));
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

  fo4::test::write_subrecord(out, "EPFT", epft(8));
  fo4::test::write_subrecord(out, "EPFD", two_u32_payload(0x02000830, 0x3F800000u));

  const std::uint8_t entry_codes[] = {5, 12, 13, 14};
  std::uint32_t next_form = 0x02000818;
  for (const auto code : entry_codes) {
    fo4::test::write_subrecord(out, "PRKE", prke(2));
    fo4::test::write_subrecord(out, "DATA", entry_point(code));
    fo4::test::write_subrecord(out, "EPFT", epft(2));
    fo4::test::write_subrecord(out, "EPFD", two_u32_payload(next_form++, 0x3F800000u));
  }

  fo4::test::write_subrecord(out, "PRKE", prke(0));
  fo4::test::ByteWriter quest;
  quest.u32(0x02000860);
  quest.u16(5);
  fo4::test::write_subrecord(out, "DATA", quest.bytes);
  fo4::test::write_subrecord(out, "PRKE", prke(1));
  fo4::test::write_subrecord(out, "DATA", u32_payload(0x02000870));
  fo4::test::write_subrecord(out, "PRKE", prke(2));
  fo4::test::write_subrecord(out, "DATA", entry_point(4));

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
  ASSERT_EQ(epfd.size(), 11u);
  EXPECT_EQ(read_u32(epfd[0]->data), 0x02000811u);
  ASSERT_EQ(epfd[1]->data.size(), 8u);
  EXPECT_EQ(read_u32(epfd[1]->data, 0), 0x02000812u);
  EXPECT_EQ(read_u32(epfd[1]->data, 4), 0x02000813u);
  EXPECT_EQ(read_u32(epfd[2]->data), 0x01000814u);
  EXPECT_EQ(read_u32(epfd[3]->data), 0x01000815u);
  EXPECT_EQ(read_u32(epfd[4]->data), 0x01000816u);
  EXPECT_EQ(read_u32(epfd[5]->data), 0x02000817u);
  ASSERT_EQ(epfd[6]->data.size(), 8u);
  EXPECT_EQ(read_u32(epfd[6]->data, 0), 0x01000830u);
  EXPECT_EQ(read_u32(epfd[6]->data, 4), 0x3F800000u);
  for (std::size_t at = 7; at < 11; ++at) {
    ASSERT_EQ(epfd[at]->data.size(), 8u);
    EXPECT_EQ(read_u32(epfd[at]->data, 0), 0x01000818u + static_cast<std::uint32_t>(at - 7));
    EXPECT_EQ(read_u32(epfd[at]->data, 4), 0x3F800000u);
  }

  const auto epf3 = find_subs(*record, "EPF3");
  ASSERT_EQ(epf3.size(), 1u);
  EXPECT_EQ(read_u32(epf3[0]->data), 0x02000899u);
}

TEST(FormLink, PerkEffectDataRemapsOnlyQuestAndAbilityFormIds) {
  auto plugin = fo4::read_plugin(make_plugin(), "PerkPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "PerkPatch.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:PerkPatch.esp"));
  ASSERT_NE(record, nullptr);

  const auto data = find_subs(*record, "DATA");
  ASSERT_EQ(data.size(), 9u);
  EXPECT_EQ(data[0]->data.size(), 5u);
  EXPECT_EQ(read_u32(data[0]->data), 0x02000850u);
  EXPECT_EQ(data[1]->data, entry_point(4));
  for (std::size_t at = 1; at < 6; ++at) {
    EXPECT_EQ(data[at]->data.size(), 3u);
  }
  ASSERT_EQ(data[6]->data.size(), 6u);
  EXPECT_EQ(read_u32(data[6]->data), 0x01000860u);
  EXPECT_EQ(data[6]->data[4], std::byte{5});
  EXPECT_EQ(data[6]->data[5], std::byte{0});
  ASSERT_EQ(data[7]->data.size(), 4u);
  EXPECT_EQ(read_u32(data[7]->data), 0x01000870u);
  EXPECT_EQ(data[8]->data, entry_point(4));
}

TEST(FormLink, PerkEntryPointCollectionReportsOnlyFormIdEpfdVariants) {
  auto plugin = fo4::read_plugin(make_plugin(), "PerkPatch.esp");
  const auto links = fo4::collect_form_links(plugin);

  std::vector<std::uint32_t> epfd;
  for (const auto& link : links) {
    if (link.subrecord.to_string() != "EPFD") continue;
    epfd.push_back(link.raw.value());
    EXPECT_EQ(link.offset, 0u);
    EXPECT_EQ(link.key.owner().filename(), "KeepMe.esm");
  }
  const std::vector<std::uint32_t> expected{0x02000814u, 0x02000815u, 0x02000816u, 0x02000830u,
                                            0x02000818u, 0x02000819u, 0x0200081Au, 0x0200081Bu};
  EXPECT_EQ(epfd, expected);
}

TEST(FormLink, PerkEffectDataCollectionReportsOnlyQuestAndAbilityFormIds) {
  auto plugin = fo4::read_plugin(make_plugin(), "PerkPatch.esp");
  const auto links = fo4::collect_form_links(plugin);

  std::vector<std::uint32_t> data;
  for (const auto& link : links) {
    if (link.subrecord.to_string() != "DATA") continue;
    data.push_back(link.raw.value());
    EXPECT_EQ(link.offset, 0u);
    EXPECT_EQ(link.key.owner().filename(), "KeepMe.esm");
  }
  const std::vector<std::uint32_t> expected{0x02000860u, 0x02000870u};
  EXPECT_EQ(data, expected);
}
