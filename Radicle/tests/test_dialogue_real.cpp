#include "fo4/c_api_dialogue.h"
#include "fo4/environment/dialogue.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace {

class DialogueReal : public testing::TestWithParam<const char*> {};

TEST_P(DialogueReal, EveryOfficialDialogueAndSceneDecodesAndMasterIsByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0); ASSERT_EQ(count, refs.size()); std::map<std::string, int> totals;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "DIAL" && type != "INFO" && type != "SCEN") continue;
    SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++totals[type];
    if (type == "DIAL") {
      EXPECT_NO_THROW(fo4::env::decode_dialogue_topic(env, *refs[i])); radicle_dialogue_topic_info value{};
      EXPECT_EQ(radicle_dialogue_topic_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
    } else if (type == "INFO") {
      EXPECT_NO_THROW(fo4::env::decode_dialogue_info(env, *refs[i])); const radicle_dialogue_info* value = nullptr;
      EXPECT_EQ(radicle_dialogue_info_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_dialogue_info_free(value);
    } else {
      const auto scene = fo4::env::decode_scene(env, *refs[i]);
      for (const auto& action : scene.actions) {
        EXPECT_TRUE(action.end_phase.has_value());
        ++totals["SCEN_actions_with_ENAM"];
      }
      const radicle_scene_info* value = nullptr;
      ASSERT_EQ(radicle_scene_decode(abi, records[i], &value), RADICLE_STATUS_OK);
      for (std::size_t action = 0; action < value->action_count; ++action) {
        EXPECT_TRUE(value->actions[action].has_end_phase);
      }
      radicle_scene_free(value);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, total] : totals) RecordProperty(type, total);
  const auto bytes = fo4::write_plugin(env.plugin(0)); const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, DialogueReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
