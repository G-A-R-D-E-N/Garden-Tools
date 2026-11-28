#include "fo4/c_api_dialogue.h"
#include "fo4/c_api_vmad.h"
#include "fo4/environment/info_result.hpp"
#include "fo4/environment/scene_targets.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"
#include "official_masters.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>

namespace {

class DialogueResultsReal : public testing::TestWithParam<const char*> {};

const fo4::env::RecordRef& master_record(const fo4::env::Environment& env, std::uint32_t id) {
  return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}});
}

void pinned_scenes(const fo4::env::Environment& env) {
  const auto stages = [&](std::uint32_t id) { return fo4::env::decode_scene_targets(env, master_record(env, id)).quest_stages; };
  const auto begin_end = stages(0x17B58);
  ASSERT_TRUE(begin_end); EXPECT_EQ((*begin_end)[0], 102); EXPECT_EQ((*begin_end)[1], 91);
  const auto begin_only = stages(0x19E146);
  ASSERT_TRUE(begin_only); EXPECT_EQ((*begin_only)[0], 105); EXPECT_EQ((*begin_only)[1], -1);
  const auto end_only = stages(0x71407);
  ASSERT_TRUE(end_only); EXPECT_EQ((*end_only)[0], -1); EXPECT_EQ((*end_only)[1], 45);
  int timer_stage = -1;
  for (const auto& action : fo4::env::decode_scene_targets(env, master_record(env, 0x36FDF)).actions) {
    if (action.quest_stage) timer_stage = *action.quest_stage;
  }
  EXPECT_EQ(timer_stage, 20);
  const auto entries = [&](std::uint32_t id) {
    std::size_t total = 0;
    for (const auto& action : fo4::env::decode_scene_targets(env, master_record(env, id)).actions) total += action.start_entries.size();
    return total;
  };
  EXPECT_EQ(entries(0x23A00), 3u); EXPECT_EQ(entries(0x1B762), 4u); EXPECT_EQ(entries(0xDED2E), 6u);
  bool greeting = false;
  for (const auto& action : fo4::env::decode_scene_targets(env, master_record(env, 0x2F42F)).actions) {
    if (action.type == 4) { greeting = action.end_scene_greeting; EXPECT_TRUE(action.start_entries.empty()); }
  }
  EXPECT_TRUE(greeting);
}

void pinned_infos(const fo4::env::Environment& env) {
  using fo4::env::InfoFragmentKind;
  const auto result = [&](std::uint32_t id) { return fo4::env::decode_info_result(env, master_record(env, id)); };
  const auto begin = result(0x17D124);
  EXPECT_EQ(begin.fragment_state, fo4::env::InfoFragmentState::present); EXPECT_EQ(begin.fragment_flags, 1);
  ASSERT_EQ(begin.rows.size(), 1u); EXPECT_EQ(begin.rows[0].kind, InfoFragmentKind::on_begin);
  EXPECT_EQ(begin.rows[0].script, "Fragments:TopicInfos:TIF_DialogueDiamondCity_0017D124");
  EXPECT_EQ(begin.rows[0].function, "Fragment_Begin");
  ASSERT_EQ(begin.fragment_script.properties.size(), 1u); EXPECT_EQ(begin.fragment_script.properties[0].name, "Caps001");
  const auto end = result(0x23BAD);
  ASSERT_EQ(end.rows.size(), 1u); EXPECT_EQ(end.rows[0].kind, InfoFragmentKind::on_end);
  EXPECT_EQ(end.rows[0].function, "Fragment_End");
  const auto both = result(0x34CF6);
  ASSERT_EQ(both.rows.size(), 2u); EXPECT_EQ(both.rows[0].function, "Fragment_Begin"); EXPECT_EQ(both.rows[1].function, "Fragment_End");
  const auto both_stages = result(0x3A487).stages;
  ASSERT_TRUE(both_stages); EXPECT_EQ((*both_stages)[0], 57); EXPECT_EQ((*both_stages)[1], 62);
  const auto end_stage = result(0x1DAA2).stages;
  ASSERT_TRUE(end_stage); EXPECT_EQ((*end_stage)[0], 0xFFFF); EXPECT_EQ((*end_stage)[1], 35);
  const auto begin_stage = result(0xA4744).stages;
  ASSERT_TRUE(begin_stage); EXPECT_EQ((*begin_stage)[0], 8); EXPECT_EQ((*begin_stage)[1], 0xFFFF);
  EXPECT_EQ(result(0x673E3).start_scene_phase, "Barter");
}

TEST_P(DialogueResultsReal, EveryOfficialSceneTargetAndInfoResultDecodesCleanAndMatchesTheAbi) {
  const auto* dir = ::fo4::test::fo4_data_dir_cstr();
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const std::filesystem::path native_paths[] = {path};
  const auto env = fo4::env::Environment::load(native_paths);
  const auto filename = path.string();
  const char* paths[] = {filename.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0);
  ASSERT_EQ(count, refs.size());
  std::map<std::string, int> totals;
  std::map<std::uint16_t, int> action_types;
  std::size_t start_entries = 0;
  int greetings = 0, failures = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "SCEN" && type != "INFO") continue;
    ++totals[type];
    const int before = testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
    try {
      if (type == "SCEN") {
        const auto native = fo4::env::decode_scene_targets(env, *refs[i]);
        const auto scene = fo4::env::decode_scene(env, *refs[i]);
        ASSERT_EQ(native.actions.size(), scene.actions.size());
        const radicle_scene_targets* view = nullptr;
        ASSERT_EQ(radicle_scene_targets_decode(abi, records[i], &view), RADICLE_STATUS_OK);
        ASSERT_EQ(view->action_count, native.actions.size());
        EXPECT_EQ(view->has_quest_stages, native.quest_stages ? 1 : 0);
        if (native.quest_stages) {
          EXPECT_EQ(view->quest_begin_stage, (*native.quest_stages)[0]);
          EXPECT_EQ(view->quest_end_stage, (*native.quest_stages)[1]);
        }
        for (std::size_t a = 0; a < native.actions.size(); ++a) {
          const auto& action = native.actions[a];
          EXPECT_EQ(action.type, scene.actions[a].type);
          EXPECT_FALSE(action.malformed);
          EXPECT_EQ(view->actions[a].type, action.type);
          EXPECT_EQ(view->actions[a].malformed, action.malformed ? 1 : 0);
          EXPECT_EQ(view->actions[a].link_count, action.links.size());
          EXPECT_EQ(view->actions[a].package_count, action.packages.size());
          EXPECT_EQ(view->actions[a].head_tracking_count, action.head_tracking.size());
          EXPECT_EQ(view->actions[a].start_entry_count, action.start_entries.size());
          ++action_types[action.type];
          start_entries += action.start_entries.size();
          if (action.end_scene_greeting) ++greetings;
        }
        radicle_scene_targets_free(view);
      } else {
        const auto native = fo4::env::decode_info_result(env, *refs[i]);
        EXPECT_NE(native.fragment_state, fo4::env::InfoFragmentState::unreadable) << native.fragment_failure;
        ++totals["INFO_state_" + std::to_string(static_cast<int>(native.fragment_state))];
        if (native.fragment_state == fo4::env::InfoFragmentState::present) ++totals["INFO_flags_" + std::to_string(native.fragment_flags)];
        if (native.stages && (*native.stages)[0] == 0xFFFF && (*native.stages)[1] != 0xFFFF) ++totals["INFO_end_only"];
        if (native.stages && (*native.stages)[1] == 0xFFFF && (*native.stages)[0] != 0xFFFF) ++totals["INFO_begin_only"];
        if (native.stages && (*native.stages)[0] != 0xFFFF && (*native.stages)[1] != 0xFFFF) ++totals["INFO_both_stages"];
        const radicle_info_result* view = nullptr;
        ASSERT_EQ(radicle_info_result_decode(abi, records[i], &view), RADICLE_STATUS_OK);
        EXPECT_EQ(view->fragment_state, static_cast<radicle_info_fragment_state>(native.fragment_state));
        EXPECT_EQ(view->row_count, native.rows.size());
        EXPECT_EQ(view->has_stages, native.stages ? 1 : 0);
        radicle_info_result_free(view);
        radicle_vmad_scripts* scripts = nullptr;
        const auto status = radicle_info_fragment_script(abi, records[i], &scripts);
        EXPECT_EQ(status, native.fragment_state == fo4::env::InfoFragmentState::present ? RADICLE_STATUS_OK : RADICLE_STATUS_NOT_FOUND);
        if (status == RADICLE_STATUS_OK) radicle_vmad_scripts_destroy(scripts);
      }
    } catch (const std::exception& error) {
      ADD_FAILURE() << "decode threw: " << error.what();
    }
    if (testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count() != before) ++failures;
  }
  radicle_free_record_array(records);
  radicle_env_destroy(abi);
  EXPECT_EQ(failures, 0);
  for (const auto& [name, total] : totals) { RecordProperty(name, total); }
  for (const auto& [type, total] : action_types) { RecordProperty("SCEN_action_type_" + std::to_string(type), total); }
  if (std::strcmp(GetParam(), "Fallout4.esm") == 0 && exact_counts_known(build)) {
    EXPECT_EQ(totals["SCEN"], 3568);
    EXPECT_EQ(action_types[0], 10288); EXPECT_EQ(action_types[1], 2391); EXPECT_EQ(action_types[2], 854);
    EXPECT_EQ(action_types[3], 2422); EXPECT_EQ(action_types[4], 687); EXPECT_EQ(action_types[5], 437);
    EXPECT_EQ(action_types[6], 1748);
    EXPECT_EQ(start_entries, 727u); EXPECT_EQ(greetings, 24);
    EXPECT_EQ(totals["INFO_state_0"], 73543); EXPECT_EQ(totals["INFO_state_1"], 2748); EXPECT_EQ(totals["INFO_state_3"], 1796);
    EXPECT_EQ(totals["INFO_flags_1"], 861); EXPECT_EQ(totals["INFO_flags_2"], 848); EXPECT_EQ(totals["INFO_flags_3"], 87);
    EXPECT_EQ(totals["INFO_end_only"], 3385); EXPECT_EQ(totals["INFO_begin_only"], 679); EXPECT_EQ(totals["INFO_both_stages"], 48);
    pinned_scenes(env);
    pinned_infos(env);
  }
  std::cout << GetParam() << " SCEN=" << totals["SCEN"] << " INFO=" << totals["INFO"] << " failures=" << failures << '\n';
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, DialogueResultsReal,
    testing::ValuesIn(fo4::test::kOfficialMasters));

}
