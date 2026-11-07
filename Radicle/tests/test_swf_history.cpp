#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_history.hpp"
#include "swf_character_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using swf_character_test::movie_of;

std::vector<std::byte> fixture_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {1, {}},
  });
}

fo4::swf::SwfTagPlan show_frame_plan(std::size_t tag_index, std::uint8_t payload) {
  fo4::swf::SwfTagPlan plan;
  fo4::swf::SwfTagReplacement replacement;
  replacement.tag_index = tag_index;
  replacement.payload = {static_cast<std::byte>(payload)};
  plan.replacements.push_back(std::move(replacement));
  return plan;
}

std::vector<std::byte> current_bytes(const fo4::swf::SwfEditHistory& history) {
  std::vector<std::byte> bytes;
  EXPECT_TRUE(history.write_bytes(bytes));
  return bytes;
}

}  // namespace

TEST(SwfHistory, AnEditBecomesACommandAndUndoReplaysTheOriginal) {
  const auto movie = fixture_movie();
  fo4::swf::SwfEditHistory history;
  std::string message;
  ASSERT_TRUE(history.open(movie, message)) << message;
  ASSERT_EQ(history.undo_count(), 0u);
  EXPECT_EQ(current_bytes(history), movie);

  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x00), message)) << message;
  EXPECT_EQ(history.revision(), 1u);
  EXPECT_EQ(history.undo_count(), 1u);
  EXPECT_EQ(history.redo_count(), 0u);
  const auto after_first = current_bytes(history);
  EXPECT_NE(after_first, movie);

  ASSERT_TRUE(history.undo(message)) << message;
  EXPECT_EQ(history.revision(), 2u);
  EXPECT_EQ(history.undo_count(), 0u);
  EXPECT_EQ(history.redo_count(), 1u);
  EXPECT_EQ(current_bytes(history), movie);

  ASSERT_TRUE(history.redo(message)) << message;
  EXPECT_EQ(history.undo_count(), 1u);
  EXPECT_EQ(current_bytes(history), after_first);
}

TEST(SwfHistory, UndoAndRedoOfASequenceEqualTheForwardSequence) {
  const auto movie = fixture_movie();
  fo4::swf::SwfEditHistory forward;
  std::string message;
  ASSERT_TRUE(forward.open(movie, message));
  ASSERT_TRUE(forward.apply(show_frame_plan(1, 0x01), message)) << message;
  ASSERT_TRUE(forward.apply(show_frame_plan(1, 0x02), message)) << message;
  const auto forward_bytes = current_bytes(forward);

  fo4::swf::SwfEditHistory history;
  ASSERT_TRUE(history.open(movie, message));
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x01), message));
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x02), message));
  ASSERT_EQ(current_bytes(history), forward_bytes);
  ASSERT_TRUE(history.undo(message));
  ASSERT_TRUE(history.undo(message));
  EXPECT_EQ(current_bytes(history), movie);
  ASSERT_TRUE(history.redo(message));
  ASSERT_TRUE(history.redo(message));
  EXPECT_EQ(current_bytes(history), forward_bytes);
  EXPECT_EQ(history.revision(), 6u);
}

TEST(SwfHistory, AFailedEditLeavesTheHistoryAndBytesUnchanged) {
  const auto movie = fixture_movie();
  fo4::swf::SwfEditHistory history;
  std::string message;
  ASSERT_TRUE(history.open(movie, message));
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x03), message));
  const auto before = current_bytes(history);
  const auto revision = history.revision();
  const auto undo_count = history.undo_count();

  fo4::swf::SwfTagPlan removal;
  fo4::swf::SwfTagRemoval remove_end;
  remove_end.tag_index = 2;
  removal.removals.push_back(remove_end);
  EXPECT_FALSE(history.apply(removal, message));
  EXPECT_NE(message.find("refused"), std::string::npos) << message;

  fo4::swf::SwfTagPlan out_of_range;
  fo4::swf::SwfTagReplacement replacement;
  replacement.tag_index = 99;
  replacement.payload = {std::byte{0}};
  out_of_range.replacements.push_back(replacement);
  EXPECT_FALSE(history.apply(out_of_range, message));

  EXPECT_EQ(history.revision(), revision);
  EXPECT_EQ(history.undo_count(), undo_count);
  EXPECT_EQ(current_bytes(history), before);
}

TEST(SwfHistory, ANewEditAfterUndoDropsTheRedoTail) {
  const auto movie = fixture_movie();
  fo4::swf::SwfEditHistory history;
  std::string message;
  ASSERT_TRUE(history.open(movie, message));
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x04), message));
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x05), message));
  ASSERT_TRUE(history.undo(message));
  EXPECT_EQ(history.redo_count(), 1u);
  ASSERT_TRUE(history.apply(show_frame_plan(1, 0x06), message));
  EXPECT_EQ(history.redo_count(), 0u);
  EXPECT_EQ(history.undo_count(), 2u);
  EXPECT_FALSE(history.redo(message));
}

TEST(SwfHistory, RefusesToOpenBytesThatDoNotDecode) {
  fo4::swf::SwfEditHistory history;
  std::string message;
  const std::vector<std::byte> garbage = {std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'}};
  EXPECT_FALSE(history.open(garbage, message));
  EXPECT_FALSE(history.apply(show_frame_plan(0, 0x00), message));
  EXPECT_FALSE(history.undo(message));
  std::vector<std::byte> bytes;
  EXPECT_FALSE(history.write_bytes(bytes));
}
