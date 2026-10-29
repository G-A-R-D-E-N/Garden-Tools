#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::size_t count_fonts(const fo4::swf::SwfFile& file, std::size_t& layout_fonts,
                        std::size_t& total_glyphs) {
  std::size_t fonts = 0;
  layout_fonts = 0;
  total_glyphs = 0;
  for (const fo4::swf::SwfTag& tag : file.tags) {
    if (!tag.font.has_value()) {
      continue;
    }
    ++fonts;
    if (tag.font->has_layout) {
      ++layout_fonts;
    }
    total_glyphs += tag.font->glyph_count;
  }
  return fonts;
}

TEST(SwfFontsReal, DecodesEmbeddedFontProgramsAndInterfaceFonts) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const std::filesystem::path data_dir(dir);
  const GameBuild build = detect_game_build(data_dir);
  record_game_build(build);
  const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const fo4::archive::Ba2GeneralArchive archive = fo4::archive::read_ba2_general_file(archive_path);
  std::size_t movies = 0;
  std::size_t fonts = 0;
  std::size_t layout_fonts = 0;
  std::size_t total_glyphs = 0;
  std::vector<std::string> decoded;
  for (const fo4::archive::Ba2GeneralEntry& entry : archive.entries) {
    if (!entry.path.ends_with("fonts_programs.swf") && !entry.path.ends_with("fonts_en.swf")) {
      continue;
    }
    const fo4::swf::SwfReadResult read =
        fo4::swf::read_swf(fo4::archive::extract_ba2_general_entry(entry));
    if (!read.ok()) {
      continue;
    }
    ++movies;
    std::size_t movie_layout = 0;
    std::size_t movie_glyphs = 0;
    const std::size_t movie_fonts = count_fonts(read.file, movie_layout, movie_glyphs);
    fonts += movie_fonts;
    layout_fonts += movie_layout;
    total_glyphs += movie_glyphs;
    decoded.push_back(entry.path + " fonts=" + std::to_string(movie_fonts) + " glyphs=" +
                      std::to_string(movie_glyphs));
    for (const fo4::swf::SwfTag& tag : read.file.tags) {
      if (tag.font.has_value() && tag.font->has_layout && !tag.font->advances.empty()) {
        EXPECT_EQ(tag.font->advances.size(), tag.font->glyph_count);
        ASSERT_FALSE(tag.font->glyph_codes.empty());
      }
    }
  }
  RecordProperty("font_movies", std::to_string(movies));
  RecordProperty("fonts", std::to_string(fonts));
  RecordProperty("layout_fonts", std::to_string(layout_fonts));
  RecordProperty("total_glyphs", std::to_string(total_glyphs));
  std::string summary;
  for (const std::string& line : decoded) {
    summary += line + "; ";
  }
  RecordProperty("decoded", summary.substr(0, 2000));
  ASSERT_GT(movies, 0u) << "no font movies in " << archive_path.string();
  EXPECT_GT(fonts, 0u) << summary;
  EXPECT_GT(layout_fonts, 0u) << summary;
  EXPECT_GT(total_glyphs, 100u) << summary;
}

}  // namespace
