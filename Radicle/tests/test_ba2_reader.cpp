#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/c_api_env.h"
#include "fo4/environment/strings.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/io/sha256.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "ba2_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ba2_fixture::Ba2ReaderTest;
using ba2_fixture::bytes_of;
using ba2_fixture::general;
using ba2_fixture::text_of;

using fo4::archive::Ba2Error;
using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;
using fo4::archive::Ba2Reader;
using fo4::archive::Ba2ByteSource;
using fo4::archive::Ba2Snapshot;
using fo4::archive::Ba2TextureArchive;
using fo4::archive::Ba2TextureEntry;

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

}  // namespace

namespace {

class MutableBa2Source final : public Ba2ByteSource {
public:
  explicit MutableBa2Source(std::vector<std::byte> bytes) : bytes(std::move(bytes)) {}

  std::uint64_t size() const noexcept override { return bytes.size(); }
  void read(std::uint64_t offset, std::span<std::byte> out) const override {
    if (offset > bytes.size() || out.size() > bytes.size() - offset) throw Ba2Error("test source truncated");
    if (offset == watch_offset && out.size() == watch_size) ++watched_reads;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    if (mutate_after_read) { mutate_after_read = false; bytes.at(mutation_offset) ^= std::byte{1}; }
  }

  mutable std::vector<std::byte> bytes;
  mutable bool mutate_after_read{false};
  std::size_t mutation_offset{0};
  std::uint64_t watch_offset{std::numeric_limits<std::uint64_t>::max()};
  std::size_t watch_size{0};
  mutable std::size_t watched_reads{0};
};

}  // namespace

// Detects: held source hashing must include every byte across buffer boundaries without requiring an archive parse.
TEST_F(Ba2ReaderTest, HeldSourceHashIncludesAllChunks) {
  std::vector<std::byte> data(150000, std::byte{42});
  data[65536] = std::byte{7};
  auto source = std::make_shared<MutableBa2Source>(data);
  EXPECT_EQ(fo4::archive::hash_byte_source(source), fo4::io::sha256_hex(data));
  source->bytes.back() ^= std::byte{1};
  EXPECT_NE(fo4::archive::hash_byte_source(source), fo4::io::sha256_hex(data));
}

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

// Detects: a held archive snapshot must bound extraction and reject source mutation or truncation.
TEST_F(Ba2ReaderTest, SnapshotHashesBoundedPayloadAndRejectsSourceMutation) {
  Ba2GeneralArchive archive;
  archive.entries.push_back(general("Meshes\\Weapons\\Gun.nif", "gun-bytes"));
  archive.entries.push_back(general("Strings\\Small.txt", std::string(16, 'x'), true));
  const auto archive_bytes = fo4::archive::write_ba2_general(archive);
  auto source = std::make_shared<MutableBa2Source>(archive_bytes);
  auto snapshot = Ba2Snapshot::open(source);
  // Detects: a crafted archive count or index footprint must be rejected before allocating an oversized snapshot index.
  EXPECT_THROW(static_cast<void>(Ba2Snapshot::open(source, {1, 1024 * 1024})), Ba2Error);
  EXPECT_THROW(static_cast<void>(Ba2Snapshot::open(source, {100, 1})), Ba2Error);
  auto huge_count = archive_bytes;
  std::fill_n(huge_count.begin() + 12, 4, std::byte{0xff});
  try {
    static_cast<void>(Ba2Snapshot::open(std::make_shared<MutableBa2Source>(std::move(huge_count))));
    FAIL() << "oversized snapshot index was accepted";
  } catch (const Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("index entry limit"), std::string::npos);
  }
  EXPECT_EQ(snapshot.archive_sha256().size(), 64u);
  ASSERT_EQ(snapshot.find("meshes/weapons/gun.nif"), std::optional<std::size_t>{0});
  const auto payload = snapshot.extract(0, 64);
  EXPECT_EQ(text_of(payload.bytes), "gun-bytes");
  EXPECT_EQ(payload.size, 9u);
  EXPECT_EQ(payload.sha256, "87b7698dfa70ccec8bad7082415d04af8f9a4e87233bbeefdd3766df77b9fcdb");
  EXPECT_THROW(static_cast<void>(snapshot.extract(0, 8)), Ba2Error);
  ASSERT_GT(snapshot.entries()[1].chunks[0].packed_size, 1u);
  EXPECT_THROW(static_cast<void>(snapshot.extract(1, 1)), Ba2Error);
  const std::array<std::size_t, 2> indices{0, 1};
  const auto batch = snapshot.extract_many(indices, 64);
  ASSERT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch[0].bytes, payload.bytes);
  EXPECT_EQ(batch[1].size, 16u);
  EXPECT_THROW(static_cast<void>(snapshot.extract_many(indices, 10)), Ba2Error);
  // Detects: a multi-table batch must perform a constant number of whole-source hashes, independent of table count.
  auto counted = std::make_shared<MutableBa2Source>(archive_bytes);
  counted->watch_offset = 0; counted->watch_size = archive_bytes.size();
  const auto countedSnapshot = Ba2Snapshot::open(counted);
  ASSERT_EQ(counted->watched_reads, 2u);
  ASSERT_EQ(countedSnapshot.extract_many(indices, 64).size(), 2u);
  EXPECT_EQ(counted->watched_reads, 4u);
  const std::array<std::size_t, 8> repeated{0, 1, 0, 1, 0, 1, 0, 1};
  ASSERT_EQ(countedSnapshot.extract_many(repeated, 512).size(), repeated.size());
  EXPECT_EQ(counted->watched_reads, 6u);

  auto changing_open_source = std::make_shared<MutableBa2Source>(archive_bytes);
  changing_open_source->mutation_offset = static_cast<std::size_t>(snapshot.entries()[0].chunks[0].offset);
  changing_open_source->mutate_after_read = true;
  EXPECT_THROW(static_cast<void>(Ba2Snapshot::open(changing_open_source)), Ba2Error);

  auto changing_source = std::make_shared<MutableBa2Source>(archive_bytes);
  const auto changing = Ba2Snapshot::open(changing_source);
  changing_source->mutate_after_read = true;
  EXPECT_THROW(static_cast<void>(changing.extract(0, 64)), Ba2Error);

  source->bytes.at(static_cast<std::size_t>(snapshot.entries()[0].chunks[0].offset)) =
      std::byte{static_cast<unsigned char>('X')};
  EXPECT_THROW(static_cast<void>(snapshot.extract(0, 64)), Ba2Error);

  auto truncated_source = std::make_shared<MutableBa2Source>(archive_bytes);
  auto truncated = Ba2Snapshot::open(truncated_source);
  truncated_source->bytes.resize(10);
  EXPECT_THROW(static_cast<void>(truncated.extract(0, 64)), Ba2Error);
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

// Detects: texture snapshot extraction must preserve DDS data and enforce the caller's byte limit.
TEST_F(Ba2ReaderTest, SnapshotExtractsDx10TextureWithinCallerLimit) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "Textures\\Rock_d.dds";
  entry.texture = {4, 8, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(std::vector<std::byte>(16, std::byte{0x5A})));
  archive.entries.push_back(entry);
  const auto path = data() / "Snapshot - Textures.ba2";
  fo4::archive::write_ba2_texture_file(archive, path);
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(stream);
  const auto file_size = stream.tellg();
  ASSERT_GE(file_size, 0);
  std::vector<std::byte> archive_bytes(static_cast<std::size_t>(file_size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(archive_bytes.data()), static_cast<std::streamsize>(archive_bytes.size()));
  ASSERT_TRUE(stream);
  const auto snapshot = Ba2Snapshot::open(std::make_shared<MutableBa2Source>(std::move(archive_bytes)));
  const auto payload = snapshot.extract(0, 304);
  ASSERT_EQ(payload.bytes.size(), 144u);
  EXPECT_EQ(text_of({payload.bytes.begin(), payload.bytes.begin() + 4}), "DDS ");
  EXPECT_EQ(payload.bytes.back(), std::byte{0x5A});
  EXPECT_THROW(static_cast<void>(snapshot.extract(0, 303)), Ba2Error);
  const std::array<std::size_t, 1> indices{0};
  EXPECT_EQ(snapshot.extract_many(indices, 304)[0].bytes, payload.bytes);
}

// Detects: texture snapshot extraction must reject an undersized output budget before reading its chunk.
TEST_F(Ba2ReaderTest, SnapshotPreflightsThe148ByteDx10Header) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "Textures\\Modern.dds";
  entry.texture = {4, 8, 1, 95, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(std::vector<std::byte>(16, std::byte{0x5A})));
  archive.entries.push_back(entry);
  const auto path = data() / "Snapshot - DX10.ba2";
  fo4::archive::write_ba2_texture_file(archive, path);
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(stream);
  const auto file_size = stream.tellg();
  ASSERT_GE(file_size, 0);
  std::vector<std::byte> archive_bytes(static_cast<std::size_t>(file_size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(archive_bytes.data()), static_cast<std::streamsize>(archive_bytes.size()));
  ASSERT_TRUE(stream);

  auto source = std::make_shared<MutableBa2Source>(std::move(archive_bytes));
  const auto snapshot = Ba2Snapshot::open(source);
  const auto chunk = snapshot.entries()[0].chunks[0];
  source->watch_offset = chunk.offset;
  source->watch_size = chunk.unpacked_size;
  EXPECT_THROW(static_cast<void>(snapshot.extract(0, 343)), Ba2Error);
  EXPECT_EQ(source->watched_reads, 0u);

  const auto payload = snapshot.extract(0, 344);
  ASSERT_EQ(payload.bytes.size(), 164u);
  EXPECT_EQ(text_of({payload.bytes.begin(), payload.bytes.begin() + 4}), "DDS ");
  EXPECT_EQ(source->watched_reads, 1u);
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
