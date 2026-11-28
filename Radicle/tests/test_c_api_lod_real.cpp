#include "fo4/c_api_lod.h"

#include "env_node_fixture.hpp"
#include "fo4/archive/assets.hpp"
#include "fo4/hash.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/terrain_texture.hpp"
#include "fo4/nif/nif.hpp"
#include "lod_bto_compare.hpp"
#include "real_data.hpp"
#include "temp_dir.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string_view>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace fo4::test::envnode::capi_lod;

namespace {

using TempRoot = fo4::test::TempDir;

// Invariant: a milestone reaches the log before the next step starts, so a run that is killed mid-flight (an
// external kill leaves no gtest summary and no WER event) still shows the last step it finished instead of dying
// inside a buffered stdout block that reads like a silent exit.
void say(const std::string& line) {
  std::cout << line << '\n';
  std::cout.flush();
}

// Invariant: a failed verify names the tiles and atlas maps that broke it, so a red gate points at the artifact
// instead of only at a counter. Detects: verify reporting a count with no way to tell which file it counted.
std::string broken_rows(const std::string& report) {
  std::string listed;
  std::size_t shown = 0;
  std::size_t hidden = 0;
  for (std::size_t at = report.find('{'); at != std::string::npos; at = report.find('{', at + 1)) {
    const auto end = report.find('}', at);
    if (end == std::string::npos) break;
    const auto row = report.substr(at, end - at + 1);
    const bool broken = row.find("\"state\":\"missing\"") != std::string::npos ||
                        row.find("\"state\":\"partial\"") != std::string::npos ||
                        row.find("\"state\":\"stale\"") != std::string::npos;
    if (!broken) continue;
    if (shown < 8) {
      if (!listed.empty()) listed += ", ";
      listed += row;
    } else {
      ++hidden;
    }
    ++shown;
  }
  if (shown == 0) return std::string{"no broken row in the verify report"};
  std::string out{"broken rows: " + listed};
  if (hidden != 0) out += " (+ " + std::to_string(hidden) + " more)";
  return out;
}

// Invariant: a count the summary omits is a count of zero, so a clean run reads as zero rather than as a missing
// field. Detects: a reader that treats an omitted zero as a failure or as "unknown".
int64_t json_count(const std::string& report, std::string_view name) {
  const auto needle = "\"" + std::string{name} + "\":";
  const auto at = report.find(needle);
  if (at == std::string::npos) return 0;
  return std::stoll(report.substr(at + needle.size()));
}

// Invariant: the reported file list is the on-disk truth, so the test counts extensions, not report rows.
std::size_t count_files(const std::filesystem::path& root, std::string_view extension) {
  std::error_code status;
  if (!std::filesystem::exists(root, status)) return 0;
  std::size_t count = 0;
  for (std::filesystem::recursive_directory_iterator it(root, status), end; it != end; it.increment(status)) {
    if (status) break;
    if (!it->is_regular_file(status)) continue;
    if (it->path().extension() == extension) ++count;
  }
  return count;
}

}  // namespace

// Detects: one applied session on a real install plans, writes and verifies every Commonwealth tile, and a written tile's preview is its file.
TEST(CApiLodReal, GeneratesCommonwealthLodEndToEnd) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  if (!std::filesystem::exists(master)) FAIL() << "FO4_DATA_DIR is set but holds no Fallout4.esm: " << dir;
  std::vector<std::filesystem::path> archives;
  try {
    const std::vector<std::string> plugins{"Fallout4.esm"};
    const std::vector<std::string> ini_archives{};
    archives = fo4::archive::archive_load_order(data, ini_archives, plugins);
  } catch (const std::exception& failure) {
    FAIL() << "the archive load order could not be resolved: " << failure.what();
  }
  if (archives.empty()) FAIL() << "FO4_DATA_DIR is set but no archives resolved from it: " << dir;
  std::string archive_paths;
  for (const auto& archive : archives) archive_paths += archive.string() + '\n';

  TempRoot staging;
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(master.string().c_str(), staging.path.string().c_str(), archive_paths.c_str(), &lod),
            RADICLE_STATUS_OK);
  struct Close {
    radicle_lod* value;
    ~Close() { radicle_lod_destroy(value); }
  } close{lod};

  std::string ledger;
  auto run_plan = [&](const std::string& current, std::string* plan_text, std::string* next_ledger) {
    uint8_t* plan = nullptr;
    size_t plan_size = 0;
    uint8_t* merged = nullptr;
    size_t merged_size = 0;
    uint8_t* commit = nullptr;
    size_t commit_size = 0;
    const auto status = radicle_lod_plan_json(lod, current.c_str(), &plan, &plan_size, &merged, &merged_size,
                                              &commit, &commit_size);
    *plan_text = text_of(plan, plan_size);
    *next_ledger = text_of(merged, merged_size);
    radicle_free_bytes(plan);
    radicle_free_bytes(merged);
    radicle_free_bytes(commit);
    return status;
  };

  std::string plan_text;
  std::string merged_ledger;
  ASSERT_EQ(run_plan(std::string{}, &plan_text, &merged_ledger), RADICLE_STATUS_OK);
  say("plan bytes: " + std::to_string(plan_text.size()));

  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* written_ledger = nullptr;
  size_t written_ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, merged_ledger.c_str(), 1, &report, &report_size, &written_ledger,
                                      &written_ledger_size),
            RADICLE_STATUS_OK);
  const auto generated = text_of(report, report_size);
  radicle_free_bytes(report);
  say(generated);

  const auto btr = count_files(staging.path, ".BTR") + count_files(staging.path, ".btr");
  const auto bto = count_files(staging.path, ".BTO") + count_files(staging.path, ".bto");
  say("BTR files: " + std::to_string(btr) + " BTO files: " + std::to_string(bto));
  ASSERT_GT(btr, 0u);
  ASSERT_GT(bto, 0u);

  EXPECT_EQ(json_count(generated, "unsupportedCandidates"), 0);
  EXPECT_EQ(json_count(generated, "failed"), 0);
  EXPECT_EQ(json_count(generated, "objectMissingMeshes"), 0);

  uint8_t* terrain_dds = nullptr;
  size_t terrain_dds_size = 0;
  const auto verify_status = radicle_lod_verify_json(lod, text_of(written_ledger, written_ledger_size).c_str(),
                                                     &terrain_dds, &terrain_dds_size);
  // Constraint: radicle_free_bytes clears the last error, so the detail naming the failing atlas is read first.
  const char* raw_verify_error = radicle_last_error();
  const std::string verify_error = raw_verify_error == nullptr ? std::string{} : std::string{raw_verify_error};
  const auto verified = text_of(terrain_dds, terrain_dds_size);
  radicle_free_bytes(terrain_dds);
  say(verified);
  const auto broken = broken_rows(verified);
  ASSERT_EQ(verify_status, RADICLE_STATUS_OK)
      << "verify rejected the Commonwealth LOD this run wrote: " << verify_error << '\n' << broken;
  EXPECT_EQ(json_count(verified, "missing"), 0) << broken;
  EXPECT_EQ(json_count(verified, "partial"), 0) << broken;
  EXPECT_EQ(json_count(verified, "stale"), 0) << broken;
  EXPECT_EQ(json_count(verified, "texturesMissing"), 0) << broken;
  EXPECT_EQ(json_count(verified, "texturesStale"), 0) << broken;
  EXPECT_GT(json_count(verified, "ok"), 0) << broken;

  // Constraint: the atlas names come from the writers, so this cannot pass against a layout they never write.
  for (std::size_t index = 0; index < fo4::lod::k_object_atlas_maps; ++index) {
    const auto map = static_cast<fo4::lod::ObjectAtlasMap>(index);
    // Invariant: the local writer path is what lands inside the Data folder the session was opened with, so the
    // Data-less texture-set path a NIF names is not what this check may join to it.
    const auto path = fo4::lod::object_atlas_local_path(staging.path, "Commonwealth", map);
    say("atlas " + std::string{fo4::lod::object_atlas_map_name(map)} + " -> " + path.string());
    EXPECT_TRUE(std::filesystem::exists(path)) << path.string();
  }

  // Invariant: a preview must be the exact BTO generate wrote, so the test compares it to the file on disk.
  std::filesystem::path sample;
  std::error_code status;
  const auto objects = staging.path / "Meshes" / "Terrain" / "Commonwealth" / "Objects";
  for (std::filesystem::recursive_directory_iterator it(objects, status), end; it != end; it.increment(status)) {
    if (status) break;
    const auto extension = it->path().extension().string();
    if (extension == ".BTO" || extension == ".bto") {
      sample = it->path();
      break;
    }
  }
  ASSERT_FALSE(sample.empty()) << "no written object tile to preview";
  const auto tile = fo4::lod::parse_lod_tile_path(sample.lexically_relative(staging.path).generic_string());
  ASSERT_TRUE(tile.has_value()) << sample.string();
  uint8_t* preview = nullptr;
  size_t preview_size = 0;
  ASSERT_EQ(radicle_lod_preview_object_nif(lod, tile->worldspace.c_str(), tile->level, tile->x, tile->y, &preview,
                                           &preview_size),
            RADICLE_STATUS_OK);
  std::vector<std::byte> previewed(preview_size);
  std::memcpy(previewed.data(), preview, preview_size);
  radicle_free_bytes(preview);
  EXPECT_EQ(previewed, read_bytes(sample)) << sample.string();

  // Invariant: every written Commonwealth object tile is content-checked against the vanilla tile, see the code note.
  const auto vanilla_assets = fo4::archive::AssetResolver::open(data, archives);
  const auto vanilla_name = [](std::int32_t level, std::int32_t x, std::int32_t y) {
    return "Meshes\\Terrain\\Commonwealth\\Objects\\Commonwealth." + std::to_string(level) + "." + std::to_string(x) +
           "." + std::to_string(y) + ".BTO";
  };
  fo4::test::lodcompare::Sweep sweep;
  sweep.cap = SIZE_MAX;
  std::set<std::string> written;
  for (std::filesystem::recursive_directory_iterator it(objects, status), end; it != end; it.increment(status)) {
    if (status) break;
    const auto extension = it->path().extension().string();
    if (extension != ".BTO" && extension != ".bto") continue;
    const auto named = fo4::lod::parse_lod_tile_path(it->path().lexically_relative(staging.path).generic_string());
    if (!named.has_value()) {
      ADD_FAILURE() << "a written object tile has a name parse_lod_tile_path rejects: " << it->path().string();
      continue;
    }
    const auto label = it->path().filename().string();
    written.insert(vanilla_name(named->level, named->x, named->y));
    const auto ours_bytes = read_bytes(it->path());
    fo4::test::lodcompare::TileComparison compared;
    std::string manifest = "bto-manifest " + label + " ours " + std::to_string(ours_bytes.size()) + ' ' +
                           fo4::test::lodcompare::hex64(fo4::fnv1a64_bytes(ours_bytes));
    try {
      const auto ours = fo4::nif::read_nif(ours_bytes);
      fo4::test::lodcompare::check_generated_tile(ours, true, compared);
      const auto reference = vanilla_assets.locate(vanilla_name(named->level, named->x, named->y));
      if (!reference) {
        compared.note("tile-set", "written but the vanilla archives hold no such tile");
        manifest += " vanilla none";
      } else {
        const auto vanilla_bytes = vanilla_assets.read(*reference);
        const auto vanilla = fo4::nif::read_nif(vanilla_bytes);
        auto against = fo4::test::lodcompare::compare_object_tile(ours, vanilla);
        for (auto& [tier, messages] : against.diffs) {
          for (auto& message : messages) compared.note(tier, std::move(message));
        }
        const auto whole = fo4::test::lodcompare::first_difference(ours_bytes, vanilla_bytes);
        manifest += " vanilla " + std::to_string(vanilla_bytes.size()) + ' ' +
                    fo4::test::lodcompare::hex64(fo4::fnv1a64_bytes(vanilla_bytes)) +
                    (whole ? " first difference at " + fo4::test::lodcompare::place_of(ours, *whole)
                           : std::string{" identical"});
      }
    } catch (const std::exception& failure) {
      compared.note("layout", std::string{"a NIF reader threw: "} + failure.what());
    }
    say(manifest);
    sweep.add(label, compared);
  }
  EXPECT_FALSE(static_cast<bool>(status)) << "the object tile folder could not be walked to the end: " << status.message();
  ASSERT_GT(sweep.tiles, 0u);
  fo4::test::lodcompare::TileComparison missing;
  for (const std::int32_t level : {4, 8, 16, 32}) {
    for (std::int32_t x = -96; x < 96; x += level) {
      for (std::int32_t y = -96; y < 96; y += level) {
        const auto name = vanilla_name(level, x, y);
        if (written.count(name) == 0 && vanilla_assets.locate(name)) {
          missing.note("tile-set", "vanilla ships " + name + " but this run wrote no such tile");
        }
      }
    }
  }
  sweep.add("tile-set probe", missing);
  say("written Commonwealth object tiles compared: " + std::to_string(sweep.tiles - 1));
  sweep.expect_clean("generated Commonwealth object LOD against vanilla", {"self-check"});
}
