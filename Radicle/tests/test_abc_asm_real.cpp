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
#include <span>
#include <string>
#include <vector>

namespace {

std::string build_label(const GameBuild& build) {
  const char* release = build.release == GameRelease::AE   ? "AE"
                        : build.release == GameRelease::OG ? "OG"
                        : build.release == GameRelease::NG ? "NG"
                                                           : "unknown";
  const std::string version = build.file_version.empty() ? std::string{"unknown"} : build.file_version;
  return std::string{release} + " " + version;
}

std::size_t first_difference(std::span<const std::byte> expected,
                             const std::vector<std::uint8_t>& actual) {
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (index >= actual.size() ||
        static_cast<std::uint8_t>(expected[index]) != actual[index]) {
      return index;
    }
  }
  return expected.size();
}

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

void check_archive(const std::filesystem::path& archive_path, const GameBuild& build,
                   AssembleStats& stats) {
  const std::string build_name = build_label(build);
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
      const std::size_t block_index = stats.blocks;
      ++stats.blocks;
      fo4::abc::AbcFile file;
      const fo4::abc::AbcStatus parse_status = fo4::abc::AbcFile::parse(
          reinterpret_cast<const std::uint8_t*>(block.payload.data()), block.payload.size(),
          file);
      if (parse_status != fo4::abc::AbcStatus::Ok) {
        ++stats.parse_failures;
        ADD_FAILURE() << entry.path << ": ABC block " << block_index << " (" << block.payload.size()
                      << " bytes) failed to parse on " << build_name
                      << ": " << fo4::abc::status_name(parse_status);
        continue;
      }
      ++stats.parsed;
      const std::string text = fo4::abc::render_abc_text(file);
      stats.characters += text.size();
      fo4::abc::AbcFile assembled;
      fo4::abc::AbcTextError error;
      if (fo4::abc::assemble_abc_text(text, assembled, &error) != fo4::abc::AbcStatus::Ok) {
        ++stats.assemble_failures;
        ADD_FAILURE() << entry.path << ": ABC block " << block_index << " failed to assemble on "
                      << build_name << " at line " << error.line << ": " << error.message;
        continue;
      }
      std::vector<std::uint8_t> serialized;
      const fo4::abc::AbcStatus serialize_status = assembled.serialize(serialized);
      const std::size_t difference = first_difference(block.payload, serialized);
      const bool identical = serialize_status == fo4::abc::AbcStatus::Ok &&
                             serialized.size() == block.payload.size() &&
                             difference == block.payload.size();
      if (!identical) {
        ++stats.serialize_mismatches;
        ADD_FAILURE() << entry.path << ": ABC block " << block_index
                      << " assembled block does not serialize to its bytes on " << build_name
                      << ": serialize " << fo4::abc::status_name(serialize_status)
                      << ", sizes " << serialized.size() << " of " << block.payload.size()
                      << ", first differing byte " << difference;
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
  check_archive(archive_path, build, stats);
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
