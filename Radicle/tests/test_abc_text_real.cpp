#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_text.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace {

std::uint64_t fnv1a(const std::string& text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  return hash;
}

struct DumpStats {
  std::size_t movies{0};
  std::size_t lzma{0};
  std::size_t blocks{0};
  std::size_t parsed{0};
  std::size_t failed_parses{0};
  std::size_t decode_failures{0};
  std::size_t characters{0};
  std::uint64_t hash{1469598103934665603ULL};
};

void dump_archive(const std::filesystem::path& archive_path, DumpStats& stats) {
  const fo4::archive::Ba2GeneralArchive archive =
      fo4::archive::read_ba2_general_file(archive_path);
  for (const fo4::archive::Ba2GeneralEntry& entry : archive.entries) {
    const std::vector<std::byte> bytes = fo4::archive::extract_ba2_general_entry(entry);
    const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
    if (read.status == fo4::swf::SwfStatus::unsupported_compression) {
      ++stats.lzma;
      continue;
    }
    if (!read.ok()) {
      continue;
    }
    ++stats.movies;
    for (const fo4::swf::SwfAbcBlock& block : fo4::swf::abc_blocks(read.file)) {
      ++stats.blocks;
      fo4::abc::AbcFile file;
      const fo4::abc::AbcStatus status = fo4::abc::AbcFile::parse(
          reinterpret_cast<const std::uint8_t*>(block.payload.data()), block.payload.size(),
          file);
      if (status != fo4::abc::AbcStatus::Ok) {
        ++stats.failed_parses;
        continue;
      }
      ++stats.parsed;
      const std::string text = fo4::abc::render_abc_text(file);
      if (text.find("decode failed") != std::string::npos) {
        ++stats.decode_failures;
      }
      stats.characters += text.size();
      const std::uint64_t text_hash = fnv1a(text);
      stats.hash ^= text_hash + 0x9E3779B97F4A7C15ULL + (stats.hash << 6u) + (stats.hash >> 2u);
    }
  }
}

}  // namespace

TEST(AbcTextReal, DumpsEveryVanillaInterfaceBlockDeterministically) {
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

  DumpStats first;
  dump_archive(archive_path, first);
  DumpStats second;
  dump_archive(archive_path, second);

  RecordProperty("movies", std::to_string(first.movies));
  RecordProperty("lzma", std::to_string(first.lzma));
  RecordProperty("blocks", std::to_string(first.blocks));
  RecordProperty("parsed", std::to_string(first.parsed));
  RecordProperty("failed_parses", std::to_string(first.failed_parses));
  RecordProperty("decode_failures", std::to_string(first.decode_failures));
  RecordProperty("characters", std::to_string(first.characters));
  RecordProperty("dump_hash", std::to_string(first.hash));

  ASSERT_GT(first.movies, 0u);
  ASSERT_GT(first.blocks, 0u);
  EXPECT_EQ(first.failed_parses, 0u);
  EXPECT_EQ(first.decode_failures, 0u);
  EXPECT_GT(first.characters, 0u);
  EXPECT_EQ(first.blocks, second.blocks);
  EXPECT_EQ(first.parsed, second.parsed);
  EXPECT_EQ(first.hash, second.hash);
}
