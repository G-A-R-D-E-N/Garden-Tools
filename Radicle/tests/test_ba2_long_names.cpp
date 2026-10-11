#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/archive/ba2_texture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;
using fo4::archive::Ba2Reader;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::string text_of(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

Ba2GeneralEntry general_entry(std::string path, std::string_view payload) {
  Ba2GeneralEntry entry;
  entry.path = std::move(path);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes_of(payload)));
  return entry;
}

std::string long_name(std::string_view tail) {
  return "Meshes\\" + std::string(290, 'a') + std::string(tail);
}

struct TempDir {
  TempDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("radicle_ba2_long_names_" + std::to_string(id));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

}  // namespace

TEST(Ba2LongNames, DistinctLongNamesKeepSeparateEntries) {
  const TempDir dir;
  const auto path = dir.path / "Long.ba2";
  Ba2GeneralArchive archive;
  archive.has_string_table = true;
  archive.entries = {
      general_entry(long_name("_one.nif"), "one"),
      general_entry(long_name("_two.nif"), "two"),
      general_entry("Meshes\\Normal.nif", "normal"),
  };
  fo4::archive::write_ba2_general_file(archive, path);

  const auto reader = Ba2Reader::open(path);
  ASSERT_EQ(reader.entries().size(), 3u);
  EXPECT_EQ(reader.find(long_name("_one.nif")), std::optional<std::size_t>{0});
  EXPECT_EQ(reader.find(long_name("_two.nif")), std::optional<std::size_t>{1});
  EXPECT_EQ(reader.find("meshes/normal.NIF"), std::optional<std::size_t>{2});
  EXPECT_EQ(text_of(reader.extract(1)), "two");
}

TEST(Ba2LongNames, TextureLookupKeepsDistinctLongNames) {
  fo4::archive::Ba2TextureArchive archive;
  for (const auto& name : {long_name("_one.dds"), long_name("_two.dds")}) {
    fo4::archive::Ba2TextureEntry entry;
    entry.path = name;
    archive.entries.push_back(entry);
  }
  EXPECT_EQ(fo4::archive::find_ba2_texture_entry(archive, long_name("_two.dds")),
            &archive.entries[1]);
}
