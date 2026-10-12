#include "behavior_writer_fixture.hpp"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_behavior.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
using namespace behavior_writer_test;

class BehaviorWriterAbi : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_behavior_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    input = root / "Behavior.esp";
    const auto bytes = fixture();
    std::ofstream stream(input, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  std::filesystem::path root, input;
};

TEST_F(BehaviorWriterAbi, WritersSaveReparseAndExposeSignedOptionalValues) {
  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &raw), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)> plugin(raw, radicle_plugin_destroy);
  const radicle_form_key pack{"Behavior.esp",0x800}, style{"Behavior.esp",0x801}, idle{"Behavior.esp",0x802};
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, pack, "PACK\\DurationMinutes", 0, "4294967295"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, pack, "PACK\\Month", 0, "-1"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, pack, "PACK\\LocationRadius", 0, "-2147483648"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(raw, pack, "PACK\\LocationRadius", 2, "1"), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, style, "CSTY\\ThrowMaxTargets", 0, "4294967295"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, style, "CSTY\\OffensiveMult", 0, "0.75"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(raw, idle, "IDLE\\AnimationEvent", 0, "edited"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(raw, idle, "IDLE\\ReplayDelay", 0, "65535"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_formlink(raw, pack, "PACK\\LocationObject", 1, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(raw, pack, "PACK\\LocationKeyword", 1, nullptr), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(raw, pack, "PACK\\LocationKeyword", 0, &idle), RADICLE_STATUS_INVALID_ARGUMENT);
  std::size_t count = 0;
  ASSERT_EQ(radicle_plugin_record_field_count(raw, pack, "PACK\\CombatStyle", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  radicle_form_key made{};
  EXPECT_EQ(radicle_plugin_create_record(raw, "PACK", "Refused", &made), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_create_record(raw, "CSTY", "NewStyle", &made), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_create_record(raw, "IDLE", "NewIdle", &made), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_add_subrecord(raw, made, "DNAM"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_remove_subrecord(raw, made, "DNAM", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(raw, made, "DATA", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(raw, pack, "PKCU", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto output = (root / "out" / "Behavior.esp").string();
  ASSERT_EQ(radicle_plugin_write_file(raw, output.c_str()), RADICLE_STATUS_OK);
  const char* paths[]{output.c_str()}; radicle_env* raw_env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_env), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(raw_env, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(raw_env, pack, &record), RADICLE_STATUS_OK);
  const radicle_package_info* raw_package = nullptr;
  ASSERT_EQ(radicle_package_decode(raw_env, record, &raw_package), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_package_info, decltype(&radicle_package_free)> package(raw_package, radicle_package_free);
  EXPECT_EQ(package->duration_minutes, 0xFFFFFFFFu); EXPECT_EQ(package->month, -1);
  ASSERT_EQ(package->location_count, 4u);
  EXPECT_EQ(package->locations[0].radius, INT32_MIN); EXPECT_EQ(package->locations[0].has_radius, 1);
  EXPECT_EQ(package->locations[1].form.plugin, nullptr); EXPECT_EQ(package->locations[1].type, 4);
  EXPECT_EQ(package->locations[2].has_radius, 0); EXPECT_EQ(package->locations[2].form.local_id, 0x804u);
  ASSERT_EQ(radicle_env_winner(raw_env, style, &record), RADICLE_STATUS_OK);
  radicle_combat_style_info combat{};
  ASSERT_EQ(radicle_combat_style_get_info(raw_env, record, &combat), RADICLE_STATUS_OK);
  EXPECT_EQ(combat.general[0], .75f); EXPECT_EQ(combat.throw_max_targets, 0xFFFFFFFFu);
  ASSERT_EQ(radicle_env_winner(raw_env, idle, &record), RADICLE_STATUS_OK);
  const radicle_idle_info* raw_idle = nullptr;
  ASSERT_EQ(radicle_idle_decode(raw_env, record, &raw_idle), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_idle_info, decltype(&radicle_idle_free)> decoded(raw_idle, radicle_idle_free);
  EXPECT_STREQ(decoded->animation_event, "edited"); EXPECT_EQ(decoded->replay_delay, 65535);
}

TEST_F(BehaviorWriterAbi, RecordFromAnotherEnvironmentIsRejectedByBehaviorDecoders) {
  const auto path = input.string();
  const char* paths[]{path.c_str()};
  radicle_env* raw_a = nullptr; radicle_env* raw_b = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_a), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env_a(raw_a, radicle_env_destroy);
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_b), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env_b(raw_b, radicle_env_destroy);
  const radicle_form_key pack_key{"Behavior.esp",0x800}, style_key{"Behavior.esp",0x801}, idle_key{"Behavior.esp",0x802};
  const radicle_record *pack = nullptr, *style = nullptr, *idle = nullptr;
  ASSERT_EQ(radicle_env_winner(raw_a, pack_key, &pack), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(raw_a, style_key, &style), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(raw_a, idle_key, &idle), RADICLE_STATUS_OK);
  radicle_package_info package_sentinel{};
  const radicle_package_info* package_out = &package_sentinel;
  EXPECT_EQ(radicle_package_decode(raw_b, pack, &package_out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(package_out, &package_sentinel);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  radicle_combat_style_info combat{};
  combat.throw_max_targets = 7u;
  EXPECT_EQ(radicle_combat_style_get_info(raw_b, style, &combat), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(combat.throw_max_targets, 7u);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  radicle_idle_info idle_sentinel{};
  const radicle_idle_info* idle_out = &idle_sentinel;
  EXPECT_EQ(radicle_idle_decode(raw_b, idle, &idle_out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(idle_out, &idle_sentinel);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  ASSERT_EQ(radicle_package_decode(raw_a, pack, &package_out), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_package_info, decltype(&radicle_package_free)> owned(package_out, radicle_package_free);
  ASSERT_EQ(radicle_combat_style_get_info(raw_a, style, &combat), RADICLE_STATUS_OK);
  EXPECT_NE(combat.throw_max_targets, 7u);
}

}
