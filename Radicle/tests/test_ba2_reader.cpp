#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/c_api_env.h"
#include "fo4/environment/strings.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::archive::Ba2Error;
using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;
using fo4::archive::Ba2Reader;
using fo4::archive::Ba2TextureArchive;
using fo4::archive::Ba2TextureEntry;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::string text_of(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

Ba2GeneralEntry general(std::string path, std::string_view payload, bool compressed = false) {
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

// Constraint: weakly_canonical must resolve a junction as well as a directory symlink.
bool create_directory_alias(const std::filesystem::path& target, const std::filesystem::path& link) {
  std::error_code ec;
  std::filesystem::create_directory_symlink(target, link, ec);
  if (!ec) return true;
#if defined(_WIN32)
  const auto command = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\" > NUL";
  return std::system(command.c_str()) == 0;
#else
  return false;
#endif
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

}  // namespace

TEST_F(Ba2ReaderTest, IndexesGeneralArchiveAndExtractsOnDemand) {
  const auto path = write_general("Mod - Main.ba2", {general("Meshes\\Weapons\\Gun.nif", "gun-bytes"),
                                                     general("Materials\\Gun.bgsm", std::string(4096, 'm'), true)});
  const auto reader = Ba2Reader::open(path);
  EXPECT_EQ(reader.kind(), fo4::archive::Ba2Kind::general);
  EXPECT_EQ(reader.version(), 1u);
  EXPECT_TRUE(reader.has_name_table());
  ASSERT_EQ(reader.entries().size(), 2u);
  EXPECT_EQ(reader.entries()[0].path, "Meshes\\Weapons\\Gun.nif");
  EXPECT_EQ(reader.entries()[1].unpacked_size(), 4096u);

  EXPECT_EQ(reader.find("meshes/weapons/GUN.NIF"), std::optional<std::size_t>{0});
  EXPECT_FALSE(reader.find("meshes/weapons/rifle.nif").has_value());
  EXPECT_EQ(text_of(reader.extract(0)), "gun-bytes");
  EXPECT_EQ(text_of(reader.extract(1)), std::string(4096, 'm'));

  const auto eager = fo4::archive::read_ba2_general_file(path);
  EXPECT_EQ(reader.extract(1), fo4::archive::extract_ba2_general_entry(eager.entries[1]));
  EXPECT_THROW(static_cast<void>(reader.extract(2)), std::out_of_range);
}

TEST_F(Ba2ReaderTest, RebuildsDdsForTextureArchives) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "Textures\\Rock_d.dds";
  entry.texture = {4, 8, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(std::vector<std::byte>(16, std::byte{0x5A})));
  archive.entries.push_back(entry);
  const auto path = data() / "Mod - Textures.ba2";
  fo4::archive::write_ba2_texture_file(archive, path);

  const auto reader = Ba2Reader::open(path);
  EXPECT_EQ(reader.kind(), fo4::archive::Ba2Kind::texture);
  ASSERT_EQ(reader.entries().size(), 1u);
  EXPECT_EQ(reader.entries()[0].texture.width, 8u);
  EXPECT_EQ(reader.entries()[0].texture.dxgi_format, 71u);
  const auto dds = reader.extract(*reader.find("textures/rock_d.dds"));
  ASSERT_EQ(dds.size(), 128u + 16u);
  EXPECT_EQ(text_of({dds.begin(), dds.begin() + 4}), "DDS ");
  EXPECT_EQ(dds.back(), std::byte{0x5A});
}

TEST_F(Ba2ReaderTest, FindsByHashWithoutNameTable) {
  const auto path = write_general("Unnamed.ba2", {general("meshes\\a.nif", "a"), general("meshes\\b.nif", "b")}, false);
  const auto reader = Ba2Reader::open(path);
  EXPECT_FALSE(reader.has_name_table());
  EXPECT_TRUE(reader.entries()[0].path.empty());
  const auto index = reader.find("Meshes/B.nif");
  ASSERT_TRUE(index.has_value());
  EXPECT_EQ(text_of(reader.extract(*index)), "b");
}

TEST_F(Ba2ReaderTest, RejectsMalformedArchives) {
  const auto good = write_general("Good.ba2", {general("a.txt", "payload")});
  auto bytes = fo4::archive::write_ba2_general(fo4::archive::read_ba2_general_file(good));

  EXPECT_THROW(Ba2Reader::open(data() / "Missing.ba2"), Ba2Error);
  EXPECT_THROW(Ba2Reader::open(write_file(data() / "Short.ba2", {bytes.begin(), bytes.begin() + 20})), Ba2Error);
  auto wrong_tag = bytes;
  wrong_tag[8] = std::byte{'X'};
  EXPECT_THROW(Ba2Reader::open(write_file(data() / "Tag.ba2", wrong_tag)), Ba2Error);
  // Chunk offset lives at header (24) + entry fields (16).
  auto bad_offset = bytes;
  bad_offset[40 + 3] = std::byte{0x7F};
  EXPECT_THROW(Ba2Reader::open(write_file(data() / "Offset.ba2", bad_offset)), Ba2Error);
  auto huge_count = bytes;
  huge_count[12] = std::byte{0xFF};
  huge_count[13] = std::byte{0xFF};
  EXPECT_THROW(Ba2Reader::open(write_file(data() / "Count.ba2", huge_count)), Ba2Error);
}

TEST_F(Ba2ReaderTest, OrdersArchivesLikeTheGameMountsThem) {
  for (const auto* name : {"Fallout4 - Interface.ba2", "fallout4 - textures1.ba2", "Mod.ba2", "Mod - Textures.ba2",
                           "Mod - Main.ba2", "Other.ba2", "ModX - Main.ba2", "Mod - .ba2"}) {
    write_file(data() / name, bytes_of("x"));
  }
  const std::vector<std::string> ini{"Fallout4 - Textures1.ba2", "Fallout4 - Interface.ba2", "Missing.ba2",
                                     "Mod - Main.ba2"};
  const std::vector<std::string> plugins{"Fallout4.esm", "Mod.esp"};
  std::vector<std::string> names;
  for (const auto& path : fo4::archive::archive_load_order(data(), ini, plugins)) {
    names.push_back(path.filename().string());
  }
  EXPECT_EQ(names, (std::vector<std::string>{"fallout4 - textures1.ba2", "Fallout4 - Interface.ba2",
                                             "Mod - Main.ba2", "Mod.ba2", "Mod - Textures.ba2"}));
  EXPECT_TRUE(fo4::archive::archive_load_order(root_ / "Nope", ini, plugins).empty());
}

TEST_F(Ba2ReaderTest, SuffixedArchivesBelongToTheNameBeforeTheLastSeparator) {
  for (const auto* name : {"Mod - Patch - Main.ba2", "Late.ba2"}) write_file(data() / name, bytes_of("x"));
  const auto names = [&](const std::vector<std::string>& plugins) {
    std::vector<std::string> out;
    for (const auto& path : fo4::archive::archive_load_order(data(), {}, plugins)) out.push_back(path.filename().string());
    return out;
  };
  EXPECT_EQ(names({"Mod.esp", "Late.esp", "Mod - Patch.esp"}),
            (std::vector<std::string>{"Late.ba2", "Mod - Patch - Main.ba2"}));
  EXPECT_EQ(names({"Mod.esp"}), std::vector<std::string>{});
}

TEST_F(Ba2ReaderTest, ErrorsNameNonAnsiPathsInUtf8) {
  const auto folder = root_ / std::filesystem::path(std::u16string{0x041C, 0x043E, 0x0434, 0x044B});
  write_file(folder / "Broken.ba2", bytes_of("not an archive"));
  const auto expect_utf8 = [&](const auto& action) {
    try {
      action();
      ADD_FAILURE() << "expected an error";
    } catch (const std::exception& error) {
      EXPECT_NE(std::string(error.what()).find("\xD0\x9C\xD0\xBE\xD0\xB4\xD1\x8B"), std::string::npos);
    }
  };
  expect_utf8([&] { (void)fo4::archive::Ba2Reader::open(folder / "Broken.ba2"); });
  expect_utf8([&] { (void)fo4::archive::Ba2Reader::open(folder / "Missing.ba2"); });
  expect_utf8([&] { (void)fo4::archive::read_ba2_general_file(folder / "Missing.ba2"); });
  expect_utf8([&] { (void)fo4::archive::read_ba2_texture_file(folder / "Missing.ba2"); });
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  expect_utf8([&] { (void)assets.read({fo4::archive::AssetOrigin::loose, folder / "Missing.nif", 0, 0}); });
  expect_utf8([&] { (void)fo4::read_plugin_file(folder / "Missing.esp"); });
  std::filesystem::create_directories(folder / "Blocked.esp" / "inner");
  expect_utf8([&] { fo4::io::write_file_atomic(folder / "Blocked.esp", bytes_of("payload")); });
}

TEST_F(Ba2ReaderTest, LooseFilesThenLaterArchivesWin) {
  const auto low = write_general("Low.ba2", {general("Meshes\\Shared.nif", "low"), general("Meshes\\LowOnly.nif", "low-only")});
  const auto high = write_general("High.ba2", {general("meshes\\shared.nif", "high"), general("meshes\\over.nif", "archived")});
  write_file(data() / "MESHES" / "Over.NIF", bytes_of("loose"));
  const std::vector<std::filesystem::path> archives{low, high};
  const auto assets = fo4::archive::AssetResolver::open(data(), archives);

  const auto shared = assets.locate("Data\\Meshes\\Shared.nif");
  ASSERT_TRUE(shared.has_value());
  EXPECT_EQ(shared->origin, fo4::archive::AssetOrigin::archive);
  EXPECT_EQ(shared->archive, 1u);
  EXPECT_EQ(text_of(assets.read(*shared)), "high");
  EXPECT_EQ(text_of(assets.read(*assets.locate("meshes/lowonly.nif"))), "low-only");

  const auto over = assets.locate("meshes/over.nif");
  ASSERT_TRUE(over.has_value());
  EXPECT_EQ(over->origin, fo4::archive::AssetOrigin::loose);
  EXPECT_EQ(text_of(assets.read(*over)), "loose");
  EXPECT_EQ(assets.locate_in_archives("meshes/over.nif")->archive, 1u);

  EXPECT_FALSE(assets.locate("meshes/none.nif").has_value());
  EXPECT_FALSE(assets.locate("meshes").has_value());
  EXPECT_THROW(static_cast<void>(assets.locate("meshes/../secret.nif")), fo4::archive::AssetPathError);
  EXPECT_THROW(static_cast<void>(assets.locate("/data/")), fo4::archive::AssetPathError);
  EXPECT_EQ(fo4::archive::normalize_asset_path("\\Data\\Meshes//A.NIF"), "meshes/a.nif");

  const auto archive_only = fo4::archive::AssetResolver::open({}, archives);
  EXPECT_EQ(archive_only.locate("meshes/over.nif")->origin, fo4::archive::AssetOrigin::archive);
}

TEST_F(Ba2ReaderTest, ListingReturnsTheSameWinnersAsLocateAndKeepsShadowedCopiesBehindThem) {
  const auto low = write_general("Low.ba2", {general("Meshes\\Shared.nif", "low"), general("Meshes\\LowOnly.nif", "low-only"),
                                            general("Meshes\\a.hkx", "x"), general("Textures\\t.nif", "t")});
  const auto high = write_general("High.ba2", {general("meshes\\shared.nif", "high"), general("meshes\\over.nif", "archived")});
  const auto unnamed = write_general("Unnamed.ba2", {general("meshes\\lowonly.nif", "h")}, false);
  const auto odd = write_general("Odd.ba2", {general("Meshes\\\\Rain.nif", "rain"),
                                            general("Data\\Meshes\\Hail.nif", "hail")});
  write_file(data() / "MESHES" / "Over.NIF", bytes_of("loose"));
  write_file(data() / "Data" / "Meshes" / "Nested.nif", bytes_of("nested"));
  const std::vector<std::filesystem::path> archives{low, high, unnamed, odd};
  const auto assets = fo4::archive::AssetResolver::open(data(), archives);

  const auto winners = assets.list("Data\\Meshes", ".NIF", false);
  ASSERT_EQ(winners.size(), 3u);
  EXPECT_EQ(winners[0].path, "meshes/lowonly.nif");
  EXPECT_EQ(winners[0].location.archive, 2u);
  EXPECT_EQ(winners[0].size, 1u);
  EXPECT_EQ(winners[1].path, "meshes/over.nif");
  EXPECT_EQ(winners[1].location.origin, fo4::archive::AssetOrigin::loose);
  EXPECT_EQ(winners[1].size, 5u);
  EXPECT_EQ(winners[2].path, "meshes/shared.nif");
  EXPECT_EQ(winners[2].location.archive, 1u);
  for (const auto& winner : winners) {
    EXPECT_FALSE(winner.shadowed);
    const auto located = assets.locate(winner.path);
    ASSERT_TRUE(located.has_value());
    EXPECT_EQ(located->origin, winner.location.origin);
    EXPECT_EQ(located->archive, winner.location.archive);
    EXPECT_EQ(located->entry, winner.location.entry);
  }

  const auto all = assets.list("meshes", ".nif", true);
  ASSERT_EQ(all.size(), 6u);
  EXPECT_EQ((std::vector<bool>{all[0].shadowed, all[1].shadowed, all[2].shadowed, all[3].shadowed, all[4].shadowed, all[5].shadowed}),
            (std::vector<bool>{false, true, false, true, false, true}));
  EXPECT_EQ(all[1].location.archive, 0u);
  EXPECT_EQ(all[3].location.origin, fo4::archive::AssetOrigin::archive);
  EXPECT_EQ(all[3].location.archive, 1u);
  EXPECT_EQ(all[5].location.archive, 0u);
  EXPECT_FALSE(assets.locate("meshes/nested.nif").has_value());

  EXPECT_EQ(assets.list("", ".hkx", false).size(), 1u);
  EXPECT_EQ(assets.list("mesh", ".nif", false).size(), 0u);
  EXPECT_THROW(static_cast<void>(assets.list("meshes/../x", "", false)), fo4::archive::AssetPathError);

  EXPECT_FALSE(assets.locate("meshes/rain.nif").has_value());
  EXPECT_FALSE(assets.locate("meshes/hail.nif").has_value());
  for (const auto& item : all) EXPECT_TRUE(assets.locate(item.path).has_value()) << item.path;

  write_file(data() / "meshes" / "foonif", bytes_of("suffix"));
  const auto bare = assets.list("", "nif", false);
  EXPECT_EQ(bare.size(), assets.list("", ".nif", false).size());
  EXPECT_EQ(std::count_if(bare.begin(), bare.end(),
                          [](const auto& item) { return item.path == "meshes/foonif"; }),
            0);
}

TEST_F(Ba2ReaderTest, RejectsDriveAndStreamPaths) {
  const auto assets = fo4::archive::AssetResolver::open(data(), {});
  for (const auto* path : {"C:/outside.nif", "C:outside.nif", "meshes/C:/outside.nif", "meshes/a.nif:stream"}) {
    SCOPED_TRACE(path);
    EXPECT_THROW(static_cast<void>(assets.locate(path)), fo4::archive::AssetPathError);
    EXPECT_THROW(static_cast<void>(assets.locate_in_archives(path)), fo4::archive::AssetPathError);
  }
}

TEST_F(Ba2ReaderTest, ExtensionFilterMatchesMultiDotSuffixesOnADotBoundary) {
  write_file(data() / "meshes" / "pack.tar.gz", bytes_of("pack"));
  write_file(data() / "meshes" / "pack.gz", bytes_of("gz"));
  write_file(data() / "meshes" / "xtar.gz", bytes_of("xtar"));
  write_file(data() / "meshes" / "foonif", bytes_of("suffix"));
  write_file(data() / ".gz", bytes_of("top"));
  const auto assets = fo4::archive::AssetResolver::open(data(), {});

  const auto tarballs = assets.list("meshes", ".tar.gz", false);
  ASSERT_EQ(tarballs.size(), 1u);
  EXPECT_EQ(tarballs[0].path, "meshes/pack.tar.gz");
  EXPECT_EQ(tarballs[0].size, 4u);

  // Constraint: the caller may omit the leading dot, but the match must still start on one.
  const auto bare = assets.list("meshes", "tar.gz", false);
  ASSERT_EQ(bare.size(), 1u);
  EXPECT_EQ(bare[0].path, "meshes/pack.tar.gz");

  // Constraint: a one-dot filter must still name the last dot component.
  EXPECT_EQ(assets.list("meshes", ".gz", false).size(), 3u);
  EXPECT_EQ(assets.list("", ".gz", false).size(), 4u);
  EXPECT_EQ(assets.list("meshes", ".nif", false).size(), 0u);
  EXPECT_EQ(assets.list("meshes", "gzip", false).size(), 0u);
}

TEST_F(Ba2ReaderTest, StringTablesFallBackToArchives) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x81, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, 0x800);
  fo4::test::ByteWriter full;
  full.u32(0x10);
  fo4::test::write_subrecord(out, "FULL", full.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  auto plugin = write_file(data() / "Base.esm", out.bytes);

  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings,
                                                 {{0x10, bytes_of("Archived Commonwealth")}}};
  const auto table_bytes = fo4::localization::write_string_table(table);
  const auto archive = write_general("Base - Main.ba2", {general("Strings\\Base_en.STRINGS", text_of(table_bytes))});

  const std::vector<std::filesystem::path> plugins{plugin};
  const auto env = fo4::env::Environment::load(plugins);
  const std::vector<std::filesystem::path> archives{archive};
  const auto assets = fo4::archive::AssetResolver::open(data(), archives);
  const auto tables = fo4::env::StringTables::load(env, data() / "Strings", "en", &assets);
  const auto* path = tables.table_path(0, fo4::env::StringSource::normal);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->parent_path().filename(), "Strings");
  EXPECT_EQ(tables.resolve_name(env, env.versions({fo4::PluginKey::from_filename("Base.esm"),
                                                   fo4::LocalFormId{0x800}})[0]),
            std::optional<std::string_view>{"Archived Commonwealth"});

  const auto without = fo4::env::StringTables::load(env, data() / "Strings", "en");
  EXPECT_EQ(without.table_path(0, fo4::env::StringSource::normal), nullptr);

  std::filesystem::create_directories(data() / "Strings");
  fo4::localization::write_string_table_file(
      {fo4::localization::StringTableKind::strings, {{0x10, bytes_of("Loose Commonwealth")}}},
      data() / "Strings" / "Base_en.STRINGS");
  const auto loose = fo4::env::StringTables::load(env, data() / "Strings", "en", &assets);
  EXPECT_EQ(loose.lookup(0, fo4::env::StringSource::normal, 0x10), std::optional<std::string_view>{"Loose Commonwealth"});
}

TEST_F(Ba2ReaderTest, LooseRootsResolveHighToLowAndShadowLowerRoots) {
  const auto archive = write_general("Base.ba2", {general("meshes/a.nif", "arc"), general("meshes/b.nif", "arc")});
  const auto mod_high = data().parent_path() / "ModHigh";
  const auto mod_low = data().parent_path() / "ModLow";
  write_file(mod_high / "Meshes" / "A.nif", bytes_of("high"));
  write_file(mod_low / "Meshes" / "a.nif", bytes_of("low"));
  write_file(mod_low / "Meshes" / "c.nif", bytes_of("c"));
  write_file(data() / "meshes" / "c.nif", bytes_of("data"));
  const std::vector<std::filesystem::path> roots{mod_high, mod_low, data()};
  const std::vector<std::filesystem::path> archives{archive};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, archives);

  EXPECT_EQ(text_of(assets.read(*assets.locate("meshes/a.nif"))), "high");
  EXPECT_EQ(text_of(assets.read(*assets.locate("meshes/c.nif"))), "c");
  EXPECT_EQ(assets.locate("meshes/b.nif")->origin, fo4::archive::AssetOrigin::archive);

  const auto all = assets.list("meshes", ".nif", true);
  ASSERT_EQ(all.size(), 6u);
  EXPECT_EQ((std::vector<bool>{all[0].shadowed, all[1].shadowed, all[2].shadowed, all[3].shadowed, all[4].shadowed, all[5].shadowed}),
            (std::vector<bool>{false, true, true, false, false, true}));
  EXPECT_EQ(all[0].location.loose_path, mod_high / "Meshes" / "A.nif");
  EXPECT_EQ(all[1].location.loose_path, mod_low / "Meshes" / "a.nif");
  EXPECT_EQ(all[2].location.origin, fo4::archive::AssetOrigin::archive);
  EXPECT_EQ(all[4].path, "meshes/c.nif");

  const auto none = fo4::archive::AssetResolver::open_roots({}, archives);
  EXPECT_EQ(none.locate("meshes/a.nif")->origin, fo4::archive::AssetOrigin::archive);

  const std::string high_text = mod_high.string();
  const std::string low_text = mod_low.string();
  const std::string archive_text = archive.string();
  const char* root_paths[] = {high_text.c_str(), low_text.c_str()};
  const char* archive_paths[] = {archive_text.c_str()};
  radicle_assets* handle = nullptr;
  ASSERT_EQ(radicle_assets_open_roots(root_paths, 2, archive_paths, 1, &handle), RADICLE_STATUS_OK);
  radicle_asset_location location{};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_assets_read(handle, "meshes/a.nif", &bytes, &size, &location), RADICLE_STATUS_OK);
  EXPECT_EQ(location.origin, RADICLE_ASSET_LOOSE);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), size), "high");
  radicle_free_bytes(bytes);
  radicle_assets_destroy(handle);
  EXPECT_EQ(radicle_assets_open_roots(nullptr, 1, archive_paths, 1, &handle), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(Ba2ReaderTest, RepeatedLooseRootIsKeptOnceAtItsFirstPosition) {
  const auto root_a = data().parent_path() / "ModA";
  const auto root_b = data().parent_path() / "ModB";
  write_file(root_a / "Meshes" / "a.nif", bytes_of("a"));
  write_file(root_b / "Meshes" / "b.nif", bytes_of("b"));
  const std::vector<std::filesystem::path> roots{root_a, root_a, root_b};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});

  ASSERT_EQ(assets.roots().size(), 2u);
  EXPECT_EQ(assets.roots()[0], root_a);
  EXPECT_EQ(assets.roots()[1], root_b);
  const auto all = assets.list("meshes", ".nif", true);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].path, "meshes/a.nif");
  EXPECT_EQ(all[0].location.loose_path, root_a / "Meshes" / "a.nif");
  EXPECT_FALSE(all[0].shadowed);
  EXPECT_FALSE(all[1].shadowed);
}

TEST_F(Ba2ReaderTest, LooseWalkTerminatesOnDirectorySymlinkBackToItsParent) {
  write_file(data() / "meshes" / "x.nif", bytes_of("x"));
  if (!create_directory_alias(data() / "meshes", data() / "meshes" / "up")) {
    GTEST_SKIP() << "directory symlinks and junctions are both unavailable in this session";
  }
  const auto assets = fo4::archive::AssetResolver::open(data(), {});
  const auto all = assets.list("", ".nif", true);
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].path, "meshes/x.nif");
  EXPECT_EQ(text_of(assets.read(*assets.locate("meshes/x.nif"))), "x");
}

TEST_F(Ba2ReaderTest, LooseWalkListsASymlinkedSiblingUnderBothKeys) {
  write_file(data() / "meshes" / "x.nif", bytes_of("x"));
  if (!create_directory_alias(data() / "meshes", data() / "a_link")) {
    GTEST_SKIP() << "directory symlinks and junctions are both unavailable in this session";
  }
  const auto assets = fo4::archive::AssetResolver::open(data(), {});
  const auto all = assets.list("", ".nif", true);
  std::vector<std::string> keys;
  for (const auto& item : all) keys.push_back(item.path);
  EXPECT_EQ(keys, (std::vector<std::string>{"a_link/x.nif", "meshes/x.nif"}));
  for (const auto& key : {"a_link/x.nif", "meshes/x.nif"}) {
    SCOPED_TRACE(key);
    const auto located = assets.locate(key);
    ASSERT_TRUE(located.has_value());
    EXPECT_EQ(text_of(assets.read(*located)), "x");
  }
}

TEST_F(Ba2ReaderTest, LooseWalkLeavesTheDataFolderOutOfTheTree) {
  write_file(data() / "data" / "meshes" / "x.nif", bytes_of("nested"));
  write_file(data() / "meshes" / "x.nif", bytes_of("real"));
  const auto assets = fo4::archive::AssetResolver::open(data(), {});
  const auto all = assets.list("meshes", ".nif", true);
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].path, "meshes/x.nif");
  EXPECT_FALSE(all[0].shadowed);
  EXPECT_EQ(all[0].location.loose_path, data() / "meshes" / "x.nif");
}

TEST_F(Ba2ReaderTest, CApiWritesGeneralArchivesThatReadBack) {
  const std::string big(4096, 'm');
  const radicle_ba2_file files[] = {
      {"Meshes\\Gun.nif", reinterpret_cast<const uint8_t*>("gun"), 3},
      {"Materials\\Gun.bgsm", reinterpret_cast<const uint8_t*>(big.data()), big.size()},
      {"Empty.txt", nullptr, 0}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 3, 1, 1, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto path = write_file(data() / "Written.ba2", written);
  const auto reader = Ba2Reader::open(path);
  ASSERT_EQ(reader.entries().size(), 3u);
  EXPECT_EQ(text_of(reader.extract(*reader.find("meshes/gun.nif"))), "gun");
  EXPECT_EQ(reader.extract(*reader.find("materials/gun.bgsm")).size(), 4096u);
  EXPECT_EQ(reader.entries()[1].name_hash, fo4::archive::compute_ba2_hashes("Materials\\Gun.bgsm").name);

  ASSERT_EQ(radicle_ba2_write_general(files, 3, 8, 0, &bytes, &size), RADICLE_STATUS_OK);
  radicle_free_bytes(bytes);
  EXPECT_NE(radicle_ba2_write_general(files, 3, 5, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(bytes, nullptr);
  const radicle_ba2_file no_path[] = {{nullptr, nullptr, 0}};
  EXPECT_EQ(radicle_ba2_write_general(no_path, 1, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ba2_write_general(nullptr, 1, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(Ba2ReaderTest, CApiWritesPinnedBytesForOneGeneralEntry) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"a.txt", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<uint8_t> got(bytes, bytes + size);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const std::vector<uint8_t> expected = {
      0x42, 0x54, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00, 0x47, 0x4E, 0x52, 0x4C,
      0x01, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xCE, 0x51, 0xB5, 0x3A, 0x74, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x01, 0x10, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0D, 0xF0, 0xAD, 0xBA,
      0x78, 0x05, 0x00, 0x61, 0x2E, 0x74, 0x78, 0x74,
  };
  EXPECT_EQ(got, expected);

  const auto reader = Ba2Reader::open(write_file(data() / "Pinned.ba2", written));
  ASSERT_EQ(reader.entries().size(), 1u);
  EXPECT_EQ(reader.entries()[0].name_hash, 0x3AB551CEu);
  EXPECT_EQ(text_of(reader.extract(*reader.find("a.txt"))), "x");
}

TEST_F(Ba2ReaderTest, CApiWritesVersionSevenGeneralArchives) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"a.txt", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 7, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto reader = Ba2Reader::open(write_file(data() / "Seven.ba2", written));
  EXPECT_EQ(reader.version(), 7u);
  EXPECT_EQ(text_of(reader.extract(*reader.find("a.txt"))), "x");
}

TEST_F(Ba2ReaderTest, CApiWritesHeaderOnlyArchiveForZeroEntries) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(nullptr, 0, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<uint8_t> got(bytes, bytes + size);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const std::vector<uint8_t> expected = {0x42, 0x54, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00,
                                         0x47, 0x4E, 0x52, 0x4C, 0x00, 0x00, 0x00, 0x00,
                                         0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(got, expected);

  const auto reader = Ba2Reader::open(write_file(data() / "Empty.ba2", written));
  EXPECT_EQ(reader.version(), 1u);
  EXPECT_TRUE(reader.entries().empty());
}

TEST_F(Ba2ReaderTest, CApiWriteGeneralReportsCallerErrorsAsInvalidArgument) {
  const char payload[] = "x";
  const auto buffer = reinterpret_cast<const uint8_t*>(payload);
  uint8_t* bytes = nullptr;
  size_t size = 0;

  const radicle_ba2_file files[] = {{"Meshes\\Gun.nif", buffer, 1}};
  const auto refuse = [&](const radicle_ba2_file* entries, size_t count, uint32_t version) {
    bytes = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(1));
    size = 7;
    EXPECT_EQ(radicle_ba2_write_general(entries, count, version, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
  };

  refuse(files, 1, 5);
  EXPECT_STREQ(radicle_last_error(), "unsupported BA2 general version 5; supported GNRL versions are 1, 7 and 8");
  refuse(files, 1, 0);

  const radicle_ba2_file trailing[] = {{"Meshes\\", buffer, 1}};
  const radicle_ba2_file separator[] = {{"\\", buffer, 1},
                                        {"/", buffer, 1},
                                        {"Meshes/", buffer, 1}};
  const radicle_ba2_file non_1252[] = {{"Meshes\\\xC4\x80.nif", buffer, 1},
                                       {"Meshes\\\xFF.nif", buffer, 1}};
  refuse(trailing, 1, 1);
  for (const auto& entry : separator) refuse(&entry, 1, 1);
  for (const auto& entry : non_1252) refuse(&entry, 1, 1);
  const radicle_ba2_file dot_paths[] = {{".", buffer, 1}, {"\\.", buffer, 1}};
  for (const auto& entry : dot_paths) refuse(&entry, 1, 1);

  // Constraint: the 259-byte fixture must carry a directory so both hashes stay non-zero.
  const std::string at_hash_limit(260, 'a');
  const std::string under_hash_limit = "Meshes\\" + std::string(252, 'a');
  ASSERT_EQ(under_hash_limit.size(), 259u);
  const radicle_ba2_file too_long[] = {{at_hash_limit.c_str(), buffer, 1}};
  const radicle_ba2_file longest[] = {{under_hash_limit.c_str(), buffer, 1}};
  refuse(too_long, 1, 1);
  ASSERT_EQ(radicle_ba2_write_general(longest, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  bytes = nullptr;
  size = 0;

  const auto hashes = fo4::archive::compute_ba2_hashes(under_hash_limit);
  EXPECT_NE(hashes.name, 0u);
  EXPECT_NE(hashes.directory, 0u);
  const auto longest_reader = Ba2Reader::open(write_file(data() / "Longest.ba2", written));
  ASSERT_EQ(longest_reader.entries().size(), 1u);
  EXPECT_EQ(longest_reader.entries()[0].name_hash, hashes.name);
  EXPECT_EQ(longest_reader.entries()[0].directory_hash, hashes.directory);

#if SIZE_MAX > 0xFFFFFFFFu
  const radicle_ba2_file oversized[] = {{"Meshes\\Gun.nif", buffer, size_t{1} << 32}};
  refuse(oversized, 1, 1);
  refuse(files, size_t{1} << 32, 1);
#endif

  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_NE(bytes, nullptr);
  radicle_free_bytes(bytes);
}

TEST_F(Ba2ReaderTest, CApiWrittenNonAsciiPathsHashTheirWindows1252Bytes) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"Meshes\\Caf\xC3\xA9.nif", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto reader = Ba2Reader::open(write_file(data() / "Cafe.ba2", written));
  EXPECT_EQ(text_of(reader.extract(*reader.find("meshes/caf\xC3\xA9.nif"))), "x");
  EXPECT_EQ(reader.entries()[0].name_hash, 0x8AF76F07u);
  EXPECT_EQ(reader.entries()[0].directory_hash, 0x0B70CE37u);
  EXPECT_EQ(reader.entries()[0].extension_hash, 0x0066696Eu);
  EXPECT_EQ(reader.entries()[0].name_hash, fo4::archive::compute_ba2_hashes("Meshes\\Caf\xE9.nif").name);
  EXPECT_NE(reader.entries()[0].name_hash, fo4::archive::compute_ba2_hashes("Meshes\\Caf\xC3\xA9.nif").name);
}

TEST_F(Ba2ReaderTest, CApiReadsArchivesAndAssets) {
  const auto low = write_general("Low.ba2", {general("Meshes\\Shared.nif", "low")});
  const auto high = write_general("High.ba2", {general("Meshes\\Shared.nif", "high")});

  radicle_ba2* ba2 = nullptr;
  EXPECT_EQ(radicle_ba2_open((data() / "Missing.ba2").string().c_str(), &ba2), RADICLE_STATUS_PARSE_ERROR);
  ASSERT_EQ(radicle_ba2_open(high.string().c_str(), &ba2), RADICLE_STATUS_OK);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(ba2, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.kind, RADICLE_BA2_GENERAL);
  EXPECT_EQ(info.entry_count, 1u);
  radicle_ba2_entry_info entry{};
  ASSERT_EQ(radicle_ba2_get_entry(ba2, 0, &entry), RADICLE_STATUS_OK);
  EXPECT_STREQ(entry.path, "Meshes\\Shared.nif");
  EXPECT_EQ(entry.unpacked_size, 4u);
  EXPECT_EQ(radicle_ba2_get_entry(ba2, 1, &entry), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t index = 9;
  ASSERT_EQ(radicle_ba2_find(ba2, "meshes/shared.nif", &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(radicle_ba2_find(ba2, "meshes/none.nif", &index), RADICLE_STATUS_NOT_FOUND);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_extract(ba2, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), size), "high");
  radicle_free_bytes(bytes);
  radicle_ba2_close(ba2);

  write_file(data() / "Mod.ba2", bytes_of("x"));
  const char* ini[] = {"Missing.ba2"};
  const char* plugins[] = {"Mod.esp"};
  char** order = nullptr;
  size_t order_count = 0;
  ASSERT_EQ(radicle_archive_load_order(data().string().c_str(), ini, 1, plugins, 1, &order, &order_count),
            RADICLE_STATUS_OK);
  ASSERT_EQ(order_count, 1u);
  EXPECT_EQ(std::filesystem::path(order[0]).filename(), "Mod.ba2");
  radicle_free_string_array(order, order_count);

  const std::string low_path = low.string();
  const std::string high_path = high.string();
  const char* archives[] = {low_path.c_str(), high_path.c_str()};
  radicle_assets* assets = nullptr;
  ASSERT_EQ(radicle_assets_open(nullptr, archives, 2, &assets), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_assets_archive_count(assets), 2u);
  const radicle_ba2* borrowed = nullptr;
  ASSERT_EQ(radicle_assets_get_archive(assets, 0, &borrowed), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_ba2_get_info(borrowed, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.entry_count, 1u);

  radicle_asset_location location{};
  ASSERT_EQ(radicle_assets_locate(assets, "Meshes/Shared.nif", &location), RADICLE_STATUS_OK);
  EXPECT_EQ(location.origin, RADICLE_ASSET_ARCHIVE);
  EXPECT_EQ(location.archive_index, 1u);
  EXPECT_EQ(radicle_assets_locate(assets, "../x", &location), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_assets_read(assets, "meshes/none.nif", &bytes, &size, nullptr), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_assets_read(assets, "meshes/shared.nif", &bytes, &size, &location), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), size), "high");
  radicle_free_bytes(bytes);
  radicle_asset_list* list = nullptr;
  radicle_asset_list_entry listed{};
  EXPECT_EQ(radicle_assets_list(nullptr, "", "", 0, &list), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_assets_list(assets, "../x", "", 0, &list), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(list, nullptr);
  ASSERT_EQ(radicle_assets_list(assets, nullptr, nullptr, 1, &list), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_asset_list_count(list), 2u);
  ASSERT_EQ(radicle_asset_list_get(list, 0, &listed), RADICLE_STATUS_OK);
  EXPECT_STREQ(listed.path, "meshes/shared.nif");
  EXPECT_EQ(listed.location.archive_index, 1u);
  EXPECT_EQ(listed.size, 4u);
  EXPECT_EQ(listed.shadowed, 0);
  ASSERT_EQ(radicle_asset_list_get(list, 1, &listed), RADICLE_STATUS_OK);
  EXPECT_EQ(listed.location.archive_index, 0u);
  EXPECT_EQ(listed.shadowed, 1);
  EXPECT_EQ(radicle_asset_list_get(list, 2, &listed), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_asset_list_destroy(list);
  radicle_asset_list_destroy(nullptr);
  radicle_assets_destroy(assets);
}
