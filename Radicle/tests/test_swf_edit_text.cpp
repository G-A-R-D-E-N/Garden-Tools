#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_text_edit.hpp"
#include "swf_character_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using swf_character_test::movie_of;
using swf_test::SwfBuilder;

SwfBuilder& rect(SwfBuilder& body, std::int32_t x_min, std::int32_t x_max, std::int32_t y_min,
                 std::int32_t y_max) {
  body.rect(x_min, x_max, y_min, y_max, 12);
  return body;
}

// Constraint: the fixture flags every optional field so one movie pins the whole tag-37 field order.
std::vector<std::byte> edit_text_payload() {
  SwfBuilder body;
  body.u16(11);
  rect(body, 0, 400, 0, 200);
  body.u8(0x87);  // HasFont | HasColor | HasMaxLength | HasText
  body.u8(0xA0);  // HasFontClass | HasLayout
  body.u16(13);
  body.text("Arial");
  body.u16(200);
  body.u8(10);
  body.u8(20);
  body.u8(30);
  body.u8(255);
  body.u16(64);
  body.u8(1);
  body.u16(100);
  body.u16(200);
  body.u16(50);
  body.u16(static_cast<std::uint32_t>(static_cast<std::uint16_t>(-20)));
  body.text("variable");
  body.text("hello");
  return body.body;
}

std::vector<std::byte> edit_text_movie() {
  return movie_of({
      {37, edit_text_payload()},
      {1, {}},
  });
}

std::vector<std::byte> shape_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {1, {}},
  });
}

std::size_t text_offset(const std::vector<std::byte>& payload) {
  const std::string needle = "hello";
  for (std::size_t index = 0; index + needle.size() <= payload.size(); ++index) {
    bool match = true;
    for (std::size_t at = 0; at < needle.size(); ++at) {
      if (payload[index + at] != static_cast<std::byte>(needle[at])) {
        match = false;
        break;
      }
    }
    if (match) {
      return index;
    }
  }
  return payload.size();
}

}  // namespace

TEST(SwfEditText, ReadsEveryDeclaredFieldIncludingTheLayout) {
  const auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& tag = result.file.tags[0];
  ASSERT_EQ(tag.code, fo4::swf::tag::kDefineEditText);
  ASSERT_TRUE(tag.edit_text.has_value());
  const auto& text = *tag.edit_text;
  EXPECT_EQ(text.character_id, 11);
  EXPECT_EQ(text.bounds.x_min, 0);
  EXPECT_EQ(text.bounds.x_max, 400);
  EXPECT_EQ(text.bounds.y_min, 0);
  EXPECT_EQ(text.bounds.y_max, 200);
  EXPECT_EQ(text.flags1, 0x87);
  EXPECT_EQ(text.flags2, 0xA0);
  ASSERT_TRUE(text.font_id.has_value());
  EXPECT_EQ(*text.font_id, 13);
  EXPECT_EQ(text.font_class, "Arial");
  ASSERT_TRUE(text.font_height_twips.has_value());
  EXPECT_EQ(*text.font_height_twips, 200);
  ASSERT_TRUE(text.color.has_value());
  EXPECT_EQ(*text.color, 0xFF0A141Eu);
  ASSERT_TRUE(text.max_length.has_value());
  EXPECT_EQ(*text.max_length, 64);
  ASSERT_TRUE(text.layout.has_value());
  EXPECT_EQ(text.layout->align, 1);
  EXPECT_EQ(text.layout->left_margin, 100);
  EXPECT_EQ(text.layout->right_margin, 200);
  EXPECT_EQ(text.layout->indent, 50);
  EXPECT_EQ(text.layout->leading, -20);
  EXPECT_EQ(text.variable, "variable");
  EXPECT_EQ(text.text, "hello");
}

TEST(SwfEditText, AnUnsetEditReEncodesThePayloadByteIdentically) {
  const auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok());
  const auto& tag = result.file.tags[0];
  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::replace_edit_text(tag, {}, out, message)) << message;
  EXPECT_EQ(out, tag.payload);
}

TEST(SwfEditText, ReplacingTheTextKeepsEveryEarlierByteAndDecodesBack) {
  auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok());
  const auto original = result.file.tags[0].payload;
  const std::size_t offset = text_offset(original);
  ASSERT_LT(offset, original.size());

  fo4::swf::SwfEditTextEdit edit;
  edit.text = "world";
  std::string message;
  ASSERT_TRUE(fo4::swf::apply_edit_text(result.file, 0, edit, message)) << message;

  const auto& edited = result.file.tags[0];
  ASSERT_TRUE(edited.edit_text.has_value());
  EXPECT_EQ(edited.edit_text->text, "world");
  EXPECT_EQ(edited.edit_text->bounds.x_max, 400);
  ASSERT_GE(edited.payload.size(), offset);
  for (std::size_t index = 0; index < offset; ++index) {
    ASSERT_EQ(edited.payload[index], original[index]) << "byte " << index;
  }
  std::vector<std::byte> expected_tail;
  for (const char ch : std::string("world")) {
    expected_tail.push_back(static_cast<std::byte>(ch));
  }
  expected_tail.push_back(std::byte{0});
  ASSERT_EQ(edited.payload.size(), offset + expected_tail.size());
  for (std::size_t index = 0; index < expected_tail.size(); ++index) {
    ASSERT_EQ(edited.payload[offset + index], expected_tail[index]);
  }
}

TEST(SwfEditText, RecolouringChangesOnlyTheFourColourBytes) {
  auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok());
  const auto original = result.file.tags[0].payload;

  fo4::swf::SwfEditTextEdit edit;
  edit.color = 0x80112233u;
  std::string message;
  ASSERT_TRUE(fo4::swf::apply_edit_text(result.file, 0, edit, message)) << message;

  const auto& edited = result.file.tags[0];
  ASSERT_TRUE(edited.edit_text.has_value());
  EXPECT_EQ(edited.edit_text->color, 0x80112233u);
  EXPECT_EQ(edited.edit_text->text, "hello");
  ASSERT_EQ(edited.payload.size(), original.size());
  std::size_t differences = 0;
  for (std::size_t index = 0; index < original.size(); ++index) {
    if (edited.payload[index] != original[index]) {
      ++differences;
    }
  }
  EXPECT_EQ(differences, 4u);
}

TEST(SwfEditText, ReplacingTheBoundsReEncodesTheRectAndKeepsEveryOtherField) {
  auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok());

  fo4::swf::SwfEditTextEdit edit;
  edit.bounds = fo4::swf::SwfRect{0, 1000, 0, 500};
  std::string message;
  ASSERT_TRUE(fo4::swf::apply_edit_text(result.file, 0, edit, message)) << message;

  const auto& text = *result.file.tags[0].edit_text;
  EXPECT_EQ(text.bounds.x_max, 1000);
  EXPECT_EQ(text.bounds.y_max, 500);
  EXPECT_EQ(text.color, 0xFF0A141Eu);
  EXPECT_EQ(text.font_class, "Arial");
  EXPECT_EQ(text.max_length, 64);
  ASSERT_TRUE(text.layout.has_value());
  EXPECT_EQ(text.layout->leading, -20);
  EXPECT_EQ(text.variable, "variable");
  EXPECT_EQ(text.text, "hello");
}

TEST(SwfEditText, RefusesATagThatIsNotAnEditText) {
  const auto result = fo4::swf::read_swf(shape_movie());
  ASSERT_TRUE(result.ok());
  std::vector<std::byte> out;
  std::string message;
  EXPECT_FALSE(fo4::swf::replace_edit_text(result.file.tags[0], {}, out, message));
  EXPECT_NE(message.find("not a decoded DefineEditText"), std::string::npos) << message;
}

TEST(SwfEditText, RefusesBoundsThatDoNotFitARect) {
  const auto result = fo4::swf::read_swf(edit_text_movie());
  ASSERT_TRUE(result.ok());
  fo4::swf::SwfEditTextEdit edit;
  edit.bounds = fo4::swf::SwfRect{std::numeric_limits<std::int32_t>::min(),
                                  std::numeric_limits<std::int32_t>::max(), 0, 0};
  std::vector<std::byte> out;
  std::string message;
  EXPECT_FALSE(fo4::swf::replace_edit_text(result.file.tags[0], edit, out, message));
  EXPECT_NE(message.find("RECT"), std::string::npos) << message;
}
