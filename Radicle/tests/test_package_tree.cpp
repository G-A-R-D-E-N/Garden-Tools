#include "fo4/environment/package_procedure.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "package_tree_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace fo4;
using Status = fo4::env::PackageTreeMutationStatus;
namespace fixture = fo4::test::packtree;

Plugin read_fixture() { return read_plugin(fixture::plugin(), "Pack.esp"); }
FormKey key(std::uint32_t local) { return FormKey{PluginKey::from_filename("Pack.esp"), LocalFormId{local}}; }

std::vector<std::pair<std::string, std::vector<std::byte>>> subs_of(const Plugin& plugin, std::uint32_t local) {
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  const auto* record = find_record(plugin, key(local));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

std::vector<std::uint8_t> leaf_input_ids(const Record& record, const env::PackageProcedureNode& node) {
  std::vector<std::uint8_t> ids;
  for (std::size_t at = node.opaque_begin; at < node.opaque_end; ++at) {
    const auto& sub = record.subrecords[at];
    if (sub.signature == Signature::from_string("PKC2") && sub.data.size() == 1) {
      ids.push_back(std::to_integer<std::uint8_t>(sub.data[0]));
    }
  }
  return ids;
}

std::uint32_t pkcu_input_count(const Plugin& plugin, std::uint32_t local) {
  const auto* record = find_record(plugin, key(local));
  if (record == nullptr) return 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == Signature::from_string("PKCU") && sub.data.size() == 12) {
      std::uint32_t value = 0;
      std::memcpy(&value, sub.data.data(), 4);
      return value;
    }
  }
  return 0;
}

}  // namespace

TEST(PackageTree, AddsLeafNodesAndRebuildsEveryBranchCount) {
  auto plugin = read_fixture();
  const auto before = env::decode_package_procedure(*find_record(plugin, key(0x800)));
  ASSERT_TRUE(before.decoded);
  ASSERT_EQ(before.nodes.size(), 4u);
  ASSERT_EQ(before.nodes[1].child_count, 2u);
  const auto original = subs_of(plugin, 0x800);

  std::size_t added = 99;
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 1, "Activate", &added), Status::updated);
  auto nested = env::decode_package_procedure(*find_record(plugin, key(0x800)));
  ASSERT_TRUE(nested.decoded);
  ASSERT_EQ(nested.nodes.size(), 5u);
  EXPECT_EQ(nested.nodes[1].child_count, 3u);
  EXPECT_EQ(nested.nodes[1].children.size(), 3u);
  ASSERT_LT(added, nested.nodes.size());
  EXPECT_EQ(nested.nodes[added].name, "Activate");

  std::size_t root_added = 99;
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 0, "Wander", &root_added), Status::updated);
  auto rooted = env::decode_package_procedure(*find_record(plugin, key(0x800)));
  ASSERT_TRUE(rooted.decoded);
  ASSERT_EQ(rooted.nodes.size(), 6u);
  EXPECT_EQ(rooted.nodes[0].child_count, 2u);
  EXPECT_EQ(rooted.nodes[root_added].name, "Wander");

  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), root_added), Status::updated);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), added), Status::updated);
  EXPECT_EQ(subs_of(plugin, 0x800), original);
  const auto reloaded = read_plugin(write_plugin(plugin), "Pack.esp");
  EXPECT_EQ(subs_of(reloaded, 0x800), original);
}

TEST(PackageTree, AddsDefaultInputsForAProcedureAndRemovalRestoresTheBytes) {
  auto plugin = read_fixture();
  const auto original = subs_of(plugin, 0x800);
  std::size_t added = 99;
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 1, "Travel", &added), Status::updated);
  const auto* record = find_record(plugin, key(0x800));
  ASSERT_NE(record, nullptr);
  const auto grown = env::decode_package_procedure(*record);
  ASSERT_TRUE(grown.decoded);
  ASSERT_EQ(grown.nodes.size(), 5u);
  ASSERT_LT(added, grown.nodes.size());
  EXPECT_EQ(grown.nodes[added].name, "Travel");
  ASSERT_EQ(grown.inputs.size(), 8u);
  ASSERT_EQ(pkcu_input_count(plugin, 0x800), 8u);
  const auto payload_ids = leaf_input_ids(*record, grown.nodes[added]);
  ASSERT_EQ(payload_ids.size(), 8u);
  for (std::size_t slot = 0; slot < payload_ids.size(); ++slot) {
    EXPECT_EQ(payload_ids[slot], grown.inputs[slot].index);
  }

  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), added), Status::updated);
  EXPECT_EQ(pkcu_input_count(plugin, 0x800), 0u);
  EXPECT_EQ(subs_of(plugin, 0x800), original);
  const auto reloaded = read_plugin(write_plugin(plugin), "Pack.esp");
  EXPECT_EQ(subs_of(reloaded, 0x800), original);
}

TEST(PackageTree, RemovesALeafAndDropsTheInputsOnlyItUsedThenReindexes) {
  auto plugin = read_fixture();
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x805), 1), Status::updated);
  const auto* record = find_record(plugin, key(0x805));
  ASSERT_NE(record, nullptr);
  const auto decoded = env::decode_package_procedure(*record);
  ASSERT_TRUE(decoded.decoded);
  ASSERT_EQ(decoded.nodes.size(), 2u);
  EXPECT_EQ(decoded.nodes[1].name, "Wander");
  EXPECT_EQ(pkcu_input_count(plugin, 0x805), 1u);
  ASSERT_EQ(decoded.inputs.size(), 1u);
  EXPECT_EQ(decoded.inputs[0].index, 0);
  ASSERT_TRUE(decoded.next_input_id);
  EXPECT_EQ(*decoded.next_input_id, 1);
  const auto kept_ids = leaf_input_ids(*record, decoded.nodes[1]);
  ASSERT_EQ(kept_ids.size(), 1u);
  EXPECT_EQ(kept_ids[0], 0);
  const auto reloaded = read_plugin(write_plugin(plugin), "Pack.esp");
  EXPECT_TRUE(env::decode_package_procedure(*find_record(reloaded, key(0x805))).decoded);
}

TEST(PackageTree, KeepsAnInputThatAnotherLeafStillReferences) {
  auto plugin = read_fixture();
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x804), 1), Status::updated);
  const auto* record = find_record(plugin, key(0x804));
  ASSERT_NE(record, nullptr);
  const auto decoded = env::decode_package_procedure(*record);
  ASSERT_TRUE(decoded.decoded);
  ASSERT_EQ(decoded.nodes.size(), 2u);
  ASSERT_EQ(decoded.inputs.size(), 2u);
  EXPECT_EQ(decoded.inputs[0].index, 1);
  EXPECT_EQ(decoded.inputs[1].index, 3);
  EXPECT_EQ(pkcu_input_count(plugin, 0x804), 2u);
  const auto kept_ids = leaf_input_ids(*record, decoded.nodes[1]);
  ASSERT_EQ(kept_ids.size(), 2u);
  EXPECT_EQ(kept_ids[0], 3);
  EXPECT_EQ(kept_ids[1], 1);
}

TEST(PackageTree, RefusesOnlyWhenSomethingElseRefersOrThePayloadIsUnknown) {
  auto plugin = read_fixture();
  const auto original = write_plugin(plugin);
  std::size_t out = 99;
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 2, "Activate", &out), Status::not_a_container);
  EXPECT_EQ(out, 0u);
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 0, "Fly", &out), Status::unknown_procedure);
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x800), 9, "Activate", &out), Status::index_out_of_range);
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x802), 0, "Activate", &out), Status::not_a_package);
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x803), 0, "Activate", &out), Status::undecodable);
  EXPECT_EQ(env::add_package_procedure_node(plugin, key(0x807), 0, "Travel", &out), Status::templated_package);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), 1), Status::not_a_leaf);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), 0), Status::root_node);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), 9), Status::index_out_of_range);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x801), 2), Status::leaf_payload_undetermined);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x806), 1), Status::reference_orphaned);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x808), 1), Status::input_id_conflict);
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x802), 0), Status::not_a_package);
  EXPECT_EQ(write_plugin(plugin), original);
}

TEST(PackageTree, RemovesALeafAndKeepsTheRestOfTheTreeStable) {
  auto plugin = read_fixture();
  EXPECT_EQ(env::remove_package_procedure_node(plugin, key(0x800), 3), Status::updated);
  const auto decoded = env::decode_package_procedure(*find_record(plugin, key(0x800)));
  ASSERT_TRUE(decoded.decoded);
  ASSERT_EQ(decoded.nodes.size(), 3u);
  EXPECT_EQ(decoded.nodes[1].child_count, 1u);
  EXPECT_EQ(decoded.nodes[2].name, "Activate");
  const auto reloaded = read_plugin(write_plugin(plugin), "Pack.esp");
  EXPECT_TRUE(env::decode_package_procedure(*find_record(reloaded, key(0x800))).decoded);
}
