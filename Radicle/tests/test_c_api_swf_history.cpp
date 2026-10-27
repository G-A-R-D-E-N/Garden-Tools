#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_character_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

struct HistoryDeleter {
  void operator()(radicle_swf_history* history) const { radicle_swf_history_destroy(history); }
};

using HistoryHandle = std::unique_ptr<radicle_swf_history, HistoryDeleter>;

std::vector<std::byte> fixture_movie() {
  return swf_character_test::movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {1, {}},
  });
}

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

HistoryHandle open_history(const SwfHandle& swf) {
  radicle_swf_history* raw = nullptr;
  EXPECT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  return HistoryHandle(raw);
}

void plan_replacing(radicle_swf_tag_plan& plan, radicle_swf_tag_replacement& replacement,
                    std::size_t tag_index, const std::vector<std::uint8_t>& payload) {
  replacement = {};
  replacement.tag_index = tag_index;
  replacement.payload = payload.data();
  replacement.size = payload.size();
  plan = {};
  plan.replacements = &replacement;
  plan.replacement_count = 1;
}

std::vector<std::byte> history_bytes(const HistoryHandle& history) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_history_write_bytes(history.get(), &bytes, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  std::vector<std::byte> out;
  if (bytes != nullptr) {
    out.assign(reinterpret_cast<const std::byte*>(bytes),
               reinterpret_cast<const std::byte*>(bytes) + size);
    radicle_free_bytes(bytes);
  }
  return out;
}

}  // namespace

TEST(CApiSwfHistory, AppliesUndoesAndRedoesWithARevisionCounter) {
  const auto movie = fixture_movie();
  const auto swf = open(movie);
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), 0u);
  EXPECT_EQ(history_bytes(history), movie);

  const std::vector<std::uint8_t> payload = {0x00};
  radicle_swf_tag_replacement replacement{};
  radicle_swf_tag_plan plan{};
  plan_replacing(plan, replacement, 1, payload);
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_history_apply(history.get(), &plan, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.revision, 1u);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), 1u);
  const auto after_apply = history_bytes(history);
  EXPECT_NE(after_apply, movie);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_history_undo_count(history.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.revision, 2u);
  EXPECT_EQ(history_bytes(history), movie);
  ASSERT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(history_bytes(history), after_apply);
  ASSERT_EQ(radicle_swf_history_redo_count(history.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("nothing to redo"), std::string::npos)
      << radicle_last_error();
}

TEST(CApiSwfHistory, ARefusedPlanLeavesTheRevisionAndBytesUnchanged) {
  const auto movie = fixture_movie();
  const auto swf = open(movie);
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);
  const std::vector<std::uint8_t> payload = {0x01};
  radicle_swf_plan_result result{};

  radicle_swf_tag_replacement good_replacement{};
  radicle_swf_tag_plan good{};
  plan_replacing(good, good_replacement, 1, payload);
  ASSERT_EQ(radicle_swf_history_apply(history.get(), &good, &result), RADICLE_STATUS_OK);
  const auto before = history_bytes(history);
  const auto revision = radicle_swf_history_revision(history.get());

  radicle_swf_tag_replacement replacement{};
  replacement.tag_index = 99;
  replacement.payload = payload.data();
  replacement.size = payload.size();
  radicle_swf_tag_plan bad{};
  bad.replacements = &replacement;
  bad.replacement_count = 1;
  EXPECT_EQ(radicle_swf_history_apply(history.get(), &bad, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), revision);
  EXPECT_EQ(history_bytes(history), before);

  radicle_swf_tag_insertion insertion{};
  insertion.before_tag_index = 0;
  insertion.code = 0;
  radicle_swf_tag_plan bad_insert{};
  bad_insert.insertions = &insertion;
  bad_insert.insertion_count = 1;
  EXPECT_EQ(radicle_swf_history_apply(history.get(), &bad_insert, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), revision);
}

TEST(CApiSwfHistory, RejectsMissingArguments) {
  radicle_swf_history* history = nullptr;
  EXPECT_EQ(radicle_swf_history_create(nullptr, &history), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(history, nullptr);
  EXPECT_EQ(radicle_swf_history_revision(nullptr), 0u);
  std::size_t count = 1;
  EXPECT_EQ(radicle_swf_history_undo_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(count, 0u);
}
