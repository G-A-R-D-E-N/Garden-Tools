#include "fo4/archive/ba2_reader.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

namespace {

struct NamedArchive {
  const char* filename;
  std::uint32_t version;
  fo4::archive::Ba2Kind kind;
  std::size_t entries;
};

class Ba2NamedRealData : public testing::TestWithParam<NamedArchive> {};

TEST_P(Ba2NamedRealData, ExtractsAndRebuildsOneEntry) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto fixture = GetParam();
  const auto path = std::filesystem::path{dir} / fixture.filename;
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing named archive: " << path;
  SCOPED_TRACE(fixture.filename);
  const auto build = detect_game_build(dir);
  record_game_build(build);

  const auto reader = fo4::archive::Ba2Reader::open(path);
  RecordProperty("version", static_cast<int>(reader.version()));
  RecordProperty("entries", static_cast<int>(reader.entries().size()));
  if (exact_counts_known(build)) { ASSERT_EQ(reader.version(), fixture.version); }
  ASSERT_EQ(reader.kind(), fixture.kind);
  if (exact_counts_known(build)) { ASSERT_EQ(reader.entries().size(), fixture.entries); }
  ASSERT_TRUE(reader.has_name_table());
  const auto expected = reader.extract(0);
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(reader.find(reader.entries()[0].path), 0u);

  if (fixture.kind == fo4::archive::Ba2Kind::general) {
    auto archive = fo4::archive::read_ba2_general_file(path);
    if (exact_counts_known(build)) { ASSERT_EQ(archive.entries.size(), fixture.entries); }
    EXPECT_EQ(fo4::archive::extract_ba2_general_entry(archive.entries[0]), expected);
    archive.entries.resize(1);
    const auto rebuilt = fo4::archive::read_ba2_general(fo4::archive::write_ba2_general(archive));
    ASSERT_EQ(rebuilt.entries.size(), 1u);
    EXPECT_EQ(rebuilt.version, reader.version());
    EXPECT_EQ(rebuilt.entries[0].path, archive.entries[0].path);
    EXPECT_EQ(rebuilt.entries[0].unknown, archive.entries[0].unknown);
    EXPECT_EQ(rebuilt.entries[0].name_hash, archive.entries[0].name_hash);
    EXPECT_EQ(rebuilt.entries[0].extension_hash, archive.entries[0].extension_hash);
    EXPECT_EQ(rebuilt.entries[0].directory_hash, archive.entries[0].directory_hash);
    EXPECT_EQ(fo4::archive::extract_ba2_general_entry(rebuilt.entries[0]), expected);
  } else {
    ASSERT_GE(expected.size(), 128u);
    EXPECT_EQ(std::to_integer<char>(expected[0]), 'D');
    EXPECT_EQ(std::to_integer<char>(expected[1]), 'D');
    EXPECT_EQ(std::to_integer<char>(expected[2]), 'S');
    EXPECT_EQ(std::to_integer<char>(expected[3]), ' ');
    auto archive = fo4::archive::read_ba2_texture_file(path);
    if (exact_counts_known(build)) { ASSERT_EQ(archive.entries.size(), fixture.entries); }
    EXPECT_EQ(fo4::archive::extract_ba2_texture_entry(archive.entries[0]), expected);
    archive.entries.resize(1);
    const auto rebuilt = fo4::archive::read_ba2_texture(fo4::archive::write_ba2_texture(archive));
    ASSERT_EQ(rebuilt.entries.size(), 1u);
    EXPECT_EQ(rebuilt.version, reader.version());
    EXPECT_EQ(rebuilt.entries[0].path, archive.entries[0].path);
    EXPECT_EQ(rebuilt.entries[0].unknown, archive.entries[0].unknown);
    EXPECT_EQ(rebuilt.entries[0].name_hash, archive.entries[0].name_hash);
    EXPECT_EQ(rebuilt.entries[0].extension_hash, archive.entries[0].extension_hash);
    EXPECT_EQ(rebuilt.entries[0].directory_hash, archive.entries[0].directory_hash);
    const auto& before = archive.entries[0];
    const auto& after = rebuilt.entries[0];
    EXPECT_EQ(after.texture.height, before.texture.height);
    EXPECT_EQ(after.texture.width, before.texture.width);
    EXPECT_EQ(after.texture.mip_count, before.texture.mip_count);
    EXPECT_EQ(after.texture.dxgi_format, before.texture.dxgi_format);
    EXPECT_EQ(after.texture.flags, before.texture.flags);
    EXPECT_EQ(after.texture.tile_mode, before.texture.tile_mode);
    ASSERT_EQ(after.chunks.size(), before.chunks.size());
    for (std::size_t i = 0; i < before.chunks.size(); ++i) {
      SCOPED_TRACE(i);
      EXPECT_EQ(after.chunks[i].mip_first, before.chunks[i].mip_first);
      EXPECT_EQ(after.chunks[i].mip_last, before.chunks[i].mip_last);
    }
    EXPECT_EQ(fo4::archive::extract_ba2_texture_entry(rebuilt.entries[0]), expected);
  }
}

INSTANTIATE_TEST_SUITE_P(
    InstalledVersions, Ba2NamedRealData,
    testing::Values(
        NamedArchive{"Fallout4 - Nvflex.ba2", 1, fo4::archive::Ba2Kind::general, 32},
        NamedArchive{"DLCworkshop02 - Textures.ba2", 1, fo4::archive::Ba2Kind::texture, 171},
        NamedArchive{"DLCRobot - Voices_en.ba2", 7, fo4::archive::Ba2Kind::general, 6112},
        NamedArchive{"DLCworkshop01 - Textures.ba2", 7, fo4::archive::Ba2Kind::texture, 32},
        NamedArchive{"ccBGSFO4001-PipBoy(Black) - Main.ba2", 8, fo4::archive::Ba2Kind::general, 41},
        NamedArchive{"ccBGSFO4001-PipBoy(Black) - Textures.ba2", 8, fo4::archive::Ba2Kind::texture, 4}));

}
