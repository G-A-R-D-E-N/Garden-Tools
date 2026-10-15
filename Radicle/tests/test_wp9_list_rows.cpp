#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

Bytes u8_bytes(std::uint8_t value) {
  ByteWriter out;
  out.u8(value);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes u32_words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                std::initializer_list<std::pair<std::string_view, Bytes>> subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  for (const auto& [subrecord, data] : subrecords) {
    fo4::test::write_subrecord(out, subrecord, data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

ByteWriter make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
  add_record(out, "CPTH", 0xA01,
             {{"SNAM", u32_bytes(0x00000A10)}, {"SNAM", u32_bytes(0x00000A11)}});
  add_record(out, "DOOR", 0xA02,
             {{"TNAM", u32_bytes(0x00000A20)}, {"TNAM", u32_bytes(0x00000A21)}});
  add_record(out, "EQUP", 0xA03,
             {{"PNAM", u32_words({0x00000A30, 0x00000A31, 0x00000A32})}});
  add_record(out, "LVSP", 0xA04,
             {{"OBND", Bytes(12)},
              {"LVLD", u8_bytes(5)},
              {"LVLM", u8_bytes(0)},
              {"LVLF", u8_bytes(1)},
              {"LLCT", u8_bytes(0)}});
  return out;
}

fo4::schema::FormLinkListDescriptor list(std::string_view record, std::string_view path) {
  const auto descriptor =
      fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(record), path);
  if (!descriptor) throw std::invalid_argument("unknown FormLink list row " + std::string(path));
  return *descriptor;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view key,
                            std::string_view signature) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return out;
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) out.push_back(sub.data);
  }
  return out;
}

Bytes payload(const fo4::Plugin& plugin, std::string_view key, std::string_view signature) {
  const auto all = payloads(plugin, key, signature);
  if (all.empty()) {
    ADD_FAILURE() << key << ' ' << signature;
    return {};
  }
  return all.front();
}

}  // namespace

TEST(WP9ListRows, PerSubrecordListsRetargetAtIndexOne) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const struct {
    std::string_view record;
    std::string_view key;
    std::string_view path;
    std::string_view subrecord;
  } rows[] = {
      {"CPTH", "000A01:Patch.esp", "CPTH\\CameraShots", "SNAM"},
      {"DOOR", "000A02:Patch.esp", "DOOR\\RandomTeleportDestinations", "TNAM"},
  };
  for (const auto& row : rows) {
    SCOPED_TRACE(row.path);
    const auto descriptor = list(row.record, row.path);
    EXPECT_EQ(descriptor.storage, fo4::schema::FormLinkListStorage::per_subrecord);
    EXPECT_EQ(descriptor.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_FALSE(descriptor.nullable);
    const auto key = fo4::FormKey::parse(row.key);
    EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, descriptor), 2u);
    EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                  plugin, key, descriptor, 1, fo4::FormKey::parse("000A20:Objects.esp")),
              fo4::schema::FormLinkListMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, "000A01:Patch.esp", "SNAM"),
            (std::vector<Bytes>{u32_bytes(0x00000A10), u32_bytes(0x01000A20)}));
  EXPECT_EQ(payloads(reread, "000A02:Patch.esp", "TNAM"),
            (std::vector<Bytes>{u32_bytes(0x00000A20), u32_bytes(0x01000A20)}));
}

TEST(WP9ListRows, PackedSlotParentsRetargetAtIndexOne) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto descriptor = list("EQUP", "EQUP\\SlotParents");
  EXPECT_EQ(descriptor.storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_EQ(descriptor.subrecord, fo4::Signature::from_string("PNAM"));
  EXPECT_FALSE(descriptor.nullable);
  const auto key = fo4::FormKey::parse("000A03:Patch.esp");
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, descriptor), 3u);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, key, descriptor, 1, fo4::FormKey::parse("000A20:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, "000A03:Patch.esp", "PNAM"),
            u32_words({0x00000A30, 0x01000A20, 0x00000A32}));
  EXPECT_EQ(fo4::schema::record_formlink_list_size(reread, key, descriptor), 3u);
}

TEST(WP9ListRows, LvspChanceNoneMaxCountAndFlagsReadAndWrite) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A04:Patch.esp");
  const std::string_view paths[] = {"LVSP\\ChanceNone", "LVSP\\MaxCount", "LVSP\\Flags"};
  const std::string_view subrecords[] = {"LVLD", "LVLM", "LVLF"};
  const std::string_view initial[] = {"5", "0", "1"};
  const std::string_view written[] = {"9", "2", "4"};
  for (std::size_t i = 0; i < 3; ++i) {
    SCOPED_TRACE(paths[i]);
    const auto field = fo4::schema::scalar_field_descriptor(paths[i]);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(subrecords[i]));
    const auto data = payload(plugin, "000A04:Patch.esp", subrecords[i]);
    EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar),
                                         field->scalar),
              initial[i]);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, written[i]),
              fo4::ScalarMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, "000A04:Patch.esp", "LVLD"), u8_bytes(9));
  EXPECT_EQ(payload(reread, "000A04:Patch.esp", "LVLM"), u8_bytes(2));
  EXPECT_EQ(payload(reread, "000A04:Patch.esp", "LVLF"), u8_bytes(4));
}
