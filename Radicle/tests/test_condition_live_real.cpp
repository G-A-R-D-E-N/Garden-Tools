#include "fo4/c_api.h"
#include "fo4/c_api_condition_live.h"
#include "fo4/c_api_conditions.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_ui_records.h"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace {

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

EnvPtr load_fallout4(const char* dir) {
  const auto path = (std::filesystem::path{dir} / "Fallout4.esm").string();
  const char* paths[] = {path.c_str()};
  radicle_env* env = nullptr;
  EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
  return EnvPtr(env, radicle_env_destroy);
}

radicle_lock_kind named_band(std::uint8_t level) {
  struct Band {
    std::uint8_t from;
    radicle_lock_kind kind;
  };
  static constexpr Band bands[] = {{0, RADICLE_LOCK_NONE},      {1, RADICLE_LOCK_NOVICE},
                                   {50, RADICLE_LOCK_ADVANCED}, {75, RADICLE_LOCK_EXPERT},
                                   {100, RADICLE_LOCK_MASTER},  {251, RADICLE_LOCK_BARRED},
                                   {252, RADICLE_LOCK_CHAINED}, {253, RADICLE_LOCK_REQUIRES_TERMINAL},
                                   {254, RADICLE_LOCK_INACCESSIBLE}, {255, RADICLE_LOCK_REQUIRES_KEY}};
  radicle_lock_kind kind = RADICLE_LOCK_NONE;
  for (const auto& band : bands) {
    if (level >= band.from) kind = band.kind;
  }
  return kind;
}

struct Recorder {
  bool answer_all = false;
  std::array<std::size_t, 16> per_fact{};
  std::size_t form_missing = 0;

  static std::int32_t thunk(void* user, const radicle_condition_query* query, double* out) {
    auto& self = *static_cast<Recorder*>(user);
    if (query->fact >= 1 && query->fact < 16) ++self.per_fact[static_cast<std::size_t>(query->fact)];
    const bool needs_form = query->fact != RADICLE_CONDITION_FACT_CURRENT_TIME &&
                            query->fact != RADICLE_CONDITION_FACT_IS_DEAD && query->fact != RADICLE_CONDITION_FACT_LEVEL;
    if (needs_form && (query->form.plugin == nullptr || *query->form.plugin == '\0')) ++self.form_missing;
    *out = 1.0;
    return self.answer_all ? 1 : 0;
  }

  radicle_condition_live_state state() { return {sizeof(radicle_condition_live_state), this, &Recorder::thunk}; }
};

}  // namespace

TEST(ConditionLiveReal, AHostThatKnowsNothingNeverMakesAVanillaConditionListTrueOrFalse) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to installed Fallout 4 data");
  const auto env = load_fallout4(dir);
  ASSERT_NE(env, nullptr);
  Recorder host;
  const auto live = host.state();
  std::size_t lists = 0, rows_seen = 0;
  for (const char* signature : {"INFO", "PACK"}) {
    const radicle_record** records = nullptr;
    size_t count = 0;
    ASSERT_EQ(radicle_env_winners_of(env.get(), signature, &records, &count), RADICLE_STATUS_OK);
    for (size_t i = 0; i < count; ++i) {
      const radicle_condition_list* list = nullptr;
      if (radicle_record_conditions(env.get(), records[i], nullptr, &list) != RADICLE_STATUS_OK) continue;
      radicle_condition_result result{};
      ASSERT_EQ(radicle_conditions_evaluate_live(env.get(), records[i], list->rows, list->count, &live, &result),
                RADICLE_STATUS_OK);
      if (list->count == 0) {
        EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_TRUE);
      } else {
        ++lists;
        rows_seen += list->count;
        EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
      }
      radicle_condition_list_free(list);
    }
    radicle_free_record_array(records);
  }
  RecordProperty("LISTS_WITH_ROWS", static_cast<int>(lists));
  RecordProperty("ROWS", static_cast<int>(rows_seen));
  EXPECT_GT(lists, 0u);
  EXPECT_EQ(host.form_missing, 0u);
}

TEST(ConditionLiveReal, VanillaDialogueRowsReachTheHostWithTypedFormKeys) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to installed Fallout 4 data");
  const auto env = load_fallout4(dir);
  ASSERT_NE(env, nullptr);
  Recorder host;
  host.answer_all = true;
  const auto live = host.state();
  const radicle_record** records = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_winners_of(env.get(), "INFO", &records, &count), RADICLE_STATUS_OK);
  for (size_t i = 0; i < count; ++i) {
    const radicle_condition_list* list = nullptr;
    if (radicle_record_conditions(env.get(), records[i], nullptr, &list) != RADICLE_STATUS_OK) continue;
    radicle_condition_result result{};
    EXPECT_EQ(radicle_conditions_evaluate_live(env.get(), records[i], list->rows, list->count, &live, &result),
              RADICLE_STATUS_OK);
    radicle_condition_list_free(list);
  }
  radicle_free_record_array(records);
  EXPECT_GT(host.per_fact[RADICLE_CONDITION_FACT_QUEST_STAGE_DONE], 0u);
  EXPECT_GT(host.per_fact[RADICLE_CONDITION_FACT_IS_ID], 0u);
  EXPECT_EQ(host.form_missing, 0u);
}

TEST(ConditionLiveReal, PlacedReferenceLockDataMatchesTheNamedLevelsAndTheLeveledFlag) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to installed Fallout 4 data");
  const auto env = load_fallout4(dir);
  ASSERT_NE(env, nullptr);
  const radicle_record** records = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_winners_of(env.get(), "REFR", &records, &count), RADICLE_STATUS_OK);
  std::size_t locked = 0, requires_key = 0, undecodable = 0;
  for (size_t i = 0; i < count; ++i) {
    radicle_lock_info info{};
    if (radicle_lock_info_read(env.get(), records[i], &info) != RADICLE_STATUS_OK) {
      ++undecodable;
      continue;
    }
    if (info.has_lock_data == 0) continue;
    ++locked;
    EXPECT_EQ(info.kind, named_band(info.level)) << "level " << static_cast<int>(info.level);
    EXPECT_EQ(info.leveled_lock, (info.flags & 0x04) != 0 ? 1 : 0);
    if (info.level == 255) {
      ++requires_key;
      EXPECT_EQ(info.kind, RADICLE_LOCK_REQUIRES_KEY);
    }
  }
  radicle_free_record_array(records);
  RecordProperty("LOCKED_REFERENCES", static_cast<int>(locked));
  RecordProperty("UNDECODABLE_REFERENCES", static_cast<int>(undecodable));
  EXPECT_EQ(undecodable, 0u);
  EXPECT_GT(locked, 0u);
  EXPECT_GT(requires_key, 0u);
}

TEST(ConditionLiveReal, EveryVanillaGlobIsHostFirstAndOnlyAConstantFallsBackToItsRecord) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to installed Fallout 4 data");
  const auto env = load_fallout4(dir);
  ASSERT_NE(env, nullptr);
  Recorder silent;
  Recorder answering;
  answering.answer_all = true;
  const auto silent_state = silent.state();
  const auto answering_state = answering.state();
  const radicle_record** records = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_winners_of(env.get(), "GLOB", &records, &count), RADICLE_STATUS_OK);
  std::size_t constants = 0, unset = 0;
  for (size_t i = 0; i < count; ++i) {
    double value = -1.0;
    radicle_glob_live_reason reason = RADICLE_GLOB_LIVE_NOT_GLOB;
    ASSERT_EQ(radicle_glob_value_live(env.get(), records[i], &answering_state, &value, &reason), RADICLE_STATUS_OK);
    EXPECT_EQ(reason, RADICLE_GLOB_LIVE_HOST);
    EXPECT_EQ(value, 1.0);
    ASSERT_EQ(radicle_glob_value_live(env.get(), records[i], &silent_state, &value, &reason), RADICLE_STATUS_OK);
    if (reason == RADICLE_GLOB_LIVE_RECORD_CONSTANT) {
      ++constants;
    } else {
      EXPECT_EQ(reason, RADICLE_GLOB_LIVE_NO_VALUE);
      EXPECT_EQ(value, 0.0);
      ++unset;
    }
  }
  radicle_free_record_array(records);
  RecordProperty("CONSTANT_GLOBS", static_cast<int>(constants));
  RecordProperty("HOST_ONLY_GLOBS", static_cast<int>(unset));
  EXPECT_GT(count, 0u);
  EXPECT_GT(unset, 0u);
}
