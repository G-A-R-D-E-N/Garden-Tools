#include "dialogue_writer_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_dialogue.h"
#include "fo4/c_api_env.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace fo4::test::dialogue;

radicle_form_key rkey(std::uint32_t id) { return radicle_form_key{k_plugin, id}; }

class DialogueWriterCApi : public testing::Test {
protected:
  void SetUp() override {
    std::filesystem::create_directories(root.path / "out");
    path = root.path / k_plugin;
    const auto bytes = build();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  TempRoot root;
  std::filesystem::path path;
};

struct Authored {
  std::uint32_t branch = 0, topic = 0, first = 0, second = 0;
};

std::string num(std::uint32_t value) { return std::to_string(value); }

std::string render(const fo4::Group& group) {
  std::string out = "G" + std::to_string(group.header.group_type) + "(" + num(group.header.label_as_u32()) + ")[";
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) {
      out += record->header.signature.to_string() + num(record->form_key.local().value()) + " ";
    } else {
      out += render(std::get<fo4::Group>(child)) + " ";
    }
  }
  return out + "]";
}

const fo4::Group* quest_children(const fo4::Plugin& plugin, std::uint32_t quest) {
  for (const auto& top : plugin.groups) {
    for (const auto& child : top.children) {
      const auto* group = std::get_if<fo4::Group>(&child);
      if (group != nullptr && group->header.group_type == static_cast<std::int32_t>(fo4::GroupType::quest_children) &&
          group->header.label_as_u32() == quest) {
        return group;
      }
    }
  }
  return nullptr;
}

std::vector<std::uint8_t> bytes_of(radicle_plugin* plugin) {
  std::uint8_t* bytes = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &count), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::uint8_t> out(bytes, bytes + count);
  radicle_free_bytes(bytes);
  return out;
}

Authored author_topic_with_two_infos(radicle_plugin* plugin) {
  const radicle_form_key quest = rkey(k_quest);
  radicle_form_key branch{}, topic{}, first{}, second{};
  radicle_dialogue_branch_spec branch_spec{};
  branch_spec.editor_id = "GreetBranch";
  EXPECT_EQ(radicle_plugin_create_dialogue_branch(plugin, quest, &branch_spec, &branch), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_dialogue_topic_spec topic_spec{};
  topic_spec.editor_id = "Greeting";
  topic_spec.name = "Hello there";
  topic_spec.priority = 50.0f;
  topic_spec.has_branch = 1;
  topic_spec.branch = branch;
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, quest, &topic_spec, &topic), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(radicle_plugin_set_dialogue_branch_start(plugin, branch, topic), RADICLE_STATUS_OK);

  radicle_dialogue_condition condition{};
  condition.flags_byte = 0x20;
  condition.function = 14;
  condition.param1 = 7;
  condition.unknown3 = -1;
  radicle_dialogue_response_spec first_response{};
  first_response.text = "Hello, friend";
  first_response.number = 1;
  first_response.camera_target_alias = -1;
  first_response.camera_location_alias = -1;
  radicle_dialogue_info_spec first_spec{};
  first_spec.flags = 0x0200;
  first_spec.reset_time_encoded = 24;
  first_spec.responses = &first_response;
  first_spec.response_count = 1;
  first_spec.conditions = &condition;
  first_spec.condition_count = 1;
  EXPECT_EQ(radicle_plugin_create_dialogue_info(plugin, topic, &first_spec, &first), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_dialogue_response_spec second_response{};
  second_response.text = "Caf\xC3\xA9 second";
  second_response.script_notes = "note";
  second_response.number = 2;
  second_response.interrupt_percent = 20;
  second_response.has_emotion = 1;
  second_response.emotion = rkey(k_kywd);
  second_response.has_sound = 1;
  second_response.sound = rkey(k_sndr);
  second_response.camera_target_alias = -1;
  second_response.camera_location_alias = -1;
  radicle_dialogue_info_spec second_spec{};
  second_spec.responses = &second_response;
  second_spec.response_count = 1;
  EXPECT_EQ(radicle_plugin_create_dialogue_info(plugin, topic, &second_spec, &second), RADICLE_STATUS_OK)
      << radicle_last_error();
  return {branch.local_id, topic.local_id, first.local_id, second.local_id};
}

}

TEST_F(DialogueWriterCApi, FieldSceneAndAuthoringStatusesMapAndSurviveReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key topic = rkey(k_topic), info = rkey(k_info), scene = rkey(k_scene), bare = rkey(k_bare);
  const radicle_form_key sndr = rkey(k_sndr), misc = rkey(k_misc), quest = rkey(k_quest), templ = rkey(k_template);
  const radicle_form_key kywd = rkey(k_kywd);
  const radicle_form_key unknown_master{"Other.esm", 1};

  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, topic, "DIAL\\Priority", 0, "60"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, topic, "DIAL\\Category", 0, "9"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, bare, "DIAL\\Category", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, info, "DIAL\\Priority", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, topic, "DIAL\\InfoCount", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, info, "INFO\\InterruptPercentage", 1, "20"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, info, "INFO\\InterruptPercentage", 2, "20"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, info, "INFO\\ResponseText", 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, info, "INFO\\ScriptNotes", 0, "\xE9"), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, info, "INFO\\SoundFile", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\SoundFile", 0, &sndr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\SoundFile", 1, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\Emotion", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\Speaker", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\Speaker", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, info, "INFO\\SharedInfo", 0, &info), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\EndPhase", 0, "2"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\EndPhase", 1, "2"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\EndPhase", 9, "2"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Actor\\Alias", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Nope", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, topic, "SCEN\\Flags", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, rkey(0x8FF), "SCEN\\Flags", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Flags", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, scene, "SCEN\\TemplateScene", &scene), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, scene, "SCEN\\ParentQuest", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, scene, "SCEN\\ParentQuest", &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, scene, "SCEN\\ParentQuest", &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, templ, "SCEN\\TemplateScene", &scene), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, topic, "SCEN\\ParentQuest", &quest), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 0, "SCEN\\Action\\Topic", &topic), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 0, "SCEN\\Action\\Topic", &misc),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 0, "SCEN\\Action\\Topic", nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 0, "SCEN\\Nope", &topic),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 1, "SCEN\\Action\\Topic", &topic),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 9, "SCEN\\Action\\Topic", &topic),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 2, "SCEN\\Action\\AnimArchType", &kywd),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, scene, 0, "SCEN\\ParentQuest", &quest),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, topic, 0, "SCEN\\Action\\Topic", &topic),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_formlink(plugin, rkey(0x8FF), 0, "SCEN\\Action\\Topic", &topic),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_formlink(plugin, scene, "SCEN\\Action\\Topic", &topic),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\LoopingMax", 0, "7.5"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\LoopingMax", 1, "7.5"),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_scalar(plugin, scene, "SCEN\\Action\\TimerMax", 1, "8.5"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, RADICLE_SCENE_PART_ACTION, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, RADICLE_SCENE_PART_PHASE, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, rkey(0x8FF), RADICLE_SCENE_PART_ACTOR, &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, topic, RADICLE_SCENE_PART_ACTOR, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, static_cast<radicle_scene_part>(3), &count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "DATA"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "DATA"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "QNAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, info, "IOVR"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, bare, "PNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, info, "GREE", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, info, "GREE", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, scene, "FNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_form_key refused_key{};
  for (const char* signature : {"DIAL", "INFO", "SCEN"}) {
    EXPECT_EQ(radicle_plugin_create_record(plugin, signature, "New", &refused_key), RADICLE_STATUS_INVALID_ARGUMENT) << signature;
  }

  const auto saved = (root.path / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  radicle_dialogue_topic_info topic_info{};
  ASSERT_EQ(radicle_env_winner(env, topic, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_topic_get_info(env, handle, &topic_info), RADICLE_STATUS_OK);
  EXPECT_EQ(topic_info.priority, 60.0f);
  const radicle_dialogue_info* decoded = nullptr;
  ASSERT_EQ(radicle_env_winner(env, info, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, handle, &decoded), RADICLE_STATUS_OK);
  ASSERT_EQ(decoded->response_count, 2u);
  EXPECT_EQ(decoded->responses[0].sound.local_id, k_sndr);
  EXPECT_EQ(decoded->responses[1].sound.plugin, nullptr);
  EXPECT_EQ(decoded->responses[1].interrupt_percent, 20);
  EXPECT_STREQ(decoded->responses[0].text.text, "Caf\xC3\xA9");
  radicle_dialogue_info_free(decoded);
  const radicle_scene_info* decoded_scene = nullptr;
  ASSERT_EQ(radicle_env_winner(env, scene, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_scene_decode(env, handle, &decoded_scene), RADICLE_STATUS_OK);
  ASSERT_EQ(decoded_scene->action_count, 3u);
  EXPECT_EQ(decoded_scene->actions[0].end_phase, 2u);
  EXPECT_EQ(decoded_scene->actions[1].has_start_phase, 1);
  EXPECT_EQ(decoded_scene->actions[1].has_end_phase, 0);
  ASSERT_EQ(decoded_scene->actions[1].payload_count, 2u);
  EXPECT_STREQ(decoded_scene->actions[1].payload[0].signature, "SNAM");
  radicle_scene_free(decoded_scene);
  radicle_env_destroy(env);
}

TEST_F(DialogueWriterCApi, MalformedTopLevelLinksRefuseNativeAndCApiWritesWithoutChangingBytes) {
  struct Case { std::uint32_t record; const char* path; std::uint32_t target; };
  const Case cases[] = {
      {k_topic, "DIAL\\Branch", k_dlbr}, {k_topic, "DIAL\\Quest", k_quest},
      {k_topic, "DIAL\\Keyword", k_kywd}, {k_info, "INFO\\PreviousTopic", k_topic},
      {k_info, "INFO\\PreviousInfo", k_group}, {k_info, "INFO\\SharedInfo", k_group},
      {k_info, "INFO\\InfoGroup", k_group}, {k_info, "INFO\\Speaker", k_npc},
      {k_info, "INFO\\StartScene", k_scene}, {k_info, "INFO\\AudioOutputOverride", k_sopm},
      {k_info, "INFO\\ResetGlobal", k_glob}};
  for (const auto& item : cases) {
    for (const std::size_t size : {0u, 3u, 5u, 8u}) {
      SCOPED_TRACE(std::string(item.path) + " size=" + std::to_string(size));
      auto native = read(build());
      const auto field = fo4::schema::formlink_field_descriptor(item.path).value();
      fo4::visit_records(native, [&](fo4::Record& record) {
        if (record.form_key != key(item.record)) { return; }
        std::erase_if(record.subrecords, [&](const fo4::Subrecord& sub) { return sub.signature == field.subrecord; });
        record.subrecords.push_back({field.subrecord, Bytes(size, std::byte{0x55}), false});
        record.original_range.clear();
        record.decompressed_payload.clear();
        native.original_range.clear();
      });
      const auto before = fo4::write_plugin(native);
      fo4::write_plugin_file(native, path);
      EXPECT_THROW(link(native, item.record, item.path, 0, key(item.target)), fo4::schema::FormLinkFieldError);
      EXPECT_EQ(fo4::write_plugin(native), before);
      radicle_plugin* plugin = nullptr;
      ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
      const auto target = rkey(item.target);
      EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(item.record), item.path, 0, &target),
                RADICLE_STATUS_INVALID_ARGUMENT);
      std::uint8_t* bytes = nullptr;
      std::size_t count = 0;
      ASSERT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &count), RADICLE_STATUS_OK);
      EXPECT_EQ(count, before.size());
      if (count == before.size()) { EXPECT_EQ(std::memcmp(bytes, before.data(), count), 0); }
      radicle_free_bytes(bytes);
      radicle_plugin_destroy(plugin);
    }
  }
}

TEST_F(DialogueWriterCApi, DecodeRefusesRecordsFromAnotherEnvironment) {
  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* owner = nullptr;
  radicle_env* other = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &owner), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(paths, 1, &other), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record *topic = nullptr, *info = nullptr, *scene = nullptr;
  ASSERT_EQ(radicle_env_winner(owner, rkey(k_topic), &topic), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(owner, rkey(k_info), &info), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(owner, rkey(k_scene), &scene), RADICLE_STATUS_OK);

  radicle_dialogue_topic_info topic_info{};
  topic_info.flags = 9;
  EXPECT_EQ(radicle_dialogue_topic_get_info(other, topic, &topic_info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(topic_info.flags, 9);
  radicle_dialogue_info sentinel_info{};
  const radicle_dialogue_info* decoded = &sentinel_info;
  EXPECT_EQ(radicle_dialogue_info_decode(other, info, &decoded), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(decoded, &sentinel_info);
  radicle_scene_info sentinel_scene{};
  const radicle_scene_info* decoded_scene = &sentinel_scene;
  EXPECT_EQ(radicle_scene_decode(other, scene, &decoded_scene), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(decoded_scene, &sentinel_scene);

  EXPECT_EQ(radicle_dialogue_topic_get_info(owner, topic, &topic_info), RADICLE_STATUS_OK);
  radicle_env_destroy(other);
  radicle_env_destroy(owner);
}

TEST_F(DialogueWriterCApi, ScenePhaseAuthoringStatusesMapAndSurviveReload) {
  const auto file = path.string();
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key scene = rkey(k_scene), quest = rkey(k_quest);

  EXPECT_EQ(radicle_plugin_insert_scene_phase(plugin, scene, 1, "Middle"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_phase_name(plugin, scene, 1, "Renamed"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_insert_scene_phase(plugin, scene, 9, "TooFar"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_phase(plugin, rkey(0x8FF), 0, "Ghost"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_insert_scene_phase(plugin, scene, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_phase(plugin, scene, 9), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_phase(plugin, rkey(0x8FF), 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_phase_name(plugin, scene, 9, "Ghost"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_phase_name(plugin, scene, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_phase_name(plugin, quest, 0, "Outside"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_phase(plugin, quest, 0, "Outside"), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t count = 0;
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, RADICLE_SCENE_PART_PHASE, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const char* paths[] = {file.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, scene, &record), RADICLE_STATUS_OK);
  const radicle_scene_info* decoded = nullptr;
  ASSERT_EQ(radicle_scene_decode(env, record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(decoded->phase_count, 3u);
  EXPECT_STREQ(decoded->phases[0].name, "Start");
  EXPECT_STREQ(decoded->phases[1].name, "Renamed");
  EXPECT_STREQ(decoded->phases[2].name, "");
  EXPECT_EQ(decoded->phases[1].has_editor_width, 1);
  EXPECT_EQ(decoded->phases[1].editor_width, 350u);
  EXPECT_EQ(decoded->phases[1].has_flags, 0);
  EXPECT_EQ(decoded->phases[1].has_quest_stages, 0);
  EXPECT_EQ(decoded->actor_count, 2u);
  EXPECT_EQ(decoded->action_count, 3u);
  radicle_scene_free(decoded);
  radicle_env_destroy(env);
}

TEST_F(DialogueWriterCApi, SceneActorAndActionAuthoringStatusesMapAndSurviveReload) {
  const auto file = path.string();
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key scene = rkey(k_scene), quest = rkey(k_quest);

  EXPECT_EQ(radicle_plugin_insert_scene_actor(plugin, scene, 1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_insert_scene_actor(plugin, scene, 9), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_actor(plugin, rkey(0x8FF), 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_insert_scene_actor(plugin, quest, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_actor(plugin, scene, 9), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_actor(plugin, rkey(0x8FF), 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_scene_actor(plugin, quest, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_action(plugin, scene, 1, 2), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_insert_scene_action(plugin, scene, 9, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_action(plugin, scene, 1, 7), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_insert_scene_action(plugin, rkey(0x8FF), 0, 2), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_insert_scene_action(plugin, quest, 0, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_action(plugin, scene, 9), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_action(plugin, rkey(0x8FF), 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_scene_action(plugin, quest, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_name(plugin, scene, 1, "Timer"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_scene_action_name(plugin, scene, 9, "Ghost"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_action_name(plugin, rkey(0x8FF), 0, "Ghost"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_scene_action_name(plugin, scene, 1, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_scene_action_name(plugin, quest, 0, "Outside"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_scene_actor(plugin, scene, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_scene_action(plugin, scene, 0), RADICLE_STATUS_OK);

  size_t count = 0;
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, RADICLE_SCENE_PART_ACTOR, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, scene, RADICLE_SCENE_PART_ACTION, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const char* paths[] = {file.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, scene, &record), RADICLE_STATUS_OK);
  const radicle_scene_info* decoded = nullptr;
  ASSERT_EQ(radicle_scene_decode(env, record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(decoded->actor_count, 2u);
  EXPECT_EQ(decoded->actors[0].alias, -1);
  EXPECT_EQ(decoded->actors[0].behavior_flags, 26u);
  EXPECT_EQ(decoded->actors[1].alias, 1);
  ASSERT_EQ(decoded->action_count, 3u);
  EXPECT_EQ(decoded->actions[0].type, 2);
  EXPECT_EQ(decoded->actions[0].has_actor_alias, 1);
  EXPECT_EQ(decoded->actions[0].actor_alias, -1);
  EXPECT_EQ(decoded->actions[0].payload_count, 0u);
  EXPECT_STREQ(decoded->actions[0].name, "Timer");
  EXPECT_EQ(decoded->actions[1].type, 2);
  EXPECT_EQ(decoded->actions[2].type, 1);
  radicle_scene_free(decoded);
  radicle_env_destroy(env);
}

TEST_F(DialogueWriterCApi, AuthoredTopicSavesWithTheRealGroupShapeChainAndConditions) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto authored = author_topic_with_two_infos(plugin);
  const auto saved = (root.path / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(plugin);

  auto reloaded = fo4::read_plugin_file(saved);
  fo4::assign_record_form_keys(reloaded);
  const auto* children = quest_children(reloaded, k_quest);
  ASSERT_NE(children, nullptr);
  const std::string expected = "G10(" + num(k_quest) + ")[DLBR" + num(authored.branch) + " DIAL" + num(k_topic) +
                               " G7(" + num(k_topic) + ")[INFO" + num(k_info) + " INFO" + num(k_group) + " ] DIAL" +
                               num(authored.topic) + " G7(" + num(authored.topic) + ")[INFO" + num(authored.first) +
                               " INFO" + num(authored.second) + " ] SCEN" + num(k_scene) + " SCEN" +
                               num(k_template) + " DIAL" + num(k_bare) + " ]";
  EXPECT_EQ(render(*children), expected);
  EXPECT_EQ(payloads(reloaded, authored.branch, "SNAM"), std::vector<Bytes>{words({authored.topic})});
  EXPECT_EQ(payloads(reloaded, authored.topic, "TIFC"), std::vector<Bytes>{words({2})});

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.topic), &record), RADICLE_STATUS_OK);
  radicle_dialogue_topic_info topic_info{};
  ASSERT_EQ(radicle_dialogue_topic_get_info(env, record, &topic_info), RADICLE_STATUS_OK);
  EXPECT_EQ(topic_info.branch.local_id, authored.branch);
  EXPECT_EQ(topic_info.has_info_count, 1);
  EXPECT_EQ(topic_info.info_count, 2u);

  const radicle_dialogue_info* first = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.first), &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, record, &first), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(first->flags, 0x0200);
  EXPECT_EQ(first->reset_time_encoded, 24);
  EXPECT_EQ(first->previous_info.plugin, nullptr);
  ASSERT_EQ(first->condition_count, 1u);
  EXPECT_EQ(first->conditions[0].flags_byte, 0x20);
  EXPECT_EQ(first->conditions[0].function, 14);
  EXPECT_EQ(first->conditions[0].param1, 7);
  ASSERT_EQ(first->response_count, 1u);
  EXPECT_STREQ(first->responses[0].text.text, "Hello, friend");
  EXPECT_EQ(first->responses[0].number, 1);
  radicle_dialogue_info_free(first);

  const radicle_dialogue_info* second = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.second), &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, record, &second), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(second->previous_info.local_id, authored.first);
  EXPECT_EQ(second->condition_count, 0u);
  ASSERT_EQ(second->response_count, 1u);
  EXPECT_STREQ(second->responses[0].text.text, "Caf\xC3\xA9 second");
  EXPECT_EQ(second->responses[0].emotion.local_id, k_kywd);
  EXPECT_EQ(second->responses[0].sound.local_id, k_sndr);
  EXPECT_EQ(second->responses[0].interrupt_percent, 20);
  radicle_dialogue_info_free(second);
  radicle_env_destroy(env);
}

TEST_F(DialogueWriterCApi, RemovingInfosRelinksTheChainAndDropsTheEmptyTopicGroup) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto authored = author_topic_with_two_infos(plugin);
  const auto saved = (root.path / "out" / k_plugin).string();

  EXPECT_EQ(radicle_plugin_remove_dialogue_info(plugin, rkey(authored.first)), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.topic), &record), RADICLE_STATUS_OK);
  radicle_dialogue_topic_info topic_info{};
  ASSERT_EQ(radicle_dialogue_topic_get_info(env, record, &topic_info), RADICLE_STATUS_OK);
  EXPECT_EQ(topic_info.info_count, 1u);
  const radicle_dialogue_info* second = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.second), &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, record, &second), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(second->previous_info.plugin, nullptr);
  radicle_dialogue_info_free(second);
  radicle_env_destroy(env);

  EXPECT_EQ(radicle_plugin_remove_dialogue_info(plugin, rkey(authored.second)), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(plugin);

  auto reloaded = fo4::read_plugin_file(saved);
  fo4::assign_record_form_keys(reloaded);
  const auto* children = quest_children(reloaded, k_quest);
  ASSERT_NE(children, nullptr);
  const std::string expected = "G10(" + num(k_quest) + ")[DLBR" + num(authored.branch) + " DIAL" + num(k_topic) +
                               " G7(" + num(k_topic) + ")[INFO" + num(k_info) + " INFO" + num(k_group) + " ] DIAL" +
                               num(authored.topic) + " SCEN" + num(k_scene) + " SCEN" + num(k_template) + " DIAL" +
                               num(k_bare) + " ]";
  EXPECT_EQ(render(*children), expected);
  EXPECT_EQ(payloads(reloaded, authored.topic, "TIFC"), std::vector<Bytes>{words({0})});
}

TEST_F(DialogueWriterCApi, UpdatedInfoSavesWithNewResponsesNoConditionsAndKeepsTheChain) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto authored = author_topic_with_two_infos(plugin);
  radicle_dialogue_response_spec response{};
  response.text = "Updated line";
  response.number = 3;
  response.camera_target_alias = -1;
  response.camera_location_alias = -1;
  radicle_dialogue_info_spec spec{};
  spec.flags = 0x0100;
  spec.reset_time_encoded = 12;
  spec.responses = &response;
  spec.response_count = 1;
  EXPECT_EQ(radicle_plugin_update_dialogue_info(plugin, rkey(authored.first), &spec), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto saved = (root.path / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  const radicle_dialogue_info* first = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.first), &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, record, &first), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(first->flags, 0x0100);
  EXPECT_EQ(first->reset_time_encoded, 12);
  EXPECT_EQ(first->condition_count, 0u);
  ASSERT_EQ(first->response_count, 1u);
  EXPECT_STREQ(first->responses[0].text.text, "Updated line");
  EXPECT_EQ(first->responses[0].number, 3);
  radicle_dialogue_info_free(first);

  const radicle_dialogue_info* second = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(authored.second), &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_dialogue_info_decode(env, record, &second), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(second->previous_info.local_id, authored.first);
  radicle_dialogue_info_free(second);
  radicle_env_destroy(env);
}

// Regression guard: radicle_plugin_create_scene must map each status to its C code and append a SCEN that survives a save and reload.
TEST_F(DialogueWriterCApi, CreateSceneMapsStatusesAndAppendsASceneThatSurvivesReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto before = bytes_of(plugin);
  radicle_scene_spec spec{};
  spec.editor_id = "Made scene";
  spec.flags = 0x24;
  spec.index = 7;
  radicle_form_key out = rkey(0x9999);

  EXPECT_EQ(radicle_plugin_create_scene(nullptr, rkey(k_quest), &spec, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), nullptr, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), &spec, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_topic), &spec, &out), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(out.plugin, nullptr);
  EXPECT_FALSE(std::string(radicle_last_error()).empty());
  EXPECT_EQ(radicle_plugin_create_scene(plugin, radicle_form_key{"Other.esm", k_quest}, &spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out.plugin, nullptr);
  EXPECT_EQ(bytes_of(plugin), before);

  ASSERT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), &spec, &out), RADICLE_STATUS_OK);
  EXPECT_STREQ(out.plugin, k_plugin);
  const auto raw_bytes = bytes_of(plugin);
  std::vector<std::byte> plugin_bytes(raw_bytes.size());
  for (std::size_t index = 0; index < raw_bytes.size(); ++index) {
    plugin_bytes[index] = static_cast<std::byte>(raw_bytes[index]);
  }
  const auto saved = fo4::test::dialogue::read(plugin_bytes);
  EXPECT_EQ(order(saved, out.local_id), (Order{"EDID", "FNAM", "PNAM", "INAM", "VNAM", "XNAM"}));
  EXPECT_EQ(payloads(saved, out.local_id, "FNAM"), std::vector<Bytes>{words({0x24})});
  EXPECT_EQ(payloads(saved, out.local_id, "XNAM"), std::vector<Bytes>{words({7})});
  radicle_plugin_destroy(plugin);
}

TEST_F(DialogueWriterCApi, AuthoringRefusalsCarryAStatusAndMessageAndLeaveThePluginAlone) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto before = bytes_of(plugin);
  radicle_dialogue_branch_spec branch_spec{};
  branch_spec.editor_id = "Branch";
  radicle_dialogue_topic_spec topic_spec{};
  topic_spec.editor_id = "Topic";
  radicle_dialogue_info_spec info_spec{};
  radicle_form_key out = rkey(0x9999);

  EXPECT_EQ(radicle_plugin_create_dialogue_branch(nullptr, rkey(k_quest), &branch_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_dialogue_branch(plugin, rkey(k_quest), nullptr, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_dialogue_branch(plugin, rkey(k_quest), &branch_spec, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, rkey(k_quest), nullptr, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_dialogue_info(plugin, rkey(k_topic), nullptr, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_dialogue_branch_start(nullptr, rkey(k_dlbr), rkey(k_topic)),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_dialogue_info(nullptr, rkey(k_info)), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_update_dialogue_info(nullptr, rkey(k_info), &info_spec), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_update_dialogue_info(plugin, rkey(k_info), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_create_dialogue_branch(plugin, rkey(k_topic), &branch_spec, &out),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(out.plugin, nullptr);
  EXPECT_FALSE(std::string(radicle_last_error()).empty());
  EXPECT_EQ(radicle_plugin_set_dialogue_branch_start(plugin, rkey(k_topic), rkey(k_topic)), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_dialogue_info(plugin, rkey(k_topic)), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, radicle_form_key{"Other.esm", k_quest}, &topic_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out.plugin, nullptr);
  info_spec.response_count = 1;
  EXPECT_EQ(radicle_plugin_create_dialogue_info(plugin, rkey(k_topic), &info_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);

  branch_spec.editor_id = "\xFF";
  EXPECT_EQ(radicle_plugin_create_dialogue_branch(plugin, rkey(k_quest), &branch_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  topic_spec.name = "\xE4\xB8\x80";
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, rkey(k_quest), &topic_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  topic_spec.name = nullptr;
  topic_spec.subtype_name = "ABC";
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, rkey(k_quest), &topic_spec, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  topic_spec.subtype_name = nullptr;
  topic_spec.has_branch = 1;
  topic_spec.branch = rkey(k_topic);
  EXPECT_EQ(radicle_plugin_create_dialogue_topic(plugin, rkey(k_quest), &topic_spec, &out),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes_of(plugin), before);
  radicle_plugin_destroy(plugin);
}
