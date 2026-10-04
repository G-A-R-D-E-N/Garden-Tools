#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

class NavmeshReal : public testing::TestWithParam<const char*> {};

TEST_P(NavmeshReal, EveryOfficialNavmeshDecodesAndUnchangedMasterIsIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0); ASSERT_EQ(count, refs.size());
  std::size_t navm = 0, navi = 0, infos = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature;
    if (type == fo4::Signature::from_string("NAVM")) {
      SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++navm;
      EXPECT_NO_THROW(fo4::env::decode_navmesh(env, *refs[i])); const radicle_navmesh* value = nullptr;
      EXPECT_EQ(radicle_navmesh_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_navmesh_free(value);
    } else if (type == fo4::Signature::from_string("NAVI")) {
      ++navi;
      EXPECT_NO_THROW(infos += fo4::env::decode_navmesh_info(env, *refs[i]).size()); const radicle_navi* value = nullptr;
      EXPECT_EQ(radicle_navi_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_navi_free(value);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  RecordProperty("NAVM", static_cast<int>(navm)); RecordProperty("NAVI", static_cast<int>(navi)); RecordProperty("NVMI", static_cast<int>(infos));
  const auto bytes = fo4::write_plugin(env.plugin(0)); const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, NavmeshReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
