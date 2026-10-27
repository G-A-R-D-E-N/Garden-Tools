#include <fo4/avm2/display.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using fo4::avm2::DisplayObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Runtime;
using fo4::avm2::SnapshotEntryKind;
using fo4::avm2::StaticTextObject;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

const std::vector<std::byte> kFontPayload = bytes_of({
    0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x48, 0x69,
});

const std::vector<std::byte> kTwoRecordPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x02, 0x08,
    0x88, 0x01, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00,
    0x80, 0x01, 0x40, 0x00,
    0x00,
});

const std::vector<std::byte> kNoFontPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x02, 0x08,
    0x80, 0x01, 0x40, 0x00,
    0x00,
});

const std::vector<std::byte> kOutsideFontPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x02, 0x08,
    0x88, 0x01, 0x00, 0x40, 0x00, 0x01, 0x80, 0x00,
    0x00,
});

const std::vector<std::byte> kZeroGlyphFirstPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x02, 0x08,
    0x81, 0x00, 0x00, 0x00,
    0x88, 0x01, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00,
    0x80, 0x01, 0x40, 0x00,
    0x00,
});

const std::vector<std::byte> kRgbaTwoGlyphPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x02, 0x08,
    0x8C, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x40, 0x00, 0x02, 0x00, 0x10, 0x00,
    0x00,
});

const std::vector<std::byte> kFontPayloadXY = bytes_of({
    0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x58, 0x59,
});

std::vector<std::byte> static_text_movie(const std::vector<std::byte>& text_payload,
                                         const std::vector<std::byte>& font = kFontPayload,
                                         std::uint16_t text_tag = fo4::swf::tag::kDefineText) {
  swf_test::SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kDefineFont2, font, false);
  builder.tag(text_tag, text_payload, false);
  builder.tag(0);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

fo4::swf::SwfFile placed_static_text_file(const std::vector<std::byte>& text_payload,
                                          const std::vector<std::byte>& font = kFontPayload,
                                          std::uint16_t text_tag = fo4::swf::tag::kDefineText) {
  const fo4::swf::SwfReadResult result =
      fo4::swf::read_swf(static_text_movie(text_payload, font, text_tag));
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  fo4::swf::SwfFile file = result.file;
  fo4::swf::SwfTag place;
  place.code = fo4::swf::tag::kPlaceObject2;
  fo4::swf::SwfPlacement placement;
  placement.depth = std::uint16_t{1};
  placement.character_id = std::uint16_t{7};
  place.placement = placement;
  file.tags.push_back(place);
  fo4::swf::SwfTag show;
  show.code = fo4::swf::tag::kShowFrame;
  file.tags.push_back(show);
  return file;
}

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  Harness() { runtime.link(); }
};

TEST(Avm2StaticText, DefineTextPlacesAStaticTextThatReadsTheFontCodes) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kTwoRecordPayload));
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  DisplayObject& placed = *movie->root()->children()[0];
  ASSERT_NE(placed.display_class(), nullptr);
  EXPECT_EQ(placed.display_class()->name, "flash.text::StaticText");
  const auto* text = dynamic_cast<const StaticTextObject*>(&placed);
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text(), "Hi");

  Multiname multiname;
  multiname.name = "text";
  Value out;
  ASSERT_TRUE(fo4::avm2::read_display_property(harness.interpreter, placed, multiname, out));
  ASSERT_TRUE(out.is_string());
  EXPECT_EQ(out.as_string(), "Hi");
}

TEST(Avm2StaticText, TextIsEmptyWhenNoFontIsSelected) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kNoFontPayload));
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const auto* text = dynamic_cast<const StaticTextObject*>(movie->root()->children()[0].get());
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text(), "");
}

TEST(Avm2StaticText, TextIsEmptyWhenAGlyphIsOutsideTheFontTable) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kOutsideFontPayload));
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const auto* text = dynamic_cast<const StaticTextObject*>(movie->root()->children()[0].get());
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text(), "");
}

TEST(Avm2StaticText, SnapshotKeepsStaticTextAsAShape) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kTwoRecordPayload));
  ASSERT_NE(movie, nullptr);
  const fo4::avm2::FrameSnapshot snapshot = movie->take_snapshot();
  ASSERT_EQ(snapshot.entries.size(), 2u);
  EXPECT_EQ(snapshot.entries[1].kind, SnapshotEntryKind::Shape);
  EXPECT_EQ(snapshot.entries[1].character_id, 7u);
}

TEST(Avm2StaticText, DefineText2ReadsGlyphsPastEachRgbaColour) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter,
      placed_static_text_file(kRgbaTwoGlyphPayload, kFontPayload, fo4::swf::tag::kDefineText2));
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const auto* text = dynamic_cast<const StaticTextObject*>(movie->root()->children()[0].get());
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text(), "Hi");
}

TEST(Avm2StaticText, ZeroGlyphRecordWithoutFontDoesNotBlankTheText) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kZeroGlyphFirstPayload));
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const auto* text = dynamic_cast<const StaticTextObject*>(movie->root()->children()[0].get());
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text(), "Hi");
}

TEST(Avm2StaticText, FontIdReusedByAnotherMovieReadsItsOwnFont) {
  Harness harness;
  const std::shared_ptr<SwfRuntime> first = SwfRuntime::create(
      harness.runtime, harness.interpreter, placed_static_text_file(kTwoRecordPayload));
  const std::shared_ptr<SwfRuntime> second = SwfRuntime::create(
      harness.runtime, harness.interpreter,
      placed_static_text_file(kTwoRecordPayload, kFontPayloadXY));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(first->root()->children().size(), 1u);
  ASSERT_EQ(second->root()->children().size(), 1u);
  const auto* first_text =
      dynamic_cast<const StaticTextObject*>(first->root()->children()[0].get());
  const auto* second_text =
      dynamic_cast<const StaticTextObject*>(second->root()->children()[0].get());
  ASSERT_NE(first_text, nullptr);
  ASSERT_NE(second_text, nullptr);
  EXPECT_EQ(first_text->text(), "Hi");
  EXPECT_EQ(second_text->text(), "XY");
}

}  // namespace
