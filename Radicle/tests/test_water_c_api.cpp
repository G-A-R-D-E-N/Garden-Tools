#include "water_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::water;

radicle_form_key rkey(std::uint32_t id) { return radicle_form_key{k_plugin, id}; }

void write_file(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}

TEST(WaterCApi, FieldLinkAuthoringAndCreateStatusesMapAndSurviveReload) {
  const TempDir dir;
  std::filesystem::create_directories(dir.path() / "out");
  const auto source = dir.path() / k_plugin;
  write_file(source, build(fixture()));
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key full = rkey(k_full), old = rkey(k_short), bare = rkey(k_bare);
  const radicle_form_key sndr = rkey(k_sndr), spel = rkey(k_spel), imgs = rkey(k_imgs), misc = rkey(k_misc);
  const radicle_form_key unknown_master{"Other.esm", 1};

  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, full, "WATR\\DepthAmount", "2048"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, full, "WATR\\DeepColorBlue", 0, "77"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, full, "WATR\\DeepColorBlue", 1, "77"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, full, "WATR\\DeepColorBlue", 0, "256"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, old, "WATR\\SiltAmount", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, old, "WATR\\ScreenSpaceReflections", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, full, "WATR\\ScreenSpaceReflections", "2"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, full, "WATR\\NoSuchField", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, rkey(k_misc), "WATR\\Flags", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, rkey(0x999), "WATR\\Flags", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, full, "WATR\\Flags", "4"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, full, "WATR\\AngularVelocityY", "9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, full, "WATR\\Layer2NoiseTexture", 0, "water\\b\xC3\xA4.dds"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, full, "WATR\\Name", 0, "\xE4"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, bare, "WATR\\Name", 0, "Pond"), RADICLE_STATUS_NOT_FOUND);

  size_t count = 9;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, full, "WATR\\ImageSpace", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, bare, "WATR\\OpenSound", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, full, "WATR\\OpenSound", 0, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, full, "WATR\\ImageSpace", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, full, "WATR\\ImageSpace", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, full, "WATR\\ConsumeSpell", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, old, "WATR\\ContactSpell", 0, &spel), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, old, "WATR\\ImageSpace", 0, &imgs), RADICLE_STATUS_OK);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "SNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "SNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, bare, "WATR\\OpenSound", 0, &sndr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "NAM0"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "INAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "GNAM"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(0x999), "NAM0"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, bare, "DNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, bare, "FNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, full, "GNAM", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, full, "GNAM", 0), RADICLE_STATUS_NOT_FOUND);

  radicle_form_key created{};
  ASSERT_EQ(radicle_plugin_create_record(plugin, "WATR", "CWater", &created), RADICLE_STATUS_OK);
  const auto created_id = created.local_id;
  EXPECT_EQ(created_id, 0x900u);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, created, "WATR\\Layer1UVScale", "64"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_add_subrecord(plugin, created, "NAM2"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin, created, "WATR\\Layer1NoiseTexture", 0, "water\\c.dds"), RADICLE_STATUS_OK);

  const auto saved = (dir.path() / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  radicle_water_info water{};
  ASSERT_EQ(radicle_env_winner(env, full, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_water_get_info(env, handle, &water), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(water.fog_depth_amount, 2048.0f);
  EXPECT_EQ(water.fog_deep_color.blue, 77);
  EXPECT_EQ(water.flags, 4);
  EXPECT_FLOAT_EQ(water.angular_velocity[1], 9.0f);
  EXPECT_STREQ(water.noise_textures[1], "water\\b\xC3\xA4.dds");
  ASSERT_EQ(radicle_env_winner(env, rkey(created_id), &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_water_get_info(env, handle, &water), RADICLE_STATUS_OK);
  EXPECT_EQ(water.has_visual, 1);
  EXPECT_FLOAT_EQ(water.fog_depth_amount, 8192.0f);
  EXPECT_FLOAT_EQ(water.noise_uv_scale[0], 64.0f);
  EXPECT_FLOAT_EQ(water.noise_uv_scale[1], 100.0f);
  EXPECT_EQ(water.screen_space_reflections, 1);
  EXPECT_STREQ(water.noise_textures[0], "water\\c.dds");
  ASSERT_EQ(radicle_env_winner(env, bare, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_water_get_info(env, handle, &water), RADICLE_STATUS_OK);
  EXPECT_EQ(water.has_linear_velocity, 1);
  radicle_env_destroy(env);

  std::ifstream file(saved, std::ios::binary);
  const std::vector<char> chars{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  Bytes bytes(chars.size());
  std::memcpy(bytes.data(), chars.data(), chars.size());
  const auto reread = read(bytes);
  EXPECT_EQ(payload(reread, k_bare, "SNAM"), words({k_sndr}));
  EXPECT_EQ(payload(reread, k_full, "SNAM"), words({0}));
  EXPECT_EQ(order(reread, k_full), (Order{"EDID", "FULL", "ANAM", "FNAM", "TNAM", "SNAM", "XNAM", "YNAM", "INAM", "DATA", "DNAM",
                                          "NAM0", "NAM1", "NAM2", "NAM3", "NAM4"}));
  for (const auto id : {k_full, k_short, k_bare, created_id}) { EXPECT_TRUE(authoring_clean(reread, key(id))) << id; }
}

TEST(WaterCApi, ResolveRejectsARecordFromAnotherEnvironment) {
  const TempDir dir;
  const auto source = (dir.path() / k_plugin).string();
  write_file(source, build(fixture()));
  const char* paths[] = {source.c_str()};
  radicle_env* env_a = nullptr;
  radicle_env* env_b = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env_a), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(paths, 1, &env_b), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(env_a, rkey(k_full), &handle), RADICLE_STATUS_OK);
  radicle_water_selection selection{};
  selection.uses_default_water = 7;
  EXPECT_EQ(radicle_water_resolve(env_b, handle, &selection), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(selection.uses_default_water, 7);
  radicle_env_destroy(env_b);
  radicle_env_destroy(env_a);
}
