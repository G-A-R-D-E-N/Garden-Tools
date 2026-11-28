#include "fo4/environment/environment.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_relocate_doors.hpp"
#include "fo4/plugin/edit_history.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "exterior_cell_creation_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace fo4::test::exterior_cell;

// A relocation must not leave the record in the old groups; the game reads that as a cell in the wrong place.
TEST(ExteriorCellRelocation, MovesTheCellHierarchyToTheRequestedBlockAndSubBlock) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");

  const auto result =
      fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32);
  EXPECT_EQ(result.status, fo4::forms::CellRelocateStatus::relocated);

  const auto* moved = fo4::forms::find_exterior_cell(plugin, placed_world_key(), -33, 32);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(moved->form_key, placed_cell_key());
  EXPECT_EQ(fo4::forms::find_exterior_cell(plugin, placed_world_key(), 1, 2), nullptr);

  const auto* xclc = unique_subrecord(*moved, "XCLC");
  ASSERT_NE(xclc, nullptr);
  ASSERT_EQ(xclc->data.size(), 12u);
  EXPECT_EQ(read_i32(xclc->data, 0), -33);
  EXPECT_EQ(read_i32(xclc->data, 4), 32);

  const auto* world_children = world_children_group(plugin);
  ASSERT_NE(world_children, nullptr);
  EXPECT_EQ(count_groups(*world_children, fo4::GroupType::exterior_cell_block), 1u);
  const auto* block = child_group(world_children->children,
                                  fo4::GroupType::exterior_cell_block, 0xFFFE0001u);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(count_groups(*block, fo4::GroupType::exterior_cell_subblock), 1u);
  EXPECT_NE(child_group(block->children, fo4::GroupType::exterior_cell_subblock, 0xFFFB0004u),
            nullptr);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* round_trip =
      fo4::forms::find_exterior_cell(reread, placed_world_key(), -33, 32);
  ASSERT_NE(round_trip, nullptr);
  EXPECT_EQ(round_trip->form_key, placed_cell_key());
}

// The move must carry the CELL children group, or every reference, LAND and NAVM under it is orphaned.
TEST(ExteriorCellRelocation, CarriesTheCellChildrenGroupBehindTheCell) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, true), "Cells.esp");

  const auto result =
      fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32);
  ASSERT_EQ(result.status, fo4::forms::CellRelocateStatus::relocated);

  const auto* world_children = world_children_group(plugin);
  ASSERT_NE(world_children, nullptr);
  const auto* block = child_group(world_children->children,
                                  fo4::GroupType::exterior_cell_block, 0xFFFE0001u);
  ASSERT_NE(block, nullptr);
  const auto* subblock = child_group(block->children,
                                     fo4::GroupType::exterior_cell_subblock, 0xFFFB0004u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 2u);

  const auto* record = std::get_if<fo4::Record>(&subblock->children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->form_key, placed_cell_key());
  const auto* children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_children));
  EXPECT_EQ(children->header.label_as_u32(), k_placed_cell_form_id);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000820:Cells.esp")), nullptr);
}

// A same-block move must reuse the block group and must drop the emptied source sub-block.
TEST(ExteriorCellRelocation, ReusesTheBlockGroupAndDropsTheEmptiedSubBlock) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");

  const auto result =
      fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 9, 3);
  ASSERT_EQ(result.status, fo4::forms::CellRelocateStatus::relocated);

  const auto* world_children = world_children_group(plugin);
  ASSERT_NE(world_children, nullptr);
  EXPECT_EQ(count_groups(*world_children, fo4::GroupType::exterior_cell_block), 1u);
  const auto* block = child_group(world_children->children,
                                  fo4::GroupType::exterior_cell_block, 0x00000000u);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(count_groups(*block, fo4::GroupType::exterior_cell_subblock), 1u);
  EXPECT_NE(child_group(block->children, fo4::GroupType::exterior_cell_subblock, 0x00010000u),
            nullptr);
  EXPECT_EQ(child_group(block->children, fo4::GroupType::exterior_cell_subblock, 0x00000000u),
            nullptr);
}

// Two cells must never share one coordinate, and the refusal must leave the bytes alone.
TEST(ExteriorCellRelocation, RefusesAnOccupiedDestinationWithoutTouchingBytes) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");
  static_cast<void>(fo4::forms::create_exterior_cell(
      plugin, cell_creation(placed_world_key(), 3, 4)));
  const auto before = fo4::write_plugin(plugin);

  const auto result =
      fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 3, 4);
  EXPECT_EQ(result.status, fo4::forms::CellRelocateStatus::destination_occupied);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// An interior cell and the persistent exterior cell must both be refused; neither has a grid position to re-file.
TEST(ExteriorCellRelocation, RefusesInteriorAndPersistentCells) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");
  const auto interior = fo4::forms::create_interior_cell(plugin, "Basement");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, interior, 5, 5).status,
            fo4::forms::CellRelocateStatus::not_exterior);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto persistent = fo4::read_plugin(plugin_with_persistent_exterior_cell(), "Cells.esp");
  const auto persistent_before = fo4::write_plugin(persistent);
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(persistent, placed_cell_key(), 5, 5).status,
            fo4::forms::CellRelocateStatus::persistent_cell);
  EXPECT_EQ(fo4::write_plugin(persistent), persistent_before);
}

// A refusal must name the right reason; an absent key and a non-CELL key are different failures.
TEST(ExteriorCellRelocation, DistinguishesMissingKeysFromNonCellKeys) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(
                plugin, fo4::FormKey::parse("0008FF:Cells.esp"), 5, 5)
                .status,
            fo4::forms::CellRelocateStatus::cell_missing);
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(
                plugin, fo4::FormKey::parse("002100:Cells.esp"), 5, 5)
                .status,
            fo4::forms::CellRelocateStatus::not_a_cell);
}

// A misfiled cell must not be spread to a second coordinate.
TEST(ExteriorCellRelocation, RefusesAnXclcThatDisagreesWithItsGroupLabels) {
  auto plugin = fo4::read_plugin(plugin_with_misfiled_exterior_cell(), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::malformed);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// A truncated XCLC must be refused rather than rewritten into a broken override.
TEST(ExteriorCellRelocation, RefusesATruncatedXclc) {
  auto plugin = fo4::read_plugin(plugin_with_truncated_xclc(), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::malformed);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// A duplicated source block label must come back as a refusal, not escape as an exception.
TEST(ExteriorCellRelocation, RefusesADuplicatedSourceBlockLabelWithoutThrowing) {
  auto plugin = fo4::read_plugin(plugin_with_duplicate_source_block(), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  fo4::forms::CellRelocateResult result;
  EXPECT_NO_THROW(result = fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32));
  EXPECT_EQ(result.status, fo4::forms::CellRelocateStatus::malformed);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// A coordinate outside the CK range must be refused before it reaches a group label the header cannot represent.
TEST(ExteriorCellRelocation, RefusesCoordinatesOutsideTheCreationKitRange) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 200, 5).status,
            fo4::forms::CellRelocateStatus::invalid_coordinate);
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 5, -111).status,
            fo4::forms::CellRelocateStatus::invalid_coordinate);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// A no-op move must leave the plugin bytes alone; a dirty plugin creates a spurious override.
TEST(ExteriorCellRelocation, RelocatingToTheSameCoordinateChangesNothing) {
  auto plugin =
      fo4::read_plugin(plugin_with_placed_exterior_cell(1, 2, false), "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 1, 2).status,
            fo4::forms::CellRelocateStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// References must move with the cell; left at their old absolute positions they sit outside the 4096x4096 square of the new grid.
TEST(ExteriorCellRelocation, TranslatesPlacedReferencesByTheGridDeltaTimes4096) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {placed("REFR", 0x821, {1.0f, 2.0f, 3.0f, 0.5f, 0.25f, 0.125f})},
          {placed("REFR", 0x822, {10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f}),
           placed("ACHR", 0x823, {4100.5f, 8200.25f, -5.0f, 1.0f, 2.0f, 3.0f}),
           placed("PGRE", 0x824, {4097.0f, 8193.0f, 9.0f, 0.0f, 0.0f, 0.0f}),
           placed("PHZD", 0x825, {5000.0f, 9000.0f, 1.0f, 0.0f, 0.0f, 0.0f})}),
      "Cells.esp");

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);

  constexpr float dx = -34.0f * 4096.0f;
  constexpr float dy = 30.0f * 4096.0f;
  const auto first = data_of(plugin, 0x822);
  ASSERT_EQ(first.size(), 24u);
  EXPECT_EQ(float_at(first, 0), 10.0f + dx);
  EXPECT_EQ(float_at(first, 4), 20.0f + dy);
  EXPECT_EQ(float_at(first, 8), 30.0f);
  EXPECT_EQ(float_at(first, 12), 0.0f);
  const auto actor = data_of(plugin, 0x823);
  EXPECT_EQ(float_at(actor, 0), 4100.5f + dx);
  EXPECT_EQ(float_at(actor, 4), 8200.25f + dy);
  EXPECT_EQ(float_at(actor, 8), -5.0f);
  EXPECT_EQ(float_at(actor, 12), 1.0f);
  EXPECT_EQ(float_at(actor, 16), 2.0f);
  EXPECT_EQ(float_at(actor, 20), 3.0f);
  EXPECT_EQ(float_at(data_of(plugin, 0x824), 0), 4097.0f + dx);
  EXPECT_EQ(float_at(data_of(plugin, 0x825), 4), 9000.0f + dy);
  const auto persistent = data_of(plugin, 0x821);
  EXPECT_EQ(float_at(persistent, 0), 1.0f + dx);
  EXPECT_EQ(float_at(persistent, 4), 2.0f + dy);
  EXPECT_EQ(float_at(persistent, 8), 3.0f);
  EXPECT_EQ(float_at(persistent, 12), 0.5f);
  EXPECT_EQ(float_at(persistent, 16), 0.25f);
  EXPECT_EQ(float_at(persistent, 20), 0.125f);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  EXPECT_EQ(data_of(reread, 0x822), first);
}

// A reference far west of the origin must be translated with exact float arithmetic, not truncated or sign-flipped.
TEST(ExteriorCellRelocation, TranslatesALargeNegativeCoordinateExactly) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          -20, -5, {},
          {placed("REFR", 0x822, {-80000.25f, -18000.5f, 12.0f, 0.0f, 0.0f, 0.0f})}),
      "Cells.esp");

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 3, -5).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto data = data_of(plugin, 0x822);
  EXPECT_EQ(float_at(data, 0), -80000.25f + 23.0f * 4096.0f);
  EXPECT_EQ(float_at(data, 4), -18000.5f);
  EXPECT_EQ(float_at(data, 8), 12.0f);
}

// An axis that does not change must keep its exact bytes; adding 0.0 would rewrite -0.0 as +0.0.
TEST(ExteriorCellRelocation, LeavesAnUnchangedAxisBitIdentical) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {}, {placed("REFR", 0x822, {10.0f, -0.0f, -0.0f, -0.0f, 0.0f, 0.0f})}),
      "Cells.esp");
  const auto before = data_of(plugin, 0x822);

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 5, 2).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto after = data_of(plugin, 0x822);
  ASSERT_EQ(after.size(), 24u);
  EXPECT_EQ(float_at(after, 0), 10.0f + 4.0f * 4096.0f);
  EXPECT_TRUE(std::equal(before.begin() + 4, before.end(), after.begin() + 4));
}

// Moving a cell away and back must restore the exact original bytes, or a relocation cannot be undone.
TEST(ExteriorCellRelocation, MovingBackRestoresTheOriginalBytes) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {placed("REFR", 0x821, {1.0f, 2.0f, 3.0f, 0.5f, 0.25f, 0.125f})},
          {placed("REFR", 0x822, {-12345.25f, 8200.5f, 30.0f, 0.0f, 0.0f, 0.0f}),
           rec("NAVM", k_navmesh_form_id, {{"NVNM", nvnm_for_grid(1, 2)}})}),
      "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);
  EXPECT_NE(fo4::write_plugin(plugin), before);
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 1, 2).status,
            fo4::forms::CellRelocateStatus::relocated);

  EXPECT_EQ(after_tes4(fo4::write_plugin(plugin)), after_tes4(before));
}

TEST(ExteriorCellRelocation, TouchesOnlyTheDataOfReferencesAndNoOtherRecord) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {},
          {rec("REFR", 0x822,
               {{"EDID", zstring("Marker")},
                {"NAME", bytes_of({k_stat_form_id})},
                {"XSCL", floats({1.5f})},
                {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}}),
           rec("LAND", 0x824, {{"DATA", bytes_of({0x1Fu})}})}),
      "Cells.esp");
  const auto* ref_before = fo4::find_record(plugin, cells_key(0x822));
  const auto* land_before = fo4::find_record(plugin, cells_key(0x824));
  const auto* stat_before = fo4::find_record(plugin, cells_key(k_stat_form_id));
  ASSERT_NE(ref_before, nullptr);
  ASSERT_NE(land_before, nullptr);
  ASSERT_NE(stat_before, nullptr);
  const auto ref_subrecords = ref_before->subrecords;
  const auto land_subrecords = land_before->subrecords;
  const auto stat_subrecords = stat_before->subrecords;

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto* ref = fo4::find_record(plugin, cells_key(0x822));
  const auto* land = fo4::find_record(plugin, cells_key(0x824));
  const auto* stat = fo4::find_record(plugin, cells_key(k_stat_form_id));
  ASSERT_NE(ref, nullptr);
  ASSERT_NE(land, nullptr);
  ASSERT_NE(stat, nullptr);
  ASSERT_EQ(ref->subrecords.size(), ref_subrecords.size());
  for (std::size_t i = 0; i < ref_subrecords.size(); ++i) {
    EXPECT_EQ(ref->subrecords[i].signature, ref_subrecords[i].signature);
    if (ref_subrecords[i].signature != fo4::Signature::from_string("DATA")) {
      EXPECT_EQ(ref->subrecords[i].data, ref_subrecords[i].data);
    }
  }
  ASSERT_EQ(land->subrecords.size(), land_subrecords.size());
  EXPECT_EQ(land->subrecords[0].data, land_subrecords[0].data);
  ASSERT_EQ(stat->subrecords.size(), stat_subrecords.size());
  for (std::size_t i = 0; i < stat_subrecords.size(); ++i) {
    EXPECT_EQ(stat->subrecords[i].data, stat_subrecords[i].data);
  }
}

// NVNM vertices, waypoints and grid bounds are world-absolute and its parent grid names the cell, so all four must follow the cell.
TEST(ExteriorCellRelocation, TranslatesTheNavmeshAndRewritesItsParentGrid) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {}, {rec("NAVM", k_navmesh_form_id, {{"NVNM", nvnm_for_grid(1, 2)}})}),
      "Cells.esp");
  ASSERT_EQ(nvnm_of(plugin), nvnm_for_grid(1, 2));

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);

  EXPECT_EQ(nvnm_of(plugin), nvnm_for_grid(-33, 32));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  EXPECT_EQ(nvnm_of(reread), nvnm_for_grid(-33, 32));
}

// A navmesh that cannot be translated must refuse the whole move; leaving it behind would strand it outside its cell.
TEST(ExteriorCellRelocation, RefusesAnUntranslatableNavmeshAndRollsBackEverything) {
  const auto good_ref = placed("REFR", 0x822, {10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f});

  auto stale_parent = nvnm_for_grid(1, 2);
  stale_parent[12] = std::byte{9};
  auto truncated = nvnm_for_grid(1, 2);
  truncated.resize(truncated.size() - 1);
  auto trailing = nvnm_for_grid(1, 2);
  trailing.push_back(std::byte{0});
  auto old_version = nvnm_for_grid(1, 2);
  old_version[0] = std::byte{14};

  for (const auto& nvnm : {stale_parent, truncated, trailing, old_version}) {
    auto plugin = fo4::read_plugin(
        plugin_with_cell_contents(
            1, 2, {}, {good_ref, rec("NAVM", k_navmesh_form_id, {{"NVNM", nvnm}})}),
        "Cells.esp");
    const auto before = fo4::write_plugin(plugin);

    EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
              fo4::forms::CellRelocateStatus::malformed);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

// A reference whose DATA cannot be translated must refuse the move and must not leave earlier references shifted.
TEST(ExteriorCellRelocation, RefusesAnUntranslatableReferenceAndRollsBackEverything) {
  const auto good_ref = placed("REFR", 0x822, {10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f});
  const auto short_data = rec("REFR", 0x823, {{"DATA", floats({1.0f, 2.0f, 3.0f})}});
  const auto no_data = rec("ACHR", 0x823, {{"NAME", bytes_of({k_stat_form_id})}});
  const auto nan_data = placed(
      "REFR", 0x823, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

  for (const auto& bad : {short_data, no_data, nan_data}) {
    auto plugin = fo4::read_plugin(
        plugin_with_cell_contents(1, 2, {}, {good_ref, bad}), "Cells.esp");
    const auto before = fo4::write_plugin(plugin);

    EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
              fo4::forms::CellRelocateStatus::malformed);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

// A deleted placed reference legitimately carries no DATA and must not block the move.
TEST(ExteriorCellRelocation, SkipsDeletedChildrenWithoutData) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {},
          {placed("REFR", 0x822, {10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f}),
           rec("REFR", 0x823, {}, k_deleted_flag), rec("NAVM", 0x824, {}, k_deleted_flag)}),
      "Cells.esp");

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);
  EXPECT_EQ(float_at(data_of(plugin, 0x822), 0), 10.0f - 34.0f * 4096.0f);
}

// A same-grid request must not translate anything, even with a populated children group.
TEST(ExteriorCellRelocation, SameCoordinateLeavesPopulatedChildrenUntranslated) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {},
          {placed("REFR", 0x822, {10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f}),
           rec("NAVM", k_navmesh_form_id, {{"NVNM", nvnm_for_grid(1, 2)}})}),
      "Cells.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 1, 2).status,
            fo4::forms::CellRelocateStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

}  // namespace
