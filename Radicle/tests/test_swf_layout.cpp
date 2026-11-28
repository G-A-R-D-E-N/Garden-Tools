#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_layout.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfCharacterKind;
using fo4::swf::SwfLayout;
using swf_test::SwfBuilder;
using swf_test::TagPayload;

fo4::swf::SwfFile parse(const std::vector<std::byte>& bytes) {
  auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << result.message;
  return std::move(result.file);
}

SwfLayout layout_of(const std::vector<std::byte>& bytes) {
  const auto file = parse(bytes);
  SwfLayout layout;
  std::string message;
  EXPECT_TRUE(fo4::swf::build_layout(file, layout, message)) << message;
  return layout;
}

std::string refusal_of(const std::vector<std::byte>& bytes) {
  const auto file = parse(bytes);
  SwfLayout layout;
  std::string message;
  EXPECT_FALSE(fo4::swf::build_layout(file, layout, message));
  EXPECT_TRUE(layout.states.empty());
  EXPECT_TRUE(layout.rows.empty());
  return message;
}

std::vector<std::byte> raw(std::initializer_list<std::uint8_t> values) {
  std::vector<std::byte> out;
  for (const std::uint8_t value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

std::vector<std::byte> identity() { return swf_test::matrix_full(0x10000, 0x10000, 0, 0, 0, 0); }

std::vector<std::byte> place(std::uint16_t depth, std::uint16_t character,
                             const std::vector<std::byte>& matrix, const std::string& name = {},
                             std::uint8_t extra_flags = 0) {
  SwfBuilder body;
  const auto flags = static_cast<std::uint8_t>((matrix.empty() ? 0x00 : 0x04) |
                                               (name.empty() ? 0x00 : 0x20) | 0x02 | extra_flags);
  body.u8(flags);
  body.u16(depth);
  body.u16(character);
  body.bytes(matrix);
  if (!name.empty()) {
    body.text(name);
  }
  return body.body;
}

// Constraint: PlaceObject3 with the visible flag set (flags3 0x20) and a hidden value of 0.
std::vector<std::byte> hidden_place3(std::uint16_t depth, std::uint16_t character,
                                     const std::string& name) {
  SwfBuilder body;
  body.u8(0x26);
  body.u8(0x20);
  body.u16(depth);
  body.u16(character);
  body.bytes(identity());
  body.text(name);
  body.u8(0);
  return body.body;
}

std::vector<std::byte> styled_edit_text() {
  SwfBuilder body;
  body.u16(11);
  body.rect(0, 400, 0, 200, 12);
  body.u8(0xE5);
  body.u8(0x60);
  body.u16(3);
  body.u16(240);
  body.u8(0x11);
  body.u8(0x22);
  body.u8(0x33);
  body.u8(0xFF);
  body.u8(2);
  body.u16(0);
  body.u16(0);
  body.u16(0);
  body.u16(0);
  body.text("v");
  body.text("Hi");
  return body.body;
}

// Constraint: DefineButton2 carries its ActionOffset at payload bytes 3 and 4, so a nonzero offset marks condition actions.
std::vector<std::byte> button(std::uint16_t action_offset) {
  SwfBuilder body;
  body.u16(20);
  body.u8(0);
  body.u16(action_offset);
  body.u8(0x01);
  body.u16(11);
  body.u16(1);
  body.bytes(swf_test::matrix_full(0x10000, 0x10000, 0, 0, 2000, 1000));
  body.u8(0);
  body.u8(0);
  return body.body;
}

// Constraint: DefineButton (tag 7) has no colour transform, so its action list follows the record terminator directly.
enum class ButtonAction { none, stop, goto_frame };

std::vector<std::byte> plain_button(ButtonAction action) {
  SwfBuilder body;
  body.u16(21);
  body.u8(0x01);
  body.u16(11);
  body.u16(1);
  body.bytes(swf_test::matrix_full(0x10000, 0x10000, 0, 0, 2000, 1000));
  body.u8(0);
  if (action == ButtonAction::stop) {
    body.u8(0x07);
  } else if (action == ButtonAction::goto_frame) {
    body.u8(0x81);
    body.u16(2);
    body.u16(0);
  }
  body.u8(0);
  return body.body;
}

// One labelled frame holding a named edit text, with the extra tags placed before its ShowFrame.
std::vector<std::byte> labelled_field_movie(const std::vector<TagPayload>& extra) {
  std::vector<TagPayload> tags = {{37, styled_edit_text()}, {43, swf_test::frame_label("Open")}};
  tags.insert(tags.end(), extra.begin(), extra.end());
  tags.push_back({26, place(1, 11, identity(), "Field")});
  tags.push_back({1, {}});
  return swf_test::movie_of(1, tags);
}

std::string shape_of(const SwfLayout& layout) {
  std::string out;
  for (const auto& font : layout.fonts) {
    out += "f" + std::to_string(font.font_id) + font.name + ";";
  }
  for (const auto& row : layout.rows) {
    out += row.path + ":" + std::to_string(row.character_id) + ":" +
           std::to_string(row.clip_frame_count) + ":" + std::to_string(row.rect.width) + ";";
  }
  return out;
}

}  // namespace

// Regression: the stage read in twips, the frame rate was not divided by 256, or a label was attached to the frame after it.
TEST(SwfLayout, ReportsTheStageFrameRateAndTheLabelOfEachFrame) {
  const auto movie = swf_test::movie_of(2, {{43, swf_test::frame_label("Open")}, {1, {}},
                                            {43, swf_test::frame_label("Closed")}, {1, {}}});
  const auto layout = layout_of(movie);
  EXPECT_DOUBLE_EQ(layout.stage.x, 0.0);
  EXPECT_DOUBLE_EQ(layout.stage.width, 550.0);
  EXPECT_DOUBLE_EQ(layout.stage.height, 400.0);
  EXPECT_DOUBLE_EQ(layout.frame_rate, 12.0);
  ASSERT_EQ(layout.states.size(), 2u);
  EXPECT_EQ(layout.states[0].label, "Open");
  EXPECT_EQ(layout.states[0].timeline_id, 0);
  EXPECT_EQ(layout.states[0].frame, 0u);
  EXPECT_EQ(layout.states[1].label, "Closed");
  EXPECT_EQ(layout.states[1].frame, 1u);
}

// Regression: a root timeline without labels exported no state, so a menu with no labels had nothing to show.
TEST(SwfLayout, UnlabelledRootGetsOneImplicitStateAtFrameZero) {
  const auto movie = swf_test::movie_of(1, {{37, styled_edit_text()}, {26, place(1, 11, identity(), "Field")}, {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states.size(), 1u);
  EXPECT_EQ(layout.states[0].label, "");
  EXPECT_EQ(layout.states[0].frame, 0u);
  ASSERT_EQ(layout.states[0].row_count, 1u);
  EXPECT_EQ(layout.rows[0].path, "Field");
}

// Regression: the composed rectangle, parent path or draw order of a named clip's child came out wrong.
TEST(SwfLayout, NamedRowsCarryComposedRectsParentPathsAndTextFields) {
  const auto cell = place(1, 11, swf_test::matrix_full(0x20000, 0x20000, 0, 0, 200, 100), "Cell");
  const auto sprite = swf_test::sprite_tag(14, 1, {{26, cell}, {1, {}}});
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()},
          {43, swf_test::frame_label("Open")},
          {39, sprite},
          {26, place(2, 14, identity(), "Art")},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states.size(), 1u);
  ASSERT_EQ(layout.states[0].row_count, 2u);
  const auto& art = layout.rows[layout.states[0].first_row];
  const auto& field = layout.rows[layout.states[0].first_row + 1];

  EXPECT_EQ(art.path, "Art");
  EXPECT_EQ(art.parent_path, "");
  EXPECT_EQ(art.kind, SwfCharacterKind::sprite);
  EXPECT_EQ(art.clip_frame_count, 1u);
  EXPECT_TRUE(art.visible);
  ASSERT_TRUE(art.has_rect);
  EXPECT_DOUBLE_EQ(art.rect.x, 10.0);
  EXPECT_DOUBLE_EQ(art.rect.y, 5.0);
  EXPECT_DOUBLE_EQ(art.rect.width, 40.0);
  EXPECT_DOUBLE_EQ(art.rect.height, 20.0);

  EXPECT_EQ(field.path, "Art/Cell");
  EXPECT_EQ(field.parent_path, "Art");
  EXPECT_EQ(field.depth, 1);
  EXPECT_EQ(field.character_id, 11);
  EXPECT_EQ(field.kind, SwfCharacterKind::edit_text);
  EXPECT_DOUBLE_EQ(field.world_transform.scale_x, 2.0);
  EXPECT_DOUBLE_EQ(field.world_transform.translate_x, 10.0);
  EXPECT_DOUBLE_EQ(field.world_transform.translate_y, 5.0);
  ASSERT_TRUE(field.has_rect);
  EXPECT_DOUBLE_EQ(field.rect.x, 10.0);
  EXPECT_DOUBLE_EQ(field.rect.width, 40.0);
  EXPECT_DOUBLE_EQ(field.rect.height, 20.0);

  ASSERT_TRUE(field.text.has_value());
  ASSERT_TRUE(field.text->font_id.has_value());
  EXPECT_EQ(*field.text->font_id, 3);
  ASSERT_TRUE(field.text->font_height.has_value());
  EXPECT_DOUBLE_EQ(*field.text->font_height, 12.0);
  ASSERT_TRUE(field.text->color.has_value());
  EXPECT_EQ(*field.text->color, 0xFF112233u);
  EXPECT_TRUE(field.text->has_layout);
  EXPECT_EQ(field.text->align, 2);
  EXPECT_TRUE(field.text->multiline);
  EXPECT_TRUE(field.text->word_wrap);
  EXPECT_TRUE(field.text->auto_size);
  EXPECT_FALSE(field.text->html);
  EXPECT_FALSE(field.text->border);
  EXPECT_EQ(field.text->variable, "v");
  EXPECT_EQ(field.text->text, "Hi");
  EXPECT_FALSE(field.text->has_font_class);
  EXPECT_TRUE(field.text->font_class.empty());
}

// Constraint: DefineEditText holds a font class name instead of a FontID when flags2 bit 7 is set, followed by the font height; has_font_class follows the flag, not the name.
std::vector<std::byte> class_edit_text(std::uint16_t id, const std::string& font_class) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, 400, 0, 200, 12);
  body.u8(0x80);
  body.u8(0x80);
  body.text(font_class);
  body.u16(240);
  body.text("v");
  body.text("Hi");
  return body.body;
}

TEST(SwfLayout, ATextFieldThatNamesItsFontByClassReportsTheClassAndNoFontId) {
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()},
          {37, class_edit_text(12, "TextFont")},
          {37, class_edit_text(13, "")},
          {26, place(1, 11, identity(), "Plain")},
          {26, place(2, 12, identity(), "Classed")},
          {26, place(3, 13, identity(), "Empty")},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.rows.size(), 3u);
  ASSERT_TRUE(layout.rows[0].text.has_value());
  EXPECT_FALSE(layout.rows[0].text->has_font_class);
  EXPECT_TRUE(layout.rows[0].text->font_id.has_value());
  const auto& classed = layout.rows[1].text;
  ASSERT_TRUE(classed.has_value());
  EXPECT_TRUE(classed->has_font_class);
  EXPECT_EQ(classed->font_class, "TextFont");
  EXPECT_FALSE(classed->font_id.has_value());
  ASSERT_TRUE(classed->font_height.has_value());
  EXPECT_DOUBLE_EQ(*classed->font_height, 12.0);
  EXPECT_EQ(classed->variable, "v");
  EXPECT_EQ(classed->text, "Hi");
  const auto& empty = layout.rows[2].text;
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->has_font_class);
  EXPECT_TRUE(empty->font_class.empty());
  EXPECT_FALSE(empty->font_id.has_value());
}

// Regression: a hidden ancestor left its children visible, and a fully hidden clip reported a rectangle.
TEST(SwfLayout, AHiddenAncestorHidesItsChildrenAndAddsNoRectangle) {
  const auto sprite = swf_test::sprite_tag(14, 1, {{26, place(1, 11, identity(), "Cell")}, {1, {}}});
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()}, {43, swf_test::frame_label("Open")}, {39, sprite},
          {70, hidden_place3(2, 14, "Art")}, {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states.size(), 1u);
  ASSERT_EQ(layout.states[0].row_count, 2u);
  const auto& art = layout.rows[layout.states[0].first_row];
  const auto& cell = layout.rows[layout.states[0].first_row + 1];
  EXPECT_FALSE(art.visible);
  EXPECT_FALSE(art.has_rect);
  EXPECT_EQ(cell.path, "Art/Cell");
  EXPECT_FALSE(cell.visible);
}

// Regression: a script that jumps or moves objects was exported as if the timeline were authoritative.
TEST(SwfLayout, ScriptBeyondStopMarksEveryRowAndAStopOnlyScriptDoesNot) {
  const auto plain = layout_of(labelled_field_movie({}));
  EXPECT_FALSE(plain.scripted);
  EXPECT_EQ(plain.rows[0].flags, 0);

  const auto stop = layout_of(labelled_field_movie({{12, raw({0x07, 0x00})}}));
  EXPECT_FALSE(stop.scripted);
  EXPECT_FALSE(layout_of(labelled_field_movie({{12, raw({0x00})}})).scripted);

  const auto jump = layout_of(labelled_field_movie({{12, raw({0x81, 0x02, 0x00, 0x00, 0x00, 0x00})}}));
  EXPECT_TRUE(jump.scripted);
  ASSERT_FALSE(jump.rows.empty());
  EXPECT_EQ(jump.rows[0].flags & fo4::swf::kLayoutScripted, fo4::swf::kLayoutScripted);

  EXPECT_TRUE(layout_of(labelled_field_movie({{72, raw({0x00})}})).scripted);
  EXPECT_TRUE(layout_of(labelled_field_movie({{26, place(2, 11, identity(), "Clip", 0x80)}})).scripted);
  EXPECT_TRUE(layout_of(labelled_field_movie({{34, button(4)}})).scripted);
  EXPECT_FALSE(layout_of(labelled_field_movie({{34, button(0)}})).scripted);
}

// Regression: AS3 and AVM1 timeline script were one flag, so a UI could not tell a frame-script movie it can still lay out from an ActionScript 3 program that owns the stage.
TEST(SwfLayout, AnAs3MovieAndALegacyJumpAreFlaggedSeparatelyAndScriptedStaysTheirUnion) {
  constexpr std::uint8_t kScripted = fo4::swf::kLayoutScripted;
  constexpr std::uint8_t kAs3 = fo4::swf::kLayoutHasAs3;
  constexpr std::uint8_t kLegacy = fo4::swf::kLayoutLegacyMoves;
  const std::vector<std::byte> jump = raw({0x81, 0x02, 0x00, 0x00, 0x00, 0x00});

  const auto neither = layout_of(labelled_field_movie({{12, raw({0x07, 0x00})}}));
  EXPECT_FALSE(neither.has_as3);
  EXPECT_FALSE(neither.legacy_moves);
  EXPECT_EQ(neither.rows[0].flags, 0);

  const auto as3 = layout_of(labelled_field_movie({{72, raw({0x00})}}));
  EXPECT_TRUE(as3.scripted);
  EXPECT_TRUE(as3.has_as3);
  EXPECT_FALSE(as3.legacy_moves);
  EXPECT_EQ(as3.rows[0].flags, kScripted | kAs3);

  const auto legacy = layout_of(labelled_field_movie({{12, jump}}));
  EXPECT_TRUE(legacy.scripted);
  EXPECT_FALSE(legacy.has_as3);
  EXPECT_TRUE(legacy.legacy_moves);
  EXPECT_EQ(legacy.rows[0].flags, kScripted | kLegacy);

  const auto both = layout_of(labelled_field_movie({{12, jump}, {72, raw({0x00})}}));
  EXPECT_TRUE(both.scripted);
  EXPECT_TRUE(both.has_as3);
  EXPECT_TRUE(both.legacy_moves);
  EXPECT_EQ(both.rows[0].flags, kScripted | kAs3 | kLegacy);

  EXPECT_TRUE(layout_of(labelled_field_movie({{34, button(4)}})).legacy_moves);
  EXPECT_TRUE(layout_of(labelled_field_movie({{26, place(2, 11, identity(), "Clip", 0x80)}})).legacy_moves);
}

// Regression: two rows that share a path were exported as if the name identified one instance.
TEST(SwfLayout, NamesUsedTwiceInOneStateAreBothFlagged) {
  const auto movie = labelled_field_movie({{26, place(2, 11, identity(), "Field")}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states[0].row_count, 2u);
  for (std::size_t row = 0; row < 2; ++row) {
    EXPECT_EQ(layout.rows[layout.states[0].first_row + row].path, "Field");
    EXPECT_EQ(layout.rows[layout.states[0].first_row + row].flags & fo4::swf::kLayoutDuplicatePath,
              fo4::swf::kLayoutDuplicatePath);
  }
}

// Regression: a button reported no rectangle, so its hit area and art could not be placed.
TEST(SwfLayout, ButtonRowsUseTheRectangleOfTheirUpStateRecords) {
  const auto movie = labelled_field_movie({{34, button(0)}, {26, place(3, 20, identity(), "Btn")}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states[0].row_count, 2u);
  const auto& btn = layout.rows[layout.states[0].first_row + 1];
  EXPECT_EQ(btn.path, "Btn");
  EXPECT_EQ(btn.kind, SwfCharacterKind::button);
  ASSERT_TRUE(btn.has_rect);
  EXPECT_DOUBLE_EQ(btn.rect.x, 100.0);
  EXPECT_DOUBLE_EQ(btn.rect.y, 50.0);
  EXPECT_DOUBLE_EQ(btn.rect.width, 20.0);
  EXPECT_DOUBLE_EQ(btn.rect.height, 10.0);
}

// Regression: a sprite's own labels were dropped, or its states were filed under the root timeline.
TEST(SwfLayout, SpriteLabelsAreStatesOfTheirOwnTimeline) {
  const auto sprite = swf_test::sprite_tag(
      14, 2, {{26, place(1, 11, identity(), "Cell")}, {43, swf_test::frame_label("inner")}, {1, {}},
              {1, {}}});
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()}, {43, swf_test::frame_label("Open")}, {39, sprite},
          {26, place(2, 14, identity(), "Art")}, {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states.size(), 2u);
  EXPECT_EQ(layout.states[0].label, "Open");
  EXPECT_EQ(layout.states[0].timeline_id, 0);
  EXPECT_EQ(layout.states[1].label, "inner");
  EXPECT_EQ(layout.states[1].timeline_id, 14);
  EXPECT_EQ(layout.states[1].frame, 0u);
  ASSERT_EQ(layout.states[1].row_count, 1u);
  EXPECT_EQ(layout.rows[layout.states[1].first_row].path, "Cell");
  EXPECT_EQ(layout.rows[layout.states[1].first_row].parent_path, "");
}

// Regression: an undefined character or an unplaceable move exported a partial layout instead of a refusal with its message.
TEST(SwfLayout, RefusesAnUndefinedCharacterAndAnUnplaceableMovie) {
  const auto undefined = swf_test::movie_of(1, {{43, swf_test::frame_label("Open")},
                                                {26, place(1, 99, identity(), "Ghost")}, {1, {}}});
  EXPECT_NE(refusal_of(undefined).find("99"), std::string::npos);

  SwfBuilder move;
  move.u8(0x05);
  move.u16(3);
  move.push_bits(0, 1);
  move.push_bits(0, 1);
  move.push_bits(11, 5);
  move.push_bits(0, 11);
  move.push_bits(0, 11);
  move.align();
  const auto unplaceable = swf_test::movie_of(1, {{43, swf_test::frame_label("Open")}, {26, move.body}, {1, {}}});
  EXPECT_NE(refusal_of(unplaceable).find("depth"), std::string::npos);
}

// Regression: a DefineButton (tag 7) action list was never read, so a handler that jumped the timeline left the movie unflagged, while a stop-only handler must stay neutral like a stop-only frame script.
TEST(SwfLayout, PlainButtonsFlagTheMovieOnlyWhenTheirActionListCanMoveTheTimeline) {
  EXPECT_FALSE(layout_of(labelled_field_movie({{7, plain_button(ButtonAction::none)}})).scripted);
  EXPECT_FALSE(layout_of(labelled_field_movie({{7, plain_button(ButtonAction::stop)}})).scripted);
  EXPECT_TRUE(layout_of(labelled_field_movie({{7, plain_button(ButtonAction::goto_frame)}})).scripted);
}

// Regression: a named child of an unnamed clip was exported under a parent path that matched no row, with the rule undocumented.
TEST(SwfLayout, UnnamedClipsHaveNoRowButTheirNamedChildrenKeepASyntheticParent) {
  const auto sprite = swf_test::sprite_tag(14, 1, {{26, place(1, 11, identity(), "Cell")}, {1, {}}});
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()}, {43, swf_test::frame_label("Open")}, {39, sprite},
          {26, place(2, 14, identity())}, {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states.size(), 1u);
  ASSERT_EQ(layout.states[0].row_count, 1u);
  const auto& cell = layout.rows[layout.states[0].first_row];
  EXPECT_EQ(cell.path, "#14/Cell");
  EXPECT_EQ(cell.parent_path, "#14");
}

// Regression: a label past the last ShowFrame raised a refusal or a flag, where its state should simply hold no rows.
TEST(SwfLayout, ALabelPastTheLastFrameIsAnEmptyStateWithNoFlag) {
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()}, {26, place(1, 11, identity(), "Field")}, {1, {}},
          {43, swf_test::frame_label("End")}});
  const auto layout = layout_of(movie);
  EXPECT_FALSE(layout.scripted);
  ASSERT_EQ(layout.states.size(), 1u);
  EXPECT_EQ(layout.states[0].label, "End");
  EXPECT_EQ(layout.states[0].frame, 1u);
  EXPECT_EQ(layout.states[0].row_count, 0u);
}

// Constraint: a font entry must hold only what its tag holds, and a text row's font id must resolve to an entry.
TEST(SwfLayout, TheLayoutListsEachDefinedFontWithOnlyWhatItsTagHolds) {
  const auto movie = swf_test::movie_of(
      1, {{91, swf_test::font4_payload(9, 0x05, "Cff")},
          {10, swf_test::one_glyph_font()},
          {48, swf_test::font2_payload(5, "Plain")},
          {75, swf_test::font3_payload(3, "Face")},
          {37, styled_edit_text()},
          {26, place(1, 11, identity(), "Field")},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 4u);
  ASSERT_TRUE(layout.rows[0].text.has_value());
  ASSERT_TRUE(layout.rows[0].text->font_id.has_value());
  EXPECT_EQ(*layout.rows[0].text->font_id, layout.fonts[0].font_id);

  const auto& face = layout.fonts[0];
  EXPECT_EQ(face.font_id, 3);
  EXPECT_EQ(face.version, 3);
  EXPECT_EQ(face.name, "Face");
  EXPECT_TRUE(face.has_style);
  EXPECT_TRUE(face.bold);
  EXPECT_FALSE(face.italic);
  EXPECT_TRUE(face.has_language);
  EXPECT_EQ(face.language, 1);
  EXPECT_TRUE(face.has_glyph_count);
  EXPECT_EQ(face.glyph_count, 1u);
  EXPECT_TRUE(face.has_outlines);
  EXPECT_TRUE(face.has_metrics);
  EXPECT_EQ(face.ascent, 800);
  EXPECT_EQ(face.descent, 200);
  EXPECT_EQ(face.leading, 40);
  EXPECT_EQ(face.units_per_em, 20480u);
  EXPECT_TRUE(face.has_codes);
  EXPECT_EQ(face.codes, std::vector<std::uint16_t>({0x41}));
  EXPECT_FALSE(face.has_font_name);
  EXPECT_TRUE(face.font_name.empty());
  EXPECT_FALSE(face.has_font_class);
  EXPECT_TRUE(face.font_class.empty());

  const auto& plain = layout.fonts[1];
  EXPECT_EQ(plain.font_id, 5);
  EXPECT_EQ(plain.version, 2);
  EXPECT_EQ(plain.name, "Plain");
  EXPECT_TRUE(plain.has_style);
  EXPECT_FALSE(plain.bold);
  EXPECT_TRUE(plain.italic);
  EXPECT_TRUE(plain.has_glyph_count);
  EXPECT_EQ(plain.glyph_count, 0u);
  EXPECT_FALSE(plain.has_outlines);
  EXPECT_FALSE(plain.has_metrics);
  EXPECT_TRUE(plain.has_codes);
  EXPECT_TRUE(plain.codes.empty());

  const auto& legacy = layout.fonts[2];
  EXPECT_EQ(legacy.font_id, 7);
  EXPECT_EQ(legacy.version, 1);
  EXPECT_EQ(legacy.name, "");
  EXPECT_FALSE(legacy.has_style);
  EXPECT_FALSE(legacy.has_language);
  EXPECT_TRUE(legacy.has_glyph_count);
  EXPECT_EQ(legacy.glyph_count, 1u);
  EXPECT_TRUE(legacy.has_outlines);
  EXPECT_FALSE(legacy.has_codes);
  EXPECT_TRUE(legacy.codes.empty());

  const auto& cff = layout.fonts[3];
  EXPECT_EQ(cff.font_id, 9);
  EXPECT_EQ(cff.version, 4);
  EXPECT_EQ(cff.name, "Cff");
  EXPECT_TRUE(cff.has_style);
  EXPECT_TRUE(cff.bold);
  EXPECT_FALSE(cff.italic);
  EXPECT_FALSE(cff.has_language);
  EXPECT_FALSE(cff.has_glyph_count);
  EXPECT_TRUE(cff.has_outlines);
  EXPECT_FALSE(cff.has_metrics);
  EXPECT_FALSE(cff.has_codes);

  EXPECT_TRUE(layout_of(labelled_field_movie({})).fonts.empty());
}

// Constraint: a DefineFont4 name must end with a null byte, or the layout is refused.
TEST(SwfLayout, ADefineFont4WithoutANameTerminatorIsRefused) {
  const auto movie = swf_test::movie_of(1, {{91, raw({9, 0, 0x05, 'a', 'b'})}, {1, {}}});
  EXPECT_NE(refusal_of(movie).find("did not decode"), std::string::npos);
}

// Constraint: DefineFont2 carries UI8 codes unless its wide flag is set, DefineFont3 always UI16; each table is in glyph index order.
TEST(SwfLayout, ADefineFont2OrFont3CodeTableIsCopiedInGlyphOrderWhateverItsWidth) {
  const auto movie = swf_test::movie_of(
      1, {{75, swf_test::font3_payload(3, "Face")},
          {48, swf_test::font2_codes_payload(4, "Narrow", {0x41, 0xFF, 0x20}, false)},
          {48, swf_test::font2_codes_payload(5, "Wide", {0x20AC, 0x41, 0x1234}, true)},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 3u);
  EXPECT_EQ(layout.fonts[0].codes, std::vector<std::uint16_t>({0x41}));
  ASSERT_TRUE(layout.fonts[1].has_codes);
  EXPECT_EQ(layout.fonts[1].glyph_count, 3u);
  EXPECT_EQ(layout.fonts[1].codes, std::vector<std::uint16_t>({0x41, 0xFF, 0x20}));
  ASSERT_TRUE(layout.fonts[2].has_codes);
  EXPECT_EQ(layout.fonts[2].codes, std::vector<std::uint16_t>({0x20AC, 0x41, 0x1234}));
}

// Constraint: a DefineFont (version 1) font has no table of its own; DefineFontInfo and DefineFontInfo2 supply it, and a font info tag for any other version is ignored.
TEST(SwfLayout, ADefineFontInfoGivesAVersionOneFontItsCodeTableAndOtherVersionsKeepTheirOwn) {
  auto second = swf_test::one_glyph_font();
  second[0] = std::byte{8};
  const auto movie = swf_test::movie_of(
      1, {{10, swf_test::one_glyph_font()},
          {13, swf_test::font_info_payload(7, "Old", {0x61}, false, 0x06)},
          {10, second},
          {62, swf_test::font_info2_payload(8, "Newer", 5, {0x263A}, 0x02)},
          {75, swf_test::font3_payload(3, "Face")},
          {13, swf_test::font_info_payload(3, "Other", {0x7A}, true, 0x04)},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 3u);
  EXPECT_EQ(layout.fonts[0].font_id, 3);
  EXPECT_EQ(layout.fonts[0].codes, std::vector<std::uint16_t>({0x41}));
  EXPECT_EQ(layout.fonts[0].name, "Face");
  EXPECT_TRUE(layout.fonts[0].bold);
  EXPECT_FALSE(layout.fonts[0].italic);
  EXPECT_EQ(layout.fonts[0].language, 1);
  ASSERT_TRUE(layout.fonts[1].has_codes);
  EXPECT_EQ(layout.fonts[1].codes, std::vector<std::uint16_t>({0x61}));
  EXPECT_EQ(layout.fonts[1].name, "Old");
  EXPECT_TRUE(layout.fonts[1].has_style);
  EXPECT_TRUE(layout.fonts[1].bold);
  EXPECT_TRUE(layout.fonts[1].italic);
  EXPECT_FALSE(layout.fonts[1].has_language);
  EXPECT_EQ(layout.fonts[1].language, 0);
  ASSERT_TRUE(layout.fonts[2].has_codes);
  EXPECT_EQ(layout.fonts[2].codes, std::vector<std::uint16_t>({0x263A}));
  EXPECT_EQ(layout.fonts[2].name, "Newer");
  EXPECT_TRUE(layout.fonts[2].has_style);
  EXPECT_TRUE(layout.fonts[2].bold);
  EXPECT_FALSE(layout.fonts[2].italic);
  EXPECT_TRUE(layout.fonts[2].has_language);
  EXPECT_EQ(layout.fonts[2].language, 5);
}

// Constraint: only the italic and bold bits of a font info flag byte are read, a version 1 font with no info tag keeps no name or style, and DefineFontName stays separate from the info name.
TEST(SwfLayout, AFontInfoFlagByteGivesStyleFromItsItalicAndBoldBitsOnly) {
  auto second = swf_test::one_glyph_font();
  second[0] = std::byte{8};
  const auto movie = swf_test::movie_of(
      1, {{10, swf_test::one_glyph_font()},
          {13, swf_test::font_info_payload(7, "Face", {0x61}, true, 0x38)},
          {88, swf_test::font_name_payload(7, "Display", "(c)")},
          {10, second},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 2u);
  EXPECT_EQ(layout.fonts[0].name, "Face");
  EXPECT_TRUE(layout.fonts[0].has_style);
  EXPECT_FALSE(layout.fonts[0].bold);
  EXPECT_FALSE(layout.fonts[0].italic);
  EXPECT_FALSE(layout.fonts[0].has_language);
  EXPECT_TRUE(layout.fonts[0].has_font_name);
  EXPECT_EQ(layout.fonts[0].font_name, "Display");
  EXPECT_EQ(layout.fonts[1].name, "");
  EXPECT_FALSE(layout.fonts[1].has_style);
  EXPECT_FALSE(layout.fonts[1].has_language);
  EXPECT_FALSE(layout.fonts[1].has_codes);
}

// Constraint: a font info table that cannot be read, or does not hold one code per glyph, refuses the layout instead of guessing the mapping.
TEST(SwfLayout, AFontInfoTableThatIsUnreadableOrOfTheWrongLengthIsRefused) {
  const auto count = swf_test::movie_of(
      1, {{10, swf_test::one_glyph_font()},
          {13, swf_test::font_info_payload(7, "Old", {0x61, 0x62}, false)},
          {1, {}}});
  EXPECT_NE(refusal_of(count).find("glyph count"), std::string::npos);
  const auto odd = swf_test::movie_of(
      1, {{10, swf_test::one_glyph_font()}, {13, raw({7, 0, 0, 0x01, 0x61})}, {1, {}}});
  EXPECT_NE(refusal_of(odd).find("did not decode"), std::string::npos);
  const auto cut = swf_test::movie_of(1, {{62, raw({7, 0, 3, 'a'})}, {1, {}}});
  EXPECT_NE(refusal_of(cut).find("did not decode"), std::string::npos);
}

// Constraint: DefineFontName is reported beside the font tag's own name and never replaces it; a font it does not name has no flag.
TEST(SwfLayout, ADefineFontNameIsReportedSeparatelyFromTheFontTagsOwnName) {
  const auto movie = swf_test::movie_of(
      1, {{75, swf_test::font3_payload(3, "Face")},
          {88, swf_test::font_name_payload(3, "Display Face", "(c) Maker")},
          {48, swf_test::font2_payload(5, "Plain")},
          {88, swf_test::font_name_payload(5, "", "")},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 2u);
  EXPECT_EQ(layout.fonts[0].name, "Face");
  EXPECT_TRUE(layout.fonts[0].has_font_name);
  EXPECT_EQ(layout.fonts[0].font_name, "Display Face");
  EXPECT_EQ(layout.fonts[0].font_copyright, "(c) Maker");
  EXPECT_TRUE(layout.fonts[1].has_font_name);
  EXPECT_TRUE(layout.fonts[1].font_name.empty());
  const auto none = layout_of(swf_test::movie_of(1, {{75, swf_test::font3_payload(3, "Face")}, {1, {}}}));
  EXPECT_FALSE(none.fonts[0].has_font_name);
  const auto cut = swf_test::movie_of(1, {{88, raw({3, 0, 'a', 0, 'b'})}, {1, {}}});
  EXPECT_NE(refusal_of(cut).find("did not decode"), std::string::npos);
}

// Constraint: font_class is the SymbolClass entry whose id is the font's character id, the first entry when an id repeats, and unset for a font no entry names.
TEST(SwfLayout, AFontCharacterLinkedBySymbolClassReportsItsClass) {
  const auto movie = swf_test::movie_of(
      1, {{75, swf_test::font3_payload(3, "Face")},
          {48, swf_test::font2_payload(5, "Plain")},
          {76, swf_test::symbol_class_payload({{3, "FaceClass"}, {3, "Other"}, {14, "Menu"}})},
          {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.fonts.size(), 2u);
  EXPECT_TRUE(layout.fonts[0].has_font_class);
  EXPECT_EQ(layout.fonts[0].font_class, "FaceClass");
  EXPECT_FALSE(layout.fonts[1].has_font_class);
  EXPECT_TRUE(layout.fonts[1].font_class.empty());
}

// Constraint: a sprite row must carry the full span of its timeline and every other row must carry none.
TEST(SwfLayout, ASpriteRowCarriesItsFullFrameSpanAndOtherRowsCarryNone) {
  const auto sprite = swf_test::sprite_tag(
      14, 3, {{26, place(1, 11, identity(), "Cell")}, {1, {}}, {1, {}}, {1, {}}});
  const auto movie = swf_test::movie_of(
      1, {{37, styled_edit_text()}, {43, swf_test::frame_label("Open")}, {39, sprite},
          {26, place(2, 14, identity(), "Art")}, {1, {}}});
  const auto layout = layout_of(movie);
  ASSERT_EQ(layout.states[0].row_count, 2u);
  const auto& art = layout.rows[layout.states[0].first_row];
  const auto& cell = layout.rows[layout.states[0].first_row + 1];
  EXPECT_EQ(art.kind, SwfCharacterKind::sprite);
  EXPECT_EQ(art.clip_frame_count, 3u);
  EXPECT_EQ(art.clip_first_frame, 0u);
  EXPECT_EQ(art.clip_last_frame, 2u);
  EXPECT_EQ(cell.clip_frame_count, 0u);
  EXPECT_EQ(cell.clip_first_frame, 0u);
  EXPECT_EQ(cell.clip_last_frame, 0u);
}

// Constraint: a CWS movie must lay out identically to its FWS twin.
TEST(SwfLayout, ACwsMovieLaysOutIdenticallyToItsFwsTwin) {
  const auto sprite = swf_test::sprite_tag(14, 1, {{26, place(1, 11, identity(), "Cell")}, {1, {}}});
  const auto fws = swf_test::movie_of(
      1, {{75, swf_test::font3_payload(3, "Face")}, {37, styled_edit_text()}, {43, swf_test::frame_label("Open")},
          {39, sprite}, {26, place(2, 14, identity(), "Art")}, {1, {}}});
  const auto cws = fo4::swf::write_swf(parse(fws), fo4::swf::SwfCompression::zlib);
  ASSERT_GE(cws.size(), 3u);
  EXPECT_EQ(static_cast<char>(cws[0]), 'C');
  EXPECT_EQ(static_cast<char>(cws[1]), 'W');
  EXPECT_EQ(static_cast<char>(cws[2]), 'S');
  const auto plain = layout_of(fws);
  const auto packed = layout_of(cws);
  ASSERT_EQ(packed.fonts.size(), 1u);
  EXPECT_EQ(packed.fonts[0].name, "Face");
  EXPECT_EQ(packed.states.size(), plain.states.size());
  EXPECT_EQ(shape_of(packed), shape_of(plain));
  EXPECT_FALSE(shape_of(packed).empty());
}
