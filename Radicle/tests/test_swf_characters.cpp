#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_characters.hpp"
#include "swf_character_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using fo4::swf::SwfCharacter;
using fo4::swf::SwfCharacterKind;
using fo4::swf::SwfCharacterModel;
using swf_character_test::TagPayload;
using swf_character_test::every_kind_movie;
using swf_character_test::movie_of;
using swf_character_test::symbol_payload;
using swf_character_test::symbol_text;

SwfCharacterModel read_model(const std::vector<std::byte>& bytes, fo4::swf::SwfFile& file,
                             std::string& message) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  file = result.file;
  SwfCharacterModel model;
  EXPECT_TRUE(fo4::swf::build_character_model(file, model, message)) << message;
  return model;
}

const SwfCharacter* find(const SwfCharacterModel& model, std::uint16_t id) {
  return fo4::swf::find_character(model, id);
}

TEST(SwfCharacters, CollectsEveryCharacterKindFromOneMovie) {
  auto file = fo4::swf::SwfFile{};
  std::string message;
  const auto model = read_model(every_kind_movie(), file, message);
  ASSERT_EQ(model.characters.size(), 13u);
  ASSERT_FALSE(model.refused);

  const auto* shape = find(model, 5);
  ASSERT_NE(shape, nullptr);
  EXPECT_EQ(shape->kind, SwfCharacterKind::shape);
  EXPECT_EQ(shape->tag_index, 0u);
  EXPECT_TRUE(shape->has_bounds);
  EXPECT_EQ(shape->bounds.x_min, 0);
  EXPECT_EQ(shape->bounds.x_max, 2000);
  EXPECT_EQ(shape->bounds.y_min, 0);
  EXPECT_EQ(shape->bounds.y_max, 1000);
  EXPECT_EQ(shape->name, "Game.Shape");

  const auto* morph = find(model, 12);
  ASSERT_NE(morph, nullptr);
  EXPECT_EQ(morph->kind, SwfCharacterKind::morph);
  EXPECT_EQ(morph->tag_index, 1u);
  EXPECT_TRUE(morph->has_bounds);
  EXPECT_EQ(morph->bounds.x_max, 800);
  EXPECT_EQ(morph->name, "Game.Morph");

  const auto* lossless = find(model, 3);
  ASSERT_NE(lossless, nullptr);
  EXPECT_EQ(lossless->kind, SwfCharacterKind::bitmap);
  EXPECT_EQ(lossless->tag_index, 2u);
  EXPECT_TRUE(lossless->has_bitmap_size);
  EXPECT_EQ(lossless->bitmap_width, 4);
  EXPECT_EQ(lossless->bitmap_height, 3);
  EXPECT_FALSE(lossless->has_bounds);

  const auto* jpeg = find(model, 4);
  ASSERT_NE(jpeg, nullptr);
  EXPECT_EQ(jpeg->kind, SwfCharacterKind::bitmap);
  EXPECT_FALSE(jpeg->has_bitmap_size);

  const auto* text = find(model, 9);
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->kind, SwfCharacterKind::text);
  EXPECT_TRUE(text->has_bounds);
  EXPECT_EQ(text->bounds.x_max, 600);

  const auto* edit = find(model, 11);
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->kind, SwfCharacterKind::edit_text);
  EXPECT_TRUE(edit->has_bounds);
  EXPECT_EQ(edit->bounds.y_max, 200);

  const auto* font = find(model, 13);
  ASSERT_NE(font, nullptr);
  EXPECT_EQ(font->kind, SwfCharacterKind::font);
  EXPECT_FALSE(font->has_bounds);

  const auto* sprite = find(model, 14);
  ASSERT_NE(sprite, nullptr);
  EXPECT_EQ(sprite->kind, SwfCharacterKind::sprite);
  EXPECT_EQ(sprite->tag_index, 7u);
  EXPECT_EQ(sprite->name, "ExportSprite");

  const auto* button = find(model, 15);
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->kind, SwfCharacterKind::button);

  const auto* sound = find(model, 16);
  ASSERT_NE(sound, nullptr);
  EXPECT_EQ(sound->kind, SwfCharacterKind::sound);

  const auto* video = find(model, 17);
  ASSERT_NE(video, nullptr);
  EXPECT_EQ(video->kind, SwfCharacterKind::video);

  const auto* imported = find(model, 20);
  ASSERT_NE(imported, nullptr);
  EXPECT_EQ(imported->kind, SwfCharacterKind::imported);
  EXPECT_EQ(imported->tag_index, 11u);
  EXPECT_EQ(imported->imported_from, "Shared.swf");
  EXPECT_EQ(imported->imported_name, "FontA");
  EXPECT_TRUE(imported->name.empty());

  const auto* second_import = find(model, 21);
  ASSERT_NE(second_import, nullptr);
  EXPECT_EQ(second_import->imported_name, "IconB");
}

TEST(SwfCharacters, SortsCharactersByCharacterId) {
  auto file = fo4::swf::SwfFile{};
  std::string message;
  const auto model = read_model(every_kind_movie(), file, message);
  ASSERT_FALSE(model.characters.empty());
  for (std::size_t index = 1; index < model.characters.size(); ++index) {
    EXPECT_LT(model.characters[index - 1].character_id, model.characters[index].character_id);
  }
  EXPECT_EQ(model.characters.front().character_id, 3u);
  EXPECT_EQ(model.characters.back().character_id, 21u);
}

TEST(SwfCharacters, FallsBackToTheExportNameWhenNoClassNamesTheId) {
  const auto source = movie_of({
      {46, swf_art_test::morph_payload(false)},
      {56, symbol_payload({{12, symbol_text("ExportMorph")}})},
      {1, {}},
  });
  auto file = fo4::swf::SwfFile{};
  std::string message;
  const auto model = read_model(source, file, message);
  const auto* morph = find(model, 12);
  ASSERT_NE(morph, nullptr);
  EXPECT_EQ(morph->name, "ExportMorph");
}

TEST(SwfCharacters, RefusesADuplicateCharacterId) {
  const auto result = fo4::swf::read_swf(swf_character_test::duplicate_movie());
  ASSERT_TRUE(result.ok());
  SwfCharacterModel model;
  std::string message;
  EXPECT_FALSE(fo4::swf::build_character_model(result.file, model, message));
  EXPECT_NE(message.find("defined twice"), std::string::npos) << message;
  EXPECT_TRUE(model.refused);
  EXPECT_TRUE(model.characters.empty());
}

TEST(SwfCharacters, NamesNothingForAnEntryThatDefinesNoCharacter) {
  const auto result = fo4::swf::read_swf(swf_character_test::unnamed_symbol_movie());
  ASSERT_TRUE(result.ok());
  SwfCharacterModel model;
  std::string message;
  EXPECT_TRUE(fo4::swf::build_character_model(result.file, model, message)) << message;
  EXPECT_FALSE(model.refused);
  ASSERT_EQ(model.characters.size(), 1u);
  EXPECT_EQ(model.characters.front().character_id, 3u);
  EXPECT_TRUE(model.characters.front().name.empty());
  EXPECT_EQ(find(model, 0), nullptr);
  EXPECT_EQ(find(model, 20), nullptr);
  EXPECT_EQ(find(model, 99), nullptr);
}

TEST(SwfCharacters, RefusesACharacterTagWithoutItsLeadingId) {
  const auto result = fo4::swf::read_swf(swf_character_test::truncated_character_movie());
  ASSERT_TRUE(result.ok());
  SwfCharacterModel model;
  std::string message;
  EXPECT_FALSE(fo4::swf::build_character_model(result.file, model, message));
  EXPECT_NE(message.find("character id"), std::string::npos) << message;
}

}  // namespace
