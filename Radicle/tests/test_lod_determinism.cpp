#include "fo4/c_api_lod.h"

#include "env_node_fixture.hpp"
#include "lod_object_generate_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using namespace fo4::test::envnode::capi_lod;

// Invariant: determinism is compared on relative paths and raw bytes, so two temp roots compare entry for entry.
using Tree = std::map<std::string, std::vector<std::byte>>;

Tree snapshot_tree(const std::filesystem::path& root) {
  Tree tree;
  std::error_code status;
  for (std::filesystem::recursive_directory_iterator it(root, status), end; !status && it != end;
       it.increment(status)) {
    std::error_code file_status;
    if (!it->is_regular_file(file_status)) continue;
    tree.emplace(it->path().lexically_relative(root).generic_string(), read_bytes(it->path()));
  }
  return tree;
}

// Invariant: extension comparison folds case, because tiles are written .BTO/.btr.
std::string folded_extension(std::string_view relative) {
  const auto dot = relative.rfind('.');
  const auto slash = relative.find_last_of("/\\");
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return std::string{};
  std::string extension{relative.substr(dot)};
  for (auto& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return extension;
}

std::size_t count_extension(const Tree& tree, std::string_view extension) {
  std::size_t count = 0;
  for (const auto& entry : tree) {
    if (folded_extension(entry.first) == extension) ++count;
  }
  return count;
}

// Invariant: a missing, extra or edited file is the failure, so sizes then bytes are compared per relative path.
void expect_same_tree(const Tree& left, std::string_view left_label, const Tree& right,
                      std::string_view right_label) {
  ASSERT_EQ(left.size(), right.size()) << left_label << " holds " << left.size() << " files, " << right_label
                                       << " holds " << right.size();
  for (const auto& [relative, bytes] : left) {
    const auto found = right.find(relative);
    ASSERT_NE(found, right.end()) << relative << " exists only in " << left_label;
    const auto& other = found->second;
    EXPECT_TRUE(bytes.size() == other.size() && std::equal(bytes.begin(), bytes.end(), other.begin()))
        << relative << " differs between " << left_label << " and " << right_label << " (" << bytes.size()
        << " vs " << other.size() << " bytes)";
  }
}

struct GenerateRun {
  radicle_status status{RADICLE_STATUS_INTERNAL_ERROR};
  std::string report;
  std::string ledger;
  std::string error;
};

// Invariant: both sessions must load the same plugin file, because the ledger hash covers the absolute input path.
GenerateRun generate_into(const std::string& plugin_paths, const std::string& data_dir) {
  GenerateRun run;
  radicle_lod* lod = nullptr;
  run.status = radicle_lod_open(plugin_paths.c_str(), data_dir.c_str(), "", &lod);
  if (run.status != RADICLE_STATUS_OK || lod == nullptr) {
    const char* raw_error = radicle_last_error();
    run.error = raw_error == nullptr ? std::string{} : std::string{raw_error};
    if (lod != nullptr) radicle_lod_destroy(lod);
    return run;
  }
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  run.status = radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size);
  // Constraint: radicle_free_bytes clears the last error, so it is read before either buffer is released.
  const char* raw_error = radicle_last_error();
  run.error = raw_error == nullptr ? std::string{} : std::string{raw_error};
  run.report = text_of(report, report_size);
  run.ledger = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  radicle_lod_destroy(lod);
  return run;
}

}  // namespace

// Detects (regression): worker ordering leaking into the written rows, or one build buffer shared by workers.
TEST(LodDeterminism, RewritesIdenticalTilesObjectsAtlasesAndLedgerAcrossRuns) {
  const Fixture first;
  object_lod_prepare(first, ObjectLodRefs::two_tiles);
  const auto first_inputs = snapshot_tree(first.root);
  const auto first_run = generate_into(first.plugin_paths, first.data_dir);
  ASSERT_EQ(first_run.status, RADICLE_STATUS_OK) << first_run.error;
  ASSERT_FALSE(first_run.report.empty());
  ASSERT_FALSE(first_run.ledger.empty());
  const auto first_tree = snapshot_tree(first.root);

  const Fixture second;
  ASSERT_NE(second.root.string(), first.root.string());
  object_lod_prepare(second, ObjectLodRefs::two_tiles);
  // Constraint: the ledger hash covers each loose input's write time, so the second fixture must carry the first's.
  for (const auto mesh_name : k_object_lod_meshes) {
    const auto relative = std::filesystem::path{"Meshes"} / std::string{mesh_name};
    std::filesystem::last_write_time(second.root / relative, std::filesystem::last_write_time(first.root / relative));
  }
  const auto second_inputs = snapshot_tree(second.root);
  // Invariant: a byte difference below must be generator non-determinism, never differing fixture inputs.
  expect_same_tree(second_inputs, "second fixture inputs", first_inputs, "first fixture inputs");
  const auto second_run = generate_into(first.plugin_paths, second.data_dir);
  ASSERT_EQ(second_run.status, RADICLE_STATUS_OK) << second_run.error;
  const auto second_tree = snapshot_tree(second.root);

  // Detects (regression): a write ordering that depends on worker scheduling, so the two folders disagree.
  expect_same_tree(second_tree, "second run", first_tree, "first run");
  EXPECT_EQ(second_run.ledger, first_run.ledger);
  EXPECT_EQ(second_run.report, first_run.report);

  // Invariant: the compared tree must hold terrain tiles, object tiles and both atlas sets.
  EXPECT_EQ(count_extension(first_tree, ".btr"), 4u);
  // Invariant: two placed references per level give two object tiles per shipped level, so pass two maps several tiles.
  EXPECT_EQ(count_extension(first_tree, ".bto"), 8u);
  EXPECT_EQ(count_extension(first_tree, ".dds"), 11u);
  EXPECT_NE(first_run.ledger.find("|terrain|"), std::string::npos);
  EXPECT_NE(first_run.ledger.find("|object|"), std::string::npos);
  EXPECT_NE(first_run.ledger.find("|texture|"), std::string::npos);

  // Detects (regression): a rewrite ordering that differs on the second pass over current tiles.
  const auto third_run = generate_into(first.plugin_paths, first.data_dir);
  ASSERT_EQ(third_run.status, RADICLE_STATUS_OK) << third_run.error;
  EXPECT_EQ(third_run.ledger, first_run.ledger);
  EXPECT_EQ(third_run.report, first_run.report);
  expect_same_tree(snapshot_tree(first.root), "third run", first_tree, "first run");
}
