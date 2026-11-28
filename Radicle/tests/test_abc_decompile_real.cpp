#include "fo4/abc/abc.hpp"
#include "fo4/abc/decompile.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Corpus {
  std::size_t movies{0};
  std::size_t lzma_movies{0};
  std::size_t unread_movies{0};
  std::size_t blocks{0};
  std::size_t unparsed{0};
  std::size_t classes{0};
  std::size_t bodies{0};
  std::size_t functions{0};
  std::size_t refused{0};
  std::size_t reasonless_refusals{0};
  std::size_t nondeterministic_blocks{0};
  std::size_t text_bytes{0};
  std::map<std::string, std::size_t> histogram;
  std::uint64_t hash{1469598103934665603ull};
  std::size_t cs_functions{0};
  std::size_t cs_refused{0};
  std::size_t cs_declaration_refusals{0};
  std::size_t cs_reasonless_refusals{0};
  std::size_t cs_nondeterministic_blocks{0};
  std::size_t cs_function_mismatch_blocks{0};
  std::size_t cs_text_bytes{0};
  std::map<std::string, std::size_t> cs_histogram;
  std::set<std::string> cs_unmapped_names;
  std::uint64_t cs_hash{1469598103934665603ull};
};

void fold(std::uint64_t& hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    hash ^= (value >> shift) & 0xFFu;
    hash *= 1099511628211ull;
  }
}

void write_cs_block(const std::string& path, const fo4::abc::AbcFile& file, std::size_t as3_refused,
                    std::size_t as3_functions, Corpus& corpus);

void decompile_block(const std::string& path, const fo4::abc::AbcFile& file, Corpus& corpus) {
  const fo4::abc::AbcAs3Output first = fo4::abc::write_as3(file);
  const fo4::abc::AbcAs3Output second = fo4::abc::write_as3(file);
  if (first.text != second.text || first.hash != second.hash || first.functions != second.functions ||
      first.refused.size() != second.refused.size()) {
    ++corpus.nondeterministic_blocks;
    ADD_FAILURE() << "decompile differs between two runs of " << path;
  }
  corpus.classes += file.instances.size();
  corpus.bodies += file.bodies.size();
  corpus.functions += first.functions;
  corpus.text_bytes += first.text.size();
  fold(corpus.hash, first.hash);
  for (const fo4::abc::AbcDecompileIssue& issue : first.refused) {
    ++corpus.refused;
    ++corpus.histogram[fo4::abc::decompile_status_name(issue.status)];
    if (issue.status == fo4::abc::AbcDecompileStatus::Ok || issue.message.empty()) {
      ++corpus.reasonless_refusals;
      ADD_FAILURE() << "refusal without a status or message in " << path << " method " << issue.method;
    }
  }
  write_cs_block(path, file, first.refused.size(), first.functions, corpus);
}

void write_cs_block(const std::string& path, const fo4::abc::AbcFile& file, std::size_t as3_refused,
                    std::size_t as3_functions, Corpus& corpus) {
  const fo4::abc::AbcCsOutput first = fo4::abc::write_cs(file);
  const fo4::abc::AbcCsOutput second = fo4::abc::write_cs(file);
  if (first.text != second.text || first.hash != second.hash || first.functions != second.functions ||
      first.refused.size() != second.refused.size()) {
    ++corpus.cs_nondeterministic_blocks;
    ADD_FAILURE() << "C# decompile differs between two runs of " << path;
  }
  if (first.functions != as3_functions) {
    ++corpus.cs_function_mismatch_blocks;
    ADD_FAILURE() << "C# and AS3 attempted different function counts in " << path;
  }
  corpus.cs_functions += first.functions;
  corpus.cs_text_bytes += first.text.size();
  fold(corpus.cs_hash, first.hash);
  std::size_t method_refusals = 0;
  for (const fo4::abc::AbcDecompileIssue& issue : first.refused) {
    ++corpus.cs_histogram[fo4::abc::decompile_status_name(issue.status)];
    if (issue.status == fo4::abc::AbcDecompileStatus::Ok || issue.message.empty()) {
      ++corpus.cs_reasonless_refusals;
      ADD_FAILURE() << "C# refusal without a status or message in " << path << " method " << issue.method;
    }
    if (issue.method == fo4::abc::kNoFlowIndex) {
      ++corpus.cs_declaration_refusals;
    } else {
      ++method_refusals;
    }
  }
  corpus.cs_refused += method_refusals;
  if (method_refusals < as3_refused) {
    ADD_FAILURE() << "C# refused fewer methods than AS3 in " << path;
  }
  for (const std::string& name : fo4::abc::cs_unmapped_names(file)) {
    corpus.cs_unmapped_names.insert(name);
  }
}

}  // namespace

TEST(AbcDecompileReal, EveryInterfaceMethodDecompilesOrNamesItsRefusal) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  const std::filesystem::path data_dir(dir);
  record_game_build(detect_game_build(data_dir));
  const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  Corpus corpus;
  for (const auto& entry : archive.entries) {
    if (!is_movie(entry.path)) {
      continue;
    }
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
    if (read.status == fo4::swf::SwfStatus::unsupported_compression) {
      ++corpus.lzma_movies;
      continue;
    }
    if (!read.ok()) {
      ++corpus.unread_movies;
      continue;
    }
    ++corpus.movies;
    for (const fo4::swf::SwfAbcBlock& block : fo4::swf::abc_blocks(read.file)) {
      ++corpus.blocks;
      fo4::abc::AbcFile file;
      const fo4::abc::AbcStatus status = fo4::abc::AbcFile::parse(
          reinterpret_cast<const std::uint8_t*>(block.payload.data()), block.payload.size(), file);
      if (status != fo4::abc::AbcStatus::Ok) {
        ++corpus.unparsed;
        continue;
      }
      decompile_block(entry.path, file, corpus);
    }
  }
  RecordProperty("movies", std::to_string(corpus.movies));
  RecordProperty("lzma_movies", std::to_string(corpus.lzma_movies));
  RecordProperty("unread_movies", std::to_string(corpus.unread_movies));
  RecordProperty("blocks", std::to_string(corpus.blocks));
  RecordProperty("unparsed", std::to_string(corpus.unparsed));
  RecordProperty("classes", std::to_string(corpus.classes));
  RecordProperty("bodies", std::to_string(corpus.bodies));
  RecordProperty("functions", std::to_string(corpus.functions));
  RecordProperty("refused", std::to_string(corpus.refused));
  RecordProperty("reasonless_refusals", std::to_string(corpus.reasonless_refusals));
  RecordProperty("nondeterministic_blocks", std::to_string(corpus.nondeterministic_blocks));
  RecordProperty("text_bytes", std::to_string(corpus.text_bytes));
  for (const auto& [name, count] : corpus.histogram) {
    RecordProperty("refused_" + name, std::to_string(count));
  }
  RecordProperty("as3_hash", fo4::abc::as3_hash_hex(corpus.hash));
  RecordProperty("cs_functions", std::to_string(corpus.cs_functions));
  RecordProperty("cs_refused", std::to_string(corpus.cs_refused));
  RecordProperty("cs_declaration_refusals", std::to_string(corpus.cs_declaration_refusals));
  RecordProperty("cs_reasonless_refusals", std::to_string(corpus.cs_reasonless_refusals));
  RecordProperty("cs_nondeterministic_blocks", std::to_string(corpus.cs_nondeterministic_blocks));
  RecordProperty("cs_function_mismatch_blocks", std::to_string(corpus.cs_function_mismatch_blocks));
  RecordProperty("cs_text_bytes", std::to_string(corpus.cs_text_bytes));
  for (const auto& [name, count] : corpus.cs_histogram) {
    RecordProperty("cs_refused_" + name, std::to_string(count));
  }
  RecordProperty("cs_hash", fo4::abc::as3_hash_hex(corpus.cs_hash));
  RecordProperty("cs_unmapped_name_count", std::to_string(corpus.cs_unmapped_names.size()));
  std::string unmapped;
  for (const std::string& name : corpus.cs_unmapped_names) {
    unmapped += (unmapped.empty() ? "" : ",") + name;
  }
  RecordProperty("cs_unmapped_names", unmapped);
  ASSERT_GT(corpus.movies, 0u);
  ASSERT_GT(corpus.blocks, 0u);
  EXPECT_EQ(corpus.unparsed, 0u);
  EXPECT_GT(corpus.functions, 0u);
  EXPECT_LE(corpus.refused, corpus.functions);
  EXPECT_EQ(corpus.reasonless_refusals, 0u);
  EXPECT_EQ(corpus.nondeterministic_blocks, 0u);
  EXPECT_EQ(corpus.cs_functions, corpus.functions);
  EXPECT_LE(corpus.cs_refused, corpus.cs_functions);
  EXPECT_GE(corpus.cs_refused, corpus.refused);
  EXPECT_EQ(corpus.cs_reasonless_refusals, 0u);
  EXPECT_EQ(corpus.cs_nondeterministic_blocks, 0u);
  EXPECT_EQ(corpus.cs_function_mismatch_blocks, 0u);
}
