#include "fo4/abc/abc.hpp"
#include "fo4/abc/opcodes.hpp"
#include "fo4/abc/summary.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
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

std::string bucket(std::size_t count) {
  if (count == 0) {
    return "0";
  }
  if (count <= 4) {
    return "1-4";
  }
  if (count <= 16) {
    return "5-16";
  }
  if (count <= 64) {
    return "17-64";
  }
  if (count <= 256) {
    return "65-256";
  }
  return "257+";
}

struct AbcStats {
  std::size_t total_movies{0};
  std::size_t movies{0};
  std::size_t lzma{0};
  std::size_t unreadable{0};
  std::size_t blocks{0};
  std::size_t parsed{0};
  std::size_t trailing_bytes_blocks{0};
  std::size_t bodies{0};
  std::size_t instructions{0};
  std::size_t classes{0};
  std::size_t methods{0};
  std::size_t scripts{0};
  std::map<std::string, std::size_t> unsupported;
  std::map<std::string, std::size_t> class_histogram;
  std::map<std::string, std::size_t> method_histogram;
  std::map<std::string, std::size_t> script_histogram;

  std::size_t unsupported_total() const {
    std::size_t total = 0;
    for (const auto& [status, count] : unsupported) {
      total += count;
    }
    return total;
  }
};

std::string histogram_text(const std::map<std::string, std::size_t>& histogram) {
  std::string text;
  for (const auto& [bucket_name, count] : histogram) {
    if (!text.empty()) {
      text += ' ';
    }
    text += bucket_name + "=" + std::to_string(count);
  }
  return text;
}

void check_block(const std::string& name, std::span<const std::byte> payload, AbcStats& stats) {
  ++stats.blocks;
  fo4::abc::AbcFile file;
  const auto parse_status = fo4::abc::AbcFile::parse(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), file);
  if (parse_status != fo4::abc::AbcStatus::Ok) {
    ++stats.unsupported[fo4::abc::status_name(parse_status)];
    if (parse_status == fo4::abc::AbcStatus::TrailingBytes) {
      ++stats.trailing_bytes_blocks;
    }
    return;
  }
  ++stats.parsed;

  for (const fo4::abc::AbcMethodBody& body : file.bodies) {
    ++stats.bodies;
    std::vector<fo4::abc::AbcInstruction> instructions;
    const auto decode_status = fo4::abc::decode_code(body.code, instructions);
    EXPECT_EQ(decode_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(decode_status);
    if (decode_status != fo4::abc::AbcStatus::Ok) {
      continue;
    }
    const auto operand_status = fo4::abc::validate_operand_pools(file, instructions);
    EXPECT_EQ(operand_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(operand_status);
    stats.instructions += instructions.size();
    std::string listing;
    const auto listing_status = fo4::abc::disassemble(file, body.code, listing);
    EXPECT_EQ(listing_status, fo4::abc::AbcStatus::Ok)
        << name << ": " << fo4::abc::status_name(listing_status);
  }

  fo4::abc::AbcSummary summary;
  const auto summary_status = fo4::abc::summarize(file, summary);
  ASSERT_EQ(summary_status, fo4::abc::AbcStatus::Ok)
      << name << ": " << fo4::abc::status_name(summary_status);
  EXPECT_EQ(summary.classes.size(), file.instances.size()) << name;

  std::size_t methods = 0;
  for (const fo4::abc::AbcClassSummary& klass : summary.classes) {
    methods += klass.methods.size();
  }
  stats.classes += summary.classes.size();
  stats.methods += methods;
  stats.scripts += summary.scripts.size();
  ++stats.class_histogram[bucket(summary.classes.size())];
  ++stats.method_histogram[bucket(methods)];
  ++stats.script_histogram[bucket(summary.scripts.size())];
}

void check_movie(const std::string& name, std::span<const std::byte> bytes, AbcStats& stats) {
  ++stats.total_movies;
  const auto result = fo4::swf::read_swf(bytes);
  if (result.status == fo4::swf::SwfStatus::unsupported_compression) {
    ++stats.lzma;
    return;
  }
  EXPECT_TRUE(result.ok()) << name << ": " << fo4::swf::status_name(result.status) << " "
                           << result.message;
  if (!result.ok()) {
    ++stats.unreadable;
    return;
  }
  ++stats.movies;
  for (const fo4::swf::SwfAbcBlock& block : fo4::swf::abc_blocks(result.file)) {
    const std::string label = name + ":tag" + std::to_string(block.tag_index) +
                              (block.name.empty() ? std::string{} : ":" + block.name);
    check_block(label, block.payload, stats);
  }
}

}  // namespace

TEST(AbcCorpusReal, DecodesEveryShippedInterfaceAbcBlock) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::filesystem::path data_dir(dir);

  AbcStats stats;
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

  if (stats.blocks == 0) {
    const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
    if (!std::filesystem::exists(archive_path)) {
      GTEST_SKIP() << "no Interface movies with bytecode and no Fallout4 - Interface.ba2 in "
                      "FO4_DATA_DIR";
    }
    const auto archive = fo4::archive::read_ba2_general_file(archive_path);
    for (const auto& entry : archive.entries) {
      if (!is_movie(entry.path)) {
        continue;
      }
      check_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), stats);
    }
  }

  EXPECT_EQ(stats.total_movies, stats.movies + stats.lzma + stats.unreadable);
  EXPECT_EQ(stats.blocks, stats.parsed + stats.unsupported_total());
  EXPECT_EQ(stats.trailing_bytes_blocks, 0u);
  EXPECT_GT(stats.parsed, 0u);
  EXPECT_GT(stats.bodies, 0u);

  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("lzma_movies", std::to_string(stats.lzma));
  RecordProperty("unreadable_movies", std::to_string(stats.unreadable));
  RecordProperty("abc_blocks", std::to_string(stats.blocks));
  RecordProperty("parsed_blocks", std::to_string(stats.parsed));
  RecordProperty("unsupported_blocks", std::to_string(stats.unsupported_total()));
  RecordProperty("unsupported_status_histogram", histogram_text(stats.unsupported));
  RecordProperty("bodies", std::to_string(stats.bodies));
  RecordProperty("instructions", std::to_string(stats.instructions));
  RecordProperty("total_classes", std::to_string(stats.classes));
  RecordProperty("total_methods", std::to_string(stats.methods));
  RecordProperty("total_scripts", std::to_string(stats.scripts));
  RecordProperty("class_histogram", histogram_text(stats.class_histogram));
  RecordProperty("method_histogram", histogram_text(stats.method_histogram));
  RecordProperty("script_histogram", histogram_text(stats.script_histogram));
}
