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

// A far door that lands the player in a moved cell must follow the cell, or it teleports to empty space at the old coordinate.
TEST(ExteriorCellTeleports, TranslatesFarDoorTeleportsIntoTheMovedCellAndCopiesThemIntoThePatch) {
  auto fixture = door_fixture(
      {far_door(k_far_door_form_id, xtel_data(k_door_form_id, k_xtel_x, k_xtel_y)),
       far_door(k_far_persistent_door_form_id,
                xtel_data(k_persistent_door_form_id, k_xtel_x, k_xtel_y)),
       far_door(k_far_unrelated_door_form_id,
                xtel_data(k_missing_door_form_id, k_xtel_x, k_xtel_y))});
  const auto master_before = fo4::write_plugin(fixture.master);
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto result = fo4::forms::translate_incoming_teleports(
      fixture.patch, fixture.env, door_key(k_placed_cell_form_id), -34, 30);

  ASSERT_EQ(result.status, fo4::forms::TeleportTranslateStatus::translated);
  EXPECT_EQ(result.updated, 2u);
  const auto moved_x = k_xtel_x + -34.0f * 4096.0f;
  const auto moved_y = k_xtel_y + 30.0f * 4096.0f;
  EXPECT_EQ(xtel_of(fixture.patch, k_far_door_form_id),
            xtel_data(k_door_form_id, moved_x, moved_y));
  EXPECT_EQ(xtel_of(fixture.patch, k_far_persistent_door_form_id),
            xtel_data(k_persistent_door_form_id, moved_x, moved_y));
  EXPECT_EQ(fo4::find_record(fixture.patch, door_key(k_far_unrelated_door_form_id)), nullptr);
  EXPECT_NE(fo4::find_record(fixture.patch, door_key(k_far_cell_form_id)), nullptr);
  EXPECT_EQ(fo4::write_plugin(fixture.env.plugin(0)), master_before);

  const auto reread = fo4::read_plugin(fo4::write_plugin(fixture.patch), "Patch.esp");
  EXPECT_EQ(xtel_of(reread, k_far_door_form_id), xtel_data(k_door_form_id, moved_x, moved_y));
}

// An override already held by the patch must be edited in place; copying the winner over it would drop the patch edit.
TEST(ExteriorCellTeleports, EditsAFarDoorThePatchAlreadyOverridesInPlace) {
  auto patch_door = rec("REFR", k_far_door_form_id,
                        {{"NAME", bytes_of({k_stat_form_id})},
                         {"XTEL", xtel_data(k_door_form_id, 7.5f, 8.5f)},
                         {"XSCL", floats({2.0f})},
                         {"DATA", floats({1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f})}},
                        k_persistent_flag);
  auto fixture = door_fixture(
      {far_door(k_far_door_form_id, xtel_data(k_door_form_id, k_xtel_x, k_xtel_y))},
      {std::move(patch_door)});
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto result = fo4::forms::translate_incoming_teleports(
      fixture.patch, fixture.env, door_key(k_placed_cell_form_id), -34, 30);

  ASSERT_EQ(result.status, fo4::forms::TeleportTranslateStatus::translated);
  EXPECT_EQ(result.updated, 1u);
  EXPECT_EQ(count_records(fixture.patch, k_far_door_form_id), 1u);
  EXPECT_EQ(xtel_of(fixture.patch, k_far_door_form_id),
            xtel_data(k_door_form_id, 7.5f + -34.0f * 4096.0f, 8.5f + 30.0f * 4096.0f));
  const auto* record = fo4::find_record(fixture.patch, door_key(k_far_door_form_id));
  ASSERT_NE(record, nullptr);
  const auto* scale = unique_subrecord(*record, "XSCL");
  ASSERT_NE(scale, nullptr);
  EXPECT_EQ(float_at(scale->data, 0), 2.0f);
}

// A teleport to a door that the move did not carry (here a master-only door of the cell) must keep its coordinates and leave the patch byte-identical.
TEST(ExteriorCellTeleports, LeavesTeleportsToDoorsThatDidNotMoveAndThePatchBytesUntouched) {
  auto fixture = door_fixture(
      {far_door(k_far_door_form_id, xtel_data(k_unmoved_door_form_id, k_xtel_x, k_xtel_y)),
       far_door(k_far_unrelated_door_form_id,
                xtel_data(k_missing_door_form_id, k_xtel_x, k_xtel_y))});
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);
  const auto before = fo4::write_plugin(fixture.patch);

  const auto result = fo4::forms::translate_incoming_teleports(
      fixture.patch, fixture.env, door_key(k_placed_cell_form_id), -34, 30);

  EXPECT_EQ(result.status, fo4::forms::TeleportTranslateStatus::translated);
  EXPECT_EQ(result.updated, 0u);
  EXPECT_EQ(fo4::write_plugin(fixture.patch), before);
  EXPECT_EQ(fo4::find_record(fixture.patch, door_key(k_far_cell_form_id)), nullptr);
}

// An XTEL that cannot be translated must refuse and roll back the override copies made before it was reached.
TEST(ExteriorCellTeleports, RefusesAMalformedXtelAndRollsBackTheCopies) {
  auto short_xtel = xtel_data(k_door_form_id, k_xtel_x, k_xtel_y);
  short_xtel.resize(20);
  const auto infinite_xtel =
      xtel_data(k_door_form_id, std::numeric_limits<float>::infinity(), 0.0f);
  for (const auto& bad : {short_xtel, infinite_xtel}) {
    auto fixture = door_fixture({far_door(k_far_door_form_id, bad)});
    ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), -33, 32).status,
              fo4::forms::CellRelocateStatus::relocated);
    const auto before = fo4::write_plugin(fixture.patch);

    const auto result = fo4::forms::translate_incoming_teleports(
        fixture.patch, fixture.env, door_key(k_placed_cell_form_id), -34, 30);

    EXPECT_EQ(result.status, fo4::forms::TeleportTranslateStatus::malformed);
    EXPECT_EQ(fo4::write_plugin(fixture.patch), before);
  }
}

// A move there and back must restore the teleport bytes, or an undone move would leave doors off by a whole grid distance.
TEST(ExteriorCellTeleports, MovingTheCellBackRestoresTheTeleportBytes) {
  auto fixture = door_fixture(
      {far_door(k_far_door_form_id, xtel_data(k_door_form_id, k_xtel_x, k_xtel_y))});
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), -33, 32).status,
            fo4::forms::CellRelocateStatus::relocated);
  ASSERT_EQ(fo4::forms::translate_incoming_teleports(fixture.patch, fixture.env, door_key(k_placed_cell_form_id),
                                                     -34, 30).updated,
            1u);
  ASSERT_EQ(fo4::forms::relocate_exterior_cell(fixture.patch, door_key(k_placed_cell_form_id), 1, 2).status,
            fo4::forms::CellRelocateStatus::relocated);

  const auto result = fo4::forms::translate_incoming_teleports(
      fixture.patch, fixture.env, door_key(k_placed_cell_form_id), 34, -30);

  EXPECT_EQ(result.updated, 1u);
  EXPECT_EQ(xtel_of(fixture.patch, k_far_door_form_id),
            xtel_data(k_door_form_id, k_xtel_x, k_xtel_y));
}

TEST(ExteriorCellRelocation, UndoAndRedoOfAFarMoveAreByteExactWhileMovingBackRoundsWithinHalfAnUlp) {
  auto plugin = fo4::read_plugin(
      plugin_with_cell_contents(
          1, 2, {}, {placed("REFR", 0x822, {10.1f, 20.3f, 30.0f, 0.0f, 0.0f, 0.0f})}),
      "Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  fo4::PluginEditHistory history;
  history.record(plugin);

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 110, -110).status,
            fo4::forms::CellRelocateStatus::relocated);
  const auto moved = fo4::write_plugin(plugin);
  ASSERT_NE(moved, before);
  const auto far_x = float_at(data_of(plugin, 0x822), 0);
  EXPECT_NE(static_cast<double>(far_x), static_cast<double>(10.1f) + 109.0 * 4096.0);

  auto undone = history.undo(plugin);
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(fo4::write_plugin(*undone), before);
  auto redone = history.redo(*undone);
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(fo4::write_plugin(*redone), moved);

  ASSERT_EQ(fo4::forms::relocate_exterior_cell(plugin, placed_cell_key(), 1, 2).status,
            fo4::forms::CellRelocateStatus::relocated);
  const auto back_x = float_at(data_of(plugin, 0x822), 0);
  const auto ulp = std::nextafter(far_x, std::numeric_limits<float>::infinity()) - far_x;
  EXPECT_NE(back_x, 10.1f);
  EXPECT_LE(std::fabs(back_x - 10.1f), ulp / 2.0f);
}

}  // namespace
