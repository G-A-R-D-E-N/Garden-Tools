#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/c_api_swf_verbs.h"
#include "swf_art_fixtures.hpp"
#include "swf_character_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

struct FramesDeleter {
  void operator()(radicle_swf_frames* frames) const { radicle_swf_frames_destroy(frames); }
};

using FramesHandle = std::unique_ptr<radicle_swf_frames, FramesDeleter>;

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

std::vector<std::byte> frame_label(std::string_view name) {
  swf_test::SwfBuilder body;
  body.text(name);
  return body.body;
}

std::vector<std::byte> edit_text_with_text() {
  swf_test::SwfBuilder body;
  body.u16(11);
  body.rect(0, 400, 0, 200, 12);
  body.u8(0x80);
  body.u8(0);
  body.text("variable");
  body.text("hello");
  return body.body;
}

std::vector<std::byte> place_without_matrix(std::uint16_t depth, std::uint16_t character) {
  swf_test::SwfBuilder body;
  body.u8(0x02);
  body.u16(depth);
  body.u16(character);
  return body.body;
}

std::vector<std::byte> place_with_matrix(std::uint16_t depth, std::uint16_t character) {
  swf_test::SwfBuilder body;
  body.u8(0x06);
  body.u16(depth);
  body.u16(character);
  swf_art_test::identity_matrix(body);
  return body.body;
}

radicle_swf_transform transform_of(double scale, double translate_x) {
  radicle_swf_transform transform{};
  transform.scale_x = scale;
  transform.scale_y = scale;
  transform.translate_x = translate_x;
  return transform;
}

std::string placement_json(const HistoryHandle& history) {
  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_placement_list_json(history.get(), 0, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  if (text == nullptr) return {};
  std::string out(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  return out;
}

std::vector<std::string> label_rows(const HistoryHandle& history) {
  char** rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_frame_label_rows(history.get(), 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  std::vector<std::string> out;
  for (std::size_t index = 0; index < count; ++index) out.emplace_back(rows[index]);
  radicle_free_string_array(rows, count);
  return out;
}

// Requires the root display list to hold one instance at depth on frame 0; the id comes from the frames API.
std::uint32_t root_instance_id(const HistoryHandle& history, std::uint16_t depth) {
  const auto swf = open(history_bytes(history));
  radicle_swf_frames* raw = nullptr;
  EXPECT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const FramesHandle frames(raw);
  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count), RADICLE_STATUS_OK);
  for (std::size_t index = 0; index < count; ++index) {
    radicle_swf_frames_instance instance{};
    EXPECT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, index, &instance),
              RADICLE_STATUS_OK);
    if (instance.depth == depth) return instance.instance_id;
  }
  ADD_FAILURE() << "no root instance at depth " << depth;
  return 0;
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

TEST(CApiSwfHistory, RemovesATopLevelTagAndRefusesTheEndTag) {
  const auto color = [](std::uint8_t value) {
    return std::vector<std::byte>{std::byte{value}, std::byte{value}, std::byte{value}};
  };
  const auto swf = open(swf_character_test::movie_of({{9, color(1)}, {9, color(2)}}));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);

  radicle_swf_tag_removal removal{};
  removal.tag_index = 0;
  radicle_swf_tag_plan plan{};
  plan.removals = &removal;
  plan.removal_count = 1;
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_history_apply(history.get(), &plan, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.revision, 1u);
  EXPECT_EQ(history_bytes(history), swf_character_test::movie_of({{9, color(2)}}));

  removal.tag_index = 1;
  EXPECT_EQ(radicle_swf_history_apply(history.get(), &plan, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), 1u);
}

TEST(CApiSwfHistory, ResolvesFlatIndexesAgainstTheCurrentFile) {
  const auto color = [](std::uint8_t value) {
    return std::vector<std::byte>{std::byte{value}, std::byte{value}, std::byte{value}};
  };
  const std::vector<std::byte> sprite = {std::byte{7}, std::byte{0}, std::byte{1}, std::byte{0},
                                         std::byte{0x40}, std::byte{0}, std::byte{0}, std::byte{0}};
  const auto swf = open(swf_character_test::movie_of({{9, color(1)}, {9, color(2)}}));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);

  radicle_swf_tag_insertion insertion{};
  insertion.before_tag_index = 1;
  insertion.code = 39;
  insertion.payload = reinterpret_cast<const std::uint8_t*>(sprite.data());
  insertion.size = sprite.size();
  radicle_swf_tag_plan plan{};
  plan.insertions = &insertion;
  plan.insertion_count = 1;
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_history_apply(history.get(), &plan, &result), RADICLE_STATUS_OK)
      << radicle_last_error();

  const std::vector<std::uint8_t> replacement_payload = {3, 3, 3};
  radicle_swf_tag_replacement replacement{};
  replacement.tag_index = 4;
  replacement.payload = replacement_payload.data();
  replacement.size = replacement_payload.size();
  plan = {};
  plan.replacements = &replacement;
  plan.replacement_count = 1;
  ASSERT_EQ(radicle_swf_history_apply(history.get(), &plan, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(history_bytes(history),
            swf_character_test::movie_of({{9, color(1)}, {39, sprite}, {9, color(3)}}));
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

// Constraint: an edit-text write and each frame-label verb must each be one history command that undo and redo replay.
TEST(CApiSwfVerbs, EditTextAndFrameLabelsUndoAndRedoAsOneHistory) {
  const auto movie = swf_character_test::movie_of({
      {37, edit_text_with_text()},
      {43, frame_label("start")},
      {1, {}},
  });
  const auto swf = open(movie);
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);

  const std::string_view bye = "bye";
  radicle_swf_edit_text_fields fields{};
  fields.has_text = 1;
  fields.text = reinterpret_cast<const std::uint8_t*>(bye.data());
  fields.text_size = bye.size();
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_edit_text_apply(history.get(), 0, &fields, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_edit_text_info info{};
  std::uint8_t* text = nullptr;
  std::size_t text_size = 0;
  ASSERT_EQ(radicle_swf_edit_text_read(history.get(), 0, &info, &text, &text_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(text), text_size), "bye");
  EXPECT_EQ(info.character_id, 11);
  radicle_free_bytes(text);

  ASSERT_EQ(radicle_swf_frame_label_add(history.get(), 0, 0, "mid", &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(label_rows(history), (std::vector<std::string>{"0\t0\tstart", "1\t0\tmid"}));
  ASSERT_EQ(radicle_swf_frame_label_rename(history.get(), 0, 1, "late", &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_swf_frame_label_remove(history.get(), 0, 0, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(label_rows(history), (std::vector<std::string>{"0\t0\tlate"}));
  const auto final_bytes = history_bytes(history);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_history_undo_count(history.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 4u);
  for (int step = 0; step < 4; ++step) {
    ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
        << radicle_last_error();
  }
  EXPECT_EQ(history_bytes(history), movie);
  EXPECT_EQ(label_rows(history), std::vector<std::string>{"0	0	start"});
  for (int step = 0; step < 4; ++step) {
    ASSERT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_OK)
        << radicle_last_error();
  }
  EXPECT_EQ(history_bytes(history), final_bytes);
}

// Constraint: a transform edit must move the placement list's matrix and undo must restore the authored list.
TEST(CApiSwfVerbs, TransformEditMovesThePlacementListAndUndoesIt) {
  const auto swf = open(swf_character_test::movie_of({
      {26, place_with_matrix(5, 7)},
      {1, {}},
  }));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);
  const auto before = placement_json(history);
  EXPECT_NE(before.find("\"depth\":5,\"character_id\":7,\"matrix\":{"), std::string::npos) << before;

  const radicle_swf_instance_transform edit{root_instance_id(history, 5), transform_of(2, 40)};
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_apply_instance_transforms(history.get(), 0, &edit, 1, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  const auto moved = placement_json(history);
  EXPECT_NE(moved, before);
  EXPECT_NE(moved.find("\"scale_x\":2,\"scale_y\":2,"), std::string::npos) << moved;

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(placement_json(history), before);
  ASSERT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(placement_json(history), moved);
}

// Constraint: a depth reassign and a keyframe must each be one history command, and the depth id must be the frames id.
TEST(CApiSwfVerbs, DepthReassignAndKeyframeUndoAndRedoThroughHistory) {
  const auto swf = open(swf_character_test::movie_of({
      {26, place_without_matrix(5, 7)},
      {1, {}},
      {1, {}},
  }));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);
  const auto before = placement_json(history);

  std::uint32_t moved_id = 0;
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_reassign_instance_depth(history.get(), 0, root_instance_id(history, 5), 9,
                                                &moved_id, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_NE(moved_id, 0u);
  EXPECT_EQ(moved_id, root_instance_id(history, 9));
  const auto reassigned = placement_json(history);
  EXPECT_NE(reassigned.find("\"depth\":9"), std::string::npos) << reassigned;
  EXPECT_EQ(reassigned.find("\"depth\":5"), std::string::npos) << reassigned;

  const auto keyed = transform_of(2, 0);
  ASSERT_EQ(radicle_swf_insert_instance_transform_keyframe(history.get(), 1, moved_id, &keyed, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  const auto keyframed = placement_json(history);
  EXPECT_NE(keyframed.find("\"scale_x\":2,"), std::string::npos) << keyframed;

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_history_undo_count(history.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK);
  EXPECT_EQ(placement_json(history), reassigned);
  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK);
  EXPECT_EQ(placement_json(history), before);
  ASSERT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_swf_history_redo(history.get(), &result), RADICLE_STATUS_OK);
  EXPECT_EQ(placement_json(history), keyframed);
}

// Constraint: the edit-text verbs must take the flat index radicle_swf_tag_at reports and refuse a tag inside a DefineSprite. Regression: the verbs indexed the top-level tree with the flat index, so a DefineEditText past a sprite with children was read or written at the wrong position.
TEST(CApiSwfVerbs, EditTextAddressesTheFlatIndexPastADefineSprite) {
  const auto swf = open(swf_character_test::movie_of({
      {39, swf_character_test::sprite_payload(14, 1)},
      {37, edit_text_with_text()},
      {1, {}},
  }));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);

  radicle_swf_tag tag{};
  std::size_t flat = 0;
  while (radicle_swf_tag_at(swf.get(), flat, &tag) == RADICLE_STATUS_OK && tag.code != 37) {
    ++flat;
  }
  ASSERT_EQ(tag.code, 37);
  ASSERT_GT(flat, 1u);

  const std::string_view bye = "bye";
  radicle_swf_edit_text_fields fields{};
  fields.has_text = 1;
  fields.text = reinterpret_cast<const std::uint8_t*>(bye.data());
  fields.text_size = bye.size();
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_edit_text_apply(history.get(), flat, &fields, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_edit_text_info info{};
  std::uint8_t* text = nullptr;
  std::size_t text_size = 0;
  ASSERT_EQ(radicle_swf_edit_text_read(history.get(), flat, &info, &text, &text_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(text), text_size), "bye");
  radicle_free_bytes(text);

  EXPECT_EQ(radicle_swf_edit_text_read(history.get(), 1, &info, &text, &text_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("inside a DefineSprite"), std::string::npos)
      << radicle_last_error();
}

// Invariant: a depth reassign that inserts a RemoveObject2 must keep an unrelated long-header tag between them encoded as it was, not re-encoded short.
TEST(CApiSwfVerbs, DepthReassignKeepsAnUnrelatedLongHeaderTagInTheMiddle) {
  swf_test::SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(26, place_without_matrix(5, 7), false);
  body.tag(1);
  body.tag(43, frame_label("mid"), true);
  body.tag(26, place_without_matrix(5, 8), false);
  body.tag(1);
  body.tag(0);
  const auto swf = open(swf_test::wrap_body(body.body, 10, false, false));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);

  std::uint32_t moved_id = 0;
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_reassign_instance_depth(history.get(), 0, root_instance_id(history, 5), 9,
                                                &moved_id, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(moved_id, root_instance_id(history, 9));

  swf_test::SwfBuilder expected;
  expected.tag(43, frame_label("mid"), true);
  const auto bytes = history_bytes(history);
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), expected.body.begin(), expected.body.end()),
            bytes.end())
      << "the unrelated long-header frame label was re-encoded";
}

// Constraint: a refused verb must zero its result and out parameters and leave the revision and undo stack unchanged.
TEST(CApiSwfVerbs, RefusedVerbsClearTheirOutputsAndLeaveTheHistoryUntouched) {
  const auto swf = open(swf_character_test::movie_of({
      {37, edit_text_with_text()},
      {43, frame_label("start")},
      {1, {}},
  }));
  ASSERT_NE(swf, nullptr);
  const auto history = open_history(swf);
  ASSERT_NE(history, nullptr);
  std::uint8_t sentinel = 0;

  radicle_swf_edit_text_info info{};
  info.character_id = 9;
  std::uint8_t* text = &sentinel;
  std::size_t text_size = 7;
  EXPECT_EQ(radicle_swf_edit_text_read(history.get(), 1, &info, &text, &text_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(text_size, 0u);
  EXPECT_EQ(info.character_id, 0);
  EXPECT_NE(std::string(radicle_last_error()).find("not a decoded DefineEditText"), std::string::npos)
      << radicle_last_error();

  char** rows = reinterpret_cast<char**>(&sentinel);
  std::size_t count = 7;
  EXPECT_EQ(radicle_swf_frame_label_rows(history.get(), 9, &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  radicle_swf_plan_result result{};
  const auto revision = radicle_swf_history_revision(history.get());
  EXPECT_EQ(radicle_swf_frame_label_remove(history.get(), 0, 5, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(result.revision, revision);

  const radicle_swf_instance_transform edit{99, transform_of(2, 40)};
  EXPECT_EQ(radicle_swf_apply_instance_transforms(history.get(), 99, &edit, 1, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(result.revision, revision);
  EXPECT_STRNE(radicle_last_error(), "");

  std::uint32_t moved = 77;
  EXPECT_EQ(radicle_swf_reassign_instance_depth(history.get(), 0, 999, 9, &moved, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(moved, 0u);
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), revision);
  ASSERT_EQ(radicle_swf_history_undo_count(history.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_swf_apply_instance_transforms(nullptr, 0, &edit, 1, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(result.revision, 0u);
}
