#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000803:Patch.esp";

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes channels(std::initializer_list<std::uint8_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u8(value);
  return out.bytes;
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "SOPM", 0);
  const auto record = fo4::test::begin_record(out, "SOPM", 0, 0x01000803u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleOutput"));
  fo4::test::write_subrecord(out, "MNAM", channels({1, 0, 0, 0}));
  fo4::test::write_subrecord(out, "VNAM", channels({0x9c, 0xff}));
  fo4::test::write_subrecord(out, "ONAM",
                             channels({100, 100, 0, 0, 50, 50, 50, 50, 80, 80, 0, 0, 25, 25, 25, 25}));
  fo4::test::write_subrecord(out, "ATTN",
                             channels({0, 0, 0x80, 0x3f, 0, 0, 0, 0x40, 0, 0, 0x40, 0x40, 0, 0, 0x80,
                                       0x40, 5, 10, 15, 20, 25, 30, 35, 40}));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes subrecord_at(const fo4::Plugin& plugin, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) return sub.data;
  }
  ADD_FAILURE() << signature;
  return {};
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("SOPM"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

std::string decoded(const fo4::Plugin& plugin, std::string_view path, std::size_t ordinal) {
  const auto field = row(path);
  const auto& record = *fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(ordinal);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(SopmOnamRows, ChannelsAreStridedU8Rows) {
  const auto left = row("SOPM\\FrontLeft");
  EXPECT_EQ(left.scalar.subrecord, fo4::Signature::from_string("ONAM"));
  EXPECT_EQ(left.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(left.scalar.offset, 0u);
  EXPECT_EQ(left.scalar.address.stride, 8u);

  const auto right = row("SOPM\\SideRight");
  EXPECT_EQ(right.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(right.scalar.offset, 7u);
  EXPECT_EQ(right.scalar.address.stride, 8u);
}

TEST(SopmOnamRows, ReadsOneSlotPerChannel) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_EQ(fo4::record_field_count(plugin, record, row("SOPM\\FrontLeft")),
            std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::record_field_count(plugin, record, row("SOPM\\SideRight")),
            std::optional<std::size_t>{2});
  EXPECT_EQ(decoded(plugin, "SOPM\\FrontLeft", 0), "100");
  EXPECT_EQ(decoded(plugin, "SOPM\\FrontLeft", 1), "80");
  EXPECT_EQ(decoded(plugin, "SOPM\\RearLeft", 0), "50");
  EXPECT_EQ(decoded(plugin, "SOPM\\RearLeft", 1), "25");
  EXPECT_EQ(decoded(plugin, "SOPM\\SideRight", 1), "25");
}

TEST(SopmOnamRows, WritesOneChannelAndLeavesTheOtherUntouched) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record, row("SOPM\\FrontLeft"), 1, "10"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record, row("SOPM\\SideRight"), 2, "0"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_at(reread, "ONAM"),
            channels({100, 100, 0, 0, 50, 50, 50, 50, 10, 80, 0, 0, 25, 25, 25, 25}));
  EXPECT_EQ(subrecord_at(reread, "VNAM"), channels({0x9c, 0xff}));
  EXPECT_EQ(subrecord_at(reread, "EDID"), zstr("RadicleOutput"));
}

TEST(SopmOnamRows, RefusesANonU8SetterOnAChannelRow) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  EXPECT_THROW(fo4::set_record_string(plugin, record, row("SOPM\\Center"), 0, "1"),
               fo4::schema::ScalarError);
}

TEST(SopmOnamRows, AuthoringCoversOnamBetweenVnamAndAttn) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key))) {
    EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ": " << issue.message;
  }
}
