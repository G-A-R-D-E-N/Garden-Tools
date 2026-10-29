#include "sound_writer_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_sound.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace fo4::test::sound;

class SoundWriterCApi : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_sound_capi_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "out");
    path = root / k_plugin;
    const auto bytes = build(fixture());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }

  std::filesystem::path root;
  std::filesystem::path path;
};

radicle_form_key rkey(std::uint32_t id) { return radicle_form_key{k_plugin, id}; }

}

TEST_F(SoundWriterCApi, FieldLinkAuthoringAndCreateStatusesMapAndSurviveReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key standard = rkey(k_std), weapon = rkey(k_auto), nokind = rkey(k_nokind), bare = rkey(k_bare);
  const radicle_form_key marker = rkey(k_marker), music = rkey(k_music), step = rkey(k_step), set = rkey(k_set);
  const radicle_form_key misc = rkey(k_misc), must = rkey(k_must), step2 = rkey(k_step2), compound = rkey(k_compound);
  const radicle_form_key unknown_master{"Other.esm", 1};

  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, standard, "SNDR\\FrequencyShift", 0, "-5"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, standard, "SNDR\\FrequencyShift", 0, "200"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weapon, "SNDR\\FrequencyShift", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, nokind, "SNDR\\StaticAttenuation", 0, "250"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, marker, "SNDR\\Priority", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, standard, "SNDR\\DescriptorType", 0, "519001098"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, rkey(0x999), "SNDR\\Priority", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weapon, "SNDR\\RateOfFireRpm", 1, "1000"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weapon, "SNDR\\RateOfFireRpm", 2, "1000"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, weapon, "SNDR\\RateOfFireFile", 1, "sound\\b\xC3\xA4.xwm"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, standard, "SNDR\\Sounds", 0, "sound\\\xE4"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, marker, "SOUN\\RepeatStackable", 0, "2"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, rkey(k_marker_old), "SOUN\\RepeatStackable", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, music, "MUSC\\Priority", 0, "75"), RADICLE_STATUS_OK);

  size_t count = 0;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, set, "FSTS\\Footsteps", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 5u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, music, "MUSC\\Tracks", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, standard, "SNDR\\Sounds", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weapon, "SNDR\\BaseDescriptor", 0, &compound), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, standard, "SNDR\\BaseDescriptor", 0, &compound), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weapon, "SNDR\\BaseDescriptor", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weapon, "SNDR\\BaseDescriptor", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, standard, "SNDR\\Category", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 0, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 1, &must), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 2, &must), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, set, "FSTS\\Footsteps", 0, &step2), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, set, "FSTS\\Footsteps", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, step, "FSTP\\ImpactDataSet", 0, nullptr), RADICLE_STATUS_OK);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "LNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "LNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "CNAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "GNAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, weapon, "NNAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(0x999), "NNAM"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, bare, "SNDR\\Looping", 0, "32"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, standard, "BNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, set, "DATA", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, marker, "SDSC", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, standard, "ANAM", 1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, standard, "ANAM", 4), RADICLE_STATUS_NOT_FOUND);

  radicle_form_key created{}, refused_key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin, "SOUN", "CMarker", &refused_key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin, "FSTS", "CSet", &refused_key), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_create_record(plugin, "MUSC", "CMusic", &created), RADICLE_STATUS_OK);
  const auto created_id = created.local_id;
  ASSERT_EQ(radicle_plugin_add_subrecord(plugin, created, "TNAM"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_formlink(plugin, created, "MUSC\\Tracks", 0, &must), RADICLE_STATUS_OK);

  const auto saved = (root / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  const radicle_sound_descriptor* descriptor = nullptr;
  ASSERT_EQ(radicle_env_winner(env, standard, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_sound_descriptor_decode(env, handle, &descriptor), RADICLE_STATUS_OK);
  EXPECT_EQ(descriptor->has_kind, 1);
  EXPECT_EQ(descriptor->playback.frequency_shift, -5);
  ASSERT_EQ(descriptor->file_count, 1u);
  EXPECT_STREQ(descriptor->files[0].path, "a.wav");
  radicle_sound_descriptor_free(descriptor);
  ASSERT_EQ(radicle_env_winner(env, weapon, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_sound_descriptor_decode(env, handle, &descriptor), RADICLE_STATUS_OK);
  EXPECT_EQ(descriptor->base_descriptor.local_id, k_compound);
  ASSERT_EQ(descriptor->rate_file_count, 2u);
  EXPECT_EQ(descriptor->rate_files[1].rpm, 1000u);
  EXPECT_STREQ(descriptor->rate_files[1].path, "sound\\b\xC3\xA4.xwm");
  radicle_sound_descriptor_free(descriptor);
  ASSERT_EQ(radicle_env_winner(env, nokind, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_sound_descriptor_decode(env, handle, &descriptor), RADICLE_STATUS_OK);
  EXPECT_EQ(descriptor->has_kind, 0);
  EXPECT_EQ(descriptor->playback.attenuation_hundredths, 250);
  radicle_sound_descriptor_free(descriptor);
  ASSERT_EQ(radicle_env_winner(env, bare, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_sound_descriptor_decode(env, handle, &descriptor), RADICLE_STATUS_OK);
  EXPECT_EQ(descriptor->has_controls, 1);
  EXPECT_EQ(descriptor->controls[1], 32);
  radicle_sound_descriptor_free(descriptor);

  const radicle_music_type* decoded_music = nullptr;
  ASSERT_EQ(radicle_env_winner(env, music, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_music_type_decode(env, handle, &decoded_music), RADICLE_STATUS_OK);
  EXPECT_EQ(decoded_music->priority, 75);
  ASSERT_EQ(decoded_music->track_count, 2u);
  EXPECT_EQ(decoded_music->tracks[0].plugin, nullptr);
  EXPECT_EQ(decoded_music->tracks[1].local_id, k_must);
  radicle_music_type_free(decoded_music);
  ASSERT_EQ(radicle_env_winner(env, rkey(created_id), &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_music_type_decode(env, handle, &decoded_music), RADICLE_STATUS_OK);
  EXPECT_EQ(decoded_music->priority, 50);
  ASSERT_EQ(decoded_music->track_count, 1u);
  EXPECT_EQ(decoded_music->tracks[0].local_id, k_must);
  radicle_music_type_free(decoded_music);

  const radicle_footstep_set* decoded_set = nullptr;
  ASSERT_EQ(radicle_env_winner(env, set, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_footstep_set_decode(env, handle, &decoded_set), RADICLE_STATUS_OK);
  ASSERT_EQ(decoded_set->step_count, 5u);
  EXPECT_EQ(decoded_set->counts[0], 1u);
  radicle_footstep_set_free(decoded_set);
  radicle_footstep_info info{};
  ASSERT_EQ(radicle_env_winner(env, step, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_footstep_get_info(env, handle, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.impact_set.plugin, nullptr);
  radicle_env_destroy(env);
}

TEST_F(SoundWriterCApi, MalformedRateGroupsAndBaseLinksMapToInvalidArgumentAndPreserveBytes) {
  auto entries = fixture();
  entries[2].subs[2].second.resize(6);
  entries[2].subs.insert(entries[2].subs.begin() + 4, {"FNAM", zstr("stray.wav")});
  const auto bytes = build(entries);
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const auto weapon = rkey(k_auto), standard = rkey(k_std), compound = rkey(k_compound);
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, weapon, "SNDR\\RateOfFireRpm", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, weapon, "SNDR\\RateOfFireFile", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weapon, "SNDR\\RateOfFireRpm", 0, "1200"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, weapon, "SNDR\\RateOfFireFile", 0, "new.wav"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weapon, "SNDR\\BaseDescriptor", 0, &compound), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, standard, "SNDR\\BaseDescriptor", 0, &compound), RADICLE_STATUS_NOT_FOUND);
  const auto saved = root / "out" / k_plugin;
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);
  EXPECT_EQ(fo4::write_plugin(fo4::read_plugin_file(saved)), bytes);
}
