#include "dialogue_writer_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_dialogue.h"
#include "fo4/c_api_env.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

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

}

TEST_F(DialogueWriterCApi, FieldSceneAndAuthoringStatusesMapAndSurviveReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key topic = rkey(k_topic), info = rkey(k_info), scene = rkey(k_scene), bare = rkey(k_bare);
  const radicle_form_key sndr = rkey(k_sndr), misc = rkey(k_misc), quest = rkey(k_quest), templ = rkey(k_template);
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
