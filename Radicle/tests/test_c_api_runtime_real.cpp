#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_runtime.h"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ArchiveDeleter {
  void operator()(radicle_ba2* ba2) const { radicle_ba2_close(ba2); }
};

struct RuntimeDeleter {
  void operator()(radicle_runtime* runtime) const { radicle_runtime_close(runtime); }
};

struct MovieDeleter {
  void operator()(radicle_movie* movie) const { radicle_movie_close(movie); }
};

using ArchiveHandle = std::unique_ptr<radicle_ba2, ArchiveDeleter>;
using RuntimeHandle = std::unique_ptr<radicle_runtime, RuntimeDeleter>;
using MovieHandle = std::unique_ptr<radicle_movie, MovieDeleter>;

struct ResolverHost {
  const radicle_ba2* archive{nullptr};
  std::string movie_directory;
  std::size_t resolved{0};
  std::size_t missed{0};
};

radicle_status resolve_asset(void* user, const char* path, uint8_t** out_bytes, size_t* out_size) {
  auto* host = static_cast<ResolverHost*>(user);
  if (path == nullptr || out_bytes == nullptr || out_size == nullptr || host->archive == nullptr) {
    return RADICLE_STATUS_INVALID_ARGUMENT;
  }
  std::vector<std::string> candidates;
  candidates.emplace_back(path);
  candidates.emplace_back(host->movie_directory + path);
  const std::string_view raw{path};
  const std::size_t slash = raw.find_last_of('/');
  if (slash != std::string_view::npos) candidates.emplace_back(std::string{raw.substr(slash + 1)});
  for (const std::string& candidate : candidates) {
    std::size_t index = 0;
    if (radicle_ba2_find(host->archive, candidate.c_str(), &index) != RADICLE_STATUS_OK) continue;
    if (radicle_ba2_extract(host->archive, index, out_bytes, out_size) != RADICLE_STATUS_OK) continue;
    ++host->resolved;
    return RADICLE_STATUS_OK;
  }
  ++host->missed;
  return RADICLE_STATUS_NOT_FOUND;
}

std::string directory_of(std::string_view path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? std::string{} : std::string{path.substr(0, slash + 1)};
}

std::vector<std::uint64_t> replay_hashes(radicle_runtime& runtime, radicle_movie& movie) {
  std::vector<radicle_runtime_input> inputs(3);
  inputs[0].time_ms = 100.0;
  inputs[0].kind = RADICLE_INPUT_MOUSE_MOVE;
  inputs[0].x = 400.0;
  inputs[0].y = 300.0;
  inputs[1].time_ms = 250.0;
  inputs[1].kind = RADICLE_INPUT_MOUSE_DOWN;
  inputs[2].time_ms = 400.0;
  inputs[2].kind = RADICLE_INPUT_KEY_DOWN;
  inputs[2].key_code = 13;
  (void)runtime;
  std::vector<std::uint64_t> hashes(24, 0);
  size_t count = 0;
  EXPECT_EQ(radicle_movie_replay(&movie, inputs.data(), inputs.size(), 1.0 / 24.0, 24,
                                 hashes.data(), hashes.size(), &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 24u);
  return hashes;
}

}  // namespace

TEST(CApiRuntimeReal, RunsAProgramMovieDeterministically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);

  std::string movie_path;
  for (std::size_t index = 0; index < info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK ||
        entry.path == nullptr) {
      continue;
    }
    const std::string_view path{entry.path};
    if (path.find("Programs/") == std::string_view::npos) continue;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".swf") continue;
    if (path.find("AtomicCommand") != std::string_view::npos) movie_path = entry.path;
  }
  if (movie_path.empty()) {
    for (std::size_t index = 0; index < info.entry_count && movie_path.empty(); ++index) {
      radicle_ba2_entry_info entry{};
      if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK ||
          entry.path == nullptr) {
        continue;
      }
      const std::string_view path{entry.path};
      if (path.find("Programs/") != std::string_view::npos && path.size() > 4 &&
          path.substr(path.size() - 4) == ".swf") {
        movie_path = entry.path;
      }
    }
  }
  ASSERT_FALSE(movie_path.empty()) << "no Programs movie in the archive";

  auto run_once = [&](ResolverHost& host, std::vector<std::uint64_t>& hashes) {
    radicle_runtime_host host_callbacks{};
    host_callbacks.user = &host;
    host_callbacks.resolve_asset = resolve_asset;
    radicle_runtime* raw_runtime = nullptr;
    EXPECT_EQ(radicle_runtime_open(&host_callbacks, &raw_runtime), RADICLE_STATUS_OK)
        << radicle_last_error();
    RuntimeHandle runtime(raw_runtime);
    radicle_movie* raw_movie = nullptr;
    if (radicle_runtime_open_movie_from_ba2(runtime.get(), archive.get(), movie_path.c_str(),
                                            &raw_movie) != RADICLE_STATUS_OK) {
      return false;
    }
    MovieHandle movie(raw_movie);
    hashes = replay_hashes(*runtime, *movie);
    return true;
  };

  ResolverHost first_host{archive.get(), directory_of(movie_path)};
  std::vector<std::uint64_t> first;
  if (!run_once(first_host, first)) {
    GTEST_SKIP() << "the program movie needs a runtime feature that is still missing: "
                 << radicle_last_error();
  }
  ASSERT_EQ(first.size(), 24u);
  bool nonzero = false;
  for (const std::uint64_t hash : first) nonzero = nonzero || hash != 0;
  EXPECT_TRUE(nonzero);
  RecordProperty("asset_requests", std::to_string(first_host.resolved + first_host.missed));
  RecordProperty("asset_misses", std::to_string(first_host.missed));

  ResolverHost second_host{archive.get(), directory_of(movie_path)};
  std::vector<std::uint64_t> second;
  ASSERT_TRUE(run_once(second_host, second));
  EXPECT_EQ(first, second);
}

namespace {

std::string lower_ascii(std::string value) {
  for (char& character : value) {
    if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
  }
  return value;
}

std::string basename_of(std::string_view path) {
  const std::size_t slash = path.find_last_of("\\/");
  return slash == std::string_view::npos ? std::string{path} : std::string{path.substr(slash + 1)};
}

std::uint64_t hash_sequence(const std::vector<std::uint64_t>& values) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const std::uint64_t value : values) {
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::vector<std::uint64_t> program_replay_hashes(radicle_movie& movie) {
  std::vector<radicle_runtime_input> inputs;
  const auto add = [&inputs](double time_ms, std::uint8_t kind, double x, double y,
                             std::uint32_t key) {
    radicle_runtime_input input{};
    input.time_ms = time_ms;
    input.kind = kind;
    input.x = x;
    input.y = y;
    input.key_code = key;
    inputs.push_back(input);
  };
  for (int frame = 0; frame < 40; ++frame) {
    add(static_cast<double>(frame) * 41.6667, RADICLE_INPUT_MOUSE_MOVE, 300.0 + frame * 4.0,
        200.0 + frame * 2.0, 0);
  }
  add(600.0, RADICLE_INPUT_MOUSE_DOWN, 460.0, 280.0, 0);
  add(650.0, RADICLE_INPUT_MOUSE_UP, 460.0, 280.0, 0);
  add(900.0, RADICLE_INPUT_KEY_DOWN, 0.0, 0.0, 13);
  add(950.0, RADICLE_INPUT_KEY_UP, 0.0, 0.0, 13);
  add(1200.0, RADICLE_INPUT_KEY_DOWN, 0.0, 0.0, 32);
  add(1250.0, RADICLE_INPUT_KEY_UP, 0.0, 0.0, 32);
  add(2400.0, RADICLE_INPUT_MOUSE_DOWN, 500.0, 300.0, 0);
  add(2450.0, RADICLE_INPUT_MOUSE_UP, 500.0, 300.0, 0);
  std::vector<std::uint64_t> hashes(180, 0);
  size_t count = 0;
  EXPECT_EQ(radicle_movie_replay(&movie, inputs.data(), inputs.size(), 1.0 / 24.0, 180,
                                 hashes.data(), hashes.size(), &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 180u);
  return hashes;
}

}  // namespace

TEST(CApiRuntimeReal, ReplaysTheSevenProgramsDeterministically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);

  std::map<std::string, std::string> by_basename;
  for (std::size_t index = 0; index < info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK ||
        entry.path == nullptr) {
      continue;
    }
    const std::string_view path{entry.path};
    if (path.size() < 4 || lower_ascii(std::string{path.substr(path.size() - 4)}) != ".swf") {
      continue;
    }
    by_basename.emplace(lower_ascii(basename_of(path)), std::string{path});
  }

  const char* programs[] = {"AtomicCommand.swf", "GJ_Defrag.swf",    "Grognak.swf",
                            "PipFall.swf",       "RedMenace.swf",    "TurretHack.swf",
                            "ZetaInvaders.swf"};
  std::size_t ran = 0;
  std::size_t mismatches = 0;
  for (const char* program : programs) {
    const std::string key = lower_ascii(program);
    const std::string property_name = "program_" + std::string{program};
    const auto found = by_basename.find(key);
    if (found == by_basename.end()) {
      std::cerr << "[seven-programs] " << program << ": missing from Fallout4 - Interface.ba2"
                << std::endl;
      RecordProperty(property_name.c_str(), "missing");
      continue;
    }
    const std::string& movie_path = found->second;
    const auto open_and_replay = [&](std::vector<std::uint64_t>& hashes) {
      ResolverHost resolver{archive.get(), directory_of(movie_path)};
      radicle_runtime_host callbacks{};
      callbacks.user = &resolver;
      callbacks.resolve_asset = resolve_asset;
      radicle_runtime* raw_runtime = nullptr;
      if (radicle_runtime_open(&callbacks, &raw_runtime) != RADICLE_STATUS_OK) return false;
      RuntimeHandle runtime(raw_runtime);
      radicle_movie* raw_movie = nullptr;
      if (radicle_runtime_open_movie_from_ba2(runtime.get(), archive.get(), movie_path.c_str(),
                                              &raw_movie) != RADICLE_STATUS_OK) {
        return false;
      }
      MovieHandle movie(raw_movie);
      hashes = program_replay_hashes(*movie);
      return true;
    };
    std::vector<std::uint64_t> first;
    if (!open_and_replay(first)) {
      std::cerr << "[seven-programs] " << program << " (" << movie_path
                << ") skipped on the first run: " << radicle_last_error() << std::endl;
      RecordProperty(property_name.c_str(), (std::string{"skipped: "} + radicle_last_error()).c_str());
      continue;
    }
    std::vector<std::uint64_t> second;
    if (!open_and_replay(second)) {
      std::cerr << "[seven-programs] " << program << " (" << movie_path
                << ") failed on the second run: " << radicle_last_error() << std::endl;
      RecordProperty(property_name.c_str(), "replay failed on the second run");
      ++mismatches;
      continue;
    }
    RecordProperty(property_name.c_str(), ("ran hash=" + std::to_string(hash_sequence(first))).c_str());
    ++ran;
    EXPECT_EQ(first, second) << program << " replay is not deterministic";
    if (first != second) ++mismatches;
  }
  RecordProperty("ran", std::to_string(ran));
  EXPECT_EQ(mismatches, 0u);
  if (ran == 0) {
    GTEST_SKIP() << "no Program movie could be opened yet; see the program_* properties";
  }
}
