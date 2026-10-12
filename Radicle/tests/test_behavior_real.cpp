#include "fo4/c_api_behavior.h"
#include "fo4/environment/behavior.hpp"
#include "fo4/environment/package_procedure.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace {

class BehaviorReal : public testing::TestWithParam<const char*> {};

TEST_P(BehaviorReal, EveryOfficialPackageCombatStyleAndIdleDecodesAndMasterIsByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0); ASSERT_EQ(count, refs.size()); std::map<std::string, int> totals, pack_types;
  std::map<std::string, std::string> pack_examples; int pack_templates = 0;
  std::map<std::string, int> leaf_counts; int pack_procedures = 0, pack_procedures_decoded = 0, leaf_input_mismatches = 0,
      pkcu_input_mismatches = 0, pldt_records = 0, pldt_nonzero_padding = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "PACK" && type != "CSTY" && type != "IDLE") continue;
    SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++totals[type];
    if (type == "PACK") {
      fo4::env::Package package; EXPECT_NO_THROW(package = fo4::env::decode_package(env, *refs[i])); const radicle_package_info* value = nullptr;
      EXPECT_EQ(radicle_package_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_package_free(value);
      const auto slice = package.data ? "PACK_TYPE_" + std::to_string(package.data->type) : std::string{"PACK_TYPE_none"};
      ++pack_types[slice];
      if (package.counter && package.counter->template_package != fo4::FormKey{}) ++pack_templates;
      if (!package.locations.empty() || !package.targets.empty()) pack_examples.try_emplace(slice, refs[i]->record->form_key.to_string());
      const auto tree = fo4::env::decode_package_procedure(*refs[i]->record);
      EXPECT_TRUE(tree.decoded); ++pack_procedures; if (tree.decoded) ++pack_procedures_decoded;
      for (const auto& node : tree.nodes) {
        if (node.kind != fo4::env::PackageProcedureKind::leaf) continue;
        ++leaf_counts[node.name];
        const auto expected = fo4::env::expected_input_count(node.name);
        if (expected && *expected != tree.inputs.size()) ++leaf_input_mismatches;
      }
      if (tree.decoded && package.counter && package.counter->input_count != tree.inputs.size()) ++pkcu_input_mismatches;
      const radicle_package_procedure_tree* procedure = nullptr;
      EXPECT_EQ(radicle_package_procedure_decode(abi, records[i], &procedure), RADICLE_STATUS_OK); radicle_package_procedure_free(procedure);
      for (const auto& sub : refs[i]->record->subrecords) {
        if (sub.signature.view() != "PLDT") continue;
        ++pldt_records;
        if (sub.data.size() >= 4 && (sub.data[1] != std::byte{0} || sub.data[2] != std::byte{0} || sub.data[3] != std::byte{0})) ++pldt_nonzero_padding;
      }
    } else if (type == "CSTY") {
      EXPECT_NO_THROW(fo4::env::decode_combat_style(env, *refs[i])); radicle_combat_style_info value{};
      EXPECT_EQ(radicle_combat_style_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
    } else {
      EXPECT_NO_THROW(fo4::env::decode_idle(env, *refs[i])); const radicle_idle_info* value = nullptr;
      EXPECT_EQ(radicle_idle_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_idle_free(value);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, total] : totals) RecordProperty(type, total);
  for (const auto& [slice, pack_count] : pack_types) RecordProperty(slice, pack_count);
  for (const auto& [slice, example] : pack_examples) RecordProperty(slice + "_example", example);
  RecordProperty("PACK_template_packages", pack_templates);
  for (const auto& [leaf, leaf_count] : leaf_counts) RecordProperty("PACK_LEAF_" + leaf, leaf_count);
  RecordProperty("PACK_procedure_total", pack_procedures); RecordProperty("PACK_procedure_decoded", pack_procedures_decoded);
  RecordProperty("PACK_LEAF_INPUT_COUNT_MISMATCH", leaf_input_mismatches); RecordProperty("PACK_PKCU_INPUT_COUNT_MISMATCH", pkcu_input_mismatches);
  RecordProperty("PLDT_records", pldt_records); RecordProperty("PLDT_nonzero_padding", pldt_nonzero_padding);
  EXPECT_EQ(pldt_nonzero_padding, 0);
  const auto bytes = fo4::write_plugin(env.plugin(0)); const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, BehaviorReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
