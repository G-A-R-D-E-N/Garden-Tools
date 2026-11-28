#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto f32 = fo4::schema::ScalarStorage::f32;

const fo4::Signature k_misc = fo4::Signature::from_string("MISC");

bool target_allowed(const fo4::schema::FormLinkFieldDescriptor& field, std::string_view signature) {
  return fo4::schema::formlink_field_target_allowed(field, fo4::Signature::from_string(signature));
}

Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

void write_grouped_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                          std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x01000000u | local_id);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  ByteWriter data;
  data.i32(25);
  data.f32(0.5f);
  write_grouped_record(out, "MISC", 0x0F10,
                       {{"EDID", text_bytes("TestMisc")},
                        {"DATA", data.bytes},
                        {"ICON", text_bytes("Textures\\Misc\\Test.dds")},
                        {"MICO", text_bytes("Textures\\Misc\\TestMessage.dds")},
                        {"YNAM", u32_bytes(0x01000ABCu)},
                        {"ZNAM", u32_bytes(0x01000ABDu)},
                        {"FIMD", u32_bytes(0x01000ABEu)},
                        {"PTRN", u32_bytes(0x01000ABFu)}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  return {};
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_row(std::string_view record,
                                                             std::string_view path) {
  const auto found =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  if (!found.has_value()) ADD_FAILURE() << path;
  return found;
}

Bytes field_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view record,
                  std::string_view path) {
  const auto field = scalar_row(record, path);
  if (!field.has_value()) return {};
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  if (data.empty()) return {};
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::vector<std::string> duplicate_scalar_paths(const fo4::Plugin& plugin) {
  std::vector<std::string> duplicates;
  for (const auto& issue : fo4::schema::preflight_plugin(plugin)) {
    if (issue.code == "duplicate_scalar_subrecord") duplicates.push_back(issue.path);
  }
  return duplicates;
}

TEST(ItemExtraRows, DeclaresMiscDataRows) {
  const auto value = scalar_row("MISC", "MISC\\Value");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(value->scalar.storage, i32);
  EXPECT_EQ(value->scalar.offset, 0u);

  const auto weight = scalar_row("MISC", "MISC\\Weight");
  ASSERT_TRUE(weight.has_value());
  EXPECT_EQ(weight->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(weight->scalar.storage, f32);
  EXPECT_EQ(weight->scalar.offset, 4u);
}

TEST(ItemExtraRows, DeclaresMiscSoundAndMessageFormLinks) {
  const auto pick_up = fo4::schema::formlink_field_descriptor(k_misc, "MISC\\PickUpSound");
  ASSERT_TRUE(pick_up.has_value());
  EXPECT_EQ(pick_up->subrecord, fo4::Signature::from_string("YNAM"));
  EXPECT_TRUE(target_allowed(*pick_up, "SNDR"));
  EXPECT_FALSE(target_allowed(*pick_up, "TXST"));

  const auto put_down = fo4::schema::formlink_field_descriptor(k_misc, "MISC\\PutDownSound");
  ASSERT_TRUE(put_down.has_value());
  EXPECT_EQ(put_down->subrecord, fo4::Signature::from_string("ZNAM"));

  const auto featured =
      fo4::schema::formlink_field_descriptor(k_misc, "MISC\\FeaturedItemMessage");
  ASSERT_TRUE(featured.has_value());
  EXPECT_EQ(featured->subrecord, fo4::Signature::from_string("FIMD"));
  EXPECT_TRUE(target_allowed(*featured, "MESG"));

  const auto preview = fo4::schema::formlink_field_descriptor(k_misc, "MISC\\PreviewTransform");
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->subrecord, fo4::Signature::from_string("PTRN"));
  EXPECT_TRUE(target_allowed(*preview, "TRNS"));
}

TEST(ItemExtraRows, ReadsMiscDataScalars) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(field_bytes(plugin, "000F10:Patch.esp", "MISC", "MISC\\Value"), u32_bytes(25));
  EXPECT_EQ(field_bytes(plugin, "000F10:Patch.esp", "MISC", "MISC\\Weight"), f32_bytes(0.5f));
}

TEST(ItemExtraRows, WritesMiscDataScalarsThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000F10:Patch.esp");
  const auto value = scalar_row("MISC", "MISC\\Value");
  const auto weight = scalar_row("MISC", "MISC\\Weight");
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(weight.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *value, "40"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *weight, "1.25"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(field_bytes(reread, "000F10:Patch.esp", "MISC", "MISC\\Value"), u32_bytes(40));
  EXPECT_EQ(field_bytes(reread, "000F10:Patch.esp", "MISC", "MISC\\Weight"),
            f32_bytes(1.25f));
  EXPECT_EQ(sub_bytes(reread, "000F10:Patch.esp", "ICON"),
            text_bytes("Textures\\Misc\\Test.dds"));
  EXPECT_TRUE(duplicate_scalar_paths(reread).empty());
}

}
