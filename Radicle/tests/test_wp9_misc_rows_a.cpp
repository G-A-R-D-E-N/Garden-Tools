#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_link_schema.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct ColorItem {
  std::string_view record;
  std::string_view path;
  std::size_t offset;
  std::string_view initial;
};

constexpr ColorItem k_colors[] = {
    {"ACTI", "ACTI\\MarkerColorRed", 0, "1"},
    {"ACTI", "ACTI\\MarkerColorGreen", 1, "2"},
    {"ACTI", "ACTI\\MarkerColorBlue", 2, "3"},
    {"FLOR", "FLOR\\MarkerColorRed", 0, "1"},
    {"FLOR", "FLOR\\MarkerColorGreen", 1, "2"},
    {"FLOR", "FLOR\\MarkerColorBlue", 2, "3"},
};

Bytes marker_color() {
  return {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{0}};
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                const std::vector<Sub>& subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
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
  add_record(out, "ACTI", 0xF01,
             {{"PNAM", marker_color()}, {"STCP", u32_bytes(0x00000F30)}});
  add_record(out, "FLOR", 0xF02,
             {{"PNAM", marker_color()}, {"PTRN", u32_bytes(0x00000F31)}});
  return out;
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view key,
                       std::string_view subrecord) {
  const auto record = fo4::FormKey::parse(key);
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord.to_string() == subrecord) {
      return hit.raw.value();
    }
  }
  ADD_FAILURE() << key << ' ' << subrecord;
  return 0;
}

Bytes subrecord_data(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

}  // namespace

TEST(WP9MiscRowsA, MarkerColorRowsDecodeAndWriteOnActiAndFlor) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto edited = Bytes{std::byte{9}, std::byte{9}, std::byte{9}, std::byte{0}};
  for (const auto& item : k_colors) {
    SCOPED_TRACE(item.path);
    const auto record = fo4::Signature::from_string(item.record);
    const auto field = fo4::schema::scalar_field_descriptor(record, item.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("PNAM"));
    EXPECT_EQ(field->scalar.offset, item.offset);
    EXPECT_EQ(field->scalar.payload_size, 4u);
    const auto key = std::string(item.record == "ACTI" ? "000F01:Patch.esp" : "000F02:Patch.esp");
    const auto data = subrecord_data(plugin, key, "PNAM");
    EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar),
                                         field->scalar),
              item.initial);
    EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(key), *field, "9"),
              fo4::ScalarMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000F01:Patch.esp", "PNAM"), edited);
  EXPECT_EQ(subrecord_data(reread, "000F02:Patch.esp", "PNAM"), edited);
}

TEST(WP9MiscRowsA, AnimationSoundAndPreviewTransformRetargetOnOneRecordEach) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto null_target = fo4::FormKey{};
  const auto retarget = fo4::FormKey::parse("000F20:Objects.esp");

  const auto sound = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("ACTI"),
                                                            "ACTI\\AnimationSound");
  ASSERT_TRUE(sound.has_value());
  EXPECT_EQ(sound->subrecord, fo4::Signature::from_string("STCP"));
  EXPECT_EQ(sound->offset, 0u);
  EXPECT_FALSE(sound->nullable);
  EXPECT_EQ(sound->payload_sizes[0], 4u);
  const auto stag = fo4::Signature::from_string("STAG");
  const auto wthr = fo4::Signature::from_string("WTHR");
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*sound, stag));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*sound, wthr));
  EXPECT_EQ(link_raw(plugin, "000F01:Patch.esp", "STCP"), 0x00000F30u);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(
                   plugin, fo4::FormKey::parse("000F01:Patch.esp"), *sound, null_target)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000F01:Patch.esp"), *sound,
                                           retarget),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto preview = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("FLOR"),
                                                              "FLOR\\PreviewTransform");
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->subrecord, fo4::Signature::from_string("PTRN"));
  EXPECT_FALSE(preview->nullable);
  EXPECT_EQ(preview->payload_sizes[0], 4u);
  const auto trns = fo4::Signature::from_string("TRNS");
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*preview, trns));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*preview, stag));
  EXPECT_EQ(link_raw(plugin, "000F02:Patch.esp", "PTRN"), 0x00000F31u);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(
                   plugin, fo4::FormKey::parse("000F02:Patch.esp"), *preview, null_target)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000F02:Patch.esp"),
                                           *preview, retarget),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(link_raw(reread, "000F01:Patch.esp", "STCP"), 0x01000F20u);
  EXPECT_EQ(link_raw(reread, "000F02:Patch.esp", "PTRN"), 0x01000F20u);
}
