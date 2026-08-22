#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> plugin_with_worldspace() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

const fo4::Group* top_wrld(const fo4::Plugin& plugin) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type == static_cast<std::int32_t>(fo4::GroupType::top) &&
        group.header.top_label() == fo4::Signature::from_string("WRLD")) {
      return &group;
    }
  }
  return nullptr;
}

const fo4::Group* child_group(const std::vector<fo4::GroupChild>& children,
                              fo4::GroupType type,
                              std::uint32_t label) {
  const fo4::Group* found = nullptr;
  for (const auto& child : children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group == nullptr ||
        group->header.group_type != static_cast<std::int32_t>(type) ||
        group->header.label_as_u32() != label) {
      continue;
    }
    EXPECT_EQ(found, nullptr);
    found = group;
  }
  return found;
}

std::size_t count_groups(const fo4::Group& parent, fo4::GroupType type) {
  std::size_t count = 0;
  for (const auto& child : parent.children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group != nullptr &&
        group->header.group_type == static_cast<std::int32_t>(type)) {
      ++count;
    }
  }
  return count;
}

std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

const fo4::Subrecord* unique_subrecord(const fo4::Record& record,
                                       std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  const fo4::Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    EXPECT_EQ(found, nullptr);
    found = &subrecord;
  }
  return found;
}

}

TEST(ExteriorCellCreation, CreatesRequiredWrldBlockSubblockHierarchyAndRoundTrips) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");

  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = -33,
          .y = 32,
          .editor_id = std::string_view{"CreatedExterior"},
      });
  EXPECT_EQ(cell, fo4::FormKey::parse("000900:Cells.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x901u);

  const auto* top = top_wrld(plugin);
  ASSERT_NE(top, nullptr);
  const auto* world_children = child_group(
      top->children, fo4::GroupType::world_children, 0x00000800u);
  ASSERT_NE(world_children, nullptr);
  const auto* block = child_group(
      world_children->children,
      fo4::GroupType::exterior_cell_block,
      0xFFFE0001u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = child_group(
      block->children,
      fo4::GroupType::exterior_cell_subblock,
      0xFFFB0004u);
  ASSERT_NE(subblock, nullptr);

  const auto* created = fo4::forms::find_exterior_cell(plugin, world, -33, 32);
  ASSERT_NE(created, nullptr);
  EXPECT_EQ(created->form_key, cell);
  ASSERT_EQ(created->subrecords.size(), 3u);
  EXPECT_EQ(created->subrecords[0].signature, fo4::sig_edid);
  EXPECT_EQ(created->subrecords[1].signature, fo4::sig_data);
  EXPECT_EQ(created->subrecords[2].signature.to_string(), "XCLC");
  EXPECT_EQ(created->subrecords[1].data.size(), 2u);
  EXPECT_EQ(created->subrecords[1].data[0], std::byte{0});
  EXPECT_EQ(created->subrecords[1].data[1], std::byte{0});
  EXPECT_EQ(created->subrecords[2].data.size(), 12u);
  EXPECT_EQ(read_i32(created->subrecords[2].data, 0), -33);
  EXPECT_EQ(read_i32(created->subrecords[2].data, 4), 32);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  EXPECT_EQ(reread.header.record_count, 6u);
  EXPECT_EQ(reread.header.next_form_id, 0x901u);
  const auto* round_trip = fo4::forms::find_exterior_cell(reread, world, -33, 32);
  ASSERT_NE(round_trip, nullptr);
  EXPECT_EQ(round_trip->form_key, cell);
}

TEST(ExteriorCellCreation, ReusesBlockAndSubblockAndCreatesOnlyNeededGroups) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");

  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, {.worldspace = world, .x = -33, .y = 32}));
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, {.worldspace = world, .x = -34, .y = 33}));

  const auto* top = top_wrld(plugin);
  ASSERT_NE(top, nullptr);
  const auto* world_children = child_group(
      top->children, fo4::GroupType::world_children, 0x00000800u);
  ASSERT_NE(world_children, nullptr);
  EXPECT_EQ(count_groups(*world_children, fo4::GroupType::exterior_cell_block), 1u);
  const auto* block = child_group(
      world_children->children,
      fo4::GroupType::exterior_cell_block,
      0xFFFE0001u);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(count_groups(*block, fo4::GroupType::exterior_cell_subblock), 1u);

  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, {.worldspace = world, .x = -40, .y = 40}));
  top = top_wrld(plugin);
  ASSERT_NE(top, nullptr);
  world_children = child_group(
      top->children, fo4::GroupType::world_children, 0x00000800u);
  ASSERT_NE(world_children, nullptr);
  block = child_group(
      world_children->children,
      fo4::GroupType::exterior_cell_block,
      0xFFFE0001u);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(count_groups(*block, fo4::GroupType::exterior_cell_subblock), 2u);
  EXPECT_NE(child_group(
      block->children,
      fo4::GroupType::exterior_cell_subblock,
      0xFFFB0005u), nullptr);
  EXPECT_EQ(plugin.header.next_form_id, 0x903u);
}

TEST(ExteriorCellCreation, OccupiedCoordinateFailsWithoutConsumingFormId) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, {.worldspace = world, .x = 1, .y = 2}));
  ASSERT_EQ(plugin.header.next_form_id, 0x901u);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin, {.worldspace = world, .x = 1, .y = 2})),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, 0x901u);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ExteriorCellCreation, RejectsMissingWorldspaceAndOutOfBoundsBeforeAllocation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto next_before = plugin.header.next_form_id;

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin,
          {.worldspace = fo4::FormKey::parse("000801:Cells.esp"), .x = 0, .y = 0})),
      fo4::forms::CellError);
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin,
          {.worldspace = fo4::FormKey::parse("000800:Cells.esp"), .x = 111, .y = 0})),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, next_before);
}

TEST(ExteriorCellCreation, MalformedExistingGridFailsClosedWithoutAllocation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, {.worldspace = world, .x = 1, .y = 2}));

  auto* created = const_cast<fo4::Record*>(
      fo4::forms::find_exterior_cell(plugin, world, 1, 2));
  ASSERT_NE(created, nullptr);
  auto* xclc = const_cast<fo4::Subrecord*>(unique_subrecord(*created, "XCLC"));
  ASSERT_NE(xclc, nullptr);
  xclc->data.resize(2);
  const auto next_before = plugin.header.next_form_id;

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin, {.worldspace = world, .x = 2, .y = 3})),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, next_before);
}
