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

std::vector<std::byte> subrecord_data(const fo4::Plugin& plugin, const char* key,
                                      std::string_view signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) return sub.data;
  }
  return {};
}

std::vector<std::byte> payload_with(std::size_t size,
                                    std::initializer_list<std::pair<std::size_t, std::uint32_t>> values) {
  std::vector<std::byte> data(size, std::byte{0});
  for (const auto& [offset, value] : values) std::memcpy(data.data() + offset, &value, 4);
  return data;
}

std::vector<std::byte> without_last_byte(std::vector<std::byte> data) {
  data.pop_back();
  return data;
}

std::vector<std::byte> book_dnam(std::uint8_t flags, std::uint32_t form) {
  fo4::test::ByteWriter o;
  o.u8(flags);
  o.u32(form);
  o.u32(0);
  o.u32(0);
  return o.bytes;
}

void property_row(fo4::test::ByteWriter& o, std::uint8_t value_type, std::uint32_t value1) {
  o.u8(value_type);
  o.u8(0); o.u8(0); o.u8(0);
  o.u8(0);
  o.u8(0); o.u8(0); o.u8(0);
  o.u16(0);
  o.u8(0); o.u8(0);
  o.u32(value1);
  o.u32(0);
  o.f32(1.0f);
}

std::vector<std::byte> obts_body(std::uint32_t keyword, std::uint8_t value_type,
                                 std::uint32_t value1) {
  fo4::test::ByteWriter o;
  o.u32(0);
  o.u32(1);
  o.u8(1); o.u8(0); o.u8(50); o.u8(0);
  o.u16(0);
  o.u8(0);
  o.u8(1);
  o.u32(keyword);
  o.u8(0); o.u8(0);
  property_row(o, value_type, value1);
  return o.bytes;
}

std::vector<std::byte> omod_data(std::uint32_t attach, std::uint32_t slot, std::uint32_t include,
                                 std::uint32_t value1) {
  fo4::test::ByteWriter o;
  o.u32(1);
  o.u32(2);
  o.u8(0);
  o.u8(1);
  o.u32(0x1640);
  o.u8(0);
  o.u8(0);
  o.u32(attach);
  o.u32(1);
  o.u32(slot);
  o.u32(1);
  o.u32(0x1643);
  o.u32(0x1644);
  o.u32(include);
  o.u8(0); o.u8(1); o.u8(0);
  property_row(o, 4, value1);
  property_row(o, 2, 0x1647);
  return o.bytes;
}

}  // namespace

TEST(UnionPayloadRemap, BookTeachesRemapsOnlyWhenFlagsSayFormId) {
  auto out = plugin_with_masters();
  append_record(out, "BOOK", 0x02000600, {{"DNAM", book_dnam(0x01, 0x1600)}});
  append_record(out, "BOOK", 0x02000601, {{"DNAM", book_dnam(0x10, 0x1601)}});
  append_record(out, "BOOK", 0x02000602, {{"DNAM", book_dnam(0x02, 0x1602)}});
  append_record(out, "BOOK", 0x02000603, {{"DNAM", book_dnam(0x04, 0x1603)}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000600:Patch.esp", "DNAM"), book_dnam(0x01, 0x01001600));
  EXPECT_EQ(subrecord_data(reread, "000601:Patch.esp", "DNAM"), book_dnam(0x10, 0x01001601));
  EXPECT_EQ(subrecord_data(reread, "000602:Patch.esp", "DNAM"), book_dnam(0x02, 0x1602));
  EXPECT_EQ(subrecord_data(reread, "000603:Patch.esp", "DNAM"), book_dnam(0x04, 0x01001603));
}

TEST(UnionPayloadRemap, MgefAssocItemFollowsArchetypeAndKeepsSchemaRows) {
  auto out = plugin_with_masters();
  append_record(out, "MGEF", 0x02000610,
                {{"DATA", payload_with(152, {{8, 0x1610}, {16, 0x1611}, {64, 18}})}});
  append_record(out, "MGEF", 0x02000611,
                {{"DATA", payload_with(152, {{8, 0x1612}, {16, 0x1613}, {64, 0}})}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000610:Patch.esp", "DATA"),
            payload_with(152, {{8, 0x01001610}, {16, 0x01001611}, {64, 18}}));
  EXPECT_EQ(subrecord_data(reread, "000611:Patch.esp", "DATA"),
            payload_with(152, {{8, 0x1612}, {16, 0x01001613}, {64, 0}}));
}

TEST(UnionPayloadRemap, ObtsPropertyValueRemapsOnlyFormIdValueTypes) {
  auto out = plugin_with_masters();
  append_record(out, "ARMO", 0x02000620, {{"OBTS", obts_body(0x1620, 4, 0x1630)}});
  append_record(out, "ARMO", 0x02000621, {{"OBTS", obts_body(0x1620, 2, 0x1631)}});
  append_record(out, "ARMO", 0x02000622, {{"OBTS", obts_body(0x1620, 6, 0x1632)}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000620:Patch.esp", "OBTS"),
            obts_body(0x01001620, 4, 0x01001630));
  EXPECT_EQ(subrecord_data(reread, "000621:Patch.esp", "OBTS"), obts_body(0x01001620, 2, 0x1631));
  EXPECT_EQ(subrecord_data(reread, "000622:Patch.esp", "OBTS"),
            obts_body(0x01001620, 6, 0x01001632));
}

TEST(UnionPayloadRemap, OmodDataRemapsAttachSlotsIncludesAndFormValueTypes) {
  auto out = plugin_with_masters();
  append_record(out, "OMOD", 0x02000640, {{"DATA", omod_data(0x1641, 0x1642, 0x1645, 0x1646)}});
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000640:Patch.esp", "DATA"),
            omod_data(0x01001641, 0x01001642, 0x01001645, 0x01001646));
}

TEST(UnionPayloadRemap, DamageTypeAndRaceBipedRemapByFormVersion) {
  auto out = plugin_with_masters();
  append_record(out, "DMGT", 0x02000650,
                {{"DNAM", payload_with(16, {{0, 0x1650}, {4, 0x1651}, {8, 0x1652}, {12, 0x1653}})}},
                131);
  append_record(out, "DMGT", 0x02000651, {{"DNAM", payload_with(4, {{0, 0x1654}})}}, 77);
  append_record(out, "RACE", 0x02000652,
                {{"RBPC", payload_with(12, {{0, 0x1655}, {8, 0x1656}})}}, 131);
  append_record(out, "RACE", 0x02000653, {{"RBPC", payload_with(4, {{0, 0x1657}})}}, 77);
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000650:Patch.esp", "DNAM"),
            payload_with(16, {{0, 0x01001650}, {4, 0x01001651}, {8, 0x01001652},
                              {12, 0x01001653}}));
  EXPECT_EQ(subrecord_data(reread, "000651:Patch.esp", "DNAM"), payload_with(4, {{0, 0x1654}}));
  EXPECT_EQ(subrecord_data(reread, "000652:Patch.esp", "RBPC"),
            payload_with(12, {{0, 0x01001655}, {8, 0x01001656}}));
  EXPECT_EQ(subrecord_data(reread, "000653:Patch.esp", "RBPC"), payload_with(4, {{0, 0x1657}}));
}

TEST(UnionPayloadRemap, EfshAmbientSoundFollowsFormVersionLayout) {
  auto out = plugin_with_masters();
  append_record(out, "EFSH", 0x02000660, {{"DNAM", payload_with(157, {{108, 0x1660}})}}, 131);
  append_record(out, "EFSH", 0x02000661,
                {{"DNAM", payload_with(395, {{241, 0x1661}, {305, 0x1662}})}}, 77);
  const auto reread = reread_swapped(out);
  EXPECT_EQ(subrecord_data(reread, "000660:Patch.esp", "DNAM"),
            payload_with(157, {{108, 0x01001660}}));
  EXPECT_EQ(subrecord_data(reread, "000661:Patch.esp", "DNAM"),
            payload_with(395, {{241, 0x01001661}, {305, 0x01001662}}));
}

TEST(UnionPayloadRemap, MalformedUnionPayloadsFailClosedWithoutMutation) {
  struct Malformed {
    std::string_view record;
    std::string_view subrecord;
    std::uint16_t form_version;
    std::vector<std::byte> data;
  };
  const std::vector<Malformed> cases = {
      {"BOOK", "DNAM", 131, without_last_byte(book_dnam(0x01, 0x1600))},
      {"OMOD", "DATA", 131, without_last_byte(omod_data(0x1641, 0x1642, 0x1645, 0x1646))},
      {"DMGT", "DNAM", 131, payload_with(12, {{0, 0x1650}})},
      {"RACE", "RBPC", 131, payload_with(6, {{0, 0x1655}})},
      {"EFSH", "DNAM", 131, payload_with(156, {{108, 0x1660}})},
      {"EFSH", "DNAM", 77, payload_with(157, {{108, 0x1660}})},
  };
  for (const auto& item : cases) {
    auto out = plugin_with_masters();
    append_record(out, item.record, 0x02000700, {{item.subrecord, item.data}},
                  item.form_version);
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    EXPECT_THROW((void)fo4::remap_plugin_masters(plugin, swapped_masters()), fo4::FormIdError)
        << item.record << " " << item.subrecord;
    EXPECT_EQ(fo4::write_plugin(plugin), out.bytes) << item.record << " " << item.subrecord;
  }
}
