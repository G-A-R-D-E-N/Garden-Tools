#include "fo4/c_api_env.h"
#include "fo4/c_api_script_set.h"

#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

Ba2GeneralEntry general(std::string path, std::string_view payload) {
  Ba2GeneralEntry entry;
  const auto hashes = fo4::archive::compute_ba2_hashes(path);
  entry.name_hash = hashes.name;
  entry.extension_hash = hashes.extension;
  entry.directory_hash = hashes.directory;
  entry.path = std::move(path);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes_of(payload)));
  return entry;
}

class CApiScriptSetTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_c_api_script_set_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write_loose(const std::filesystem::path& dir, std::string_view relative,
                                    std::string_view payload) {
    const auto path = dir / std::filesystem::path(std::string(relative));
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return path;
  }

  std::filesystem::path write_archive(std::string_view name, std::vector<Ba2GeneralEntry> entries) {
    Ba2GeneralArchive archive;
    archive.has_string_table = true;
    archive.entries = std::move(entries);
    const auto path = root_ / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
  }

  radicle_assets* open(const std::vector<std::string>& roots, const std::vector<std::string>& archives) {
    std::vector<const char*> root_paths;
    for (const auto& root : roots) root_paths.push_back(root.c_str());
    std::vector<const char*> archive_paths;
    for (const auto& archive : archives) archive_paths.push_back(archive.c_str());
    radicle_assets* assets = nullptr;
    EXPECT_EQ(radicle_assets_open_roots(root_paths.data(), root_paths.size(), archive_paths.data(),
                                        archive_paths.size(), &assets),
              RADICLE_STATUS_OK);
    return assets;
  }

  std::filesystem::path root_;
};

std::string text_of(uint8_t* bytes, size_t size) {
  return std::string(reinterpret_cast<const char*>(bytes), size);
}

// A pointer that a rejected call must overwrite; it is never dereferenced.
template <typename T>
T* sentinel() {
  return reinterpret_cast<T*>(static_cast<std::uintptr_t>(1));
}

}  // namespace

TEST_F(CApiScriptSetTest, ResolvesReadsAndCountsReplacedPayloads) {
  const auto data = root_ / "Data";
  const auto loose_path = write_loose(data, "Scripts/Actors/Foo.pex", "loose");
  const auto archive = write_archive("Mod - Main.ba2", {general("Scripts\\Actors\\Foo.pex", "archived"),
                                                        general("Scripts\\Bar.pex", "bar")});
  radicle_assets* assets = open({data.string()}, {archive.string()});
  ASSERT_NE(assets, nullptr);
  EXPECT_EQ(radicle_assets_root_count(assets), 1u);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(radicle_script_set_count(set), 2u);

  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "actors/foo");
  EXPECT_EQ(std::string(winner.winner.name), "actors/foo");
  EXPECT_EQ(std::string(winner.winner.path), "scripts/actors/foo.pex");
  EXPECT_EQ(winner.winner.origin, RADICLE_ASSET_LOOSE);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), loose_path));
  EXPECT_EQ(winner.winner.size, 5u);
  EXPECT_EQ(winner.replaced, 1u);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "loose");
  radicle_free_bytes(bytes);

  ASSERT_EQ(radicle_script_set_get(set, 1, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "bar");
  EXPECT_EQ(winner.winner.origin, RADICLE_ASSET_ARCHIVE);
  EXPECT_EQ(winner.replaced, 0u);
  ASSERT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "bar");
  radicle_free_bytes(bytes);

  radicle_script_set_destroy(set);
  radicle_assets_destroy(assets);
}

TEST_F(CApiScriptSetTest, ExplicitLoadIndexMovesTheWinner) {
  const auto high = root_ / "ModHigh";
  const auto low = root_ / "ModLow";
  const auto high_path = write_loose(high, "Scripts/foo.pex", "high");
  const auto low_path = write_loose(low, "Scripts/foo.pex", "low");
  radicle_assets* assets = open({high.string(), low.string()}, {});
  ASSERT_NE(assets, nullptr);
  ASSERT_EQ(radicle_assets_root_count(assets), 2u);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), high_path));
  radicle_script_set_destroy(set);

  const int32_t indexes[] = {2, 7};
  ASSERT_EQ(radicle_script_set_resolve(assets, indexes, 2, &set), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_script_set_count(set), 1u);
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), low_path));
  EXPECT_EQ(winner.winner.load_index, 7);
  EXPECT_EQ(winner.replaced, 1u);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "low");
  radicle_free_bytes(bytes);

  radicle_script_set_destroy(set);
  radicle_assets_destroy(assets);
}

TEST_F(CApiScriptSetTest, RejectsBadArguments) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/foo.pex", "foo");
  radicle_assets* assets = open({data.string()}, {});
  ASSERT_NE(assets, nullptr);

  // Invariant: every out-param keeps a sentinel until the call proves the arguments.
  // Invariant: a rejected call must clear the caller's previous handle so the sentinel stays visible here.
  radicle_script_set* out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(nullptr, nullptr, 0, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(assets, nullptr, 2, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  // Invariant: the assets handle has one root, so two indexes must be refused.
  const int32_t two[] = {1, 2};
  out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(assets, two, 2, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  EXPECT_EQ(radicle_script_set_resolve(assets, nullptr, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_script_set_count(set), 1u);
  radicle_script_set_winner winner{};
  winner.name = "stale";
  winner.winner.load_index = 9;
  winner.replaced = 7u;
  EXPECT_EQ(radicle_script_set_get(set, 1, &winner), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(winner.name, static_cast<const char*>(nullptr));
  EXPECT_EQ(winner.winner.load_index, 0);
  EXPECT_EQ(winner.replaced, 0u);
  EXPECT_EQ(radicle_script_set_get(set, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t* bytes = sentinel<uint8_t>();
  size_t size = 99u;
  EXPECT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  bytes = sentinel<uint8_t>();
  size = 99u;
  EXPECT_EQ(radicle_script_set_read(nullptr, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  winner.name = "stale";
  winner.winner.load_index = 9;
  winner.replaced = 7u;
  EXPECT_EQ(radicle_script_set_get(nullptr, 0, &winner), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(winner.name, static_cast<const char*>(nullptr));
  EXPECT_EQ(winner.winner.load_index, 0);
  EXPECT_EQ(winner.replaced, 0u);
  EXPECT_EQ(radicle_script_set_count(nullptr), 0u);
  radicle_script_set_destroy(nullptr);
  radicle_script_set_destroy(set);

  radicle_script_set* empty = nullptr;
  radicle_assets* none = open({}, {});
  ASSERT_NE(none, nullptr);
  ASSERT_EQ(radicle_script_set_resolve(none, nullptr, 0, &empty), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_script_set_count(empty), 0u);
  radicle_script_set_destroy(empty);
  radicle_assets_destroy(none);
  radicle_assets_destroy(assets);
}
