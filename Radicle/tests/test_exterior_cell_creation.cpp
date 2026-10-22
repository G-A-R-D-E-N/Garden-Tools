#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
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

fo4::forms::ExteriorCellCreation cell_creation(const fo4::FormKey& worldspace,
                                               std::int32_t x,
                                               std::int32_t y) {
  return {
      .worldspace = worldspace,
      .x = x,
      .y = y,
      .editor_id = std::nullopt,
  };
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
  // Bit 1 is set on every shipped exterior cell but one; all-clear flags are
  // not a shape the game ships.
  EXPECT_EQ(created->subrecords[1].data[0],
            std::byte{fo4::forms::k_exterior_cell_default_flags & 0xFF});
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
      plugin, cell_creation(world, -33, 32)));
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, cell_creation(world, -34, 33)));

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
      plugin, cell_creation(world, -40, 40)));
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
      plugin, cell_creation(world, 1, 2)));
  ASSERT_EQ(plugin.header.next_form_id, 0x901u);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin, cell_creation(world, 1, 2))),
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
          cell_creation(fo4::FormKey::parse("000801:Cells.esp"), 0, 0))),
      fo4::forms::CellError);
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin,
          cell_creation(fo4::FormKey::parse("000800:Cells.esp"), 111, 0))),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, next_before);
}

TEST(ExteriorCellCreation, MalformedExistingGridFailsClosedWithoutAllocation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, cell_creation(world, 1, 2)));

  auto* created = const_cast<fo4::Record*>(
      fo4::forms::find_exterior_cell(plugin, world, 1, 2));
  ASSERT_NE(created, nullptr);
  auto* xclc = const_cast<fo4::Subrecord*>(unique_subrecord(*created, "XCLC"));
  ASSERT_NE(xclc, nullptr);
  xclc->data.resize(2);
  const auto next_before = plugin.header.next_form_id;

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin, cell_creation(world, 2, 3))),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, next_before);
}

TEST(ExteriorCellCreation, UndecodableExistingCellFailsClosedWithDistinctMessage) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, cell_creation(world, 1, 2)));

  auto* created = const_cast<fo4::Record*>(
      fo4::forms::find_exterior_cell(plugin, world, 1, 2));
  ASSERT_NE(created, nullptr);
  created->undecodable = true;
  created->subrecords.clear();

  try {
    static_cast<void>(fo4::forms::find_exterior_cell(plugin, world, 1, 2));
    FAIL() << "undecodable exterior CELL was accepted";
  } catch (const fo4::forms::CellError& error) {
    EXPECT_NE(std::string(error.what()).find("undecodable CELL"), std::string::npos)
        << error.what();
  }
}

// Bit 1 is set on 61,542 of the 61,543 exterior cells across Fallout4.esm,
// DLCCoast and DLCNukaWorld, so an all-clear DATA is not the shape the game
// ships. New cells default to it and the caller can override.
TEST(ExteriorCellCreation, EmitsTheShippedExteriorDataFlags) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  fo4::forms::ExteriorCellCreation creation;
  creation.worldspace = fo4::FormKey::parse("000800:Cells.esp");
  creation.x = 3;
  creation.y = 4;
  const auto key = fo4::forms::create_exterior_cell(plugin, creation);

  const auto* created = fo4::find_record(plugin, key);
  ASSERT_NE(created, nullptr);
  const fo4::Subrecord* data = nullptr;
  for (const auto& subrecord : created->subrecords) {
    if (subrecord.signature == fo4::sig_data) data = &subrecord;
  }
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(data->data[0]),
            fo4::forms::k_exterior_cell_default_flags & 0xFF);
  EXPECT_EQ(std::to_integer<std::uint8_t>(data->data[1]),
            (fo4::forms::k_exterior_cell_default_flags >> 8) & 0xFF);
}

TEST(ExteriorCellCreation, HonoursOverriddenFlagsButRefusesTheInteriorBit) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  fo4::forms::ExteriorCellCreation creation;
  creation.worldspace = fo4::FormKey::parse("000800:Cells.esp");
  creation.x = 5;
  creation.y = 6;
  creation.flags = 0x000A;  // has water + no LOD water, a shipped combination
  const auto key = fo4::forms::create_exterior_cell(plugin, creation);
  const auto* created = fo4::find_record(plugin, key);
  ASSERT_NE(created, nullptr);
  for (const auto& subrecord : created->subrecords) {
    if (subrecord.signature != fo4::sig_data) continue;
    EXPECT_EQ(std::to_integer<std::uint8_t>(subrecord.data[0]), 0x0Au);
  }

  const auto next_before = plugin.header.next_form_id;
  creation.x = 7;
  creation.flags = 0x0001;  // interior bit contradicts the XCLC being emitted
  EXPECT_THROW(static_cast<void>(fo4::forms::create_exterior_cell(plugin, creation)),
               fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, next_before);
}

TEST(ExteriorCellCreation, StoresEditorIdAsWindows1252) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  auto creation = cell_creation(fo4::FormKey::parse("000800:Cells.esp"), 3, 4);
  creation.editor_id = std::string_view{"Caf\xC3\xA9"};
  const auto key = fo4::forms::create_exterior_cell(plugin, creation);

  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto* edid = unique_subrecord(*record, "EDID");
  ASSERT_NE(edid, nullptr);
  const std::vector<std::byte> expected{
      std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(edid->data, expected);
}

TEST(ExteriorCellCreation, EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  auto creation = cell_creation(fo4::FormKey::parse("000800:Cells.esp"), 3, 4);
  creation.editor_id = std::string_view{"Snow\xE2\x98\x83"};

  EXPECT_THROW(static_cast<void>(fo4::forms::create_exterior_cell(plugin, creation)),
               fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(plugin.header.next_form_id, 0x900u);
}

TEST(ExteriorCellCreation, CreatesAnExteriorCellInAWorldspaceMadeByCreateRecord) {
  auto plugin = fo4::read_plugin(fo4::test::make_tes4_plugin(0, 1.0f, {}), "Patch.esp");
  ASSERT_EQ(plugin.header.next_form_id, 0x800u);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_exterior_cell(
          plugin, cell_creation(fo4::FormKey::parse("000800:Patch.esp"), 5, -7))),
      fo4::forms::CellError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);

  const auto created = fo4::create_record(
      plugin, fo4::Signature::from_string("WRLD"), std::string_view{"FreshWorld"});
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(created.key, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);

  const auto cell =
      fo4::forms::create_exterior_cell(plugin, cell_creation(created.key, 5, -7));
  EXPECT_EQ(cell, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);

  const auto* top = top_wrld(plugin);
  ASSERT_NE(top, nullptr);
  ASSERT_NE(child_group(top->children, fo4::GroupType::world_children, 0x00000800u),
            nullptr);
  const auto* placed = fo4::forms::find_exterior_cell(plugin, created.key, 5, -7);
  ASSERT_NE(placed, nullptr);
  EXPECT_EQ(placed->form_key, cell);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_NE(fo4::find_record(reread, created.key), nullptr);
  const auto* round_trip = fo4::forms::find_exterior_cell(reread, created.key, 5, -7);
  ASSERT_NE(round_trip, nullptr);
  EXPECT_EQ(round_trip->form_key, cell);
}
