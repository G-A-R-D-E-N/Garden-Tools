#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/edit_history.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
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

fo4::Plugin marked_plugin(std::uint8_t marker, std::size_t payload_size) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "History.esp");
  plugin.tes4.subrecords.push_back(fo4::Subrecord{
      .signature = fo4::Signature::from_string("TST1"),
      .data = std::vector<std::byte>(payload_size, std::byte{marker}),
  });
  return plugin;
}

std::uint8_t marker_of(const fo4::Plugin& plugin) {
  return std::to_integer<std::uint8_t>(plugin.tes4.subrecords.back().data.front());
}

}  // namespace

TEST(EditHistory, UndoRestoresTheSnapshotAndRedoReapplies) {
  fo4::PluginEditHistory history;
  history.record(marked_plugin(1, 1));
  const auto restored = history.undo(marked_plugin(2, 1));
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(marker_of(*restored), 1);
  EXPECT_EQ(history.undo_count(), 0u);
  EXPECT_EQ(history.redo_count(), 1u);
  const auto reapplied = history.redo(*restored);
  ASSERT_TRUE(reapplied.has_value());
  EXPECT_EQ(marker_of(*reapplied), 2);
  EXPECT_EQ(history.undo_count(), 1u);
  EXPECT_EQ(history.redo_count(), 0u);
}

TEST(EditHistory, RecordClearsRedoSoAStaleRedoCannotReplaceANewEdit) {
  fo4::PluginEditHistory history;
  history.record(marked_plugin(1, 1));
  const auto undone = history.undo(marked_plugin(2, 1));
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(history.redo_count(), 1u);
  history.record(marked_plugin(3, 1));
  EXPECT_EQ(history.redo_count(), 0u);
  EXPECT_FALSE(history.redo(marked_plugin(4, 1)).has_value());
}

TEST(EditHistory, CapDropsTheOldestEntry) {
  fo4::PluginEditHistory history;
  for (std::uint8_t marker = 1; marker <= 51; ++marker) {
    history.record(marked_plugin(marker, 1));
  }
  EXPECT_EQ(history.undo_count(), 50u);
  fo4::Plugin current = marked_plugin(52, 1);
  for (std::size_t step = 0; step < 50; ++step) {
    auto previous = history.undo(current);
    ASSERT_TRUE(previous.has_value());
    current = std::move(*previous);
  }
  EXPECT_EQ(marker_of(current), 2);
  EXPECT_FALSE(history.undo(current).has_value());
}

TEST(EditHistory, ByteBudgetDropsOldestFirst) {
  fo4::PluginEditHistory history{2500};
  history.record(marked_plugin(1, 1000));
  history.record(marked_plugin(2, 1000));
  EXPECT_EQ(history.undo_count(), 2u);
  history.record(marked_plugin(3, 1000));
  EXPECT_EQ(history.undo_count(), 2u);
  const auto newest = history.undo(marked_plugin(4, 1000));
  ASSERT_TRUE(newest.has_value());
  EXPECT_EQ(marker_of(*newest), 3);
  const auto older = history.undo(*newest);
  ASSERT_TRUE(older.has_value());
  EXPECT_EQ(marker_of(*older), 2);
  EXPECT_FALSE(history.undo(*older).has_value());
}

TEST(EditHistory, ByteBudgetKeepsTheNewestEntryWhenOneSnapshotExceedsTheBudget) {
  fo4::PluginEditHistory history{10};
  history.record(marked_plugin(1, 1000));
  history.record(marked_plugin(2, 1000));
  EXPECT_EQ(history.undo_count(), 1u);
  const auto undone = history.undo(marked_plugin(3, 1000));
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(marker_of(*undone), 2);
}

TEST(EditHistory, RedoEnforcesTheByteBudgetForAGrownCurrent) {
  fo4::PluginEditHistory history{2500};
  history.record(marked_plugin(1, 1000));
  history.record(marked_plugin(2, 1000));
  const auto undone = history.undo(marked_plugin(3, 1000));
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(marker_of(*undone), 2);
  const auto redone = history.redo(marked_plugin(4, 2000));
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(marker_of(*redone), 3);
  EXPECT_EQ(history.undo_count(), 1u);
  const auto older = history.undo(*redone);
  ASSERT_TRUE(older.has_value());
  EXPECT_EQ(marker_of(*older), 4);
  EXPECT_FALSE(history.undo(*older).has_value());
}

TEST(EditHistory, UndoOnEmptyReturnsNullopt) {
  fo4::PluginEditHistory history;
  const auto revision = history.revision();
  EXPECT_FALSE(history.undo(marked_plugin(1, 1)).has_value());
  EXPECT_FALSE(history.redo(marked_plugin(1, 1)).has_value());
  EXPECT_EQ(history.revision(), revision);
  EXPECT_EQ(history.undo_count(), 0u);
  EXPECT_EQ(history.redo_count(), 0u);
}

TEST(EditHistory, EditThenUndoRoundTripsAPlacedReferenceMove) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto world = fo4::FormKey::parse("000800:Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 0,
          .y = 0,
          .editor_id = std::nullopt,
      });
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });

  fo4::PluginEditHistory history;
  history.record(plugin);
  ASSERT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 4000.0f, .y = 4095.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  const auto undone = history.undo(plugin);
  ASSERT_TRUE(undone.has_value());
  plugin = *undone;
  const auto restored_refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(restored_refs.size(), 1u);
  EXPECT_FLOAT_EQ(restored_refs[0].transform.x, 10.0f);
  EXPECT_FLOAT_EQ(restored_refs[0].transform.y, 20.0f);

  const auto redone = history.redo(plugin);
  ASSERT_TRUE(redone.has_value());
  plugin = *redone;
  const auto moved_refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(moved_refs.size(), 1u);
  EXPECT_FLOAT_EQ(moved_refs[0].transform.x, 4000.0f);
  EXPECT_FLOAT_EQ(moved_refs[0].transform.y, 4095.0f);
}
