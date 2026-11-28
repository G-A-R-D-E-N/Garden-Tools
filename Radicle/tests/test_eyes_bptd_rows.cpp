#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;

constexpr std::size_t kBodyPartNodeSize = 101;

Bytes u8_bytes(std::uint8_t value) { return Bytes{std::byte{value}}; }

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

Bytes body_part_node(std::uint8_t part_type, float cut_min) {
  Bytes out(kBodyPartNodeSize, std::byte{0});
  out[65] = std::byte{part_type};
  std::memcpy(out.data() + 28, &cut_min, sizeof(cut_min));
  return out;
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
  write_grouped_record(out, "EYES", 0x0E10,
                       {{"EDID", text_bytes("TestEyes")},
                        {"ICON", text_bytes("Textures\\Eyes\\Test.dds")},
                        {"DATA", u8_bytes(0x05)}});
  write_grouped_record(out, "BPTD", 0x0E20,
                       {{"EDID", text_bytes("TestBodyParts")},
                        {"BPND", body_part_node(1, 1.5f)},
                        {"BPND", body_part_node(2, 2.5f)}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord,
                std::size_t occurrence = 0) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != subrecord) continue;
    if (seen++ == occurrence) return sub.data;
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
                  std::string_view path, std::size_t occurrence = 0) {
  const auto field = scalar_row(record, path);
  if (!field.has_value()) return {};
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string(), occurrence);
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

TEST(EyesBptdRows, DeclaresEyesFlagsAndBodyPartNodeRows) {
  const auto flags = scalar_row("EYES", "EYES\\Flags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(flags->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(flags->scalar.storage, u8);
  EXPECT_EQ(flags->scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(flags->scalar.names.size(), 8u);
  EXPECT_EQ(fo4::schema::scalar_value_name(flags->scalar, 2), std::string_view("Not Male"));
  EXPECT_EQ(fo4::schema::scalar_value_name(flags->scalar, 128), std::string_view("Unknown 8"));

  const auto cut_min = scalar_row("BPTD", "BPTD\\CutMin");
  ASSERT_TRUE(cut_min.has_value());
  EXPECT_EQ(cut_min->scalar.subrecord, fo4::Signature::from_string("BPND"));
  EXPECT_EQ(cut_min->scalar.storage, f32);
  EXPECT_EQ(cut_min->scalar.offset, 28u);
  EXPECT_TRUE(cut_min->scalar.address.repeated);

  const auto part_type = scalar_row("BPTD", "BPTD\\PartType");
  ASSERT_TRUE(part_type.has_value());
  EXPECT_EQ(part_type->scalar.storage, u8);
  EXPECT_EQ(part_type->scalar.offset, 65u);
  EXPECT_EQ(part_type->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(fo4::schema::scalar_value_name(part_type->scalar, 25),
            std::string_view("Face Target Source"));
  EXPECT_FALSE(fo4::schema::scalar_value_name(part_type->scalar, 26).has_value());

  const auto flags_on_node = scalar_row("BPTD", "BPTD\\Flags");
  ASSERT_TRUE(flags_on_node.has_value());
  EXPECT_EQ(flags_on_node->scalar.offset, 64u);
  EXPECT_EQ(flags_on_node->scalar.names.size(), 8u);

  const auto on_cripple = scalar_row("BPTD", "BPTD\\OnCrippleDebrisScale");
  ASSERT_TRUE(on_cripple.has_value());
  EXPECT_EQ(on_cripple->scalar.offset, 95u);
  EXPECT_EQ(on_cripple->scalar.storage, f32);
}

TEST(EyesBptdRows, ReadsFlagsAndBodyPartNodeScalars) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto eyes = "000E10:Patch.esp";
  const auto body = "000E20:Patch.esp";

  EXPECT_EQ(field_bytes(plugin, eyes, "EYES", "EYES\\Flags"), u8_bytes(0x05));
  EXPECT_EQ(field_bytes(plugin, body, "BPTD", "BPTD\\CutMin"), f32_bytes(1.5f));
  EXPECT_EQ(field_bytes(plugin, body, "BPTD", "BPTD\\CutMin", 1), f32_bytes(2.5f));
  EXPECT_EQ(field_bytes(plugin, body, "BPTD", "BPTD\\PartType"), u8_bytes(1));
  EXPECT_EQ(field_bytes(plugin, body, "BPTD", "BPTD\\PartType", 1), u8_bytes(2));
  EXPECT_EQ(field_bytes(plugin, body, "BPTD", "BPTD\\Flags"), u8_bytes(0));
}

TEST(EyesBptdRows, CountsRepeatedBodyPartNodes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto body = fo4::FormKey::parse("000E20:Patch.esp");
  const auto cut_min = scalar_row("BPTD", "BPTD\\CutMin");
  const auto eyes_flags = scalar_row("EYES", "EYES\\Flags");
  ASSERT_TRUE(cut_min.has_value());
  ASSERT_TRUE(eyes_flags.has_value());

  const auto nodes = fo4::record_field_count(plugin, body, *cut_min);
  ASSERT_TRUE(nodes.has_value());
  EXPECT_EQ(*nodes, 2u);
  const auto flags = fo4::record_field_count(plugin, fo4::FormKey::parse("000E10:Patch.esp"),
                                             *eyes_flags);
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(*flags, 1u);
  EXPECT_TRUE(duplicate_scalar_paths(plugin).empty());
}

TEST(EyesBptdRows, WritesScalarsThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto eyes_key = fo4::FormKey::parse("000E10:Patch.esp");
  const auto body_key = fo4::FormKey::parse("000E20:Patch.esp");
  const auto eyes_flags = scalar_row("EYES", "EYES\\Flags");
  const auto cut_min = scalar_row("BPTD", "BPTD\\CutMin");
  const auto part_type = scalar_row("BPTD", "BPTD\\PartType");
  ASSERT_TRUE(eyes_flags.has_value());
  ASSERT_TRUE(cut_min.has_value());
  ASSERT_TRUE(part_type.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, eyes_key, *eyes_flags, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, body_key, *cut_min, 1, "4.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, body_key, *part_type, 0, "25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, body_key, *cut_min, 2, "1.0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, body_key, *cut_min, 1, "4.25"),
            fo4::ScalarMutationStatus::unchanged);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(field_bytes(reread, "000E10:Patch.esp", "EYES", "EYES\\Flags"), u8_bytes(7));
  EXPECT_EQ(sub_bytes(reread, "000E10:Patch.esp", "ICON"),
            text_bytes("Textures\\Eyes\\Test.dds"));
  EXPECT_EQ(field_bytes(reread, "000E20:Patch.esp", "BPTD", "BPTD\\CutMin"), f32_bytes(1.5f));
  EXPECT_EQ(field_bytes(reread, "000E20:Patch.esp", "BPTD", "BPTD\\CutMin", 1),
            f32_bytes(4.25f));
  EXPECT_EQ(field_bytes(reread, "000E20:Patch.esp", "BPTD", "BPTD\\PartType"), u8_bytes(25));
  EXPECT_EQ(field_bytes(reread, "000E20:Patch.esp", "BPTD", "BPTD\\PartType", 1), u8_bytes(2));
  EXPECT_EQ(sub_bytes(reread, "000E20:Patch.esp", "BPND", 1).size(), kBodyPartNodeSize);
  EXPECT_TRUE(duplicate_scalar_paths(reread).empty());
}

TEST(EyesBptdRows, RefusesBodyPartNodeOfTheWrongSize) {
  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "BPTD", 0x0E30,
                       {{"EDID", text_bytes("ShortBodyParts")},
                        {"BPND", Bytes(kBodyPartNodeSize - 1, std::byte{0})}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto cut_min = scalar_row("BPTD", "BPTD\\CutMin");
  ASSERT_TRUE(cut_min.has_value());

  EXPECT_TRUE(field_bytes(plugin, "000E30:Patch.esp", "BPTD", "BPTD\\CutMin").empty());
  EXPECT_EQ(sub_bytes(plugin, "000E30:Patch.esp", "BPND"),
            Bytes(kBodyPartNodeSize - 1, std::byte{0}));
}

TEST(EyesBptdRows, StringRowsUseSingleBackslashNames) {
  const auto part_node = scalar_row("BPTD", "BPTD\\PartNode");
  ASSERT_TRUE(part_node.has_value());
  EXPECT_EQ(part_node->scalar.subrecord, fo4::Signature::from_string("BPNN"));
  EXPECT_EQ(part_node->scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(part_node->scalar.address.repeated);
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("BPTD"),
                                                    "BPTD\\\\PartNode")
                   .has_value());

  const auto hit_end = scalar_row("BPTD", "BPTD\\HitReactionEnd");
  ASSERT_TRUE(hit_end.has_value());
  EXPECT_EQ(hit_end->scalar.subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(hit_end->scalar.storage, fo4::schema::ScalarStorage::zstring);

  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "BPTD", 0x0E40,
                       {{"EDID", text_bytes("StringBodyParts")},
                        {"BPND", body_part_node(1, 1.0f)},
                        {"BPNN", text_bytes("OldNode")},
                        {"FNAM", text_bytes("OldEnd")}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000E40:Patch.esp");
  EXPECT_EQ(fo4::set_record_string(plugin, key, "BPTD\\PartNode", 0, "NPC_Head"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "BPTD\\HitReactionEnd", 0, "Recoil_End"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "BPTD\\PartNode", 1, "Missing"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000E40:Patch.esp", "BPNN"), text_bytes("NPC_Head"));
  EXPECT_EQ(sub_bytes(reread, "000E40:Patch.esp", "FNAM"), text_bytes("Recoil_End"));
}
}  // namespace
