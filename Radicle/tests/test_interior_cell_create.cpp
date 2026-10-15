#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> empty_plugin(std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

const fo4::Group* top_cell_group(const fo4::Plugin& plugin) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type == 0 && group.header.top_label() == fo4::Signature::from_string("CELL")) {
      return &group;
    }
  }
  return nullptr;
}

const fo4::Group* nested_group(const fo4::Group& parent, fo4::GroupType type, std::uint32_t label) {
  for (const auto& child : parent.children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group == nullptr) continue;
    if (group->header.group_type == static_cast<std::int32_t>(type) &&
        group->header.label_as_u32() == label) {
      return group;
    }
  }
  return nullptr;
}

std::vector<std::uint32_t> group_labels(const fo4::Group& parent) {
  std::vector<std::uint32_t> labels;
  for (const auto& child : parent.children) {
    if (const auto* group = std::get_if<fo4::Group>(&child)) labels.push_back(group->header.label_as_u32());
  }
  return labels;
}

std::vector<fo4::FormKey> cell_keys(const fo4::Group& subblock) {
  std::vector<fo4::FormKey> keys;
  for (const auto& child : subblock.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) keys.push_back(record->form_key);
  }
  return keys;
}

}  // namespace

TEST(InteriorCellCreate, EmptyPluginCreatesCellBlockAndSubblockWithMatchingLabels) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  const auto cell = fo4::forms::create_interior_cell(plugin, "Kitchen");
  EXPECT_EQ(cell, fo4::FormKey::parse("000900:Cells.esp"));

  ASSERT_EQ(plugin.groups.size(), 1u);
  const auto* top = top_cell_group(plugin);
  ASSERT_NE(top, nullptr);
  ASSERT_EQ(top->children.size(), 1u);
  const auto* block = nested_group(*top, fo4::GroupType::interior_cell_block, 4u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = nested_group(*block, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 1u);

  const auto* record = std::get_if<fo4::Record>(&subblock->children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->form_key, cell);
  EXPECT_EQ(record->header.signature.to_string(), "CELL");
}

TEST(InteriorCellCreate, BlocksAreInsertedInAscendingLabelOrder) {
  auto plugin = fo4::read_plugin(empty_plugin(0x905), "Cells.esp");
  static_cast<void>(fo4::forms::create_interior_cell(plugin, "First"));
  fo4::set_hedr_next_form_id(plugin, 0x901);
  const auto second = fo4::forms::create_interior_cell(plugin, "Second");
  EXPECT_EQ(second, fo4::FormKey::parse("000901:Cells.esp"));

  const auto* top = top_cell_group(plugin);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(group_labels(*top), (std::vector<std::uint32_t>{5u, 9u}));
}

TEST(InteriorCellCreate, CellInsertedBetweenExistingCellsKeepsFormIdOrder) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  const auto first = fo4::forms::create_interior_cell(plugin, "First");
  fo4::set_hedr_next_form_id(plugin, 0x9C8);
  const auto last = fo4::forms::create_interior_cell(plugin, "Last");
  fo4::set_hedr_next_form_id(plugin, 0x964);
  const auto middle = fo4::forms::create_interior_cell(plugin, "Middle");
  EXPECT_EQ(middle, fo4::FormKey::parse("000964:Cells.esp"));

  const auto* top = top_cell_group(plugin);
  ASSERT_NE(top, nullptr);
  const auto* block = nested_group(*top, fo4::GroupType::interior_cell_block, 4u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = nested_group(*block, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  EXPECT_EQ(cell_keys(*subblock), (std::vector<fo4::FormKey>{first, middle, last}));
}

TEST(InteriorCellCreate, RefusesEmptyAndDuplicateEditorIdsWithoutMutation) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  static_cast<void>(fo4::forms::create_interior_cell(plugin, "Kitchen"));
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_interior_cell(plugin, "")), fo4::forms::CellError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_interior_cell(plugin, "kitchen")),
               fo4::forms::CellError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(InteriorCellCreate, RoundTripKeepsEditorIdAndInteriorFlag) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  const auto cell = fo4::forms::create_interior_cell(plugin, "Round");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* top = top_cell_group(reread);
  ASSERT_NE(top, nullptr);
  const auto* block = nested_group(*top, fo4::GroupType::interior_cell_block, 4u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = nested_group(*block, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 1u);
  const auto* record = std::get_if<fo4::Record>(&subblock->children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->form_key, cell);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(record->subrecords[0].data.size(), std::string_view{"Round"}.size() + 1u);
  EXPECT_EQ(record->subrecords[1].signature, fo4::Signature::from_string("DATA"));
  ASSERT_EQ(record->subrecords[1].data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(record->subrecords[1].data[0]), 1u);
}

TEST(InteriorCellCreate, NewLocalCellSortsAfterMasterOverrideWithSameLabels) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x83D);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", std::string_view{"\0\0\0\0\0\0\0\0", 8});
  fo4::test::end_record(out, tes4);

  const auto top_group = fo4::test::begin_group(out, "CELL", 0);
  const auto block_group = fo4::test::begin_group(out, std::string_view{"\x09\0\0\0", 4}, 2);
  const auto subblock_group = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 3);
  const auto override_record = fo4::test::begin_record(out, "CELL", 0, 0x0001F3A5, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Override\0", 9});
  fo4::test::write_subrecord(out, "DATA", std::string_view{"\x01\0", 2});
  fo4::test::end_record(out, override_record);
  fo4::test::end_group(out, subblock_group);
  fo4::test::end_group(out, block_group);
  fo4::test::end_group(out, top_group);

  auto plugin = fo4::read_plugin(out.bytes, "Cells.esp");
  const auto cell = fo4::forms::create_interior_cell(plugin, "Kitchen");
  EXPECT_EQ(cell, fo4::FormKey::parse("00083D:Cells.esp"));

  const auto* top = top_cell_group(plugin);
  ASSERT_NE(top, nullptr);
  const auto* block = nested_group(*top, fo4::GroupType::interior_cell_block, 9u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = nested_group(*block, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  EXPECT_EQ(cell_keys(*subblock),
            (std::vector<fo4::FormKey>{fo4::FormKey::parse("01F3A5:Fallout4.esm"), cell}));
}

TEST(InteriorCellCreate, CellInsertedBeforeLaterCellKeepsEarlierChildrenGroupAfterItsCell) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  const auto first = fo4::forms::create_interior_cell(plugin, "First");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  static_cast<void>(fo4::forms::create_placed_reference(plugin, {.cell = first, .base = base}));
  fo4::set_hedr_next_form_id(plugin, 0x9C8);
  const auto last = fo4::forms::create_interior_cell(plugin, "Last");
  fo4::set_hedr_next_form_id(plugin, 0x964);
  const auto middle = fo4::forms::create_interior_cell(plugin, "Middle");

  const auto* top = top_cell_group(plugin);
  ASSERT_NE(top, nullptr);
  const auto* block = nested_group(*top, fo4::GroupType::interior_cell_block, 4u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = nested_group(*block, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 4u);

  const auto* first_record = std::get_if<fo4::Record>(&subblock->children[0]);
  ASSERT_NE(first_record, nullptr);
  EXPECT_EQ(first_record->form_key, first);
  const auto* children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_children));
  EXPECT_EQ(children->header.label_as_u32(), 0x900u);
  EXPECT_EQ(cell_keys(*subblock), (std::vector<fo4::FormKey>{first, middle, last}));
}

TEST(InteriorCellCreate, RefusesEmbeddedNulInEditorIdWithoutMutation) {
  auto plugin = fo4::read_plugin(empty_plugin(0x900), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_interior_cell(plugin, std::string_view{"Ki\0tchen", 8})),
               fo4::forms::CellError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
