#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct MovieStats {
  std::size_t total{0};
  std::size_t movies{0};
  std::size_t gfx{0};
  std::size_t compressed{0};
  std::size_t action_script3{0};
  std::size_t lzma{0};
  std::size_t symbols{0};
  std::size_t abc_blocks{0};
  std::size_t shape_movies{0};
  std::size_t bitmap_movies{0};
  std::size_t font_movies{0};
  std::size_t text_movies{0};
  std::size_t sound_movies{0};
  std::size_t exporter_info_movies{0};
  std::size_t external_image_movies{0};
  std::size_t font_texture_movies{0};
  std::size_t shape_count{0};
  std::size_t morph_count{0};
  std::size_t morph2_count{0};
  std::size_t bitmap_count{0};
  std::size_t jpeg_count{0};
  std::size_t jpeg_tables_count{0};
  std::size_t art_failures{0};
  std::map<std::string, std::size_t> versions;

  MovieStats& operator+=(const MovieStats& other) {
    total += other.total;
    movies += other.movies;
    gfx += other.gfx;
    compressed += other.compressed;
    action_script3 += other.action_script3;
    lzma += other.lzma;
    symbols += other.symbols;
    abc_blocks += other.abc_blocks;
    shape_movies += other.shape_movies;
    bitmap_movies += other.bitmap_movies;
    font_movies += other.font_movies;
    text_movies += other.text_movies;
    sound_movies += other.sound_movies;
    exporter_info_movies += other.exporter_info_movies;
    external_image_movies += other.external_image_movies;
    font_texture_movies += other.font_texture_movies;
    shape_count += other.shape_count;
    morph_count += other.morph_count;
    morph2_count += other.morph2_count;
    bitmap_count += other.bitmap_count;
    jpeg_count += other.jpeg_count;
    jpeg_tables_count += other.jpeg_tables_count;
    art_failures += other.art_failures;
    for (const auto& [key, count] : other.versions) {
      versions[key] += count;
    }
    return *this;
  }
};

void check_art(const fo4::swf::SwfTag& tag, MovieStats& stats) {
  switch (tag.code) {
    case fo4::swf::tag::kDefineShape:
    case fo4::swf::tag::kDefineShape2:
    case fo4::swf::tag::kDefineShape3:
    case fo4::swf::tag::kDefineShape4:
      ++stats.shape_count;
      if (!tag.shape) ++stats.art_failures;
      break;
    case fo4::swf::tag::kDefineMorphShape:
    case fo4::swf::tag::kDefineMorphShape2:
      ++stats.morph_count;
      stats.morph2_count += tag.code == fo4::swf::tag::kDefineMorphShape2 ? 1u : 0u;
      if (!tag.morph_shape) ++stats.art_failures;
      break;
    case fo4::swf::tag::kDefineBitsLossless:
    case fo4::swf::tag::kDefineBitsLossless2:
      ++stats.bitmap_count;
      if (!tag.bitmap) ++stats.art_failures;
      break;
    case fo4::swf::tag::kDefineBits:
    case fo4::swf::tag::kDefineBitsJpeg2:
    case fo4::swf::tag::kDefineBitsJpeg3:
    case fo4::swf::tag::kDefineBitsJpeg4:
      ++stats.jpeg_count;
      if (!tag.jpeg) ++stats.art_failures;
      break;
    case fo4::swf::tag::kJpegTables:
      ++stats.jpeg_tables_count;
      if (!tag.jpeg_tables) ++stats.art_failures;
      break;
    default:
      break;
  }
  for (const auto& child : tag.children) {
    check_art(child, stats);
  }
}

void check_movie(const std::string& name, std::span<const std::byte> bytes, MovieStats& stats) {
  ++stats.total;
  const auto result = fo4::swf::read_swf(bytes);
  if (result.status == fo4::swf::SwfStatus::unsupported_compression) {
    ++stats.lzma;
    return;
  }
  EXPECT_TRUE(result.ok()) << name << ": " << fo4::swf::status_name(result.status) << " "
                           << result.message;
  if (!result.ok()) {
    return;
  }
  ++stats.movies;
  const auto summary = fo4::swf::summarize(result.file);
  stats.gfx += summary.is_gfx ? 1u : 0u;
  stats.compressed += summary.compressed ? 1u : 0u;
  stats.action_script3 += summary.action_script3 ? 1u : 0u;
  stats.symbols += summary.symbol_classes.size();
  stats.abc_blocks += summary.abc_blocks.size();
  stats.shape_movies += summary.shape_tags != 0 ? 1u : 0u;
  stats.bitmap_movies += summary.bitmap_tags != 0 ? 1u : 0u;
  stats.font_movies += summary.font_tags != 0 ? 1u : 0u;
  stats.text_movies += summary.text_tags != 0 ? 1u : 0u;
  stats.sound_movies += summary.sound_tags != 0 ? 1u : 0u;
  stats.exporter_info_movies += summary.exporter_info_tags != 0 ? 1u : 0u;
  stats.external_image_movies += summary.external_image_tags != 0 ? 1u : 0u;
  stats.font_texture_movies += summary.font_texture_tags != 0 ? 1u : 0u;
  ++stats.versions["v" + std::to_string(summary.version) + (summary.is_gfx ? "-gfx" : "-swf") +
                    (summary.compressed ? "-zlib" : "-raw")];
  for (const auto& tag : result.file.tags) {
    check_art(tag, stats);
  }
  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  EXPECT_EQ(fo4::swf::write_swf(result.file), original) << name;
}

MovieStats scan_archive(const std::filesystem::path& path) {
  MovieStats stats;
  const auto archive = fo4::archive::read_ba2_general_file(path);
  for (const auto& entry : archive.entries) {
    if (!is_movie(entry.path)) {
      continue;
    }
    check_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), stats);
  }
  return stats;
}

void record_archive_art(const std::string& key, const MovieStats& stats) {
  testing::Test::RecordProperty(key + "_movies", std::to_string(stats.movies));
  testing::Test::RecordProperty(key + "_shape_tags", std::to_string(stats.shape_count));
  testing::Test::RecordProperty(key + "_morph_tags", std::to_string(stats.morph_count));
  testing::Test::RecordProperty(key + "_morph2_tags", std::to_string(stats.morph2_count));
  testing::Test::RecordProperty(key + "_lossless_tags", std::to_string(stats.bitmap_count));
  testing::Test::RecordProperty(key + "_art_failures", std::to_string(stats.art_failures));
}

std::string version_histogram(const MovieStats& stats) {
  std::string text;
  for (const auto& [key, count] : stats.versions) {
    if (!text.empty()) {
      text += ' ';
    }
    text += key + "=" + std::to_string(count);
  }
  return text;
}

}  // namespace

TEST(SwfRealData, ParsesEveryShippedMovieAndRoundTripsByteIdentically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::filesystem::path data_dir(dir);

  MovieStats stats;
  const auto interface_dir = data_dir / "Interface";
  std::error_code error;
  if (std::filesystem::is_directory(interface_dir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(interface_dir, error)) {
      if (!entry.is_regular_file(error)) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (!is_movie(name)) {
        continue;
      }
      check_movie(name, read_file(entry.path()), stats);
    }
  }

  if (stats.movies == 0 && stats.lzma == 0) {
    const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
    if (!std::filesystem::exists(archive_path)) {
      GTEST_SKIP() << "no Interface movies and no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
    }
    const auto interface = scan_archive(archive_path);
    record_archive_art("interface", interface);
    stats += interface;
  }

  struct DlcArchive {
    const char* key;
    const char* file;
  };
  const DlcArchive dlc_archives[] = {
      {"dlccoast", "DLCCoast - Main.ba2"},
      {"dlcnukaworld", "DLCNukaWorld - Main.ba2"},
      {"dlcrobot", "DLCRobot - Main.ba2"},
  };
  for (const auto& dlc : dlc_archives) {
    const auto path = data_dir / dlc.file;
    if (!std::filesystem::exists(path)) {
      testing::Test::RecordProperty(std::string(dlc.key) + "_movies", std::string{"absent"});
      continue;
    }
    const auto local = scan_archive(path);
    record_archive_art(dlc.key, local);
    EXPECT_EQ(local.art_failures, 0u) << dlc.file;
    EXPECT_GT(local.movies + local.lzma, 0u) << dlc.file;
    stats += local;
  }

  EXPECT_GT(stats.movies, 0u);
  EXPECT_EQ(stats.movies + stats.lzma, stats.total);
  // Regression: every shipped shape, morph, bitmap and JPEG tag must decode.
  EXPECT_EQ(stats.art_failures, 0u);
  EXPECT_GT(stats.shape_count, 0u);
  EXPECT_GT(stats.bitmap_count, 0u);
  RecordProperty("shape_tags", std::to_string(stats.shape_count));
  RecordProperty("morph_tags", std::to_string(stats.morph_count));
  RecordProperty("morph2_tags", std::to_string(stats.morph2_count));
  RecordProperty("lossless_tags", std::to_string(stats.bitmap_count));
  RecordProperty("jpeg_tags", std::to_string(stats.jpeg_count));
  RecordProperty("jpeg_tables_tags", std::to_string(stats.jpeg_tables_count));
  RecordProperty("art_failures", std::to_string(stats.art_failures));
  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("gfx_movies", std::to_string(stats.gfx));
  RecordProperty("compressed_movies", std::to_string(stats.compressed));
  RecordProperty("action_script3_movies", std::to_string(stats.action_script3));
  RecordProperty("lzma_movies", std::to_string(stats.lzma));
  RecordProperty("symbol_classes", std::to_string(stats.symbols));
  RecordProperty("abc_blocks", std::to_string(stats.abc_blocks));
  RecordProperty("shape_movies", std::to_string(stats.shape_movies));
  RecordProperty("bitmap_movies", std::to_string(stats.bitmap_movies));
  RecordProperty("font_movies", std::to_string(stats.font_movies));
  RecordProperty("text_movies", std::to_string(stats.text_movies));
  RecordProperty("sound_movies", std::to_string(stats.sound_movies));
  RecordProperty("exporter_info_movies", std::to_string(stats.exporter_info_movies));
  RecordProperty("external_image_movies", std::to_string(stats.external_image_movies));
  RecordProperty("font_texture_movies", std::to_string(stats.font_texture_movies));
  RecordProperty("version_histogram", version_histogram(stats));
}
