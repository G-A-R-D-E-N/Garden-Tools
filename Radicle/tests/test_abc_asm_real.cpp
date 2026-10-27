#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_asm.hpp"
#include "fo4/abc/abc_text.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct AssembleStats {
  std::size_t movies{0};
  std::size_t lzma{0};
  std::size_t blocks{0};
  std::size_t parsed{0};
  std::size_t parse_failures{0};
  std::size_t assemble_failures{0};
  std::size_t serialize_mismatches{0};
  std::size_t characters{0};
};

void check_archive(const std::filesystem::path& archive_path, AssembleStats& stats) {
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
      const fo4::abc::AbcStatus parse_status = fo4::abc::AbcFile::parse(
          reinterpret_cast<const std::uint8_t*>(block.payload.data()), block.payload.size(),
          file);
      if (parse_status != fo4::abc::AbcStatus::Ok) {
        ++stats.parse_failures;
        continue;
      }
      ++stats.parsed;
      const std::string text = fo4::abc::render_abc_text(file);
      stats.characters += text.size();
      fo4::abc::AbcFile assembled;
      fo4::abc::AbcTextError error;
      if (fo4::abc::assemble_abc_text(text, assembled, &error) != fo4::abc::AbcStatus::Ok) {
        ++stats.assemble_failures;
        ADD_FAILURE() << entry.path << ": assemble failed at line " << error.line << ": "
                      << error.message;
        continue;
      }
      std::vector<std::uint8_t> serialized;
      if (assembled.serialize(serialized) != fo4::abc::AbcStatus::Ok ||
          serialized.size() != block.payload.size() ||
          !std::equal(serialized.begin(), serialized.end(), block.payload.begin())) {
        ++stats.serialize_mismatches;
        ADD_FAILURE() << entry.path << ": assembled block does not serialize to its bytes";
      }
    }
  }
}

}  // namespace

TEST(AbcAsmReal, EveryVanillaInterfaceBlockDumpAssemblesBackByteForByte) {
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
  AssembleStats stats;
  check_archive(archive_path, stats);
  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("lzma", std::to_string(stats.lzma));
  RecordProperty("blocks", std::to_string(stats.blocks));
  RecordProperty("parsed", std::to_string(stats.parsed));
  RecordProperty("parse_failures", std::to_string(stats.parse_failures));
  RecordProperty("assemble_failures", std::to_string(stats.assemble_failures));
  RecordProperty("serialize_mismatches", std::to_string(stats.serialize_mismatches));
  RecordProperty("characters", std::to_string(stats.characters));
  ASSERT_GT(stats.movies, 0u);
  ASSERT_GT(stats.blocks, 0u);
  EXPECT_EQ(stats.parse_failures, 0u);
  EXPECT_EQ(stats.assemble_failures, 0u);
  EXPECT_EQ(stats.serialize_mismatches, 0u);
}
