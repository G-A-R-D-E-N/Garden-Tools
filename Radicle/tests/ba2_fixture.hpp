#pragma once

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

namespace ba2_fixture {

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;

inline std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

inline std::string text_of(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

inline Ba2GeneralEntry general(std::string path, std::string_view payload, bool compressed = false) {
  Ba2GeneralEntry entry;
  const auto hashes = fo4::archive::compute_ba2_hashes(path);
  entry.name_hash = hashes.name;
  entry.extension_hash = hashes.extension;
  entry.directory_hash = hashes.directory;
  entry.path = std::move(path);
  entry.chunks.push_back(compressed ? fo4::archive::make_compressed_ba2_chunk(bytes_of(payload))
                                    : fo4::archive::make_stored_ba2_chunk(bytes_of(payload)));
  return entry;
}

class Ba2ReaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_ba2_reader_" + std::to_string(id));
    std::filesystem::create_directories(root_ / "Data");
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path data() const { return root_ / "Data"; }

  std::filesystem::path write_general(std::string_view name, std::vector<Ba2GeneralEntry> entries,
                                      bool named = true) {
    Ba2GeneralArchive archive;
    archive.has_string_table = named;
    archive.entries = std::move(entries);
    const auto path = data() / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
  }

  std::filesystem::path write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  std::filesystem::path root_;
};

}  // namespace ba2_fixture
