#include "fo4/forms/placed_reference.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
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
