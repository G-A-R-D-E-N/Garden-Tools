#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "abc_test_support.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fo4/abc/abc.hpp"

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

struct FramesDeleter {
  void operator()(radicle_swf_frames* frames) const { radicle_swf_frames_destroy(frames); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;
using FramesHandle = std::unique_ptr<radicle_swf_frames, FramesDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

/* Requires the handle to serialize; an empty result must only follow a failed write. */
std::vector<std::byte> written(const radicle_swf* swf) {
  std::uint8_t* raw = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_write_bytes(swf, &raw, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::byte> bytes;
  if (raw == nullptr) return bytes;
  bytes.assign(reinterpret_cast<const std::byte*>(raw),
               reinterpret_cast<const std::byte*>(raw) + size);
  radicle_free_bytes(raw);
  return bytes;
}

/* Invariant: the payload vectors must outlive the plan, because the plan points into them. */
struct Plan {
  std::vector<radicle_swf_tag_replacement> replacements;
  std::vector<radicle_swf_tag_insertion> insertions;

  void replace(std::size_t index, const std::vector<std::byte>& payload) {
    replacements.push_back(radicle_swf_tag_replacement{
        index, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
  }

  void insert(std::size_t before, std::uint16_t code, const std::vector<std::byte>& payload) {
    insertions.push_back(radicle_swf_tag_insertion{
        before, code, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
  }

  radicle_swf_tag_plan view() const {
    return radicle_swf_tag_plan{replacements.data(), replacements.size(),
                                insertions.data(), insertions.size()};
  }
};

radicle_status apply(radicle_swf* swf, const Plan& builder, radicle_swf_plan_result& result) {
  const auto plan = builder.view();
  return radicle_swf_apply_plan(swf, &plan, &result);
}

std::vector<std::byte> frame_label(std::string_view name) {
  SwfBuilder body;
  body.text(name);
  return body.body;
}

std::vector<std::byte> place_move(std::uint16_t depth) {
  SwfBuilder body;
  body.u8(0x01);
  body.u16(depth);
  return body.body;
}

/* Invariant: the flat order is DefineShape3, FrameLabel, ShowFrame, ShowFrame, End. */
std::vector<std::byte> label_movie(bool compressed) {
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(2);
  body.tag(32, swf_art_test::define_shape_3_payload(), false);
  body.tag(43, frame_label("start"), false);
  body.tag(1);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 6, false, compressed);
}

/* Invariant: flat index 1 must be the sprite's FrameLabel child, which only a flat list exposes. */
std::vector<std::byte> sprite_movie() {
  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  sprite.tag(43, frame_label("inner"), false);
  sprite.tag(1);
  sprite.tag(0);

  SwfBuilder place;
  place.u8(0x02);
  place.u16(1);
  place.u16(7);

  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(39, sprite.body, false);
  body.tag(26, place.body, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 6, false, false);
}

/* Invariant: flat index 0 is a PlaceObject2 whose payload is the one a broken plan replaces. */
std::vector<std::byte> place_movie() {
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(26, place_move(3), false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 6, false, false);
}

}  // namespace

TEST(CApiSwfEdit, BumpsRevisionOnlyForAChangedPlan) {
  auto swf = open(label_movie(false));
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_revision(swf.get()), 0u);

  const auto same = frame_label("start");
  Plan unchanged;
  unchanged.replace(1, same);
  radicle_swf_plan_result result{};
  ASSERT_EQ(apply(swf.get(), unchanged, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(result.revision, 0u);
  EXPECT_EQ(radicle_swf_revision(swf.get()), 0u);

  Plan empty;
  ASSERT_EQ(apply(swf.get(), empty, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(result.revision, 0u);

  const auto renamed = frame_label("renamed");
  Plan first;
  first.replace(1, renamed);
  ASSERT_EQ(apply(swf.get(), first, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.revision, 1u);
  EXPECT_EQ(radicle_swf_revision(swf.get()), 1u);

  const auto renamed_again = frame_label("again");
  Plan second;
  second.replace(1, renamed_again);
  ASSERT_EQ(apply(swf.get(), second, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.revision, 2u);
  EXPECT_EQ(radicle_swf_frame_label_count(swf.get()), 1u);
  radicle_swf_frame_label label{};
  ASSERT_EQ(radicle_swf_frame_label_at(swf.get(), 0, &label), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(label.name), "again");
}

TEST(CApiSwfEdit, AppliesAReplacementPlanAndSavesTheBytes) {
  auto swf = open(label_movie(false));
  ASSERT_NE(swf, nullptr);
  const auto replacement = frame_label("menu/root");
  Plan plan;
  plan.replace(1, replacement);
  radicle_swf_plan_result result{};
  ASSERT_EQ(apply(swf.get(), plan, result), RADICLE_STATUS_OK) << radicle_last_error();

  const auto bytes = written(swf.get());
  ASSERT_FALSE(bytes.empty());
  auto reparsed = open(bytes);
  ASSERT_NE(reparsed, nullptr);
  EXPECT_EQ(radicle_swf_tag_count(reparsed.get()), 5u);
  ASSERT_EQ(radicle_swf_frame_label_count(reparsed.get()), 1u);
  radicle_swf_frame_label label{};
  ASSERT_EQ(radicle_swf_frame_label_at(reparsed.get(), 0, &label), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(label.name), "menu/root");
  radicle_swf_tag tag{};
  ASSERT_EQ(radicle_swf_tag_at(reparsed.get(), 2, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 1);
  EXPECT_EQ(tag.payload_size, 0u);
  EXPECT_EQ(radicle_swf_revision(reparsed.get()), 0u);
}

TEST(CApiSwfEdit, RejectsAPlanAtomicallyAndKeepsRevisionAndBytes) {
  auto swf = open(label_movie(false));
  ASSERT_NE(swf, nullptr);
  const auto source = written(swf.get());
  const auto renamed = frame_label("unreachable");
  radicle_swf_plan_result result{};

  Plan out_of_range;
  out_of_range.replace(5, renamed);
  EXPECT_EQ(apply(swf.get(), out_of_range, result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.changed, 0);

  Plan terminal_end;
  terminal_end.replace(4, renamed);
  EXPECT_EQ(apply(swf.get(), terminal_end, result), RADICLE_STATUS_INVALID_ARGUMENT);

  Plan duplicate;
  duplicate.replace(1, renamed);
  duplicate.replace(1, renamed);
  EXPECT_EQ(apply(swf.get(), duplicate, result), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_swf_revision(swf.get()), 0u);
  EXPECT_EQ(written(swf.get()), source);
  EXPECT_EQ(radicle_swf_tag_count(swf.get()), 5u);
  radicle_swf_frame_label label{};
  ASSERT_EQ(radicle_swf_frame_label_at(swf.get(), 0, &label), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(label.name), "start");

  auto placed = open(place_movie());
  ASSERT_NE(placed, nullptr);
  const auto placed_source = written(placed.get());
  const std::vector<std::byte> truncated{std::byte{0x01}};
  Plan broken_payload;
  broken_payload.replace(0, truncated);
  EXPECT_EQ(apply(placed.get(), broken_payload, result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("did not apply"), std::string::npos)
      << radicle_last_error();
  EXPECT_EQ(radicle_swf_revision(placed.get()), 0u);
  EXPECT_EQ(written(placed.get()), placed_source);

  auto nested = open(sprite_movie());
  ASSERT_NE(nested, nullptr);
  const auto nested_source = written(nested.get());
  Plan inside_sprite;
  inside_sprite.replace(1, renamed);
  EXPECT_EQ(apply(nested.get(), inside_sprite, result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("top-level tag"), std::string::npos)
      << radicle_last_error();
  Plan insert_inside_sprite;
  insert_inside_sprite.insert(1, 43, renamed);
  EXPECT_EQ(apply(nested.get(), insert_inside_sprite, result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(nested.get()), nested_source);

  EXPECT_EQ(radicle_swf_apply_plan(swf.get(), nullptr, &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_apply_plan(swf.get(), nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfEdit, RebuildsEveryCachedViewAfterAnInsertion) {
  auto swf = open(label_movie(false));
  ASSERT_NE(swf, nullptr);
  radicle_swf_tag before{};
  ASSERT_EQ(radicle_swf_tag_at(swf.get(), 0, &before), RADICLE_STATUS_OK);
  radicle_swf_shape shape_before{};
  ASSERT_EQ(radicle_swf_shape_at(swf.get(), 0, &shape_before), RADICLE_STATUS_OK);

  const auto inserted = frame_label("mid");
  Plan plan;
  plan.insert(2, 43, inserted);
  radicle_swf_plan_result result{};
  ASSERT_EQ(apply(swf.get(), plan, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.revision, 1u);

  EXPECT_EQ(radicle_swf_tag_count(swf.get()), 6u);
  radicle_swf_info info{};
  ASSERT_EQ(radicle_swf_get_info(swf.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.tag_count, 6u);
  EXPECT_EQ(info.frame_label_count, 2u);
  ASSERT_EQ(radicle_swf_frame_label_count(swf.get()), 2u);
  radicle_swf_frame_label label{};
  ASSERT_EQ(radicle_swf_frame_label_at(swf.get(), 1, &label), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(label.name), "mid");
  EXPECT_EQ(label.tag_index, 2u);

  radicle_swf_tag tag{};
  ASSERT_EQ(radicle_swf_tag_at(swf.get(), 2, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 43);
  ASSERT_EQ(radicle_swf_tag_at(swf.get(), 3, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 1);

  std::uint8_t* payload = nullptr;
  std::size_t payload_size = 0;
  ASSERT_EQ(radicle_swf_tag_payload(swf.get(), 2, &payload, &payload_size), RADICLE_STATUS_OK);
  EXPECT_EQ(payload_size, inserted.size());
  radicle_free_bytes(payload);

  ASSERT_EQ(radicle_swf_tag_at(swf.get(), 0, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.payload_size, before.payload_size);
  radicle_swf_shape shape_after{};
  ASSERT_EQ(radicle_swf_shape_at(swf.get(), 0, &shape_after), RADICLE_STATUS_OK);
  EXPECT_EQ(shape_after.edge_count, shape_before.edge_count);

  auto reparsed = open(written(swf.get()));
  ASSERT_NE(reparsed, nullptr);
  EXPECT_EQ(radicle_swf_frame_label_count(reparsed.get()), 2u);
}

TEST(CApiSwfEdit, SavesAnUneditedHandleByteExactly) {
  const auto bytes = label_movie(true);
  auto swf = open(bytes);
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(written(swf.get()), bytes);

  Plan unchanged;
  const auto same = frame_label("start");
  unchanged.replace(1, same);
  radicle_swf_plan_result result{};
  ASSERT_EQ(apply(swf.get(), unchanged, result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(written(swf.get()), bytes);
}

TEST(CApiSwfEdit, RejectsAFrameListBuiltBeforeAnEdit) {
  auto swf = open(label_movie(false));
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  FramesHandle frames(raw);

  Plan unchanged;
  const auto same = frame_label("start");
  unchanged.replace(1, same);
  radicle_swf_plan_result result{};
  ASSERT_EQ(apply(swf.get(), unchanged, result), RADICLE_STATUS_OK);
  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count), RADICLE_STATUS_OK);

  const auto renamed = frame_label("later");
  Plan changed;
  changed.replace(1, renamed);
  ASSERT_EQ(apply(swf.get(), changed, result), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(result.changed, 1);

  EXPECT_EQ(radicle_swf_frames_timeline_count(frames.get()), 0u);
  EXPECT_NE(std::string(radicle_last_error()).find("built before"), std::string::npos)
      << radicle_last_error();
  std::uint16_t character_id = 0;
  std::size_t frame_count = 0;
  EXPECT_EQ(radicle_swf_frames_timeline_at(frames.get(), 0, &character_id, &frame_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_swf_frames_instance instance{};
  EXPECT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 0, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_frames* reopened = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &reopened), RADICLE_STATUS_OK)
      << radicle_last_error();
  FramesHandle after(reopened);
  EXPECT_EQ(radicle_swf_frames_instance_count(after.get(), 0, 0, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);

  // Constraint: destroying the movie must not turn a stale list into a readable one.
  swf.reset();
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_frames_instance_count(after.get(), 0, 0, &count), RADICLE_STATUS_OK);
}

TEST(CApiSwfAbcWrite, DumpsAssemblesAndAppliesAnEditedDoAbc2Block) {
  const std::vector<std::uint8_t> abc = fo4::test::build_abc();
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  SwfBuilder payload;
  payload.u32(0);
  payload.text("edited");
  payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()),
                                           abc.size()));
  builder.tag(fo4::swf::tag::kDoAbc2, payload.body, true);
  builder.tag(fo4::swf::tag::kEnd);
  SwfHandle swf = open(swf_test::wrap_body(builder.body, 10, false, false));
  ASSERT_NE(swf, nullptr);
  ASSERT_EQ(radicle_swf_abc_count(swf.get()), 1u);

  std::uint8_t* text_raw = nullptr;
  std::size_t text_size = 0;
  ASSERT_EQ(radicle_swf_abc_text(swf.get(), 0, &text_raw, &text_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  std::string text(reinterpret_cast<const char*>(text_raw), text_size);
  radicle_free_bytes(text_raw);
  EXPECT_NE(text.find("abc 46.16"), std::string::npos);
  EXPECT_NE(text.find("body 0 method"), std::string::npos);

  std::uint8_t* abc_raw = nullptr;
  std::size_t abc_size = 0;
  ASSERT_EQ(radicle_swf_abc_assemble(swf.get(), 0,
                                     reinterpret_cast<const std::uint8_t*>(text.data()),
                                     text.size(), &abc_raw, &abc_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(abc_size, abc.size());
  EXPECT_EQ(std::memcmp(abc_raw, abc.data(), abc.size()), 0);
  radicle_free_bytes(abc_raw);

  const std::string edited = []() {
    return std::string{"  string 1 \"RadicleEdit\"\n"};
  }();
  std::string changed = text;
  const std::size_t line = changed.find("  string 1 \"");
  ASSERT_NE(line, std::string::npos);
  const std::size_t line_end = changed.find('\n', line);
  ASSERT_NE(line_end, std::string::npos);
  changed.replace(line, line_end - line + 1, edited);
  ASSERT_EQ(radicle_swf_abc_apply_text(swf.get(), 0,
                                       reinterpret_cast<const std::uint8_t*>(changed.data()),
                                       changed.size()),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto bytes = written(swf.get());
  ASSERT_FALSE(bytes.empty());

  SwfHandle reopened = open(bytes);
  ASSERT_NE(reopened, nullptr);
  std::uint8_t* after_raw = nullptr;
  std::size_t after_size = 0;
  ASSERT_EQ(radicle_swf_abc_text(reopened.get(), 0, &after_raw, &after_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  std::string after(reinterpret_cast<const char*>(after_raw), after_size);
  radicle_free_bytes(after_raw);
  EXPECT_NE(after.find("RadicleEdit"), std::string::npos);
}
