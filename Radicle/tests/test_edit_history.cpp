#include "fo4/c_api.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_cell_edit.h"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/edit_history.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
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

namespace {

using namespace fo4::test::envnode;

using PluginHandle = std::unique_ptr<radicle_plugin, void (*)(radicle_plugin*)>;
using HistoryHandle = std::unique_ptr<radicle_edit_history, void (*)(radicle_edit_history*)>;

std::vector<std::byte> history_base() {
  const auto merged = rec("CELL", 0x804, {{"EDID", zstring("Merged")}, {"DATA", {std::byte{0x01}, std::byte{0}}}});
  const auto refs = grp(0x804, 6,
                        {grp(0x804, 9,
                             {rec("REFR", 0x2001,
                                  {{"NAME", bytes_of({0x2100})},
                                   {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}})})});
  const auto stat = rec("STAT", 0x2100, {{"EDID", zstring("TableStat")}});
  return make_plugin({}, {top("CELL", {grp(0, 2, {grp(0, 3, {merged, refs})})}), top("STAT", {stat})});
}

std::vector<std::byte> history_patch() {
  const auto merged = rec("CELL", 0x804, {{"EDID", zstring("Merged")}, {"DATA", {std::byte{0x01}, std::byte{0}}}});
  return make_plugin({"Base.esp"}, {top("CELL", {grp(0, 2, {grp(0, 3, {merged})})})});
}

void write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string utf8_text(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::vector<std::uint8_t> patch_bytes(const radicle_plugin* patch) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(patch, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::uint8_t> copy(bytes, bytes + size);
  radicle_free_bytes(bytes);
  return copy;
}

class CellEditAbi : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_cell_edit_abi_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    write_file(root_ / "Base.esp", history_base());
    write_file(root_ / "Patch.esp", history_patch());
    const auto base = utf8_text(root_ / "Base.esp");
    const auto patch = utf8_text(root_ / "Patch.esp");
    const char* raw[] = {base.c_str(), patch.c_str()};
    ASSERT_EQ(radicle_env_load(raw, 2, &env_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void TearDown() override {
    if (env_ != nullptr) radicle_env_destroy(env_);
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  void open_patch(PluginHandle& handle) {
    radicle_plugin* patch = nullptr;
    ASSERT_EQ(radicle_plugin_read_file(utf8_text(root_ / "Patch.esp").c_str(), &patch), RADICLE_STATUS_OK)
        << radicle_last_error();
    handle.reset(patch);
  }

  void open_history(HistoryHandle& handle) {
    radicle_edit_history* history = nullptr;
    ASSERT_EQ(radicle_plugin_history_create(&history), RADICLE_STATUS_OK) << radicle_last_error();
    handle.reset(history);
  }

  void edit_reference(radicle_plugin* patch) {
    radicle_placed_edit edit{};
    edit.has_transform = 1;
    edit.position[0] = 11.0f;
    edit.position[1] = 21.0f;
    edit.position[2] = 31.0f;
    radicle_placed_edit_result result{};
    ASSERT_EQ(radicle_plugin_edit_placed_reference(patch, env_, {"Base.esp", 0x2001}, &edit, &result),
              RADICLE_STATUS_OK)
        << radicle_last_error();
    ASSERT_EQ(result.status, RADICLE_PLACED_EDIT_EDITED);
  }

  std::uint64_t revision_of(const radicle_edit_history* history) {
    std::uint64_t value = 0;
    EXPECT_EQ(radicle_plugin_history_revision(history, &value), RADICLE_STATUS_OK) << radicle_last_error();
    return value;
  }

  std::size_t undo_count_of(const radicle_edit_history* history) {
    std::size_t value = 0;
    EXPECT_EQ(radicle_plugin_history_undo_count(history, &value), RADICLE_STATUS_OK) << radicle_last_error();
    return value;
  }

  std::size_t redo_count_of(const radicle_edit_history* history) {
    std::size_t value = 0;
    EXPECT_EQ(radicle_plugin_history_redo_count(history, &value), RADICLE_STATUS_OK) << radicle_last_error();
    return value;
  }

  std::filesystem::path root_;
  radicle_env* env_{nullptr};
};

}  // namespace

TEST_F(CellEditAbi, HistoryUndoRestoresPatchBytesExactly) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);
  const auto before = patch_bytes(patch.get());

  ASSERT_EQ(radicle_plugin_history_record(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  edit_reference(patch.get());
  ASSERT_NE(patch_bytes(patch.get()), before);

  ASSERT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(patch_bytes(patch.get()), before);
}

TEST_F(CellEditAbi, HistoryRedoReappliesTheEditedBytes) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);
  const auto before = patch_bytes(patch.get());

  ASSERT_EQ(radicle_plugin_history_record(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  edit_reference(patch.get());
  const auto edited = patch_bytes(patch.get());
  ASSERT_NE(edited, before);

  ASSERT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(patch_bytes(patch.get()), before);

  ASSERT_EQ(radicle_plugin_history_redo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(patch_bytes(patch.get()), edited);
}

TEST_F(CellEditAbi, HistoryCountsAndRevisionFollowEachStep) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);
  const auto start = revision_of(history.get());
  EXPECT_EQ(undo_count_of(history.get()), 0u);
  EXPECT_EQ(redo_count_of(history.get()), 0u);

  ASSERT_EQ(radicle_plugin_history_record(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  const auto recorded = revision_of(history.get());
  EXPECT_GT(recorded, start);
  EXPECT_EQ(undo_count_of(history.get()), 1u);
  edit_reference(patch.get());

  ASSERT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  const auto undone = revision_of(history.get());
  EXPECT_GT(undone, recorded);
  EXPECT_EQ(undo_count_of(history.get()), 0u);
  EXPECT_EQ(redo_count_of(history.get()), 1u);

  ASSERT_EQ(radicle_plugin_history_redo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_GT(revision_of(history.get()), undone);
  EXPECT_EQ(undo_count_of(history.get()), 1u);
  EXPECT_EQ(redo_count_of(history.get()), 0u);

  ASSERT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_history_record(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(undo_count_of(history.get()), 1u);
  EXPECT_EQ(redo_count_of(history.get()), 0u);
}

TEST_F(CellEditAbi, HistoryRefusesUndoAndRedoOnEmptyStackAndLeavesPatchUntouched) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);
  const auto before = patch_bytes(patch.get());
  const auto start = revision_of(history.get());

  EXPECT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STRNE(radicle_last_error(), "");
  EXPECT_EQ(radicle_plugin_history_redo(history.get(), patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(patch_bytes(patch.get()), before);
  EXPECT_EQ(revision_of(history.get()), start);
  EXPECT_EQ(undo_count_of(history.get()), 0u);
  EXPECT_EQ(redo_count_of(history.get()), 0u);
}

TEST_F(CellEditAbi, HistoryCapsUndoAtFiftyEntries) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);
  for (int step = 0; step < 51; ++step) {
    ASSERT_EQ(radicle_plugin_history_record(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  }
  EXPECT_EQ(undo_count_of(history.get()), 50u);
  for (int step = 0; step < 50; ++step) {
    ASSERT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_OK) << radicle_last_error();
  }
  EXPECT_EQ(radicle_plugin_history_undo(history.get(), patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CellEditAbi, HistoryNullArgumentsAreInvalidArgument) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  HistoryHandle history(nullptr, &radicle_plugin_history_destroy);
  open_history(history);

  EXPECT_EQ(radicle_plugin_history_create(nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_record(nullptr, patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_record(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_undo(nullptr, patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_undo(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_redo(nullptr, patch.get()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_redo(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  std::size_t count = 0;
  EXPECT_EQ(radicle_plugin_history_undo_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_undo_count(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_redo_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_redo_count(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  std::uint64_t revision = 0;
  EXPECT_EQ(radicle_plugin_history_revision(nullptr, &revision), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_history_revision(history.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
