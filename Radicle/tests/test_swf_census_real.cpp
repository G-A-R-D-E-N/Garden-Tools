#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_census.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

/* Scope must be every Programs movie, assets and tutorials included; paths must use backslashes. */
bool is_programs_movie(const std::string& path) {
  if (lower_extension(path) != ".swf") {
    return false;
  }
  std::string lowered = path;
  for (char& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return lowered.find("programs\\") != std::string::npos;
}

std::string manifest_game_build(const std::string& manifest) {
  constexpr std::string_view marker = "# game_build ";
  const std::size_t at = manifest.find(marker);
  if (at == std::string::npos) {
    return {};
  }
  const std::size_t begin = at + marker.size();
  const std::size_t end = manifest.find('\n', begin);
  return manifest.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

std::string read_text_file(const std::filesystem::path& path) {
  const std::vector<std::byte> bytes = read_file(path);
  std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  /* The manifest must stay LF; a CR must not turn a content check into a line-ending one. */
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

/* Only the build line may change; every committed row must stay comparable on another build. */
std::string with_game_build(const std::string& manifest, std::string_view game_build) {
  constexpr std::string_view marker = "# game_build ";
  const std::size_t at = manifest.find(marker);
  if (at == std::string::npos) {
    return manifest;
  }
  const std::size_t begin = at + marker.size();
  const std::size_t end = manifest.find('\n', begin);
  return manifest.substr(0, begin) + std::string{game_build} +
         (end == std::string::npos ? std::string{} : manifest.substr(end));
}

/* A mismatch must name the first differing line; both whole manifests must not be dumped. */
std::string first_text_difference(const std::string& actual, const std::string& expected) {
  std::size_t actual_at = 0;
  std::size_t expected_at = 0;
  std::size_t line = 1;
  while (actual_at <= actual.size() || expected_at <= expected.size()) {
    const std::size_t actual_end = actual.find('\n', actual_at);
    const std::size_t expected_end = expected.find('\n', expected_at);
    const std::size_t actual_stop = actual_end == std::string::npos ? actual.size() : actual_end;
    const std::size_t expected_stop =
        expected_end == std::string::npos ? expected.size() : expected_end;
    const std::string_view actual_line(actual.data() + actual_at, actual_stop - actual_at);
    const std::string_view expected_line(expected.data() + expected_at, expected_stop - expected_at);
    if (actual_line != expected_line) {
      return "line " + std::to_string(line) + ": census has \"" +
             std::string{actual_line.substr(0, 160)} + "\" but the manifest has \"" +
             std::string{expected_line.substr(0, 160)} + "\"";
    }
    if (actual_end == std::string::npos && expected_end == std::string::npos) {
      return "no differing line, but the lengths differ (census " + std::to_string(actual.size()) +
             ", manifest " + std::to_string(expected.size()) + ")";
    }
    if (actual_end == std::string::npos || expected_end == std::string::npos) {
      return "line " + std::to_string(line) + ": one text ends where the other continues";
    }
    actual_at = actual_end + 1;
    expected_at = expected_end + 1;
    ++line;
  }
  return "no difference";
}

bool ends_with(const std::string& value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST(SwfCensusReal, MatchesTheCommittedProgramManifest) {
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

  std::vector<fo4::swf::MovieCensus> movies;
  std::size_t blocks = 0;
  std::size_t parsed = 0;
  for (const fo4::archive::Ba2GeneralEntry& entry : archive.entries) {
    if (!is_programs_movie(entry.path)) {
      continue;
    }
    const std::vector<std::byte> bytes = fo4::archive::extract_ba2_general_entry(entry);
    const fo4::swf::SwfReadResult result = fo4::swf::read_swf(bytes);
    ASSERT_TRUE(result.ok()) << entry.path << ": " << fo4::swf::status_name(result.status) << " "
                             << result.message;
    movies.push_back(fo4::swf::census_movie(entry.path, result.file));
    blocks += movies.back().abc_blocks;
    parsed += movies.back().parsed_blocks;
  }
  ASSERT_FALSE(movies.empty()) << "no Programs movies in " << archive_path.string();
  std::sort(movies.begin(), movies.end(),
            [](const fo4::swf::MovieCensus& left, const fo4::swf::MovieCensus& right) {
              return left.movie < right.movie;
            });

  RecordProperty("program_movies", std::to_string(movies.size()));
  RecordProperty("abc_blocks", std::to_string(blocks));
  RecordProperty("abc_parsed", std::to_string(parsed));

  EXPECT_EQ(blocks, parsed) << "a Programs ABC block failed to parse";
  for (const std::string_view program :
       {"AtomicCommand", "GJ_Defrag", "Grognak", "PipFall", "RedMenace", "TurretHack",
        "ZetaInvaders"}) {
    const std::string suffix = "\\" + std::string{program} + ".swf";
    EXPECT_TRUE(std::any_of(movies.begin(), movies.end(),
                            [&suffix](const fo4::swf::MovieCensus& movie) {
                              return ends_with(movie.movie, suffix);
                            }))
        << "the archive has no Programs movie named " << program;
  }
  for (const fo4::swf::MovieCensus& movie : movies) {
    /* GrognakAssets ships no bytecode at all, so only a movie with blocks must reference names. */
    if (movie.abc_blocks != 0) {
      EXPECT_FALSE(movie.references.empty()) << movie.movie << " referenced no engine names";
    }
  }

  const std::string text = fo4::swf::census_text(movies, build.file_version);
  const std::filesystem::path manifest_path =
      std::filesystem::path{FO4_TESTDATA_DIR} / "swf" / "program_census.txt";
  ASSERT_TRUE(std::filesystem::exists(manifest_path)) << manifest_path.string();
  const std::string manifest = read_text_file(manifest_path);
  const std::string manifest_build = manifest_game_build(manifest);
  RecordProperty("manifest_game_build", manifest_build);

  /* OG 1.10.163.0 and NG 1.10.984.0 must give identical rows; another build must only check invariants. */
  static constexpr std::array<std::string_view, 2> k_verified{"1.10.163.0", "1.10.984.0"};
  const bool verified =
      std::find(k_verified.begin(), k_verified.end(), build.file_version) != k_verified.end();
  if (!verified) {
    RecordProperty("manifest_build_unverified",
                   manifest_build + " manifest vs " + build.file_version + " data folder");
    return;
  }
  const std::string expected = with_game_build(manifest, build.file_version);
  if (text != expected) {
    ADD_FAILURE() << "census differs from " << manifest_path.string() << " at "
                  << first_text_difference(text, expected);
  }
}
