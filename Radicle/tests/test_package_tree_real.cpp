#include "fo4/environment/package_procedure.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

using fo4::Signature;
using fo4::FormKey;
using Status = fo4::env::PackageTreeMutationStatus;

std::vector<FormKey> pack_records(const fo4::Plugin& plugin) {
  std::vector<FormKey> keys;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("PACK")) keys.push_back(record.form_key);
  });
  return keys;
}

std::vector<std::pair<std::string, std::vector<std::byte>>> subs_of(const fo4::Plugin& plugin, const FormKey& key) {
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

}  // namespace

TEST(PackageTreeReal, AddsAndRemovesALeafOnAnOfficialPackageAndRestoresIt) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  int edited = 0;
  int refused = 0;
  for (const auto& key : pack_records(plugin)) {
    if (edited != 0) break;
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) continue;
    const auto tree = fo4::env::decode_package_procedure(*record);
    if (!tree.decoded || tree.nodes.empty()) continue;
    if (tree.nodes[0].kind != fo4::env::PackageProcedureKind::container) continue;
    const auto original = subs_of(plugin, key);
    std::vector<FormKey> targets;
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
      if (tree.nodes[index].kind != fo4::env::PackageProcedureKind::container) continue;
      targets.push_back(key);
      const auto before_nodes = tree.nodes.size();
      std::size_t added = 0;
      const auto status = fo4::env::add_package_procedure_node(plugin, key, index, "Activate", &added);
      if (status != Status::updated) {
        ++refused;
        continue;
      }
      const auto grown = fo4::env::decode_package_procedure(*fo4::find_record(plugin, key));
      EXPECT_TRUE(grown.decoded);
      EXPECT_EQ(grown.nodes.size(), before_nodes + 1);
      EXPECT_EQ(fo4::env::remove_package_procedure_node(plugin, key, added), Status::updated);
      EXPECT_EQ(subs_of(plugin, key), original);
      ++edited;
      break;
    }
    if (edited == 0) {
      const auto status = fo4::env::remove_package_procedure_node(plugin, key, tree.nodes.size() - 1);
      if (status != Status::updated) ++refused;
    }
  }
  RecordProperty("packages_edited", edited);
  RecordProperty("packages_refused", refused);
  EXPECT_GT(edited, 0);
}
