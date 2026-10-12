#include "fo4/c_api_dialogue.h"
#include "fo4/environment/dialogue.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/text/encoding.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <type_traits>

namespace {

class DialogueReal : public testing::TestWithParam<const char*> {};

template <typename T>
void optional_value(const std::optional<T>& native, std::int32_t has, T value) {
  EXPECT_EQ(has, native.has_value() ? 1 : 0);
  if (!native) { return; }
  if constexpr (std::is_same_v<T, float>) {
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value), std::bit_cast<std::uint32_t>(*native));
  } else {
    EXPECT_EQ(value, *native);
  }
}

void key(const std::optional<fo4::FormKey>& native, radicle_form_key value) {
  const bool present = native && !native->owner().empty();
  ASSERT_EQ(value.plugin != nullptr, present);
  if (present) {
    EXPECT_EQ(fo4::PluginKey::from_filename(value.plugin), native->owner());
    EXPECT_EQ(value.local_id, native->local().value());
  } else {
    EXPECT_EQ(value.local_id, 0u);
  }
}

void text(const std::optional<std::string_view>& native, const char* value) {
  ASSERT_EQ(value != nullptr, native.has_value());
  if (native) { EXPECT_EQ(value, fo4::text::windows1252_to_utf8(*native)); }
}

void name(const fo4::env::RecordName& native, const radicle_dialogue_text& value) {
  EXPECT_EQ(value.kind, static_cast<radicle_name_kind>(native.kind));
  EXPECT_EQ(value.string_id, native.string_id);
  text(native.kind == fo4::env::NameKind::inline_text ? std::optional{native.text} : std::nullopt, value.text);
}

void conditions(const std::vector<fo4::CtdaCondition>& native,
                const radicle_dialogue_condition* values, std::size_t count) {
  ASSERT_EQ(count, native.size());
  for (std::size_t i = 0; i < count; ++i) {
    SCOPED_TRACE("condition=" + std::to_string(i));
    const auto& n = native[i];
    const auto& v = values[i];
    EXPECT_EQ(v.flags_byte, n.flags_byte);
    EXPECT_EQ(v.comparison_raw, n.comparison_raw);
    EXPECT_EQ(v.function, n.function);
    EXPECT_EQ(v.unknown2, n.unknown2);
    EXPECT_EQ(v.param1, n.param1);
    EXPECT_EQ(v.param2, n.param2);
    EXPECT_EQ(v.run_on, n.run_on);
    EXPECT_EQ(v.reference, n.reference);
    EXPECT_EQ(v.unknown3, n.unknown3);
    text(n.parameter1_string ? std::optional<std::string_view>{*n.parameter1_string} : std::nullopt, v.parameter1_string);
    text(n.parameter2_string ? std::optional<std::string_view>{*n.parameter2_string} : std::nullopt, v.parameter2_string);
  }
}

void topic(const fo4::env::DialogueTopic& n, const radicle_dialogue_topic_info& v) {
  EXPECT_EQ(v.has_data, n.data.has_value() ? 1 : 0);
  if (n.data) {
    EXPECT_EQ(v.flags, n.data->flags);
    EXPECT_EQ(v.category, n.data->category);
    EXPECT_EQ(v.subtype, n.data->subtype);
  }
  optional_value(n.priority, v.has_priority, v.priority);
  optional_value(n.info_count, v.has_info_count, v.info_count);
  EXPECT_EQ(v.has_subtype_name, n.subtype_name.has_value() ? 1 : 0);
  if (n.subtype_name) { EXPECT_EQ(std::memcmp(v.subtype_name, n.subtype_name->data(), 4), 0); }
  key(n.branch, v.branch);
  key(n.quest, v.quest);
  key(n.keyword, v.keyword);
}

void info(const fo4::env::DialogueInfo& n, const radicle_dialogue_info& v) {
  EXPECT_EQ(v.has_data, n.data.has_value() ? 1 : 0);
  if (n.data) {
    EXPECT_EQ(v.flags, (*n.data)[0]);
    EXPECT_EQ(v.reset_time_encoded, (*n.data)[1]);
  }
  optional_value(n.reset_hours(), v.has_reset_hours, v.reset_hours);
  key(n.previous_topic, v.previous_topic);
  key(n.previous_info, v.previous_info);
  key(n.shared_info, v.shared_info);
  key(n.info_group, v.info_group);
  key(n.speaker, v.speaker);
  key(n.start_scene, v.start_scene);
  name(n.prompt, v.prompt);
  ASSERT_EQ(v.response_count, n.responses.size());
  for (std::size_t i = 0; i < v.response_count; ++i) {
    SCOPED_TRACE("response=" + std::to_string(i));
    const auto& a = n.responses[i];
    const auto& b = v.responses[i];
    key(a.emotion, b.emotion);
    key(a.sound, b.sound);
    EXPECT_EQ(b.number, a.number);
    EXPECT_EQ(b.unknown, a.unknown);
    EXPECT_EQ(b.interrupt_percent, a.interrupt_percent);
    EXPECT_EQ(b.camera_target_alias, a.camera_target_alias);
    EXPECT_EQ(b.camera_location_alias, a.camera_location_alias);
    name(a.text, b.text);
    key(a.speaker_idle, b.speaker_idle);
    key(a.listener_idle, b.listener_idle);
  }
  conditions(n.conditions, v.conditions, v.condition_count);
}

void scene(const fo4::env::Scene& n, const radicle_scene_info& v) {
  optional_value(n.flags, v.has_flags, v.flags);
  key(n.parent_quest, v.parent_quest);
  ASSERT_EQ(v.phase_count, n.phases.size());
  for (std::size_t i = 0; i < v.phase_count; ++i) {
    SCOPED_TRACE("phase=" + std::to_string(i));
    const auto& a = n.phases[i];
    const auto& b = v.phases[i];
    text(a.name, b.name);
    optional_value(a.editor_width, b.has_editor_width, b.editor_width);
    optional_value(a.flags, b.has_flags, b.flags);
    EXPECT_EQ(b.has_quest_stages, a.quest_stages.has_value() ? 1 : 0);
    if (a.quest_stages) {
      EXPECT_EQ(b.start_quest_stage, (*a.quest_stages)[0]);
      EXPECT_EQ(b.completion_quest_stage, (*a.quest_stages)[1]);
    }
    conditions(a.start_conditions, b.start_conditions, b.start_condition_count);
    conditions(a.completion_conditions, b.completion_conditions, b.completion_condition_count);
  }
  ASSERT_EQ(v.actor_count, n.actors.size());
  for (std::size_t i = 0; i < v.actor_count; ++i) {
    SCOPED_TRACE("actor=" + std::to_string(i));
    EXPECT_EQ(v.actors[i].alias, n.actors[i].alias);
    EXPECT_EQ(v.actors[i].flags, n.actors[i].flags);
    EXPECT_EQ(v.actors[i].behavior_flags, n.actors[i].behavior_flags);
  }
  ASSERT_EQ(v.action_count, n.actions.size());
  for (std::size_t i = 0; i < v.action_count; ++i) {
    SCOPED_TRACE("action=" + std::to_string(i));
    const auto& a = n.actions[i];
    const auto& b = v.actions[i];
    EXPECT_EQ(b.type, a.type);
    text(a.name, b.name);
    optional_value(a.actor_alias, b.has_actor_alias, b.actor_alias);
    optional_value(a.index, b.has_index, b.index);
    optional_value(a.flags, b.has_flags, b.flags);
    optional_value(a.start_phase, b.has_start_phase, b.start_phase);
    optional_value(a.end_phase, b.has_end_phase, b.end_phase);
    ASSERT_EQ(b.payload_count, a.payload.size());
    for (std::size_t p = 0; p < b.payload_count; ++p) {
      SCOPED_TRACE("payload=" + std::to_string(p));
      EXPECT_EQ(b.payload[p].signature, a.payload[p].signature.to_string());
      ASSERT_EQ(b.payload[p].size, a.payload[p].data.size());
      if (b.payload[p].size != 0) {
        EXPECT_EQ(std::memcmp(b.payload[p].bytes, a.payload[p].data.data(), b.payload[p].size), 0);
      }
    }
  }
  conditions(n.conditions, v.conditions, v.condition_count);
}

TEST_P(DialogueReal, EveryOfficialDialogueAndSceneDecodesAndMasterIsByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
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
  std::map<std::string, int> totals{{"DIAL", 0}, {"INFO", 0}, {"SCEN", 0}, {"SCEN_actions_with_ENAM", 0}};
  std::map<std::string, int> failures{{"DIAL", 0}, {"INFO", 0}, {"SCEN", 0}};
  std::map<std::uint16_t, int> action_types;
  const auto* result = testing::UnitTest::GetInstance()->current_test_info()->result();
  int parts_seen = 0;
  const auto new_failed_parts = [&] {
    int failed = 0;
    for (; parts_seen < result->total_part_count(); ++parts_seen) { failed += result->GetTestPartResult(parts_seen).failed(); }
    return failed;
  };
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "DIAL" && type != "INFO" && type != "SCEN") { continue; }
    SCOPED_TRACE(refs[i]->record->form_key.to_string());
    ++totals[type];
    try {
      if (type == "DIAL") {
        const auto native = fo4::env::decode_dialogue_topic(env, *refs[i]);
        radicle_dialogue_topic_info value{};
        EXPECT_EQ(radicle_dialogue_topic_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
        topic(native, value);
      } else if (type == "INFO") {
        const auto native = fo4::env::decode_dialogue_info(env, *refs[i]);
        const radicle_dialogue_info* value = nullptr;
        EXPECT_EQ(radicle_dialogue_info_decode(abi, records[i], &value), RADICLE_STATUS_OK);
        EXPECT_NE(value, nullptr);
        if (value) {
          info(native, *value);
          radicle_dialogue_info_free(value);
        }
      } else {
        const auto native = fo4::env::decode_scene(env, *refs[i]);
        for (const auto& action : native.actions) {
          EXPECT_TRUE(action.end_phase.has_value());
          ++totals["SCEN_actions_with_ENAM"];
          ++action_types[action.type];
        }
        const radicle_scene_info* value = nullptr;
        EXPECT_EQ(radicle_scene_decode(abi, records[i], &value), RADICLE_STATUS_OK);
        EXPECT_NE(value, nullptr);
        if (value) {
          scene(native, *value);
          for (std::size_t a = 0; a < value->action_count; ++a) { EXPECT_EQ(value->actions[a].has_end_phase, 1); }
          radicle_scene_free(value);
        }
      }
    } catch (const std::exception& error) {
      ADD_FAILURE() << "decode threw: " << error.what();
    }
    if (new_failed_parts() != 0) { ++failures[type]; }
  }
  radicle_free_record_array(records);
  radicle_env_destroy(abi);
  for (const auto& [type, total] : totals) { RecordProperty(type, total); }
  for (const auto& [type, total] : action_types) { RecordProperty("SCEN_action_type_" + std::to_string(type), total); }
  for (const auto& [type, failed] : failures) {
    RecordProperty(type + "_decode_failures", failed);
    EXPECT_EQ(failed, 0) << type;
  }
  if (std::strcmp(GetParam(), "Fallout4.esm") == 0 && exact_counts_known(build)) {
    EXPECT_EQ(totals["DIAL"], 35443);
    EXPECT_EQ(totals["INFO"], 78087);
    EXPECT_EQ(totals["SCEN"], 3568);
  }
  std::cout << GetParam();
  for (const auto& [type, total] : totals) { std::cout << ' ' << type << '=' << total; }
  for (const auto& [type, failed] : failures) { std::cout << ' ' << type << "_decode_failures=" << failed; }
  std::cout << '\n';
  const auto bytes = fo4::write_plugin(env.plugin(0));
  const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size());
  EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, DialogueReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
