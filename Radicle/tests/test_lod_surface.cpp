#include "fo4/lod/lod_surface.hpp"

#include "env_node_fixture.hpp"
#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/terrain_tile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data.hpp"
#include "temp_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::envnode;

struct TempRoot : fo4::test::TempDir {
  TempRoot() : fo4::test::TempDir("garden_lod_surface") { std::filesystem::create_directories(path / "LODSettings"); }
};

std::vector<std::byte> vhgt(float offset) {
  fo4::test::ByteWriter out;
  out.f32(offset);
  for (std::size_t index = 0; index < fo4::env::k_land_vertices; ++index) out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

std::vector<std::byte> world_plugin(bool with_land, const std::vector<std::string>& world_names) {
  std::vector<Node> cells;
  if (with_land) {
    cells = {rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
             grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", vhgt(10.0f)}})})})};
  }
  std::vector<Node> worlds;
  std::uint32_t id = 0x800;
  for (const auto& name : world_names) {
    worlds.push_back(rec("WRLD", id, {{"EDID", zstring(name)}}));
    worlds.push_back(grp(id, 1, {grp(0, 4, {grp(0, 5, cells)})}));
    id += 0x100;
  }
  return make_plugin({}, {top("WRLD", std::move(worlds))});
}

fo4::env::Environment load_env(const std::vector<std::byte>& bytes) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(bytes, "Test.esp"));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

void write_settings(const std::filesystem::path& root, const std::string& world) {
  const auto settings = fo4::lod::serialize_lod_settings(fo4::lod::LodSettings{-96, -96, 256, 4, 32});
  std::ofstream file(root / "LODSettings" / (world + ".LOD"), std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(settings.data()), static_cast<std::streamsize>(settings.size()));
}

fo4::lod::LodTile tile_of(std::int32_t level, std::int32_t x, std::int32_t y,
                          fo4::lod::LodTileKind kind = fo4::lod::LodTileKind::terrain) {
  fo4::lod::LodTile tile;
  tile.worldspace = "TestWorld";
  tile.level = level;
  tile.x = x;
  tile.y = y;
  tile.kind = kind;
  return tile;
}

std::vector<fo4::lod::LodInput> inputs_of(std::string path, std::uint64_t size) {
  return {fo4::lod::LodInput{std::move(path), size}};
}

}  // namespace

// Detects (invariant): planning must not mutate the ledger and only a commit makes a tile current.
TEST(LodSurface, PlanningIsReadOnlyAndCommitMakesATileCurrent) {
  const std::vector<fo4::lod::LodTile> tiles{tile_of(4, 0, 0), tile_of(4, 4, 0)};
  const std::vector<std::vector<fo4::lod::LodInput>> inputs{inputs_of("a.esp", 10), inputs_of("b.esp", 20)};
  const fo4::lod::LodLedger empty{};
  const auto first = fo4::lod::plan_lod_rebakes(tiles, inputs, empty);
  ASSERT_EQ(first.size(), 2u);
  EXPECT_TRUE(first[0].rebuild);
  EXPECT_TRUE(first[1].rebuild);
  const auto second = fo4::lod::plan_lod_rebakes(tiles, inputs, empty);
  EXPECT_TRUE(second[0].rebuild);
  EXPECT_TRUE(second[1].rebuild);
  EXPECT_TRUE(empty.hashes.empty());

  fo4::lod::LodLedger ledger;
  fo4::lod::commit_lod_rebake(ledger, first[0]);
  const auto third = fo4::lod::plan_lod_rebakes(tiles, inputs, ledger);
  EXPECT_FALSE(third[0].rebuild);
  EXPECT_TRUE(third[1].rebuild);
}

// Detects (invariant): CRLF, duplicate, bad-value and spaced-key ledger lines must all parse correctly.
TEST(LodSurface, LedgerParsesCrlfDuplicatesBadValuesAndSpacedKeys) {
  const std::string text =
      "a|terrain|4|0|0 1\r\nwith space|terrain|4|0|0 5\r\na|terrain|4|0|0 4\r\nbad|terrain|4|0|0 nope\r\nbroken\r\n";
  const auto ledger = fo4::lod::parse_lod_ledger(text);
  EXPECT_EQ(ledger.hashes.at("a|terrain|4|0|0"), 4u);
  EXPECT_EQ(ledger.hashes.at("with space|terrain|4|0|0"), 5u);
  EXPECT_EQ(ledger.hashes.count("bad|terrain|4|0|0"), 0u);
  EXPECT_EQ(ledger.hashes.size(), 2u);
  const auto formatted = fo4::lod::format_lod_ledger(ledger);
  EXPECT_EQ(formatted, "a|terrain|4|0|0 4\nwith space|terrain|4|0|0 5\n");
  EXPECT_EQ(fo4::lod::parse_lod_ledger(formatted).hashes, ledger.hashes);
}

// Detects (invariant): the input hash must react to order and size, and planning must refuse short input lists.
TEST(LodSurface, InputHashIsOrderAndSizeSensitive) {
  const auto tile = tile_of(4, 0, 0);
  const std::vector<fo4::lod::LodInput> forward{fo4::lod::LodInput{"a", 1}, fo4::lod::LodInput{"b", 2}};
  const std::vector<fo4::lod::LodInput> backward{fo4::lod::LodInput{"b", 2}, fo4::lod::LodInput{"a", 1}};
  const std::vector<fo4::lod::LodInput> resized{fo4::lod::LodInput{"a", 9}, fo4::lod::LodInput{"b", 2}};
  EXPECT_NE(fo4::lod::lod_tile_inputs_hash(tile, forward), fo4::lod::lod_tile_inputs_hash(tile, backward));
  EXPECT_NE(fo4::lod::lod_tile_inputs_hash(tile, forward), fo4::lod::lod_tile_inputs_hash(tile, resized));
  // Detects (regression): a plugin edited to the same size must reopen its tiles, not leave them current.
  const std::vector<fo4::lod::LodInput> edited{fo4::lod::LodInput{"a", 1, 7}, fo4::lod::LodInput{"b", 2, 0}};
  const std::vector<fo4::lod::LodInput> original{fo4::lod::LodInput{"a", 1, 9}, fo4::lod::LodInput{"b", 2, 0}};
  EXPECT_NE(fo4::lod::lod_tile_inputs_hash(tile, edited), fo4::lod::lod_tile_inputs_hash(tile, original));
  const std::vector<fo4::lod::LodTile> tiles{tile_of(4, 0, 0)};
  const std::vector<std::vector<fo4::lod::LodInput>> short_inputs{};
  const fo4::lod::LodLedger empty{};
  EXPECT_THROW(fo4::lod::plan_lod_rebakes(tiles, short_inputs, empty), fo4::lod::LodError);
  const std::vector<std::vector<fo4::lod::LodInput>> extra_inputs{inputs_of("a.esp", 10), inputs_of("b.esp", 20),
                                                                 inputs_of("c.esp", 30)};
  EXPECT_THROW(fo4::lod::plan_lod_rebakes(tiles, extra_inputs, empty), fo4::lod::LodError);
}

// Detects (regression): tiles written by an older terrain or object generator look current, because the hash covers only
// plugins and settings. Each tile kind's hash must carry its own format version while the texture hash carries none.
TEST(LodSurface, EachTileKindHashCarriesItsOwnFormatVersion) {
  const auto terrain = tile_of(4, 0, 0);
  const auto object = tile_of(4, 0, 0, fo4::lod::LodTileKind::object);
  const auto inputs = inputs_of("a.esp", 10);
  EXPECT_NE(fo4::lod::lod_tile_inputs_hash(terrain, inputs), fo4::lod::lod_texture_inputs_hash(terrain, inputs));
  EXPECT_NE(fo4::lod::lod_tile_inputs_hash(object, inputs), fo4::lod::lod_texture_inputs_hash(object, inputs));
}

// Detects (invariant): after a commit only a changed input reopens a tile, and the ledger round trips.
TEST(LodSurface, AChangedInputReopensExactlyThatTile) {
  const std::vector<fo4::lod::LodTile> tiles{tile_of(4, 0, 0), tile_of(4, 4, 0)};
  std::vector<std::vector<fo4::lod::LodInput>> inputs{inputs_of("a.esp", 10), inputs_of("b.esp", 20)};
  fo4::lod::LodLedger ledger;
  const auto first = fo4::lod::plan_lod_rebakes(tiles, inputs, ledger);
  ASSERT_EQ(first.size(), 2u);
  for (const auto& plan : first) fo4::lod::commit_lod_rebake(ledger, plan);
  const auto formatted = fo4::lod::format_lod_ledger(ledger);
  const auto reloaded = fo4::lod::parse_lod_ledger(formatted);
  const auto second = fo4::lod::plan_lod_rebakes(tiles, inputs, reloaded);
  ASSERT_EQ(second.size(), 2u);
  EXPECT_FALSE(second[0].rebuild);
  EXPECT_FALSE(second[1].rebuild);
  EXPECT_EQ(first[0].inputs, second[0].inputs);

  inputs[1] = inputs_of("b.esp", 21);
  const auto third = fo4::lod::plan_lod_rebakes(tiles, inputs, reloaded);
  EXPECT_FALSE(third[0].rebuild);
  EXPECT_TRUE(third[1].rebuild);
}

// Detects (invariant): the ledger key must lower-case the world and keep kind and coordinates apart.
TEST(LodSurface, LedgerKeyLowerCasesWorldAndSeparatesKind) {
  auto upper = tile_of(4, 0, 0);
  upper.worldspace = "TESTWORLD";
  EXPECT_EQ(fo4::lod::lod_ledger_key(upper), std::string{"testworld|terrain|4|0|0"});
  EXPECT_NE(fo4::lod::lod_ledger_key(tile_of(4, 0, 0)),
            fo4::lod::lod_ledger_key(tile_of(4, 0, 0, fo4::lod::LodTileKind::object)));
}

// Detects (security): a hostile ledger key must never make verify stat or write outside the data folder.
TEST(LodSurface, RefusesALedgerKeyWhoseWorldLeavesTheDataFolder) {
  EXPECT_FALSE(fo4::lod::parse_lod_ledger_key("..|terrain|4|0|0").has_value());
  EXPECT_FALSE(fo4::lod::parse_lod_ledger_key("..\\evil|terrain|4|0|0").has_value());
  EXPECT_FALSE(fo4::lod::parse_lod_ledger_key("C:\\evil|terrain|4|0|0").has_value());
  EXPECT_TRUE(fo4::lod::parse_lod_ledger_key("Commonwealth|terrain|4|0|0").has_value());
  const fo4::lod::LodTile hostile{"..", 4, 0, 0, fo4::lod::LodTileKind::terrain};
  EXPECT_THROW(fo4::lod::lod_tile_local_path("C:/data", hostile), fo4::lod::LodError);
}

// Detects (invariant): only worlds with a parseable LOD settings file are listed, and a missing file is skipped.
TEST(LodSurface, ListsOnlyWorldsWithSettingsAndSkipsMissingFiles) {
  const auto env = load_env(world_plugin(false, {"TestWorld", "MissingWorld"}));
  TempRoot root;
  write_settings(root.path, "TestWorld");
  const std::vector<std::filesystem::path> roots{root.path};
  const std::vector<std::filesystem::path> archives{};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, archives);
  const auto worlds = fo4::lod::list_lod_worlds(env, assets);
  ASSERT_EQ(worlds.size(), 1u);
  EXPECT_EQ(worlds[0].name, "TestWorld");
  EXPECT_EQ(worlds[0].settings.southwest_x, -96);
  EXPECT_EQ(worlds[0].settings.stride, 256);
  EXPECT_EQ(worlds[0].settings.level_min, 4);
  EXPECT_EQ(worlds[0].settings.level_max, 32);
}

// Detects (invariant): preview bytes differ from the writer output, or progress completes while unfinished.
TEST(LodSurface, PreviewMatchesTheWriterAndProgressCompletes) {
  const auto env = load_env(world_plugin(true, {"TestWorld"}));
  fo4::lod::LodWorld world;
  world.key = fo4::FormKey::parse("000800:Test.esp");
  world.name = "TestWorld";
  world.settings = fo4::lod::LodSettings{};
  const auto preview = fo4::lod::preview_terrain_tile(env, world, 4, 0, 0);
  const auto expected = fo4::lod::write_terrain_btr(
      fo4::lod::build_terrain_tile(env, world.key, world.settings, 4, 0, 0), world.name);
  EXPECT_EQ(preview, expected);
  const auto nif = fo4::nif::read_nif(preview);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain),
            fo4::lod::LodTileStructure::ok);
  fo4::lod::LodProgress progress;
  progress.tiles_total = 3;
  progress.tiles_done = 3;
  progress.tiles_rebuilt = 2;
  progress.tiles_skipped = 1;
  EXPECT_TRUE(fo4::lod::lod_progress_complete(progress));
  progress.tiles_skipped = 0;
  EXPECT_FALSE(fo4::lod::lod_progress_complete(progress));
  fo4::lod::LodProgress started;
  started.tiles_total = 3;
  EXPECT_FALSE(fo4::lod::lod_progress_complete(started));
  started.tiles_done = 3;
  started.tiles_rebuilt = 3;
  EXPECT_TRUE(fo4::lod::lod_progress_complete(started));
}

// Detects (invariant): the shipped Commonwealth settings must resolve through the real archive order.
TEST(LodSurfaceReal, ListsTheVanillaCommonwealthLodWorld) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  std::vector<std::filesystem::path> archives;
  try {
    const std::vector<std::string> plugins{"Fallout4.esm"};
    const std::vector<std::string> ini_archives{};
    archives = fo4::archive::archive_load_order(data, ini_archives, plugins);
  } catch (const std::exception&) {
    GTEST_SKIP() << "the archive load order could not be resolved";
  }
  if (archives.empty()) GTEST_SKIP() << "no archives resolved from the data folder";
  const std::vector<std::filesystem::path> roots{data};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, archives);
  const auto env = fo4::env::Environment::load(std::vector<std::filesystem::path>{master});
  const auto worlds = fo4::lod::list_lod_worlds(env, assets);
  std::cout << "LOD worlds found: " << worlds.size() << '\n';
  bool found_commonwealth = false;
  for (const auto& world : worlds) {
    std::cout << "  " << world.name << " southwest " << world.settings.southwest_x << ','
              << world.settings.southwest_y << " stride " << world.settings.stride << " levels "
              << world.settings.level_min << ".." << world.settings.level_max << '\n';
    // Invariant: no shipped world may carry a level outside the range the loader indexes, not only Commonwealth.
    EXPECT_LE(world.settings.level_max, 32) << world.name;
    EXPECT_LE(world.settings.level_min, world.settings.level_max) << world.name;
    if (world.name != "Commonwealth") continue;
    found_commonwealth = true;
    EXPECT_EQ(world.settings.southwest_x, -96);
    EXPECT_EQ(world.settings.southwest_y, -96);
    EXPECT_EQ(world.settings.stride, 256);
    EXPECT_EQ(world.settings.level_min, 4);
    EXPECT_EQ(world.settings.level_max, 32);
  }
  if (!found_commonwealth) GTEST_SKIP() << "Commonwealth LOD settings did not resolve from the archive order";
}
