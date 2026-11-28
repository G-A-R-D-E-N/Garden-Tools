#include "fo4/c_api_lod.h"

#include "ba2_fixture.hpp"
#include "env_node_fixture.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/lod/generate_support.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/terrain_texture.hpp"
#include "fo4/nif/nif.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace fo4::test::envnode;
using namespace fo4::test::envnode::capi_lod;

// Detects (invariant): null outputs and null handles must fail without touching memory.
TEST(CApiLod, RejectsNullHandlesAndNullOutputs) {
  EXPECT_EQ(radicle_lod_open(nullptr, nullptr, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_lod_worlds_json(nullptr, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_lod_commit_json(nullptr, nullptr, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t* bytes = nullptr;
  size_t size = 123;
  EXPECT_EQ(radicle_lod_worlds_json(nullptr, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  radicle_lod_destroy(nullptr);
}

// Detects (invariant): the session must list the settings world and plan a loose tile read-only.
TEST(CApiLod, ListsWorldsAndRunsPlanCommitCycleOnALooseTile) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  ASSERT_NE(lod, nullptr);
  uint8_t* worlds = nullptr;
  size_t worlds_size = 0;
  ASSERT_EQ(radicle_lod_worlds_json(lod, &worlds, &worlds_size), RADICLE_STATUS_OK);
  const auto worlds_text = text_of(worlds, worlds_size);
  release(worlds);
  EXPECT_NE(worlds_text.find("TestWorld"), std::string::npos);
  EXPECT_NE(worlds_text.find("\"stride\":64"), std::string::npos);

  // Invariant: a loose tile in the resolver root is what the plan must enumerate.
  const auto preview_dir = fixture.root / "Meshes" / "Terrain" / "TestWorld";
  std::filesystem::create_directories(preview_dir);
  uint8_t* nif_bytes = nullptr;
  size_t nif_size = 0;
  ASSERT_EQ(radicle_lod_preview_nif(lod, "testworld", 4, 0, 0, &nif_bytes, &nif_size), RADICLE_STATUS_OK);
  ASSERT_NE(nif_bytes, nullptr);
  std::ofstream tile_file(preview_dir / "TestWorld.4.0.0.btr", std::ios::binary | std::ios::trunc);
  tile_file.write(reinterpret_cast<const char*>(nif_bytes), static_cast<std::streamsize>(nif_size));
  tile_file.close();
  const std::vector<std::byte> nif_copy(reinterpret_cast<const std::byte*>(nif_bytes),
                                        reinterpret_cast<const std::byte*>(nif_bytes) + nif_size);
  release(nif_bytes);

  uint8_t* plan = nullptr;
  size_t plan_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  uint8_t* commit = nullptr;
  size_t commit_size = 0;
  ASSERT_EQ(radicle_lod_plan_json(lod, "", &plan, &plan_size, &ledger, &ledger_size, &commit, &commit_size),
            RADICLE_STATUS_OK);
  const auto plan_text = text_of(plan, plan_size);
  const auto ledger_text = text_of(ledger, ledger_size);
  const auto commit_text = text_of(commit, commit_size);
  release(plan);
  release(ledger);
  release(commit);
  EXPECT_NE(plan_text.find("\"rebuild\":true"), std::string::npos);
  EXPECT_FALSE(commit_text.empty());
  EXPECT_TRUE(ledger_text.empty());

  uint8_t* merged = nullptr;
  size_t merged_size = 0;
  ASSERT_EQ(radicle_lod_commit_json(ledger_text.c_str(), commit_text.c_str(), &merged, &merged_size),
            RADICLE_STATUS_OK);
  const auto merged_text = text_of(merged, merged_size);
  release(merged);
  EXPECT_EQ(merged_text, commit_text);

  ASSERT_EQ(radicle_lod_plan_json(lod, merged_text.c_str(), &plan, &plan_size, &ledger, &ledger_size, &commit,
                                  &commit_size),
            RADICLE_STATUS_OK);
  const auto second_plan = text_of(plan, plan_size);
  const auto second_commit = text_of(commit, commit_size);
  release(plan);
  release(ledger);
  release(commit);
  EXPECT_EQ(second_plan.find("\"rebuild\":true"), std::string::npos);
  EXPECT_TRUE(second_commit.empty());

  const auto nif = fo4::nif::read_nif(nif_copy);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain),
            fo4::lod::LodTileStructure::ok);
  radicle_lod_destroy(lod);
}

// Detects (invariant): CRLF plugin lists and an unknown world must be handled without silent success.
TEST(CApiLod, ToleratesCrlfPathsAndRejectsAnUnknownWorld) {
  const Fixture fixture;
  const std::string crlf_paths = fixture.plugin_paths + "\r\n";
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(crlf_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_lod_preview_nif(lod, "MissingWorld", 4, 0, 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
  radicle_lod_destroy(lod);
}

// Detects (invariant): an empty merged ledger is NULL with size 0, for empty and NULL inputs alike, and frees nothing.
TEST(CApiLod, CommitOfEmptyLedgersReturnsNullWithSizeZero) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(0x1);
  size_t size = 123;
  ASSERT_EQ(radicle_lod_commit_json("", "", &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);

  bytes = reinterpret_cast<uint8_t*>(0x1);
  size = 123;
  ASSERT_EQ(radicle_lod_commit_json(nullptr, nullptr, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
}

namespace {

// Invariant: a plan row names its kind and level, so one tile's input hash can be read out of a plan.
std::string plan_inputs_of(const std::string& plan, std::string_view kind, std::int32_t level) {
  std::size_t at = 0;
  while ((at = plan.find("{\"world\":", at)) != std::string::npos) {
    const auto end = plan.find('}', at);
    if (end == std::string::npos) return {};
    const auto row = plan.substr(at, end - at + 1);
    at = end + 1;
    if (row.find("\"kind\":\"" + std::string{kind} + '"') == std::string::npos) continue;
    if (row.find("\"level\":" + std::to_string(level) + ',') == std::string::npos) continue;
    const auto inputs_at = row.find("\"inputs\":");
    if (inputs_at == std::string::npos) return {};
    const auto start = inputs_at + 9;
    return row.substr(start, row.find_first_of(",}", start) - start);
  }
  return {};
}

// Invariant: a ledger key is "<world>|<kind>|<level>|<x>|<y>", so the third pipe ends world, kind and level.
std::string sibling_tile_key(std::string_view ledger, std::string_view suffix) {
  std::size_t pipes = 0;
  for (std::size_t index = 0; index < ledger.size(); ++index) {
    if (ledger[index] != '|') continue;
    if (++pipes == 3) return std::string(ledger.substr(0, index)) + std::string(suffix);
  }
  return std::string{};
}

std::uintmax_t file_size_of(const std::filesystem::path& path) {
  std::error_code status;
  const auto size = std::filesystem::file_size(path, status);
  return status ? 0u : size;
}

// Invariant: a written tile exceeds the NIF header, and Windows keeps the overwritten seed's .btr name.
std::vector<std::filesystem::path> written_tiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  std::error_code status;
  for (std::filesystem::recursive_directory_iterator it(root, status), end; !status && it != end;
       it.increment(status)) {
    const auto extension = it->path().extension().string();
    if (extension != ".btr" && extension != ".BTR") continue;
    if (file_size_of(it->path()) > 40) found.push_back(it->path());
  }
  return found;
}

}  // namespace

// Detects (invariant): candidates must come from the loaded worldspace; an unknown world is never a silent success.
TEST(CApiLod, ReportsObjectCandidatesAndRejectsAnUnknownWorld) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_lod_candidates_json(lod, "testworld", 4, &bytes, &size), RADICLE_STATUS_OK);
  const auto text = text_of(bytes, size);
  release(bytes);
  EXPECT_NE(text.find("\"world\":\"TestWorld\""), std::string::npos);
  EXPECT_NE(text.find("\"level\":4"), std::string::npos);
  EXPECT_NE(text.find("\"referencesSeen\":0"), std::string::npos);
  EXPECT_NE(text.find("\"candidates\":[]"), std::string::npos);
  bytes = nullptr;
  size = 123;
  EXPECT_EQ(radicle_lod_candidates_json(lod, "MissingWorld", 4, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  radicle_lod_destroy(lod);
}

// Detects (invariant): generate must stay dry without apply, must not rewrite a current tile, and must read disk.
TEST(CApiLod, GeneratesOnlyWhenAppliedAndVerifiesOnDiskState) {
  const Fixture fixture;
  const auto tile_dir = fixture.root / "Meshes" / "Terrain" / "TestWorld";
  std::filesystem::create_directories(tile_dir);
  const auto seed_tile = fo4::lod::lod_tile_local_path(
      fixture.root, fo4::lod::LodTile{std::string{"TestWorld"}, 4, 0, 0, fo4::lod::LodTileKind::terrain});
  const auto seed_relative = seed_tile.lexically_relative(fixture.root).generic_string();
  std::ofstream(seed_tile, std::ios::binary | std::ios::trunc).close();
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);

  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 0, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto dry_report = text_of(report, report_size);
  const auto dry_ledger = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(dry_report.find("\"apply\":false"), std::string::npos);
  // Invariant: planning follows the lodsettings block grid, so one cell produces one tile per level.
  EXPECT_NE(dry_report.find("\"wouldWrite\":4"), std::string::npos);
  // Detects (invariant): a dry run that replaces an existing tile without naming it.
  EXPECT_NE(dry_report.find("\"wouldOverwrite\":[\"" + seed_relative + "\"]"), std::string::npos) << dry_report;
  EXPECT_EQ(dry_report.find("wouldDelete"), std::string::npos) << dry_report;
  EXPECT_EQ(dry_report.find("Truncated"), std::string::npos) << dry_report;
  // Detects: a dry run reporting "written":0, which a reader cannot tell apart from a count it never recorded.
  expect_absent_count(dry_report, "written");
  EXPECT_TRUE(dry_ledger.empty());
  EXPECT_TRUE(written_tiles(fixture.root).empty());
  EXPECT_EQ(file_size_of(seed_tile), 0u);

  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto applied_report = text_of(report, report_size);
  const auto applied_ledger = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(applied_report.find("\"written\":4"), std::string::npos);
  EXPECT_EQ(applied_report.find("wouldOverwrite"), std::string::npos) << applied_report;
  expect_absent_count(applied_report, "failed");
  // Detects: an object-drop count filed under the tile names, which no reader can tell apart from a tile count.
  // Detects: a clean run serialising "unsupportedCandidates":0 instead of omitting the count.
  expect_absent_count(applied_report, "unsupportedCandidates");
  EXPECT_EQ(applied_report.find("\"unsupported\":"), std::string::npos) << applied_report;
  EXPECT_NE(applied_ledger.find("testworld|"), std::string::npos);
  const auto written = written_tiles(fixture.root);
  ASSERT_EQ(written.size(), 4u);
  EXPECT_GT(file_size_of(written.front()), 40u);
  const auto written_tile = fo4::lod::parse_lod_tile_path(written.front().filename().string());
  ASSERT_TRUE(written_tile.has_value());
  const auto partial_path = fo4::lod::lod_tile_local_path(
      fixture.root, fo4::lod::LodTile{written_tile->worldspace, 4, 1, 0, fo4::lod::LodTileKind::terrain});
  EXPECT_EQ(partial_path.parent_path(), written.front().parent_path());

  // Invariant: the applied ledger makes the tile current, so another apply must not write it again.
  ASSERT_EQ(radicle_lod_generate_json(lod, applied_ledger.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto repeat_report = text_of(report, report_size);
  const auto repeat_ledger = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  // Detects: a repeat run whose clean summary still carries zero tile counts.
  expect_absent_count(repeat_report, "written");
  expect_absent_count(repeat_report, "wouldWrite");
  EXPECT_EQ(repeat_ledger, applied_ledger);

  // Invariant: a truncated tile must read as partial on disk even though no ledger entry covers it.
  std::ofstream(partial_path, std::ios::binary | std::ios::trunc) << "abc";

  uint8_t* verify_bytes = nullptr;
  size_t verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, "", &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto raw_state = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(raw_state.find("\"stale\":4"), std::string::npos);
  EXPECT_NE(raw_state.find("\"partial\":1"), std::string::npos);
  EXPECT_NE(raw_state.find("\"ok\":0"), std::string::npos);

  verify_bytes = nullptr;
  verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, applied_ledger.c_str(), &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto set_state = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(set_state.find("\"ok\":4"), std::string::npos);
  EXPECT_NE(set_state.find("\"partial\":1"), std::string::npos);
  EXPECT_NE(set_state.find("\"missing\":0"), std::string::npos);
  EXPECT_NE(set_state.find("\"inArchive\":true"), std::string::npos);

  // Invariant: a ledger entry whose tile is not on disk must be reported missing, not as present.
  const auto missing_ledger = applied_ledger + sibling_tile_key(applied_ledger, "|5|5") + " 7\n";
  verify_bytes = nullptr;
  verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, missing_ledger.c_str(), &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto missing_state = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(missing_state.find("\"missing\":1"), std::string::npos);
  EXPECT_NE(missing_state.find("\"state\":\"missing\""), std::string::npos);
  EXPECT_NE(missing_state.find("\"ok\":4"), std::string::npos);
  radicle_lod_destroy(lod);
}

TEST(CApiLod, WorldScopedGenerateAndVerifyNameTheirWorld) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);

  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_world_json(lod, "testworld", "", 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto report_text = text_of(report, report_size);
  const auto world_ledger = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(report_text.find("\"world\":\"TestWorld\""), std::string::npos) << report_text;
  EXPECT_NE(report_text.find("\"written\":4"), std::string::npos) << report_text;
  EXPECT_NE(world_ledger.find("testworld|"), std::string::npos);

  report = nullptr;
  report_size = 123;
  EXPECT_EQ(radicle_lod_generate_world_json(lod, "MissingWorld", "", 0, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(report, nullptr);
  EXPECT_EQ(report_size, 0u);

  uint8_t* verify_bytes = nullptr;
  size_t verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_world_json(lod, "TestWorld", world_ledger.c_str(), &verify_bytes, &verify_size),
            RADICLE_STATUS_OK);
  const auto verify_text = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(verify_text.find("\"world\":\"TestWorld\""), std::string::npos) << verify_text;
  EXPECT_NE(verify_text.find("\"ok\":4"), std::string::npos) << verify_text;

  verify_bytes = nullptr;
  verify_size = 0;
  EXPECT_EQ(radicle_lod_verify_world_json(lod, "MissingWorld", world_ledger.c_str(), &verify_bytes, &verify_size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(verify_bytes, nullptr);
  radicle_lod_destroy(lod);
}

namespace {

// Invariant: both repair tests address a tile with the writer's own naming rule, never a second copy of it.
std::filesystem::path terrain_tile_file(const std::filesystem::path& root, std::int32_t level) {
  return fo4::lod::lod_tile_local_path(
      root, fo4::lod::LodTile{std::string{"TestWorld"}, level, 0, 0, fo4::lod::LodTileKind::terrain});
}

// Invariant: one bad tile is a report row, never a failed call, so every apply run here expects OK and a ledger.
std::string generate_and_keep_ledger(radicle_lod* lod, const std::string& ledger_in, std::string& ledger_out) {
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  const auto status =
      radicle_lod_generate_json(lod, ledger_in.c_str(), 1, &report, &report_size, &ledger, &ledger_size);
  // Constraint: radicle_free_bytes clears the last error, so it is read before the payloads are released.
  const char* raw_error = radicle_last_error();
  const std::string failure = raw_error == nullptr ? std::string{} : std::string{raw_error};
  EXPECT_EQ(status, RADICLE_STATUS_OK) << failure;
  auto text = text_of(report, report_size);
  ledger_out = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  return text;
}

}  // namespace

// Detects (invariant): a current ledger row must not hide a deleted or truncated .BTR, or generate reads clean while
// the world's terrain is gone and the tile must be rebuilt.
TEST(CApiLod, RebuildsATerrainTileWhoseBtrIsMissingOrTruncatedBehindACurrentLedger) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  std::string ledger_text;
  const auto first = generate_and_keep_ledger(lod, "", ledger_text);
  ASSERT_NE(first.find("\"written\":4,"), std::string::npos) << first;
  // Detects (invariant): a world that lost no terrain still reporting a loss, which would hide a real one.
  expect_absent_count(first, "missingVhgt");
  expect_absent_count(first, "badLand");

  const auto missing = terrain_tile_file(fixture.root, 4);
  const auto truncated = terrain_tile_file(fixture.root, 8);
  const auto untouched = terrain_tile_file(fixture.root, 16);
  ASSERT_GT(file_size_of(missing), 40u) << missing.string();
  ASSERT_GT(file_size_of(truncated), 40u) << truncated.string();
  const auto untouched_bytes = read_bytes(untouched);
  std::filesystem::remove(missing);
  {
    std::ofstream out(truncated, std::ios::binary | std::ios::trunc);
    out << "truncated";
  }

  // Detects (invariant): a dry run behind a current ledger must report the two lost tiles without writing them.
  uint8_t* dry_report = nullptr;
  size_t dry_report_size = 0;
  uint8_t* dry_ledger = nullptr;
  size_t dry_ledger_size = 0;
  ASSERT_EQ(
      radicle_lod_generate_json(lod, ledger_text.c_str(), 0, &dry_report, &dry_report_size, &dry_ledger,
                                      &dry_ledger_size),
      RADICLE_STATUS_OK);
  const auto dry = text_of(dry_report, dry_report_size);
  const auto dry_kept = text_of(dry_ledger, dry_ledger_size);
  release(dry_report);
  release(dry_ledger);
  EXPECT_NE(dry.find("\"wouldWrite\":2,"), std::string::npos) << dry;
  // Detects (invariant): the deleted tile listed as an overwrite, or the truncated one left out.
  EXPECT_NE(dry.find("\"wouldOverwrite\":[\"" + truncated.lexically_relative(fixture.root).generic_string() + "\"]"),
            std::string::npos)
      << dry;
  EXPECT_EQ(dry.find(missing.lexically_relative(fixture.root).generic_string()), std::string::npos) << dry;
  EXPECT_EQ(dry.find("wouldDelete"), std::string::npos) << dry;
  expect_absent_count(dry, "written");
  EXPECT_EQ(dry_kept, ledger_text);
  EXPECT_FALSE(std::filesystem::exists(missing));
  EXPECT_LT(file_size_of(truncated), 41u);

  std::string second_ledger;
  const auto second = generate_and_keep_ledger(lod, ledger_text, second_ledger);
  // Detects (invariant): a planned-current tile must be rebuilt from disk, not skipped because the ledger agrees.
  EXPECT_NE(second.find("\"written\":2,"), std::string::npos) << second;
  expect_absent_count(second, "failed");
  EXPECT_EQ(second_ledger, ledger_text);
  for (const auto& path : {missing, truncated}) {
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
    EXPECT_GT(file_size_of(path), 40u) << path.string();
    // Detects (invariant): a repaired tile must read back as a terrain tile, so a repair cannot write a broken file.
    EXPECT_EQ(fo4::lod::check_lod_tile_structure(fo4::nif::read_nif(read_bytes(path)),
                                                fo4::lod::LodTileKind::terrain),
              fo4::lod::LodTileStructure::ok)
        << path.string();
  }
  // Invariant: the level the repair did not name keeps exactly the bytes the first run wrote.
  const auto untouched_after = read_bytes(untouched);
  EXPECT_TRUE(untouched_after == untouched_bytes);
  radicle_lod_destroy(lod);
}

// Detects (invariant): one refused tile must not abort the run, which would lose every other tile's ledger line.
TEST(CApiLod, KeepsEveryOtherTileInTheLedgerWhenOneTileWriteIsRefused) {
  const Fixture fixture;
  const auto blocked = terrain_tile_file(fixture.root, 4);
  std::filesystem::create_directories(blocked);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  std::string ledger_text;
  const auto text = generate_and_keep_ledger(lod, "", ledger_text);
  // Detects (invariant): the refused tile must not be counted as written, nor its world counted failed with it.
  EXPECT_NE(text.find("\"written\":3,"), std::string::npos) << text;
  EXPECT_NE(text.find("\"failed\":1,"), std::string::npos) << text;
  EXPECT_NE(text.find("\"level\":4,\"x\":0,\"y\":0,\"status\":\"failed\""), std::string::npos) << text;
  // Detects (invariant): a failed tile must stay out of the ledger, or no later run retries it.
  EXPECT_EQ(ledger_text.find("testworld|terrain|4|0|0 "), std::string::npos) << ledger_text;
  EXPECT_NE(ledger_text.find("testworld|terrain|8|0|0 "), std::string::npos) << ledger_text;
  for (const auto level : {8, 16, 32}) {
    EXPECT_TRUE(std::filesystem::exists(terrain_tile_file(fixture.root, level))) << level;
  }

  // Invariant: only the tile that never reached the ledger is planned again once the obstruction is gone.
  std::filesystem::remove_all(blocked);
  std::string retry_ledger;
  const auto retry = generate_and_keep_ledger(lod, ledger_text, retry_ledger);
  EXPECT_NE(retry.find("\"written\":1,"), std::string::npos) << retry;
  expect_absent_count(retry, "failed");
  ASSERT_TRUE(std::filesystem::exists(blocked)) << blocked.string();
  EXPECT_GT(file_size_of(blocked), 40u) << blocked.string();
  radicle_lod_destroy(lod);
}

// Detects (invariant): the LAND cells a world loses must reach the report, not stay inside LandGrids.
TEST(CApiLod, ReportsTheTerrainALandCellLosesUnderTerrainLoss) {
  {
    const Fixture fixture;
    radicle_lod* lod = nullptr;
    ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    std::string ledger_text;
    const auto text = generate_and_keep_ledger(lod, "", ledger_text);
    // Detects (invariant): a loss object that lists a count for a world that lost nothing.
    expect_absent_count(text, "missingVhgt");
    expect_absent_count(text, "badLand");
    radicle_lod_destroy(lod);
  }
  {
    const Fixture fixture;
    // Invariant: a LAND with no VHGT decodes but has no height offset, which is a missing VHGT rather than bad land.
    std::vector<Node> cells{rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
                            grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {})})})};
    write_bytes(fixture.root / "Test.esp",
                make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                             grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(cells))})})})}));
    radicle_lod* lod = nullptr;
    ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    std::string ledger_text;
    const auto text = generate_and_keep_ledger(lod, "", ledger_text);
    // Detects (invariant): the loss counters must reach the JSON report, not stay on LandGrids.
    EXPECT_NE(text.find("\"terrainLoss\":{\"TestWorld\":{"), std::string::npos) << text;
    EXPECT_NE(text.find("\"missingVhgt\":1"), std::string::npos) << text;
    // Invariant: the cell has no grid, so the world holds no tile and the loss is all the report can say.
    EXPECT_TRUE(ledger_text.empty()) << ledger_text;
    radicle_lod_destroy(lod);
  }
}

namespace {

// Invariant: the host sees one report per finished tile, so it never has to poll the data folder for progress.
struct ScopedProgress {
  int calls{0};
  std::int32_t last_level{0};
  std::size_t last_done{0};
  std::size_t last_total{0};
};

void record_scoped_progress(void* user, const char* world, const char* kind, std::int32_t level, std::int32_t x,
                            std::int32_t y, std::size_t done, std::size_t total) {
  auto* state = static_cast<ScopedProgress*>(user);
  EXPECT_EQ(world == nullptr ? std::string{} : std::string{world}, "TestWorld");
  const std::string seen_kind = kind == nullptr ? std::string{} : std::string{kind};
  EXPECT_TRUE(seen_kind == "terrain" || seen_kind == "object") << seen_kind;
  EXPECT_GE(x, 0);
  EXPECT_GE(y, 0);
  ++state->calls;
  state->last_level = level;
  state->last_done = done;
  state->last_total = total;
}

}  // namespace

// Detects (invariant): a scoped run must bake only its own levels, so a user who asked for one level does not wait for
// the whole data folder, and the progress sink must be called once per finished tile inside that scope.
TEST(CApiLod, ScopedGenerateBakesOnlyTheLevelsItWasGiven) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  ScopedProgress observed;
  radicle_lod_scope scope{};
  scope.level_min = 8;
  scope.level_max = 8;
  scope.progress = record_scoped_progress;
  scope.user = &observed;
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_scoped_json(lod, "", 1, "TestWorld", &scope, &report, &report_size, &ledger,
                                             &ledger_size),
            RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  release(report);
  release(ledger);
  EXPECT_NE(text.find("\"world\":\"TestWorld\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"scope\":{\"levelMin\":8,\"levelMax\":8}"), std::string::npos) << text;
  const auto level_file = [&](std::int32_t level) {
    return fo4::lod::lod_tile_local_path(
        fixture.root, fo4::lod::LodTile{std::string{"TestWorld"}, level, 0, 0, fo4::lod::LodTileKind::terrain});
  };
  // Invariant: no row outside the scoped level appears, so nothing else reached disk either.
  EXPECT_EQ(text.find("\"level\":4,"), std::string::npos) << text;
  EXPECT_EQ(text.find("\"level\":16,"), std::string::npos) << text;
  EXPECT_EQ(text.find("\"level\":32,"), std::string::npos) << text;
  EXPECT_TRUE(std::filesystem::exists(level_file(8))) << text;
  EXPECT_FALSE(std::filesystem::exists(level_file(4)));
  EXPECT_FALSE(std::filesystem::exists(level_file(16)));
  EXPECT_GE(observed.calls, 1);
  EXPECT_EQ(observed.last_level, 8);
  EXPECT_GE(observed.last_total, 1u);
  EXPECT_LE(observed.last_done, observed.last_total);
  radicle_lod_destroy(lod);
}

// Detects (invariant): a cancelled run that keeps writing and committing tiles, so a Cancel button does nothing.
TEST(CApiLod, CancelledGenerateWritesAndRecordsNoTile) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  std::int32_t cancel = 1;
  radicle_lod_scope scope{};
  scope.cancel = &cancel;
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_scoped_json(lod, "", 1, nullptr, &scope, &report, &report_size, &ledger,
                                             &ledger_size),
            RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto ledger_text = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(text.find("\"cancelled\":true"), std::string::npos) << text;
  expect_absent_count(text, "written");
  expect_absent_count(text, "wouldWrite");
  EXPECT_TRUE(ledger_text.empty()) << ledger_text;
  EXPECT_FALSE(std::filesystem::exists(fo4::lod::lod_tile_local_path(
      fixture.root, fo4::lod::LodTile{std::string{"TestWorld"}, 4, 0, 0, fo4::lod::LodTileKind::terrain})));
  radicle_lod_destroy(lod);
}

// Detects (invariant): a landed mod world with no lodsettings must get one derived from its own cells, or a run
// must skip it forever and the user can never bake its terrain.
TEST(CApiLod, DerivesAndWritesTheSettingsOfAWorldWithNoLodFile) {
  const Fixture fixture;
  std::filesystem::remove(fixture.root / "LODSettings" / "TestWorld.LOD");
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto ledger_text = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(text.find("\"lodSettingsWritten\":1"), std::string::npos) << text;
  EXPECT_NE(text.find("\"lodSettings\":[{\"world\":\"TestWorld\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"stride\":4"), std::string::npos) << text;
  EXPECT_NE(text.find("\"status\":\"written\""), std::string::npos) << text;
  // Invariant: the derived grid must bake in the same run, so a written file never comes with no tiles.
  EXPECT_NE(text.find("\"written\":1,"), std::string::npos) << text;

  const auto settings_file = fixture.root / "LODSettings" / "TestWorld.lod";
  ASSERT_TRUE(std::filesystem::exists(settings_file)) << settings_file.string();
  const auto settings = fo4::lod::parse_lod_settings(read_bytes(settings_file));
  EXPECT_EQ(settings.southwest_x, 0);
  EXPECT_EQ(settings.southwest_y, 0);
  EXPECT_EQ(settings.stride, 4);
  EXPECT_EQ(settings.level_min, 4);
  EXPECT_EQ(settings.level_max, 4);
  EXPECT_TRUE(std::filesystem::exists(fo4::lod::lod_tile_local_path(
      fixture.root, fo4::lod::LodTile{"TestWorld", 4, 0, 0, fo4::lod::LodTileKind::terrain})));
  radicle_lod_destroy(lod);

  // Invariant: the written file is what the loader reads back, so a reopened session lists the world and derives none.
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* worlds = nullptr;
  size_t worlds_size = 0;
  ASSERT_EQ(radicle_lod_worlds_json(lod, &worlds, &worlds_size), RADICLE_STATUS_OK);
  const auto worlds_text = text_of(worlds, worlds_size);
  release(worlds);
  EXPECT_NE(worlds_text.find("\"stride\":4"), std::string::npos) << worlds_text;
  ASSERT_EQ(radicle_lod_generate_json(lod, ledger_text.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto repeat = text_of(report, report_size);
  release(report);
  release(ledger);
  expect_absent_count(repeat, "lodSettingsWritten");
  EXPECT_EQ(repeat.find("\"lodSettings\":"), std::string::npos) << repeat;
  radicle_lod_destroy(lod);
}

// Detects (invariant): a tile hash must follow file names, so a copied load order keeps its tiles, and an archive stamps.
TEST(CApiLod, TileInputsFollowFileNamesNotFoldersAndTheArchiveStamp) {
  const Fixture fixture;
  // Invariant: the copy keeps the plugin name and bytes, so only its folder and write time differ.
  const auto moved = fixture.root / "moved";
  std::filesystem::create_directories(moved / "LODSettings");
  std::filesystem::copy_file(fixture.root / "Test.esp", moved / "Test.esp");
  std::filesystem::copy_file(fixture.root / "LODSettings" / "TestWorld.LOD", moved / "LODSettings" / "TestWorld.LOD");

  const auto plan_of = [](const std::string& plugins, const std::string& data_dir, const std::string& archives) {
    radicle_lod* session = nullptr;
    EXPECT_EQ(radicle_lod_open(plugins.c_str(), data_dir.c_str(), archives.c_str(), &session), RADICLE_STATUS_OK);
    uint8_t* plan = nullptr;
    size_t plan_size = 0;
    uint8_t* kept = nullptr;
    size_t kept_size = 0;
    uint8_t* commit = nullptr;
    size_t commit_size = 0;
    EXPECT_EQ(radicle_lod_plan_json(session, "", &plan, &plan_size, &kept, &kept_size, &commit, &commit_size),
              RADICLE_STATUS_OK);
    const auto text = text_of(plan, plan_size);
    release(plan);
    release(kept);
    release(commit);
    radicle_lod_destroy(session);
    return text;
  };

  const auto original = plan_of(fixture.plugin_paths, fixture.data_dir, "");
  ASSERT_FALSE(plan_inputs_of(original, "terrain", 4).empty()) << original;
  // Detects (invariant): a tile hash that must not follow the folder the plugin happens to sit in.
  const auto copy = plan_of((moved / "Test.esp").string(), moved.string(), "");
  EXPECT_EQ(plan_inputs_of(original, "terrain", 4), plan_inputs_of(copy, "terrain", 4)) << copy;

  // Invariant: the archive is a physical file the session hashes by name and write time.
  const auto archive = fixture.root / "Test.ba2";
  const auto write_archive = [&archive](std::string_view payload) {
    fo4::archive::Ba2GeneralArchive content;
    content.has_string_table = true;
    content.entries.push_back(ba2_fixture::general("Meshes\\x.nif", payload));
    fo4::archive::write_ba2_general_file(content, archive);
  };
  write_archive("first");
  const auto with_archive = plan_of(fixture.plugin_paths, fixture.data_dir, archive.string());
  // Detects (invariant): a tile hash that must name the archives its assets were resolved from.
  EXPECT_NE(plan_inputs_of(original, "terrain", 4), plan_inputs_of(with_archive, "terrain", 4)) << with_archive;

  write_archive("a longer second payload");
  const auto after = plan_of(fixture.plugin_paths, fixture.data_dir, archive.string());
  // Detects (invariant): a replaced archive that must reopen the tiles its assets fed.
  EXPECT_NE(plan_inputs_of(with_archive, "terrain", 4), plan_inputs_of(after, "terrain", 4)) << after;

  // Invariant: a same-size rewrite is only visible through the write time, so the stamp alone must reopen the tile.
  write_archive("first");
  std::error_code stamp;
  const auto bumped = std::filesystem::last_write_time(archive, stamp) + std::chrono::hours(1);
  ASSERT_FALSE(stamp);
  std::filesystem::last_write_time(archive, bumped, stamp);
  ASSERT_FALSE(stamp);
  const auto restamped = plan_of(fixture.plugin_paths, fixture.data_dir, archive.string());
  // Detects (invariant): an archive keyed by size alone, so an in-place rebuild of the same length stays current.
  EXPECT_NE(plan_inputs_of(with_archive, "terrain", 4), plan_inputs_of(restamped, "terrain", 4)) << restamped;
}

// Detects (invariant): verify must report each terrain .DDS a written .BTR names, so a deleted or edited one cannot hide.
TEST(CApiLod, VerifyReportsTheTerrainAtlasesEachBtrNames) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  std::string ledger_text;
  const auto generated = generate_and_keep_ledger(lod, "", ledger_text);
  ASSERT_NE(generated.find("\"terrainTexturesWritten\":8"), std::string::npos) << generated;

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &bytes, &size), RADICLE_STATUS_OK);
  const auto clean = text_of(bytes, size);
  release(bytes);
  // Invariant: one row per atlas of a landed terrain tile, four tiles at two maps each.
  EXPECT_NE(clean.find("\"texturesOk\":8"), std::string::npos) << clean;
  EXPECT_NE(clean.find("\"texturesMissing\":0"), std::string::npos) << clean;
  // Detects: a terrain texture row that must carry the tile it belongs to, or a stale .DDS is unattributable.
  EXPECT_NE(clean.find("\"map\":\"terrain-diffuse\",\"level\":4,\"x\":0,\"y\":0,\"state\":\"ok\""),
            std::string::npos)
      << clean;

  // Invariant: the paths come from the terrain writer, not from a second naming rule copied into the test.
  const auto missing = fo4::lod::terrain_texture_local_path(fixture.root, "TestWorld", 4, 0, 0,
                                                            fo4::lod::TerrainTextureMap::diffuse);
  const auto truncated = fo4::lod::terrain_texture_local_path(fixture.root, "TestWorld", 8, 0, 0,
                                                              fo4::lod::TerrainTextureMap::normal);
  ASSERT_GT(file_size_of(missing), 148u) << missing.string();
  ASSERT_GT(file_size_of(truncated), 148u) << truncated.string();
  std::filesystem::remove(missing);
  {
    std::ofstream out(truncated, std::ios::binary | std::ios::trunc);
    out << "not a dds";
  }

  bytes = nullptr;
  size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &bytes, &size), RADICLE_STATUS_OK);
  const auto broken = text_of(bytes, size);
  release(bytes);
  EXPECT_NE(broken.find("\"texturesMissing\":1"), std::string::npos) << broken;
  EXPECT_NE(broken.find("\"texturesStale\":1"), std::string::npos) << broken;
  EXPECT_NE(broken.find("\"map\":\"terrain-diffuse\",\"level\":4,\"x\":0,\"y\":0,\"state\":\"missing\""),
            std::string::npos)
      << broken;
  EXPECT_NE(broken.find("\"map\":\"terrain-normal\",\"level\":8,\"x\":0,\"y\":0,\"state\":\"stale\""),
            std::string::npos)
      << broken;

  // Invariant: the same ledger repairs only the two broken atlases, because the other six still match it.
  std::string repaired_ledger;
  const auto repaired = generate_and_keep_ledger(lod, ledger_text, repaired_ledger);
  EXPECT_NE(repaired.find("\"terrainTexturesWritten\":2"), std::string::npos) << repaired;
  bytes = nullptr;
  size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, repaired_ledger.c_str(), &bytes, &size), RADICLE_STATUS_OK);
  const auto after = text_of(bytes, size);
  release(bytes);
  EXPECT_NE(after.find("\"texturesOk\":8"), std::string::npos) << after;
  radicle_lod_destroy(lod);

  // Constraint: without a data folder there is no atlas to judge, so each row is unverified rather than missing.
  radicle_lod* blind = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), "", "", &blind), RADICLE_STATUS_OK);
  bytes = nullptr;
  size = 0;
  ASSERT_EQ(radicle_lod_verify_json(blind, repaired_ledger.c_str(), &bytes, &size), RADICLE_STATUS_OK);
  const auto unverified = text_of(bytes, size);
  release(bytes);
  EXPECT_NE(unverified.find("\"missing\":0,"), std::string::npos) << unverified;
  EXPECT_NE(unverified.find("\"unverified\":4"), std::string::npos) << unverified;
  EXPECT_NE(unverified.find("\"local\":\"no-data-dir\""), std::string::npos) << unverified;
  radicle_lod_destroy(blind);
}

TEST(CApiLod, DryRunListsTheTerrainAtlasesAnApplyWouldReplace) {
  const Fixture fixture;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  const auto dry = [&](const std::string& ledger_in) {
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* ledger = nullptr;
    size_t ledger_size = 0;
    EXPECT_EQ(radicle_lod_generate_json(lod, ledger_in.c_str(), 0, &report, &report_size, &ledger, &ledger_size),
              RADICLE_STATUS_OK);
    auto text = text_of(report, report_size);
    release(report);
    release(ledger);
    return text;
  };
  const auto relative = [&](const std::filesystem::path& path) {
    return path.lexically_relative(fixture.root).generic_string();
  };
  EXPECT_EQ(dry("").find("wouldOverwriteAtlases"), std::string::npos);

  std::string ledger_text;
  const auto generated = generate_and_keep_ledger(lod, "", ledger_text);
  ASSERT_NE(generated.find("\"terrainTexturesWritten\":8"), std::string::npos) << generated;
  std::vector<std::filesystem::path> atlases;
  for (const std::int32_t level : {4, 8, 16, 32}) {
    for (const auto map : {fo4::lod::TerrainTextureMap::diffuse, fo4::lod::TerrainTextureMap::normal}) {
      atlases.push_back(fo4::lod::terrain_texture_local_path(fixture.root, "TestWorld", level, 0, 0, map));
    }
  }

  const auto stale = dry("");
  for (const auto& path : atlases) {
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
    EXPECT_NE(stale.find("\"" + relative(path) + "\""), std::string::npos) << stale;
  }
  EXPECT_EQ(stale.find("wouldOverwriteAtlasesTruncated"), std::string::npos) << stale;

  EXPECT_EQ(dry(ledger_text).find("wouldOverwriteAtlases"), std::string::npos);

  const auto& damaged = atlases.at(3);
  {
    std::ofstream out(damaged, std::ios::binary | std::ios::trunc);
    out << "not a dds";
  }
  const auto partial = dry(ledger_text);
  EXPECT_NE(partial.find("\"wouldOverwriteAtlases\":[\"" + relative(damaged) + "\"]"), std::string::npos)
      << partial;
  radicle_lod_destroy(lod);
}

// Detects (invariant): a dry-run path list growing past its cap, or a cut list that does not say it was cut.
TEST(CApiLod, DryRunPathListStopsAtTheCapAndSaysItWasTruncated) {
  const std::filesystem::path root{"data"};
  const auto tile_at = [](std::int32_t x) {
    return fo4::lod::LodTile{std::string{"TestWorld"}, 4, x, 0, fo4::lod::LodTileKind::terrain};
  };
  fo4::lod::LodPathList list;
  for (std::size_t index = 0; index < fo4::lod::k_lod_dry_run_path_cap; ++index) {
    fo4::lod::add_dry_run_path(list, root, tile_at(static_cast<std::int32_t>(index)));
  }
  EXPECT_EQ(list.paths.size(), fo4::lod::k_lod_dry_run_path_cap);
  EXPECT_FALSE(list.truncated);
  std::string whole;
  fo4::lod::append_path_list(whole, "wouldOverwrite", list);
  EXPECT_EQ(whole.find("Truncated"), std::string::npos) << whole;

  fo4::lod::add_dry_run_path(list, root, tile_at(1000));
  EXPECT_EQ(list.paths.size(), fo4::lod::k_lod_dry_run_path_cap);
  EXPECT_TRUE(list.truncated);
  EXPECT_EQ(list.paths.front(), "Meshes/Terrain/TestWorld/TestWorld.4.0.0.BTR");
  std::string cut;
  fo4::lod::append_path_list(cut, "wouldOverwrite", list);
  EXPECT_NE(cut.find("],\"wouldOverwriteTruncated\":true"), std::string::npos) << cut;
  EXPECT_EQ(cut.find("TestWorld.4.1000.0"), std::string::npos) << cut;

  std::string empty;
  fo4::lod::append_path_list(empty, "wouldDelete", fo4::lod::LodPathList{});
  EXPECT_TRUE(empty.empty());

  fo4::lod::LodPathList files;
  for (std::size_t index = 0; index <= fo4::lod::k_lod_dry_run_path_cap; ++index) {
    fo4::lod::add_dry_run_file(files, root,
                               root / "Textures" / "Terrain" / "TestWorld" / ("a" + std::to_string(index)));
  }
  EXPECT_EQ(files.paths.size(), fo4::lod::k_lod_dry_run_path_cap);
  EXPECT_TRUE(files.truncated);
  EXPECT_EQ(files.paths.front(), "Textures/Terrain/TestWorld/a0");
}
