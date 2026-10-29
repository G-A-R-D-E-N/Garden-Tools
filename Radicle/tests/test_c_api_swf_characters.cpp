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

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

TEST(CApiSwfCharacters, ListsEveryCharacterAndResolvesOneById) {
  const auto swf = open(swf_character_test::small_movie());
  ASSERT_NE(swf, nullptr);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_character_count(swf.get(), &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 2u);

  radicle_swf_character character{};
  ASSERT_EQ(radicle_swf_character_at(swf.get(), 0, &character), RADICLE_STATUS_OK);
  EXPECT_EQ(character.character_id, 5);
  EXPECT_EQ(character.kind, RADICLE_SWF_CHARACTER_SHAPE);
  EXPECT_EQ(character.has_bounds, 1);
  EXPECT_EQ(character.has_bitmap_size, 0);
  EXPECT_EQ(character.tag_index, 0u);
  EXPECT_EQ(character.bounds.x_max, 2000);
  ASSERT_NE(character.name, nullptr);
  EXPECT_EQ(std::string(character.name), "Game.Shape");
  ASSERT_NE(character.imported_from, nullptr);
  EXPECT_TRUE(std::string(character.imported_from).empty());

  ASSERT_EQ(radicle_swf_character_at(swf.get(), 1, &character), RADICLE_STATUS_OK);
  EXPECT_EQ(character.character_id, 11);
  EXPECT_EQ(character.kind, RADICLE_SWF_CHARACTER_EDIT_TEXT);
  EXPECT_EQ(character.bounds.y_max, 200);

  EXPECT_EQ(radicle_swf_character_at(swf.get(), 2, &character), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_character_at(swf.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_swf_character_by_id(swf.get(), 11, &character), RADICLE_STATUS_OK);
  EXPECT_EQ(character.character_id, 11);
  EXPECT_EQ(radicle_swf_character_by_id(swf.get(), 99, &character), RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("not defined"), std::string::npos)
      << radicle_last_error();
}

TEST(CApiSwfCharacters, ReportsImportedCharactersWithTheirSourceMovie) {
  const auto swf = open(swf_character_test::every_kind_movie());
  ASSERT_NE(swf, nullptr);
  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_character_count(swf.get(), &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 13u);

  radicle_swf_character character{};
  ASSERT_EQ(radicle_swf_character_by_id(swf.get(), 20, &character), RADICLE_STATUS_OK);
  EXPECT_EQ(character.kind, RADICLE_SWF_CHARACTER_IMPORTED);
  EXPECT_EQ(character.tag_index, 13u);
  ASSERT_NE(character.imported_from, nullptr);
  EXPECT_EQ(std::string(character.imported_from), "Shared.swf");
  ASSERT_NE(character.imported_name, nullptr);
  EXPECT_EQ(std::string(character.imported_name), "FontA");

  ASSERT_EQ(radicle_swf_character_by_id(swf.get(), 34, &character), RADICLE_STATUS_NOT_FOUND);
}

TEST(CApiSwfCharacters, RefusesADuplicateIdButLeavesTheOtherGettersUsable) {
  const auto swf = open(swf_character_test::duplicate_movie());
  ASSERT_NE(swf, nullptr);

  std::size_t count = 1;
  EXPECT_EQ(radicle_swf_character_count(swf.get(), &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(count, 0u);
  EXPECT_NE(std::string(radicle_last_error()).find("defined twice"), std::string::npos)
      << radicle_last_error();
  EXPECT_EQ(radicle_swf_tag_count(swf.get()), 4u);
}

TEST(CApiSwfCharacters, NamesNothingForANameTableEntryWithNoDefinition) {
  const auto swf = open(swf_character_test::unnamed_symbol_movie());
  ASSERT_NE(swf, nullptr);

  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_character_count(swf.get(), &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 1u);
}

// Regression: the SVG export must emit an <svg> document with a <path> for a filled shape, NUL-terminated past *out_size.
TEST(CApiSwfCharacters, ExportsAFilledShapeAsSvg) {
  const auto swf = open(swf_character_test::small_movie());
  ASSERT_NE(swf, nullptr);

  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_swf_shape_svg(swf.get(), 5, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(text, nullptr);
  const std::string svg(reinterpret_cast<const char*>(text), size);
  EXPECT_TRUE(svg.starts_with("<svg")) << svg;
  EXPECT_NE(svg.find("<path"), std::string::npos) << svg;
  EXPECT_EQ(text[size], 0);
  radicle_free_bytes(text);
}

// Regression: a non-shape id, an undefined id and null arguments must each refuse with every output cleared, never leaving a stale pointer or size.
TEST(CApiSwfCharacters, RefusesNonShapesAndNullArgumentsWithOutputsCleared) {
  const auto swf = open(swf_character_test::small_movie());
  ASSERT_NE(swf, nullptr);

  std::uint8_t sentinel = 0;
  std::uint8_t* text = &sentinel;
  std::size_t size = 7;
  EXPECT_EQ(radicle_swf_shape_svg(swf.get(), 11, &text, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_NE(std::string(radicle_last_error()).find("not a decoded shape"), std::string::npos)
      << radicle_last_error();

  text = &sentinel;
  size = 7;
  EXPECT_EQ(radicle_swf_shape_svg(swf.get(), 99, &text, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  size = 7;
  EXPECT_EQ(radicle_swf_shape_svg(swf.get(), 5, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);
  text = &sentinel;
  size = 7;
  EXPECT_EQ(radicle_swf_shape_svg(nullptr, 5, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  EXPECT_EQ(radicle_swf_shape_svg(swf.get(), 5, &text, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
}

TEST(CApiSwfCharacters, SymbolCountCountsTheSymbolClassEntriesOnly) {
  const auto swf = open(swf_character_test::every_kind_movie());
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_symbol_count(swf.get()), 2u);
  const auto small = open(swf_character_test::small_movie());
  ASSERT_NE(small, nullptr);
  EXPECT_EQ(radicle_swf_symbol_count(small.get()), 1u);
  EXPECT_EQ(radicle_swf_symbol_count(nullptr), 0u);
}

TEST(CApiSwfCharacters, ExportCountCountsTheExportAssetsEntriesOnly) {
  const auto swf = open(swf_character_test::every_kind_movie());
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_export_count(swf.get()), 1u);
  const auto small = open(swf_character_test::small_movie());
  ASSERT_NE(small, nullptr);
  EXPECT_EQ(radicle_swf_export_count(small.get()), 0u);
  EXPECT_EQ(radicle_swf_export_count(nullptr), 0u);
}

TEST(CApiSwfCharacters, ReadsEachGradientStopAndRefusesAnOutOfRangeOne) {
  const auto swf = open(swf_character_test::small_movie());
  ASSERT_NE(swf, nullptr);

  radicle_swf_gradient_stop stop{};
  ASSERT_EQ(radicle_swf_shape_fill_stop_at(swf.get(), 0, 1, 0, &stop), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(stop.ratio, 0);
  EXPECT_EQ(stop.color, 0xFFFF0000u);
  ASSERT_EQ(radicle_swf_shape_fill_stop_at(swf.get(), 0, 1, 1, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.ratio, 255);
  EXPECT_EQ(stop.color, 0x800000FFu);

  stop.ratio = 9;
  stop.color = 1;
  EXPECT_EQ(radicle_swf_shape_fill_stop_at(swf.get(), 0, 1, 2, &stop), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(stop.ratio, 0);
  EXPECT_EQ(stop.color, 0u);
  EXPECT_EQ(radicle_swf_shape_fill_stop_at(swf.get(), 0, 0, 0, &stop), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_fill_stop_at(nullptr, 0, 1, 0, &stop), RADICLE_STATUS_INVALID_ARGUMENT);
}

}  // namespace
