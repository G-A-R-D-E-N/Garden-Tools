#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/record_flags.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"
#include "record_flags_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fo4::test;

namespace {

constexpr fo4::RecordFlags::value_type k_visible_when_distant = 1u << 15;

struct ExteriorLayout {
  bool persistent_cell{false};
  bool origin_cell{true};
  bool east_cell{false};
  bool reference_in_persistent{false};
  fo4::GroupType bucket{fo4::GroupType::cell_temporary_children};
  fo4::RecordFlags::value_type ref_flags{0};
  float x{10.0f};
  float y{11.0f};
  bool second_reference{false};
  bool origin_data{true};
};

std::string grid_data(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  out.u32(0);
  return std::string(reinterpret_cast<const char*>(out.bytes.data()), out.bytes.size());
}

void put_reference_children(fo4::test::ByteWriter& out, std::uint32_t cell_form, const ExteriorLayout& layout) {
  const auto children = fo4::test::begin_group(out, label_of(cell_form), 6);
  const auto bucket = fo4::test::begin_group(out, label_of(cell_form), static_cast<std::int32_t>(layout.bucket));
  const auto base = std::string{'\x01', '\x08', '\0', '\0'};
  put_record(out, "REFR", layout.ref_flags, 0x910,
             {{"NAME", base}, {"DATA", position_data(layout.x, layout.y, 12.0f)}});
  if (layout.second_reference) {
    put_record(out, "REFR", 0, 0x911,
               {{"NAME", base}, {"DATA", position_data(layout.x + 100.0f, layout.y, 12.0f)}});
  }
  fo4::test::end_group(out, bucket);
  fo4::test::end_group(out, children);
}

Bytes interior_with_two_temporary_buckets() {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", 0, 0x900, {{"DATA", std::string{'\x01', '\0'}}});
  const auto children = fo4::test::begin_group(out, label_of(0x900), 6);
  const auto persistent = fo4::test::begin_group(out, label_of(0x900), 8);
  put_record(out, "REFR", fo4::RecordFlags::persistent, 0x800,
             {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}, {"DATA", position_data(1.0f, 2.0f, 3.0f)}});
  fo4::test::end_group(out, persistent);
  for (const std::uint32_t form : {0x801u, 0x802u}) {
    const auto temporary = fo4::test::begin_group(out, label_of(0x900), 9);
    put_record(out, "REFR", 0, form,
               {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}, {"DATA", position_data(4.0f, 5.0f, 6.0f)}});
    fo4::test::end_group(out, temporary);
  }
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes exterior_world(const ExteriorLayout& layout) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = fo4::test::begin_group(out, label_of(0x800), 1);
  if (layout.persistent_cell) {
    put_record(out, "CELL", 0x400, 0x901, {{"DATA", std::string{'\x02', '\0'}}});
    if (layout.reference_in_persistent) put_reference_children(out, 0x901, layout);
  }
  const auto block = fo4::test::begin_group(out, label_of(0), 4);
  const auto subblock = fo4::test::begin_group(out, label_of(0), 5);
  if (layout.origin_cell) {
    Subrecords origin;
    if (layout.origin_data) origin.emplace_back("DATA", std::string{'\x02', '\0'});
    origin.emplace_back("XCLC", grid_data(0, 0));
    put_record(out, "CELL", 0, 0x902, origin);
    if (!layout.reference_in_persistent) put_reference_children(out, 0x902, layout);
  }
  if (layout.east_cell) {
    put_record(out, "CELL", 0, 0x903, {{"DATA", std::string{'\x02', '\0'}}, {"XCLC", grid_data(1, 0)}});
  }
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

fo4::Plugin load_exterior(const ExteriorLayout& layout) {
  return fo4::read_plugin(exterior_world(layout), "Exterior.esp");
}

}  // namespace

TEST(RecordFlags, InteriorPersistentChangeMovesReferenceBetweenBucketsAndRoundTrips) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* persistent = parent_group(reread, ref);
  ASSERT_NE(persistent, nullptr);
  EXPECT_EQ(persistent->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  const auto* children = find_group(reread, fo4::GroupType::cell_children, 0x900u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->children.size(), 1u);
  const auto* moved = fo4::find_record(reread, ref);
  ASSERT_NE(moved, nullptr);
  EXPECT_TRUE(moved->header.flags.is_persistent());

  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);
  const auto back = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* temporary = parent_group(back, ref);
  ASSERT_NE(temporary, nullptr);
  EXPECT_EQ(temporary->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  const auto* back_children = find_group(back, fo4::GroupType::cell_children, 0x900u);
  ASSERT_NE(back_children, nullptr);
  EXPECT_EQ(back_children->children.size(), 1u);
  const auto* restored = fo4::find_record(back, ref);
  ASSERT_NE(restored, nullptr);
  EXPECT_FALSE(restored->header.flags.is_persistent());
}

TEST(RecordFlags, PersistentChangeOnInteriorNavmeshKeepsItsBucket) {
  CellPlugin layout;
  layout.ref_signature = "NAVM";
  layout.ref_subs = {{"NVER", std::string(4, '\0')}};
  auto plugin = load(layout);
  const auto navm = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, navm, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* bucket = parent_group(reread, navm);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_persistent_children, 0x900u), nullptr);
  const auto* record = fo4::find_record(reread, navm);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
}

TEST(RecordFlags, ChangingDeletedBitThroughSetRecordFlagsIsRefused) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::deleted | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::deleted_bit);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, ExteriorPersistentSetMovesReferenceToWorldPersistentCell) {
  auto plugin = load_exterior({.persistent_cell = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto persistent_cell = fo4::FormKey::parse("000901:Exterior.esp");
  const auto origin = fo4::FormKey::parse("000902:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, persistent_cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, origin).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
}

TEST(RecordFlags, ExteriorPersistentClearMovesReferenceToGridCellByPosition) {
  auto plugin = load_exterior({.persistent_cell = true,
                               .east_cell = true,
                               .reference_in_persistent = true,
                               .bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp")).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000901u), nullptr);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp")).empty());
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000903:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
}

TEST(RecordFlags, ExteriorPersistentClearWithoutGridCellRefusesWithoutChange) {
  auto plugin = load_exterior({.persistent_cell = true,
                               .reference_in_persistent = true,
                               .bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, ExteriorPersistentSetWithoutPersistentCellRefusesWithoutChange) {
  auto plugin = load_exterior({});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, ExteriorPersistentClearInsideGridCellMovesBucketInPlace) {
  auto plugin = load_exterior({.bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_NE(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
}

TEST(RecordFlags, ExteriorVisibleWhenDistantToggleNeverMoves) {
  auto plugin = load_exterior({});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, k_visible_when_distant), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_NE(refs[0].record->header.flags.value() & k_visible_when_distant, 0u);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, ExteriorPersistentClearRelocatesFromWrongGridCellTemporaryBucket) {
  auto plugin = load_exterior({.east_cell = true,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp")).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000903:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
}

TEST(RecordFlags, ExteriorPersistentClearInTemporaryBucketOfSameCellStaysPut) {
  auto plugin = load_exterior({.ref_flags = fo4::RecordFlags::persistent});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, ExteriorPersistentSetInsideWorldPersistentCellMovesBucketInPlace) {
  auto plugin = load_exterior({.persistent_cell = true, .reference_in_persistent = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  EXPECT_NE(find_group(reread, fo4::GroupType::cell_children, 0x00000901u), nullptr);
}

TEST(RecordFlags, ExteriorPersistentSetMovesOnlyEditedReferenceAndKeepsSourceBucket) {
  auto plugin = load_exterior({.persistent_cell = true, .second_reference = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto second = fo4::FormKey::parse("000911:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto origin_refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(origin_refs.size(), 1u);
  EXPECT_EQ(origin_refs[0].record->form_key, second);
  EXPECT_FALSE(origin_refs[0].persistent);
  const auto* bucket = parent_group(reread, second);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  const auto persistent_refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp"));
  ASSERT_EQ(persistent_refs.size(), 1u);
  EXPECT_EQ(persistent_refs[0].record->form_key, ref);
  EXPECT_TRUE(persistent_refs[0].persistent);
}

TEST(RecordFlags, ExteriorReferenceInCellWithoutDataSetsFlagsWithoutMove) {
  auto plugin = load_exterior({.origin_data = false});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_NE(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
}

TEST(RecordFlags, ExteriorPersistentSetInTypeEightBucketOfGridCellStaysPut) {
  auto plugin = load_exterior({.bucket = fo4::GroupType::cell_persistent_children});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp")).empty());
}

TEST(RecordFlags, InteriorRelocationThatFailsLeavesPluginUnchanged) {
  auto plugin = fo4::read_plugin(interior_with_two_temporary_buckets(), "Interior.esp");
  const auto ref = fo4::FormKey::parse("000800:Interior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_flags(plugin, ref, 0), fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  const auto* record = fo4::find_record(plugin, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
}

TEST(RecordFlags, NonTes4RecordDropsMasterBitAndKeepsCompressed) {
  auto plugin = load(CellPlugin{.ref_flags = fo4::RecordFlags::master | fo4::RecordFlags::compressed});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::master | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_master());
  EXPECT_TRUE(record->header.flags.is_compressed());
  EXPECT_TRUE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
}

TEST(RecordFlags, PersistentChangeIsIgnoredOnDeletedRecord) {
  auto plugin = load(CellPlugin{.ref_flags = fo4::RecordFlags::deleted});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  EXPECT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::deleted | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::unchanged);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}
