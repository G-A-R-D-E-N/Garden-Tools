#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/c_api_swf_verbs.h"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_edit.hpp"
#include "abc_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;
using swf_test::frame_label;
using swf_test::placement_after_sprite_movie;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

struct HistoryDeleter {
  void operator()(radicle_swf_history* history) const { radicle_swf_history_destroy(history); }
};

using HistoryHandle = std::unique_ptr<radicle_swf_history, HistoryDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

SwfHandle open_abc_movie() {
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
  return open(swf_test::wrap_body(builder.body, 10, false, false));
}

std::string abc_text(const radicle_swf* swf) {
  std::uint8_t* raw = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_abc_text(swf, 0, &raw, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::string text = raw == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(raw), size);
  radicle_free_bytes(raw);
  return text;
}

std::string placements_text(const radicle_swf_history* history) {
  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_placements_json(history, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string list =
      text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  return list;
}

// Invariant: flat 1 is placement A (depth 1), flat 5 is placement C (depth 3), and the frame-0 DoAction jumps the playhead to frame 2.
std::vector<std::byte> goto_movie() {
  const std::vector<std::byte> jump{std::byte{0x81}, std::byte{0x02}, std::byte{0x00},
                                    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}};
  SwfBuilder remove;
  remove.u16(1);
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(3);
  body.tag(12, jump, false);
  body.tag(26, swf_test::place_full(1, 5, swf_test::matrix_full(0x10000, 0x10000, 0, 0, 20, 0), {}), false);
  body.tag(1);
  body.tag(28, remove.body, false);
  body.tag(1);
  body.tag(26, swf_test::place_full(3, 7, swf_test::matrix_full(0x10000, 0x10000, 0, 0, 30, 0), {}), false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfVerbs, SetDepthMovesASpriteChildThroughTheFlatVerb) {
  auto swf = open(placement_after_sprite_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_depth(history.get(), 1, 9, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_swf_placements_json(history.get(), &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string moved(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  EXPECT_NE(moved.find(R"({"tag_index":1,"depth":9,"character_id":7,"inside_sprite":true})"),
            std::string::npos)
      << moved;

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_swf_placements_json(history.get(), &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string restored(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  EXPECT_NE(
      restored.find(R"({"tag_index":1,"depth":1,"character_id":7,"inside_sprite":true})"),
      std::string::npos)
      << restored;

  EXPECT_EQ(radicle_swf_set_placement_depth(history.get(), 0, 9, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("not a placement"), std::string::npos);
  EXPECT_EQ(radicle_swf_set_placement_depth(history.get(), 99, 9, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfVerbs, SetTransformSplicesAMatrixIntoAMatrixLessSpriteChild) {
  auto swf = open(placement_after_sprite_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_transform(history.get(), 1, -40, 60, 0x20000, 0x08000,
                                                -0x10000, 0x10000, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_placement_matrix_fields matrix{};
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 1, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.has_scale, 1);
  EXPECT_EQ(matrix.scale_x, 0x20000);
  EXPECT_EQ(matrix.scale_y, 0x08000);
  EXPECT_EQ(matrix.has_rotate, 1);
  EXPECT_EQ(matrix.rotate0, -0x10000);
  EXPECT_EQ(matrix.rotate1, 0x10000);
  EXPECT_EQ(matrix.translate_x, -40);
  EXPECT_EQ(matrix.translate_y, 60);

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 1, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.has_scale, 0);
  EXPECT_EQ(matrix.scale_x, 0x10000);
  EXPECT_EQ(matrix.scale_y, 0x10000);
}

TEST(CApiSwfVerbs, SetTransformOnARootPlacementRoutesThroughTheNativeVerb) {
  swf_test::SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  sprite.tag(1);
  sprite.tag(0);
  const std::vector<std::byte> identity_matrix{std::byte{0x00}};
  auto swf = open(swf_test::movie_of(
      1,
      {{39, sprite.body},
       {26, swf_test::place_full(2, 7, identity_matrix, {})},
       {1, {}}}));
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_transform(history.get(), 3, 40, 60, 0x10000, 0x10000, 0,
                                                0, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_placement_matrix_fields matrix{};
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 3, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 40);
  EXPECT_EQ(matrix.translate_y, 60);
  EXPECT_EQ(matrix.has_scale, 0);
  EXPECT_EQ(matrix.scale_x, 0x10000);
  EXPECT_EQ(matrix.scale_y, 0x10000);

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 3, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 0);
  EXPECT_EQ(matrix.translate_y, 0);
}

TEST(CApiSwfVerbs, InsertKeyframeSplicesAMoveChildIntoTheSprite) {
  auto swf = open(placement_after_sprite_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  const auto sprite_rows = [&](const char* label) {
    std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (radicle_swf_placements_json(history.get(), &text, &size) != RADICLE_STATUS_OK) {
      ADD_FAILURE() << label << ": " << radicle_last_error();
      return std::size_t{0};
    }
    const std::string list(reinterpret_cast<const char*>(text), size);
    radicle_free_bytes(text);
    std::size_t rows = 0;
    for (std::size_t at = 0; (at = list.find("\"inside_sprite\":true", at)) != std::string::npos;
         ++at, ++rows) {
    }
    return rows;
  };
  EXPECT_EQ(sprite_rows("before"), 1u);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_insert_placement_keyframe(history.get(), 1, 0, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(sprite_rows("inserted"), 2u);

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(sprite_rows("undone"), 1u);

  EXPECT_EQ(radicle_swf_insert_placement_keyframe(history.get(), 1, 9, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_insert_placement_keyframe(history.get(), 0, 0, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_insert_placement_keyframe(history.get(), 99, 0, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfVerbs, HistoryAbcTextEditInsertsReplacesAndRefusesThroughOneVerb) {
  SwfHandle swf = open_abc_movie();
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result refused{};
  EXPECT_EQ(radicle_swf_history_abc_text_edit(nullptr, 0, 2, 0,
                                             reinterpret_cast<const std::uint8_t*>("nop"), 3, 1,
                                             &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(refused.changed, 0);

  const std::string nop = "nop";
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_history_abc_text_edit(
                history.get(), 0, 2, 0, reinterpret_cast<const std::uint8_t*>(nop.data()),
                nop.size(), 1, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  const auto disassemble = [&](const char* label) {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    if (radicle_swf_history_write_bytes(history.get(), &bytes, &size) != RADICLE_STATUS_OK) {
      ADD_FAILURE() << label << ": " << radicle_last_error();
      return std::string();
    }
    SwfHandle reopened = open(std::vector<std::byte>(reinterpret_cast<const std::byte*>(bytes),
                                                     reinterpret_cast<const std::byte*>(bytes) + size));
    radicle_free_bytes(bytes);
    if (reopened == nullptr) {
      ADD_FAILURE() << label << ": reopen failed";
      return std::string();
    }
    return abc_text(reopened.get());
  };
  EXPECT_NE(disassemble("inserted").find("      0 nop\n"), std::string::npos);

  const std::string pushtrue = "pushtrue";
  ASSERT_EQ(radicle_swf_history_abc_text_edit(
                history.get(), 0, 2, 0, reinterpret_cast<const std::uint8_t*>(pushtrue.data()),
                pushtrue.size(), 0, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string replaced = disassemble("replaced");
  EXPECT_NE(replaced.find("      0 pushtrue\n"), std::string::npos) << replaced;
  EXPECT_EQ(replaced.find("      0 nop\n"), std::string::npos) << replaced;

  const std::uint64_t revision = radicle_swf_history_revision(history.get());
  const std::string bad = "foo";
  EXPECT_EQ(radicle_swf_history_abc_text_edit(
                history.get(), 0, 2, 0, reinterpret_cast<const std::uint8_t*>(bad.data()),
                bad.size(), 1, &result),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_NE(std::string(radicle_last_error()), "");
  EXPECT_EQ(radicle_swf_history_revision(history.get()), revision);
  EXPECT_EQ(radicle_swf_history_abc_text_edit(
                history.get(), 9, 2, 0, reinterpret_cast<const std::uint8_t*>(nop.data()),
                nop.size(), 1, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfVerbs, ResolveFlatTagMapsPreorderPositions) {
  const std::vector<std::byte> bytes = placement_after_sprite_movie();
  const auto parsed =
      fo4::swf::read_swf(std::span<const std::byte>(bytes.data(), bytes.size()));
  ASSERT_TRUE(parsed.ok()) << fo4::swf::status_name(parsed.status);
  fo4::swf::SwfFlatTag addr{};
  ASSERT_TRUE(fo4::swf::resolve_flat_tag(parsed.file, 0, addr));
  EXPECT_EQ(addr.top, 0u);
  EXPECT_FALSE(addr.inside);
  ASSERT_TRUE(fo4::swf::resolve_flat_tag(parsed.file, 1, addr));
  EXPECT_EQ(addr.top, 0u);
  EXPECT_EQ(addr.child, 0u);
  EXPECT_TRUE(addr.inside);
  ASSERT_TRUE(fo4::swf::resolve_flat_tag(parsed.file, 4, addr));
  EXPECT_EQ(addr.top, 1u);
  EXPECT_FALSE(addr.inside);
  EXPECT_FALSE(fo4::swf::resolve_flat_tag(parsed.file, 99, addr));
}

// Constraint: flat 5 must move and flat 1 must stay, though the playhead list gives C the id A has authored.
TEST(CApiSwfVerbs, RootDepthEditOnAGotoMovieMovesOnlyTheNamedPlacement) {
  auto swf = open(goto_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_depth(history.get(), 5, 9, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  const std::string rows = placements_text(history.get());
  EXPECT_NE(rows.find(R"({"tag_index":5,"depth":9,"character_id":7,"inside_sprite":false})"),
            std::string::npos)
      << rows;
  EXPECT_NE(rows.find(R"({"tag_index":1,"depth":1,"character_id":5,"inside_sprite":false})"),
            std::string::npos)
      << rows;
}

// Constraint: the transform edit must rewrite C's MATRIX (flat 5) and leave A's (flat 1) at translate 20.
TEST(CApiSwfVerbs, RootTransformEditOnAGotoMovieRewritesOnlyTheNamedPlacement) {
  auto swf = open(goto_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_transform(history.get(), 5, 40, 60, 0x10000, 0x10000, 0, 0,
                                                &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_placement_matrix_fields matrix{};
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 5, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 40);
  EXPECT_EQ(matrix.translate_y, 60);
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 1, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 20);
  EXPECT_EQ(matrix.translate_y, 0);
}

// Constraint: a root keyframe adds one matrix-only move at the requested frame, carrying the placement's MATRIX.
TEST(CApiSwfVerbs, InsertKeyframeAddsAMoveAtTheRequestedRootFrame) {
  auto swf = open(swf_test::movie_of(
      2, {{26, swf_test::place_full(1, 5, swf_test::matrix_full(0x10000, 0x10000, 0, 0, 20, 0), {})},
          {1, {}},
          {1, {}}}));
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_insert_placement_keyframe(history.get(), 0, 1, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  const std::string rows = placements_text(history.get());
  EXPECT_NE(rows.find(R"({"tag_index":2,"depth":1,"character_id":null,"inside_sprite":false})"),
            std::string::npos)
      << rows;
  radicle_swf_placement_matrix_fields matrix{};
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 2, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 20);

  ASSERT_EQ(radicle_swf_history_undo(history.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(placements_text(history.get()).find("\"tag_index\":2"), std::string::npos);
}

// Constraint: a root placement without a MATRIX takes the payload write, so SetTransform adds one instead of refusing.
TEST(CApiSwfVerbs, SetTransformSplicesAMatrixIntoAMatrixLessRootPlacement) {
  auto swf = open(swf_test::movie_of(1, {{26, swf_test::place_full(2, 6, {}, {})}, {1, {}}}));
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_set_placement_transform(history.get(), 0, 40, 60, 0x10000, 0x10000, 0, 0,
                                                &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_placement_matrix_fields matrix{};
  ASSERT_EQ(radicle_swf_placement_matrix_at(history.get(), 0, &matrix), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(matrix.translate_x, 40);
  EXPECT_EQ(matrix.translate_y, 60);
  EXPECT_EQ(matrix.has_scale, 0);
}

// Constraint: a scale that cannot be encoded in MATRIX fields is named as such, not reported as a missing MATRIX.
TEST(CApiSwfVerbs, SetTransformNamesAScaleThatDoesNotFitTheMatrixFields) {
  auto swf = open(placement_after_sprite_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_history* raw = nullptr;
  ASSERT_EQ(radicle_swf_history_create(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const HistoryHandle history(raw);

  radicle_swf_plan_result result{};
  EXPECT_EQ(radicle_swf_set_placement_transform(history.get(), 1, 0, 0, 0x7FFFFFFF, 0x10000, 0, 0,
                                                &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not fit the MATRIX fields"),
            std::string::npos)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 0);
  EXPECT_EQ(radicle_swf_history_revision(history.get()), 0u);
}
