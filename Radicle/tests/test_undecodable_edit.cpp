#include "fo4/environment/base_records.hpp"
#include "fo4/forms/armor.hpp"
#include "fo4/forms/components.hpp"
#include "fo4/forms/effects.hpp"
#include "fo4/forms/eyes.hpp"
#include "fo4/forms/faction.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/forms/npc.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/forms/quest.hpp"
#include "fo4/forms/race.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/master_usage.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t k_undecodable_id = 0x00000802;
constexpr std::uint32_t k_sibling_id = 0x00000803;
constexpr std::uint32_t k_cell_id = 0x00000800;
constexpr std::uint32_t k_reference_id = 0x00000801;
constexpr std::string_view k_zero_label{"\0\0\0\0", 4};
constexpr std::string_view k_cell_label{"\0\x08\0\0", 4};

std::vector<std::byte> not_zlib() {
  fo4::test::ByteWriter stream;
  stream.raw(std::string_view{"not-zlib-data-at-all"});
  return stream.bytes;
}

void add_compressed(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                    std::uint32_t declared_size, std::span<const std::byte> stream) {
  const auto rec = fo4::test::begin_record(out, signature, fo4::RecordFlags::compressed, form_id);
  out.u32(declared_size);
  out.raw(stream);
  fo4::test::end_record(out, rec);
}

void add_empty_compressed(fo4::test::ByteWriter& out, std::string_view signature,
                          std::uint32_t form_id) {
  const auto rec = fo4::test::begin_record(out, signature, fo4::RecordFlags::compressed, form_id);
  out.u32(0);
  fo4::test::end_record(out, rec);
}

void add_plain(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id) {
  const auto rec = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
  fo4::test::end_record(out, rec);
}

void begin_tes4(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> undecodable_plugin(std::string_view signature) {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  const auto group = fo4::test::begin_group(out, signature, 0);
  add_compressed(out, signature, k_undecodable_id, 32, not_zlib());
  add_plain(out, signature, k_sibling_id);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plain_plugin(std::string_view signature, std::uint32_t form_id) {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  const auto group = fo4::test::begin_group(out, signature, 0);
  add_plain(out, signature, form_id);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> empty_compressed_base() {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  add_empty_compressed(out, "MISC", 0x00000800);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> base_patch(bool undecodable) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  if (undecodable) {
    add_compressed(out, "MISC", 0x00000800, 32, not_zlib());
  } else {
    add_empty_compressed(out, "MISC", 0x00000800);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

void expect_undecodable_intact(const fo4::Plugin& plugin, std::uint32_t form_id) {
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{form_id});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
  EXPECT_TRUE(record->subrecords.empty());
}

void expect_plugin_bytes_unchanged(const fo4::Plugin& plugin, const std::vector<std::byte>& bytes) {
  auto dirty = plugin;
  dirty.original_range.clear();
  EXPECT_EQ(fo4::write_plugin(dirty), bytes);
}

void add_interior_cell_reference(fo4::test::ByteWriter& out, bool undecodable_reference) {
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(
      out, k_zero_label, static_cast<std::int32_t>(fo4::GroupType::interior_cell_block));
  const auto subblock = fo4::test::begin_group(
      out, k_zero_label, static_cast<std::int32_t>(fo4::GroupType::interior_cell_subblock));
  const auto cell = fo4::test::begin_record(out, "CELL", 0, k_cell_id);
  fo4::test::write_subrecord(out, "DATA", std::string_view{"\x01\0", 2});
  fo4::test::end_record(out, cell);
  const auto children = fo4::test::begin_group(
      out, k_cell_label, static_cast<std::int32_t>(fo4::GroupType::cell_children));
  const auto temporary = fo4::test::begin_group(
      out, k_cell_label, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  if (undecodable_reference) {
    add_compressed(out, "REFR", k_reference_id, 32, not_zlib());
  } else {
    add_plain(out, "REFR", k_reference_id);
  }
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
}

std::vector<std::byte> interior_cell_plugin(bool undecodable_reference) {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  add_interior_cell_reference(out, undecodable_reference);
  return out.bytes;
}

constexpr std::string_view k_reference_label{"\x01\x08\0\0", 4};

void begin_master_header(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> undecodable_master_bytes() {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  add_compressed(out, "MISC", k_cell_id, 32, not_zlib());
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> target_with_cell(bool undecodable_cell) {
  fo4::test::ByteWriter out;
  begin_master_header(out);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(
      out, k_zero_label, static_cast<std::int32_t>(fo4::GroupType::interior_cell_block));
  const auto subblock = fo4::test::begin_group(
      out, k_zero_label, static_cast<std::int32_t>(fo4::GroupType::interior_cell_subblock));
  if (undecodable_cell) {
    add_compressed(out, "CELL", k_cell_id, 32, not_zlib());
  } else {
    const auto cell = fo4::test::begin_record(out, "CELL", 0, k_cell_id);
    fo4::test::write_subrecord(out, "DATA", std::string_view{"\x01\0", 2});
    fo4::test::end_record(out, cell);
  }
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

void add_dial(fo4::test::ByteWriter& out, bool undecodable) {
  if (undecodable) {
    add_compressed(out, "DIAL", k_reference_id, 32, not_zlib());
    return;
  }
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, k_reference_id);
  fo4::test::ByteWriter qnam;
  qnam.u32(k_cell_id);
  fo4::test::write_subrecord(out, "QNAM", qnam.bytes);
  fo4::test::end_record(out, dial);
}

std::vector<std::byte> info_source_bytes(bool undecodable_info) {
  fo4::test::ByteWriter out;
  begin_tes4(out);
  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  add_plain(out, "QUST", k_cell_id);
  const auto quest_children = fo4::test::begin_group(
      out, k_cell_label, static_cast<std::int32_t>(fo4::GroupType::cell_visible_distant_children));
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  add_dial(out, false);
  const auto topic = fo4::test::begin_group(
      out, k_reference_label, static_cast<std::int32_t>(fo4::GroupType::topic_children));
  if (undecodable_info) {
    add_compressed(out, "INFO", k_undecodable_id, 32, not_zlib());
  } else {
    add_plain(out, "INFO", k_undecodable_id);
  }
  fo4::test::end_group(out, topic);
  fo4::test::end_group(out, dial_top);
  fo4::test::end_group(out, quest_children);
  fo4::test::end_group(out, quest_top);
  return out.bytes;
}

std::vector<std::byte> info_target_bytes(bool undecodable_dial) {
  fo4::test::ByteWriter out;
  begin_master_header(out);
  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  add_plain(out, "QUST", k_cell_id);
  const auto quest_children = fo4::test::begin_group(
      out, k_cell_label, static_cast<std::int32_t>(fo4::GroupType::cell_visible_distant_children));
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  add_dial(out, undecodable_dial);
  const auto topic = fo4::test::begin_group(
      out, k_reference_label, static_cast<std::int32_t>(fo4::GroupType::topic_children));
  fo4::test::end_group(out, topic);
  fo4::test::end_group(out, dial_top);
  fo4::test::end_group(out, quest_children);
  fo4::test::end_group(out, quest_top);
  return out.bytes;
}

}  // namespace

TEST(UndecodableEdit, SetRecordScalarRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::set_record_scalar(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), fo4::sig_edid, "Renamed")),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, SetWeatherMnamRefusesUndecodableWeather) {
  const auto bytes = undecodable_plugin("WTHR");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_weather_mnam(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), std::nullopt)),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, SetRecordConditionsRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::set_record_conditions(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), {})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, SetRecordEditorIdRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::set_record_editor_id(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), "Renamed")),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, CopyAsOverrideRefusesUndecodableSource) {
  const auto source_bytes = undecodable_plugin("MISC");
  const auto target_bytes = plain_plugin("MISC", 0x00000804);
  const auto source = fo4::read_plugin(source_bytes, "Source.esp");
  auto target = fo4::read_plugin(target_bytes, "Target.esp");
  EXPECT_THROW(static_cast<void>(fo4::copy_record_as_override(
                   target, source, fo4::FormKey::parse("000802:Source.esp"), false)),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
  expect_undecodable_intact(source, k_undecodable_id);
}

TEST(UndecodableEdit, CopyAsOverrideRefusesUndecodableOverwriteTarget) {
  const auto source_bytes = plain_plugin("MISC", k_undecodable_id);
  const auto target_bytes = undecodable_plugin("MISC");
  const auto source = fo4::read_plugin(source_bytes, "Shared.esp");
  auto target = fo4::read_plugin(target_bytes, "Shared.esp");
  EXPECT_THROW(static_cast<void>(fo4::copy_record_as_override(
                   target, source, fo4::FormKey::parse("000802:Shared.esp"), true)),
               std::invalid_argument);
  expect_undecodable_intact(target, k_undecodable_id);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, DeleteRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::delete_local_record(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"))),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, DeleteRefusesOtherRecordWhilePluginIsUndecodable) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  try {
    static_cast<void>(fo4::delete_local_record(plugin, fo4::FormKey::parse("000803:Edit.esp")));
    FAIL() << "delete accepted a plugin with an undecodable record";
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("1 undecodable record"), std::string::npos) << message;
    EXPECT_NE(message.find("000802:Edit.esp"), std::string::npos) << message;
  }
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, RenumberRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::renumber_local_record(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), fo4::LocalFormId{0x900})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, RenumberRefusesOtherRecordWhilePluginIsUndecodable) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::renumber_local_record(
                   plugin, fo4::FormKey::parse("000803:Edit.esp"), fo4::LocalFormId{0x900})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, RemoveRecordsRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const std::vector<fo4::FormKey> keys{fo4::FormKey::parse("000802:Edit.esp")};
  EXPECT_THROW(static_cast<void>(fo4::remove_records(plugin, keys)), std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, FindIdenticalToMasterSkipsUndecodableRecord) {
  const auto base_bytes = empty_compressed_base();
  const auto valid_bytes = base_patch(false);
  const auto undecodable_bytes = base_patch(true);
  const auto base = fo4::read_plugin(base_bytes, "Base.esm");
  const auto valid = fo4::read_plugin(valid_bytes, "Patch.esp");
  const auto undecodable = fo4::read_plugin(undecodable_bytes, "Patch.esp");

  const std::vector<const fo4::Plugin*> control_order{&base, &valid};
  ASSERT_EQ(fo4::find_identical_to_master(control_order, 1).size(), 1u);

  const std::vector<const fo4::Plugin*> order{&base, &undecodable};
  EXPECT_TRUE(fo4::find_identical_to_master(order, 1).empty());
}

TEST(UndecodableEdit, PlacedReferenceCopyRefusesUndecodableSource) {
  const auto source_bytes = interior_cell_plugin(true);
  const auto target_bytes = plain_plugin("CELL", 0x00000804);
  const auto source = fo4::read_plugin(source_bytes, "Source.esp");
  auto target = fo4::read_plugin(target_bytes, "Target.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_placed_reference_as_override(
                   target, source, fo4::FormKey::parse("000801:Source.esp"), false)),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, PlacedReferenceOverwriteRefusesUndecodableTarget) {
  const auto source_bytes = interior_cell_plugin(false);
  const auto target_bytes = interior_cell_plugin(true);
  const auto source = fo4::read_plugin(source_bytes, "Shared.esp");
  auto target = fo4::read_plugin(target_bytes, "Shared.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_placed_reference_as_override(
                   target, source, fo4::FormKey::parse("000801:Shared.esp"), true)),
               std::invalid_argument);
  expect_undecodable_intact(target, k_reference_id);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, RemapRecordFormLinksRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto* found = fo4::find_record(plugin, fo4::FormKey::parse("000802:Edit.esp"));
  ASSERT_NE(found, nullptr);
  auto record = *found;
  const auto context = fo4::plugin_form_id_context(plugin);
  EXPECT_THROW(fo4::remap_record_form_links(record, context, context), std::invalid_argument);
  EXPECT_TRUE(record.undecodable);
  EXPECT_TRUE(record.subrecords.empty());
}

TEST(UndecodableEdit, SyncQuestVmadRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("QUST");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto* found = fo4::find_record(plugin, fo4::FormKey::parse("000802:Edit.esp"));
  ASSERT_NE(found, nullptr);
  auto record = *found;
  const fo4::forms::QuestStages stages{};
  const fo4::forms::QuestAliases aliases{};
  EXPECT_THROW(fo4::forms::sync_quest_vmad_stages(record, stages, stages), std::invalid_argument);
  EXPECT_THROW(fo4::forms::sync_quest_vmad_aliases(
                   record, aliases, std::span<const fo4::forms::QuestAliasCreation>{}),
               std::invalid_argument);
}

TEST(UndecodableEdit, WriteCtdaConditionsRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto* found = fo4::find_record(plugin, fo4::FormKey::parse("000802:Edit.esp"));
  ASSERT_NE(found, nullptr);
  auto record = *found;
  EXPECT_THROW(static_cast<void>(fo4::write_ctda_conditions(
                   record, std::span<const fo4::CtdaCondition>{})),
               std::invalid_argument);
  EXPECT_TRUE(record.undecodable);
}

TEST(UndecodableEdit, MasterUsageIsIncompleteWhileRecordIsUndecodable) {
  const auto valid_bytes = base_patch(false);
  const auto valid = fo4::read_plugin(valid_bytes, "Patch.esp");
  EXPECT_TRUE(fo4::scan_master_usage(valid).complete());

  const auto undecodable_bytes = base_patch(true);
  const auto undecodable = fo4::read_plugin(undecodable_bytes, "Patch.esp");
  EXPECT_FALSE(fo4::scan_master_usage(undecodable).complete());
}

TEST(UndecodableEdit, FindIdenticalToMasterSkipsWhenMasterRecordIsUndecodable) {
  const auto base_bytes = empty_compressed_base();
  const auto undecodable_base_bytes = undecodable_master_bytes();
  const auto patch_bytes = base_patch(false);
  const auto base = fo4::read_plugin(base_bytes, "Base.esm");
  const auto undecodable_base = fo4::read_plugin(undecodable_base_bytes, "Base.esm");
  const auto patch = fo4::read_plugin(patch_bytes, "Patch.esp");

  const std::vector<const fo4::Plugin*> control_order{&base, &patch};
  ASSERT_EQ(fo4::find_identical_to_master(control_order, 1).size(), 1u);

  const std::vector<const fo4::Plugin*> order{&undecodable_base, &patch};
  EXPECT_TRUE(fo4::find_identical_to_master(order, 1).empty());
}

TEST(UndecodableEdit, CopyPlacedReferenceAsNewRefusesUndecodableSource) {
  const auto source_bytes = interior_cell_plugin(true);
  const auto target_bytes = target_with_cell(false);
  const auto source = fo4::read_plugin(source_bytes, "Base.esm");
  auto target = fo4::read_plugin(target_bytes, "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_placed_reference_as_new(
                   target, source, fo4::FormKey::parse("000801:Base.esm"))),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, CopyPlacedReferenceAsNewRefusesUndecodableParentCell) {
  const auto source_bytes = interior_cell_plugin(false);
  const auto target_bytes = target_with_cell(true);
  const auto source = fo4::read_plugin(source_bytes, "Base.esm");
  auto target = fo4::read_plugin(target_bytes, "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_placed_reference_as_new(
                   target, source, fo4::FormKey::parse("000801:Base.esm"))),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, CopyInfoAsNewRefusesUndecodableSource) {
  const auto source_bytes = info_source_bytes(true);
  const auto target_bytes = info_target_bytes(false);
  const auto source = fo4::read_plugin(source_bytes, "Base.esm");
  auto target = fo4::read_plugin(target_bytes, "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_info_as_new(
                   target, source, fo4::FormKey::parse("000802:Base.esm"))),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, CopyInfoAsNewRefusesUndecodableDestinationDial) {
  const auto source_bytes = info_source_bytes(false);
  const auto target_bytes = info_target_bytes(true);
  const auto source = fo4::read_plugin(source_bytes, "Base.esm");
  auto target = fo4::read_plugin(target_bytes, "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::copy_info_as_new(
                   target, source, fo4::FormKey::parse("000802:Base.esm"))),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(target, target_bytes);
}

TEST(UndecodableEdit, FactionIndexedEditsRefuseUndecodableRecord) {
  const auto bytes = undecodable_plugin("FACT");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(fo4::forms::set_faction_relation(plugin, key, 0, fo4::forms::FactionRelation{}),
               std::invalid_argument);
  EXPECT_THROW(fo4::forms::remove_faction_relation(plugin, key, 0), std::invalid_argument);
  EXPECT_THROW(fo4::forms::set_faction_rank(plugin, key, 0, fo4::forms::FactionRank{}),
               std::invalid_argument);
  EXPECT_THROW(fo4::forms::remove_faction_rank(plugin, key, 0), std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, NpcIndexedEditsRefuseUndecodableRecord) {
  const auto bytes = undecodable_plugin("NPC_");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(static_cast<void>(
                   fo4::forms::set_npc_faction(plugin, key, 0, fo4::forms::NpcFaction{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::remove_npc_faction(plugin, key, 0)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_npc_perk(plugin, key, 0, fo4::forms::NpcPerk{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::remove_npc_perk(plugin, key, 0)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_npc_inventory_item(
                   plugin, key, 0, fo4::forms::NpcInventoryItem{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::remove_npc_inventory_item(plugin, key, 0)),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, LeveledListIndexedEditsRefuseUndecodableRecord) {
  const auto bytes = undecodable_plugin("LVLI");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::insert_leveled_list_entry(
                   plugin, key, 0, fo4::forms::LeveledListEntry{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::remove_leveled_list_entry(plugin, key, 0)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_leveled_list_entry(
                   plugin, key, 0, fo4::forms::LeveledListEntry{})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, LeveledListWholeEditsRefuseUndecodableRecord) {
  const auto bytes = undecodable_plugin("LVLN");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::replace_leveled_list_entries(
                   plugin, key, std::span<const fo4::forms::LeveledListEntry>{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_leveled_list_settings(plugin, key, 1, 1, 0)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::reset_leveled_list(plugin, key)),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, SetMagicEffectsRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("SPEL");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_magic_effects(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"),
                   std::span<const fo4::forms::MagicEffectEntry>{})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, ArmorModelEditsRefuseUndecodableRecord) {
  const auto armo_bytes = undecodable_plugin("ARMO");
  auto armo = fo4::read_plugin(armo_bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_armor_model_fields(
                   armo, fo4::FormKey::parse("000802:Edit.esp"), std::nullopt, std::nullopt,
                   std::nullopt, std::nullopt, std::nullopt, std::nullopt)),
               std::invalid_argument);
  expect_undecodable_intact(armo, k_undecodable_id);
  expect_plugin_bytes_unchanged(armo, armo_bytes);

  const auto arma_bytes = undecodable_plugin("ARMA");
  auto arma = fo4::read_plugin(arma_bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_armor_addon_model_fields(
                   arma, fo4::FormKey::parse("000802:Edit.esp"), std::nullopt, std::nullopt,
                   std::nullopt, std::nullopt)),
               std::invalid_argument);
  expect_undecodable_intact(arma, k_undecodable_id);
  expect_plugin_bytes_unchanged(arma, arma_bytes);
}

TEST(UndecodableEdit, ComponentEditRefusesUndecodableRecord) {
  const auto bytes = undecodable_plugin("MISC");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_record_components(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"),
                   std::span<const fo4::forms::ComponentEntry>{})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, EyesAndRaceBodyEditsRefuseUndecodableRecord) {
  const auto eyes_bytes = undecodable_plugin("EYES");
  auto eyes = fo4::read_plugin(eyes_bytes, "Edit.esp");
  const auto eyes_key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_eyes_data_flags(eyes, eyes_key, 1)),
               fo4::forms::EyesError);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_eyes_text_fields(
                   eyes, eyes_key, std::nullopt, std::string_view{"x"}, std::nullopt)),
               fo4::forms::EyesError);
  expect_undecodable_intact(eyes, k_undecodable_id);
  expect_plugin_bytes_unchanged(eyes, eyes_bytes);

  const auto race_bytes = undecodable_plugin("RACE");
  auto race = fo4::read_plugin(race_bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_race_body_data(
                   race, fo4::FormKey::parse("000802:Edit.esp"), {}, {})),
               std::invalid_argument);
  expect_undecodable_intact(race, k_undecodable_id);
  expect_plugin_bytes_unchanged(race, race_bytes);
}

TEST(UndecodableEdit, QuestStagesAndObjectivesWritesRefuseUndecodableQuest) {
  const auto bytes = undecodable_plugin("QUST");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(fo4::forms::write_quest_stages(plugin, key, fo4::forms::QuestStages{}),
               std::invalid_argument);
  EXPECT_THROW(fo4::forms::write_quest_objectives(plugin, key, fo4::forms::QuestObjectives{}),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, PlacedReferenceTransformScaleAndMoveRefuseUndecodableReference) {
  const auto bytes = interior_cell_plugin(true);
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  const auto reference = fo4::FormKey::parse("000801:Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::set_placed_reference_transform(
                   plugin, reference, fo4::forms::PositionRotation{})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::set_placed_reference_scale(plugin, reference, 1.0f)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::move_placed_reference(
                   plugin, reference, fo4::forms::PositionRotation{})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_reference_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, CreatePlacedReferenceRefusesUndecodableCell) {
  const auto bytes = undecodable_plugin("CELL");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin, fo4::forms::PlacedReferenceCreation{
                               .cell = fo4::FormKey::parse("000802:Edit.esp"),
                               .base = fo4::FormKey::parse("000803:Edit.esp")})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}

TEST(UndecodableEdit, FactionAndNpcAddsRefuseUndecodableRecord) {
  const auto faction_bytes = undecodable_plugin("FACT");
  auto faction = fo4::read_plugin(faction_bytes, "Edit.esp");
  const auto faction_key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(fo4::forms::add_faction_relation(
                   faction, faction_key,
                   fo4::forms::FactionRelation{fo4::FormKey::parse("000123:Fallout4.esm"), 0, {}}),
               std::invalid_argument);
  EXPECT_THROW(fo4::forms::add_faction_rank(faction, faction_key, fo4::forms::FactionRank{}),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(faction, faction_bytes);

  const auto npc_bytes = undecodable_plugin("NPC_");
  auto npc = fo4::read_plugin(npc_bytes, "Edit.esp");
  const auto npc_key = fo4::FormKey::parse("000802:Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::add_npc_faction(
                   npc, npc_key,
                   fo4::forms::NpcFaction{fo4::FormKey::parse("000123:Fallout4.esm"), 1})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::add_npc_perk(
                   npc, npc_key,
                   fo4::forms::NpcPerk{fo4::FormKey::parse("000456:Fallout4.esm"), 1})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::forms::add_npc_inventory_item(
                   npc, npc_key,
                   fo4::forms::NpcInventoryItem{fo4::FormKey::parse("000789:Fallout4.esm"), 1, std::nullopt})),
               std::invalid_argument);
  expect_plugin_bytes_unchanged(npc, npc_bytes);
}

TEST(UndecodableEdit, SetClimateTimingRefusesUndecodableClimate) {
  const auto bytes = undecodable_plugin("CLMT");
  auto plugin = fo4::read_plugin(bytes, "Edit.esp");
  EXPECT_THROW(static_cast<void>(fo4::env::set_climate_timing(
                   plugin, fo4::FormKey::parse("000802:Edit.esp"), fo4::env::ClimateTiming{})),
               std::invalid_argument);
  expect_undecodable_intact(plugin, k_undecodable_id);
  expect_plugin_bytes_unchanged(plugin, bytes);
}
