#include "fo4/environment/package_procedure.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

using fo4::Signature;
using fo4::FormKey;
using Status = fo4::env::PackageTreeMutationStatus;
using Subs = std::vector<std::pair<std::string, std::vector<std::byte>>>;

std::vector<FormKey> pack_records(const fo4::Plugin& plugin) {
  std::vector<FormKey> keys;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("PACK")) keys.push_back(record.form_key);
  });
  return keys;
}

Subs subs_of(const fo4::Plugin& plugin, const FormKey& key) {
  Subs out;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

std::vector<std::uint8_t> leaf_input_ids(const fo4::Record& record, const fo4::env::PackageProcedureNode& node) {
  std::vector<std::uint8_t> ids;
  for (std::size_t at = node.opaque_begin; at < node.opaque_end; ++at) {
    const auto& sub = record.subrecords[at];
    if (sub.signature == Signature::from_string("PKC2") && sub.data.size() == 1) {
      ids.push_back(std::to_integer<std::uint8_t>(sub.data[0]));
    }
  }
  return ids;
}

bool references_known_inputs(const fo4::Record& record, const fo4::env::PackageProcedureTree& tree) {
  std::vector<std::uint8_t> ids;
  for (const auto& input : tree.inputs) ids.push_back(input.index);
  for (const auto& node : tree.nodes) {
    if (node.kind != fo4::env::PackageProcedureKind::leaf) continue;
    for (const auto id : leaf_input_ids(record, node)) {
      if (id > 0xFE) continue;
      bool found = false;
      for (const auto known : ids) found = found || known == id;
      if (!found) return false;
    }
  }
  return true;
}

void restore_subs(fo4::Plugin& plugin, const FormKey& key, const Subs& saved) {
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key) return;
    record.subrecords.clear();
    for (const auto& [signature, data] : saved) {
      fo4::Subrecord sub;
      sub.signature = Signature::from_string(signature);
      sub.data = data;
      record.subrecords.push_back(std::move(sub));
    }
  });
}

}  // namespace

TEST(PackageTreeReal, AddsAndRemovesALeafOnAnOfficialPackageAndRestoresIt) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  int inserted = 0;
  for (const auto& key : pack_records(plugin)) {
    if (inserted != 0) break;
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) continue;
    const auto tree = fo4::env::decode_package_procedure(*record);
    if (!tree.decoded || tree.nodes.empty()) continue;
    if (tree.nodes[0].kind != fo4::env::PackageProcedureKind::container) continue;
    const auto original = subs_of(plugin, key);
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
      if (tree.nodes[index].kind != fo4::env::PackageProcedureKind::container) continue;
      std::size_t added = 0;
      if (fo4::env::add_package_procedure_node(plugin, key, index, "Activate", &added) != Status::updated) continue;
      const auto grown = fo4::env::decode_package_procedure(*fo4::find_record(plugin, key));
      EXPECT_TRUE(grown.decoded);
      EXPECT_EQ(grown.nodes.size(), tree.nodes.size() + 1);
      EXPECT_EQ(fo4::env::remove_package_procedure_node(plugin, key, added), Status::updated);
      EXPECT_EQ(subs_of(plugin, key), original);
      ++inserted;
      break;
    }
  }
  RecordProperty("packages_edited", inserted);
  EXPECT_GT(inserted, 0);
}

TEST(PackageTreeReal, AddsAndRemovesTheDefaultInputsOfATravelLeafByteIdentically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  int edited = 0;
  for (const auto& key : pack_records(plugin)) {
    if (edited != 0) break;
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) continue;
    const auto tree = fo4::env::decode_package_procedure(*record);
    if (!tree.decoded || tree.nodes.empty()) continue;
    if (tree.nodes[0].kind != fo4::env::PackageProcedureKind::container) continue;
    const bool deterministic_ids =
        tree.inputs.empty() || (tree.next_input_id && static_cast<std::size_t>(*tree.next_input_id) == tree.inputs.size());
    if (!deterministic_ids) continue;
    // remove_package_procedure_node renumbers kept inputs, so only contiguous ids can come back byte-identical.
    bool contiguous_ids = true;
    for (std::size_t at = 0; at < tree.inputs.size(); ++at) contiguous_ids = contiguous_ids && tree.inputs[at].index == at;
    if (!contiguous_ids) continue;
    const auto original = subs_of(plugin, key);
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
      if (tree.nodes[index].kind != fo4::env::PackageProcedureKind::container) continue;
      std::size_t added = 0;
      if (fo4::env::add_package_procedure_node(plugin, key, index, "Travel", &added) != Status::updated) continue;
      const auto grown = fo4::env::decode_package_procedure(*fo4::find_record(plugin, key));
      ASSERT_TRUE(grown.decoded);
      ASSERT_LT(added, grown.nodes.size());
      EXPECT_EQ(grown.nodes[added].name, "Travel");
      ASSERT_EQ(grown.inputs.size(), 8u);
      const auto with_inputs = subs_of(plugin, key);
      EXPECT_EQ(fo4::env::remove_package_procedure_node(plugin, key, added), Status::updated);
      EXPECT_EQ(subs_of(plugin, key), original);
      const auto restored_tree = fo4::env::decode_package_procedure(*fo4::find_record(plugin, key));
      EXPECT_TRUE(restored_tree.decoded);
      EXPECT_EQ(restored_tree.inputs.size(), tree.inputs.size());
      std::size_t readded = 0;
      if (fo4::env::add_package_procedure_node(plugin, key, index, "Travel", &readded) != Status::updated) continue;
      EXPECT_EQ(readded, added);
      EXPECT_EQ(subs_of(plugin, key), with_inputs);
      ++edited;
      break;
    }
  }
  RecordProperty("travel_leaves_edited", edited);
  EXPECT_GT(edited, 0);
}

TEST(PackageTreeReal, RemovesALeafWithInputsDropsOnlyItsInputsAndKeepsReferencesResolvable) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  int cascaded = 0;
  for (const auto& key : pack_records(plugin)) {
    if (cascaded != 0) break;
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) continue;
    const auto tree = fo4::env::decode_package_procedure(*record);
    if (!tree.decoded || tree.nodes.size() < 2) continue;
    const auto original = subs_of(plugin, key);
    for (std::size_t index = 1; index < tree.nodes.size(); ++index) {
      if (tree.nodes[index].kind != fo4::env::PackageProcedureKind::leaf) continue;
      if (leaf_input_ids(*record, tree.nodes[index]).empty()) continue;
      const auto before_inputs = tree.inputs.size();
      if (fo4::env::remove_package_procedure_node(plugin, key, index) != Status::updated) {
        restore_subs(plugin, key, original);
        continue;
      }
      const auto* after_record = fo4::find_record(plugin, key);
      ASSERT_NE(after_record, nullptr);
      const auto after = fo4::env::decode_package_procedure(*after_record);
      EXPECT_TRUE(after.decoded);
      EXPECT_TRUE(references_known_inputs(*after_record, after));
      if (after.inputs.size() >= before_inputs) {
        restore_subs(plugin, key, original);
        continue;
      }
      const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fallout4.esm");
      const auto* saved = fo4::find_record(reread, key);
      ASSERT_NE(saved, nullptr);
      EXPECT_TRUE(fo4::env::decode_package_procedure(*saved).decoded);
      ++cascaded;
      break;
    }
  }
  RecordProperty("cascaded_leaves_removed", cascaded);
  EXPECT_GT(cascaded, 0);
}
