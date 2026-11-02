#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_link_schema.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Item {
  std::string_view record;
  std::string_view key;
  std::uint32_t self_raw;
  std::string_view path;
  std::string_view subrecord;
  std::uint32_t initial_raw;
  std::uint32_t remapped_raw;
};

constexpr std::uint32_t k_retarget_raw = 0x01000A20;

constexpr Item k_items[] = {
    {"NPC_", "000A01:Patch.esp", 0x02000A01, "NPC_\\Race", "RNAM", 0x00000A10, 0x01000A10},
    {"WEAP", "000B01:Patch.esp", 0x02000B01, "WEAP\\ImpactDataSet", "INAM", 0x00000B10,
     0x01000B10},
    {"RACE", "000C01:Patch.esp", 0x02000C01, "RACE\\UnarmedWeapon", "UNWP", 0x00000C10,
     0x01000C10},
    {"HAZD", "000D01:Patch.esp", 0x02000D01, "HAZD\\ImageSpaceModifier", "MNAM", 0x00000D10,
     0x01000D10},
};

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
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
  for (const auto& item : k_items) {
    const auto group = fo4::test::begin_group(out, item.record, 0);
    const auto record = fo4::test::begin_record(out, item.record, 0, item.self_raw);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
    fo4::test::write_subrecord(out, item.subrecord, u32_bytes(item.initial_raw));
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
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

std::size_t link_index(const fo4::Record& record, std::string_view subrecord) {
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature.to_string() == subrecord) return i;
  }
  ADD_FAILURE() << subrecord;
  return 0;
}

}  // namespace

TEST(WP9LinkRows, NewRowsReadAndRetargetThroughFieldApi) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  for (const auto& item : k_items) {
    SCOPED_TRACE(item.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(item.record), item.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(item.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_EQ(field->payload_sizes[0], 4u);
    EXPECT_EQ(link_raw(plugin, item.key, item.subrecord), item.initial_raw);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(item.key), *field,
                                             fo4::FormKey::parse("000A20:Objects.esp")),
              fo4::FormLinkFieldMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& item : k_items) {
    SCOPED_TRACE(item.path);
    EXPECT_EQ(link_raw(reread, item.key, item.subrecord), k_retarget_raw);
  }
}

TEST(WP9LinkRows, NewRowsFollowMasterReorder) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  for (const auto& item : k_items) {
    SCOPED_TRACE(item.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr);
    const auto offsets = fo4::schema::schema_formlink_offsets(*record);
    ASSERT_EQ(offsets.size(), record->subrecords.size());
    EXPECT_EQ(offsets[link_index(*record, item.subrecord)], std::vector<std::size_t>{0});
  }
  const std::vector<fo4::PluginKey> masters{fo4::PluginKey::from_filename("Objects.esp"),
                                            fo4::PluginKey::from_filename("Fallout4.esm")};
  plugin = fo4::remap_plugin_masters(std::move(plugin), masters);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& item : k_items) {
    SCOPED_TRACE(item.path);
    EXPECT_EQ(link_raw(reread, item.key, item.subrecord), item.remapped_raw);
  }
}

TEST(WP9LinkRows, NullTargetFollowsXEditNullability) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto required =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("NPC_"), "NPC_\\Race");
  ASSERT_TRUE(required.has_value());
  EXPECT_FALSE(required->nullable);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000A01:Patch.esp"),
                                              *required, fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(subrecord_data(plugin, "000A01:Patch.esp", "RNAM"), u32_bytes(0x00000A10));

  const auto optional = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("HAZD"),
                                                               "HAZD\\ImageSpaceModifier");
  ASSERT_TRUE(optional.has_value());
  EXPECT_TRUE(optional->nullable);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000D01:Patch.esp"),
                                           *optional, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000D01:Patch.esp", "MNAM"), u32_bytes(0));
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "RNAM"), u32_bytes(0x00000A10));
}
