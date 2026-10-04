#include "fo4/c_api_sound.h"
#include "fo4/environment/sound.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace {

class SoundRealData : public testing::TestWithParam<const char*> {};

TEST_P(SoundRealData, DecodesEverySoundRecordThroughNativeAndAbiAndPreservesBytes) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  const auto name = path.string(); const char* names[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(names, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto native_records = env.plugin_records(0);
  ASSERT_EQ(count, native_records.size());
  std::map<std::string, int> totals;
  for (std::size_t i = 0; i < count; ++i) {
    const auto* ref = native_records[i];
    const auto type = ref->record->header.signature.to_string();
    if (type != "SNDR" && type != "SOUN" && type != "MUSC" && type != "FSTP" && type != "FSTS") continue;
    SCOPED_TRACE(ref->record->form_key.to_string()); ++totals[type];
    if (type == "SNDR") {
      EXPECT_NO_THROW(fo4::env::decode_sound_descriptor(env, *ref));
      const radicle_sound_descriptor* d = nullptr;
      EXPECT_EQ(radicle_sound_descriptor_decode(abi, records[i], &d), RADICLE_STATUS_OK);
      radicle_sound_descriptor_free(d);
    } else if (type == "SOUN") {
      EXPECT_NO_THROW(fo4::env::decode_sound_marker(env, *ref)); radicle_sound_marker_info m{};
      EXPECT_EQ(radicle_sound_marker_get_info(abi, records[i], &m), RADICLE_STATUS_OK);
    } else if (type == "MUSC") {
      EXPECT_NO_THROW(fo4::env::decode_music_type(env, *ref)); const radicle_music_type* m = nullptr;
      EXPECT_EQ(radicle_music_type_decode(abi, records[i], &m), RADICLE_STATUS_OK); radicle_music_type_free(m);
    } else if (type == "FSTP") {
      EXPECT_NO_THROW(fo4::env::decode_footstep(env, *ref)); radicle_footstep_info f{};
      EXPECT_EQ(radicle_footstep_get_info(abi, records[i], &f), RADICLE_STATUS_OK);
    } else {
      EXPECT_NO_THROW(fo4::env::decode_footstep_set(env, *ref)); const radicle_footstep_set* f = nullptr;
      EXPECT_EQ(radicle_footstep_set_decode(abi, records[i], &f), RADICLE_STATUS_OK); radicle_footstep_set_free(f);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, total] : totals) RecordProperty(type, total);
  const auto& plugin = env.plugin(0); const auto bytes = fo4::write_plugin(plugin); const auto original = fo4::plugin_original_bytes(plugin);
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, SoundRealData,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
