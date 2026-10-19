#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

std::string lower_extension(const std::string& path) {
  std::string extension = std::filesystem::path(path).extension().string();
  for (auto& ch : extension) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return extension;
}

bool is_movie(const std::string& path) {
  const auto extension = lower_extension(path);
  return extension == ".swf" || extension == ".gfx";
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<std::byte> bytes;
  char buffer[4096];
  while (in.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || in.gcount() > 0) {
    const auto count = in.gcount();
    for (std::streamsize index = 0; index < count; ++index) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[index])));
    }
  }
  return bytes;
}

struct MovieStats {
  std::size_t total{0};
  std::size_t movies{0};
  std::size_t gfx{0};
  std::size_t compressed{0};
  std::size_t action_script3{0};
  std::size_t lzma{0};
  std::size_t symbols{0};
  std::size_t abc_blocks{0};
  std::map<std::string, std::size_t> versions;
};

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
  ++stats.versions["v" + std::to_string(summary.version) + (summary.is_gfx ? "-gfx" : "-swf") +
                    (summary.compressed ? "-zlib" : "-raw")];
  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  EXPECT_EQ(fo4::swf::write_swf(result.file), original) << name;
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

TEST(SwfRealData, ParsesEveryInterfaceMovieAndRoundTripsByteIdentically) {
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
    const auto archive = fo4::archive::read_ba2_general_file(archive_path);
    for (const auto& entry : archive.entries) {
      if (!is_movie(entry.path)) {
        continue;
      }
      const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
      check_movie(entry.path, bytes, stats);
    }
  }

  EXPECT_GT(stats.movies, 0u);
  EXPECT_EQ(stats.movies + stats.lzma, stats.total);
  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("gfx_movies", std::to_string(stats.gfx));
  RecordProperty("compressed_movies", std::to_string(stats.compressed));
  RecordProperty("action_script3_movies", std::to_string(stats.action_script3));
  RecordProperty("lzma_movies", std::to_string(stats.lzma));
  RecordProperty("symbol_classes", std::to_string(stats.symbols));
  RecordProperty("abc_blocks", std::to_string(stats.abc_blocks));
  RecordProperty("version_histogram", version_histogram(stats));
}
