#include "fo4/c_api.h"
#include "fo4/c_api_condition_live.h"
#include "fo4/c_api_conditions.h"
#include "fo4/c_api_env.h"
#include "fo4/schema/condition_functions.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kPlugin = "Live.esp";

constexpr std::uint32_t k_glob_plain = 0x810;
constexpr std::uint32_t k_glob_const = 0x811;
constexpr std::uint32_t k_glob_empty = 0x812;
constexpr std::uint32_t k_quest = 0x820;
constexpr std::uint32_t k_faction = 0x821;
constexpr std::uint32_t k_keyword = 0x822;
constexpr std::uint32_t k_reference = 0x850;

constexpr std::uint32_t k_level = 0x900;
constexpr std::uint32_t k_or = 0x901;
constexpr std::uint32_t k_stage_done = 0x902;
constexpr std::uint32_t k_use_global = 0x903;
constexpr std::uint32_t k_alias = 0x904;
constexpr std::uint32_t k_run_on = 0x905;
constexpr std::uint32_t k_narrow = 0x906;
constexpr std::uint32_t k_rank = 0x907;
constexpr std::uint32_t k_false_beats_unknown = 0x908;
constexpr std::uint32_t k_unmapped = 0x909;
constexpr std::uint32_t k_null_form = 0x90A;
constexpr std::uint32_t k_use_const = 0x90B;
constexpr std::uint32_t k_selector = 0x90C;
constexpr std::uint32_t k_sex = 0x90D;
constexpr std::uint32_t k_item_count = 0x90E;
constexpr std::uint32_t k_alias_ref = 0x90F;
constexpr std::uint32_t k_null_item = 0x910;
constexpr std::uint32_t k_item = 0x823;

constexpr std::uint8_t k_equal = 0x00;
constexpr std::uint8_t k_greater = 0x40;
constexpr std::uint8_t k_greater_equal = 0x60;
constexpr std::uint8_t k_or_flag = 0x01;
constexpr std::uint8_t k_alias_flag = 0x02;
constexpr std::uint8_t k_use_global_flag = 0x04;

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

std::uint32_t float_bits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

fo4::CtdaCondition row(std::string_view function, std::uint8_t flags, float comparison, std::int32_t param1 = 0,
                       std::int32_t param2 = 0) {
  fo4::CtdaCondition c;
  c.function = fo4::schema::condition_function_by_name(function)->index;
  c.flags_byte = flags;
  c.comparison_raw = float_bits(comparison);
  c.param1 = param1;
  c.param2 = param2;
  return c;
}

void write_perk(fo4::test::ByteWriter& out, std::uint32_t id, const std::vector<fo4::CtdaCondition>& rows) {
  const auto perk = fo4::test::begin_record(out, "PERK", 0, id);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Live\0", 5));
  for (const auto& sub : fo4::serialize_ctda_run(rows)) {
    fo4::test::write_subrecord(out, sub.signature.to_string(), sub.data);
  }
  fo4::test::end_record(out, perk);
}

void write_glob(fo4::test::ByteWriter& out, std::uint32_t id, std::uint32_t flags, const float* value) {
  const auto glob = fo4::test::begin_record(out, "GLOB", flags, id);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Glob\0", 5));
  fo4::test::write_subrecord(out, "FNAM", std::string_view("f", 1));
  if (value != nullptr) {
    fo4::test::ByteWriter data;
    data.f32(*value);
    fo4::test::write_subrecord(out, "FLTV", data.bytes);
  }
  fo4::test::end_record(out, glob);
}

std::vector<std::byte> fixture() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  const auto globs = fo4::test::begin_group(out, "GLOB", 0);
  const float three = 3.0f;
  const float half = 0.5f;
  write_glob(out, k_glob_plain, 0, &three);
  write_glob(out, k_glob_const, 0x40, &half);
  write_glob(out, k_glob_empty, 0, nullptr);
  fo4::test::end_group(out, globs);

  const auto perks = fo4::test::begin_group(out, "PERK", 0);
  write_perk(out, k_level, {row("GetLevel", k_greater_equal, 10.0f)});
  write_perk(out, k_or, {row("HasKeyword", k_equal | k_or_flag, 1.0f, static_cast<std::int32_t>(k_keyword)),
                         row("GetInFaction", k_equal, 1.0f, static_cast<std::int32_t>(k_faction))});
  write_perk(out, k_stage_done, {row("GetStageDone", k_equal, 1.0f, static_cast<std::int32_t>(k_quest), 10)});
  auto use_global = row("GetLevel", k_greater | k_use_global_flag, 0.0f);
  use_global.comparison_raw = k_glob_plain;
  write_perk(out, k_use_global, {use_global});
  write_perk(out, k_alias, {row("GetLevel", k_equal | k_alias_flag, 1.0f)});
  auto run_on = row("GetLevel", k_greater_equal, 1.0f);
  run_on.run_on = 2;
  run_on.reference = k_reference;
  write_perk(out, k_run_on, {run_on});
  auto use_const = row("GetLevel", k_greater | k_use_global_flag, 0.0f);
  use_const.comparison_raw = k_glob_const;
  write_perk(out, k_use_const, {use_const});
  auto selector = row("GetLevel", k_greater_equal, 1.0f);
  selector.run_on = 5;
  selector.unknown3 = 1234;
  write_perk(out, k_selector, {selector});
  write_perk(out, k_narrow, {row("GetGlobalValue", k_equal, 0.1f, static_cast<std::int32_t>(k_glob_plain))});
  write_perk(out, k_rank, {row("GetFactionRank", k_equal, -1.0f, static_cast<std::int32_t>(k_faction))});
  write_perk(out, k_false_beats_unknown,
             {row("GetLevel", k_greater_equal, 10.0f), row("GetDead", k_equal, 1.0f)});
  write_perk(out, k_unmapped, {row("GetWithinDistance", k_equal, 1.0f, static_cast<std::int32_t>(k_reference), 0)});
  write_perk(out, k_null_form, {row("GetInFaction", k_equal, 1.0f, 0)});
  write_perk(out, k_sex, {row("GetIsSex", k_equal, 1.0f, 1)});
  write_perk(out, k_item_count, {row("GetItemCount", k_greater_equal, 3.0f, static_cast<std::int32_t>(k_item))});
  write_perk(out, k_alias_ref, {row("GetIsAliasRef", k_equal, 1.0f, 7)});
  write_perk(out, k_null_item, {row("GetItemCount", k_greater_equal, 1.0f, 0)});
  fo4::test::end_group(out, perks);
  return out.bytes;
}

struct Seen {
  int fact = 0;
  std::uint32_t run_on = 0;
  std::string reference_plugin;
  std::uint32_t reference_id = 0;
  std::string form_plugin;
  std::uint32_t form_id = 0;
  std::int32_t integer = 0;
  std::uint32_t reference_raw = 0;
  std::int32_t param3 = 0;
};

struct Fake {
  std::function<bool(const radicle_condition_query&, double&)> answer;
  std::vector<Seen> seen;

  static std::int32_t thunk(void* user, const radicle_condition_query* query, double* out) {
    auto& self = *static_cast<Fake*>(user);
    Seen s;
    s.fact = query->fact;
    s.run_on = query->run_on;
    if (query->reference.plugin != nullptr) {
      s.reference_plugin = query->reference.plugin;
      s.reference_id = query->reference.local_id;
    }
    if (query->form.plugin != nullptr) {
      s.form_plugin = query->form.plugin;
      s.form_id = query->form.local_id;
    }
    s.integer = query->integer;
    s.reference_raw = query->reference_raw;
    s.param3 = query->param3;
    self.seen.push_back(s);
    return self.answer(*query, *out) ? 1 : 0;
  }

  radicle_condition_live_state state() { return {sizeof(radicle_condition_live_state), this, &Fake::thunk}; }
};

class ConditionLive : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_condition_live_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto path = root_ / kPlugin;
    const auto bytes = fixture();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const auto text = path.string();
    const char* paths[] = {text.c_str()};
    radicle_env* loaded = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &loaded), RADICLE_STATUS_OK);
    env_.reset(loaded);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* record(std::uint32_t local) {
    const radicle_record* found = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{kPlugin, local}, &found), RADICLE_STATUS_OK);
    return found;
  }

  radicle_condition_result evaluate(std::uint32_t local, Fake& fake) {
    const auto* rec = record(local);
    const radicle_condition_list* list = nullptr;
    EXPECT_EQ(radicle_record_conditions(env_.get(), rec, nullptr, &list), RADICLE_STATUS_OK);
    const auto live = fake.state();
    radicle_condition_result result{};
    EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, list->rows, list->count, &live, &result),
              RADICLE_STATUS_OK);
    radicle_condition_list_free(list);
    return result;
  }

  std::filesystem::path root_;
  EnvPtr env_{nullptr, radicle_env_destroy};
};

TEST_F(ConditionLive, HostLevelDecidesATrueAFalseAndAnUnknownRow) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 12;
    return q.fact == RADICLE_CONDITION_FACT_LEVEL;
  };
  EXPECT_EQ(evaluate(k_level, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_LEVEL);
  EXPECT_EQ(fake.seen[0].run_on, 0u);
  EXPECT_TRUE(fake.seen[0].form_plugin.empty());

  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 9;
    return true;
  };
  EXPECT_EQ(evaluate(k_level, fake).verdict, RADICLE_CONDITION_VERDICT_FALSE);

  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  const auto unknown = evaluate(k_level, fake);
  EXPECT_EQ(unknown.verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_EQ(unknown.first_unevaluable_row, 0u);
  EXPECT_EQ(unknown.first_unevaluable_function, fo4::schema::condition_function_by_name("GetLevel")->index);
}

TEST_F(ConditionLive, OrGroupIsTrueWhenOneMemberIsKnownTrueEvenIfTheOtherIsUnknown) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    if (q.fact == RADICLE_CONDITION_FACT_HAS_KEYWORD) return false;
    out = 1;
    return q.fact == RADICLE_CONDITION_FACT_IN_FACTION;
  };
  EXPECT_EQ(evaluate(k_or, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 2u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_HAS_KEYWORD);
  EXPECT_EQ(fake.seen[0].form_plugin, kPlugin);
  EXPECT_EQ(fake.seen[0].form_id, k_keyword);
  EXPECT_EQ(fake.seen[1].fact, RADICLE_CONDITION_FACT_IN_FACTION);
  EXPECT_EQ(fake.seen[1].form_id, k_faction);

  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 0;
    return true;
  };
  EXPECT_EQ(evaluate(k_or, fake).verdict, RADICLE_CONDITION_VERDICT_FALSE);
}

TEST_F(ConditionLive, StageDonePassesTheStageAndQuestKey) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 1;
    return true;
  };
  EXPECT_EQ(evaluate(k_stage_done, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_QUEST_STAGE_DONE);
  EXPECT_EQ(fake.seen[0].form_id, k_quest);
  EXPECT_EQ(fake.seen[0].integer, 10);
}

TEST_F(ConditionLive, UseGlobalComparisonAsksTheHostForTheGlobalAndAnUnknownGlobalIsUnevaluable) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = q.fact == RADICLE_CONDITION_FACT_LEVEL ? 5.0 : 4.0;
    return true;
  };
  EXPECT_EQ(evaluate(k_use_global, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 2u);
  EXPECT_EQ(fake.seen[1].fact, RADICLE_CONDITION_FACT_GLOBAL_VALUE);
  EXPECT_EQ(fake.seen[1].form_id, k_glob_plain);

  fake.seen.clear();
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 5.0;
    return q.fact == RADICLE_CONDITION_FACT_LEVEL;
  };
  EXPECT_EQ(evaluate(k_use_global, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
}

TEST_F(ConditionLive, UseGlobalComparisonFallsBackToAConstantGlobalsRecordValue) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 1.0;
    return q.fact == RADICLE_CONDITION_FACT_LEVEL;
  };
  EXPECT_EQ(evaluate(k_use_const, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 2u);
  EXPECT_EQ(fake.seen[1].fact, RADICLE_CONDITION_FACT_GLOBAL_VALUE);
  EXPECT_EQ(fake.seen[1].form_id, k_glob_const);
}

TEST_F(ConditionLive, RowsThatNeedMoreContextOrHaveNoFactNeverReachTheHost) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 1;
    return true;
  };
  EXPECT_EQ(evaluate(k_alias, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_EQ(evaluate(k_unmapped, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_EQ(evaluate(k_null_form, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_TRUE(fake.seen.empty());
}

TEST_F(ConditionLive, RunOnAndTheResolvedReferenceReachTheHost) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 3;
    return true;
  };
  EXPECT_EQ(evaluate(k_run_on, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].run_on, 2u);
  EXPECT_EQ(fake.seen[0].reference_plugin, kPlugin);
  EXPECT_EQ(fake.seen[0].reference_id, k_reference);
  EXPECT_EQ(fake.seen[0].reference_raw, k_reference);
}

TEST_F(ConditionLive, SelectorRunOnsReachTheHostWithTheRawReferenceAndThirdParameter) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 3;
    return true;
  };
  EXPECT_EQ(evaluate(k_selector, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].run_on, 5u);
  EXPECT_EQ(fake.seen[0].param3, 1234);
}

TEST_F(ConditionLive, NonFiniteAndOutOfFloatRangeHostNumbersAreUnknown) {
  Fake fake;
  for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 1e300}) {
    fake.answer = [bad](const radicle_condition_query&, double& out) {
      out = bad;
      return true;
    };
    EXPECT_EQ(evaluate(k_level, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  }
}

TEST_F(ConditionLive, HostDoublesAreNarrowedToFloatBeforeComparing) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double& out) {
    out = 0.1;
    return true;
  };
  EXPECT_EQ(evaluate(k_narrow, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
}

TEST_F(ConditionLive, FactionRankOfANonMemberIsTheHostsMinusOne) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = -1;
    return q.fact == RADICLE_CONDITION_FACT_FACTION_RANK;
  };
  EXPECT_EQ(evaluate(k_rank, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
}

TEST_F(ConditionLive, AKnownFalseRowBeatsAnUnknownRowAndLiveValuesReportEachRow) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 0;
    return q.fact == RADICLE_CONDITION_FACT_IS_DEAD;
  };
  EXPECT_EQ(evaluate(k_false_beats_unknown, fake).verdict, RADICLE_CONDITION_VERDICT_FALSE);

  const auto* rec = record(k_false_beats_unknown);
  const radicle_condition_list* list = nullptr;
  ASSERT_EQ(radicle_record_conditions(env_.get(), rec, nullptr, &list), RADICLE_STATUS_OK);
  const auto live = fake.state();
  std::vector<radicle_condition_value> values(list->count);
  ASSERT_EQ(radicle_condition_live_values(env_.get(), rec, list->rows, list->count, &live, values.data()),
            RADICLE_STATUS_OK);
  EXPECT_EQ(values[0].known, 0);
  EXPECT_EQ(values[1].known, 1);
  EXPECT_EQ(values[1].value, 0.0);
  EXPECT_EQ(values[1].resolved_run_on, list->rows[1].run_on);
  EXPECT_EQ(values[1].resolved_reference, list->rows[1].reference);
  radicle_condition_list_free(list);
}

TEST_F(ConditionLive, BadArgumentsAreRefusedWithAStatus) {
  const auto* rec = record(k_level);
  const radicle_condition_list* list = nullptr;
  ASSERT_EQ(radicle_record_conditions(env_.get(), rec, nullptr, &list), RADICLE_STATUS_OK);
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  auto live = fake.state();
  radicle_condition_result result{};
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, list->rows, list->count, nullptr, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  auto short_state = live;
  short_state.struct_size = sizeof(std::size_t);
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, list->rows, list->count, &short_state, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  auto no_query = live;
  no_query.query = nullptr;
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, list->rows, list->count, &no_query, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_conditions_evaluate_live(nullptr, rec, list->rows, list->count, &live, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, list->rows, list->count, &live, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, nullptr, 1, &live, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_conditions_evaluate_live(env_.get(), rec, nullptr, 0, &live, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_TRUE);
  radicle_condition_list_free(list);
}

TEST_F(ConditionLive, LiveGlobalAsksTheHostFirstAndOnlyAConstantFallsBackToTheRecord) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 7.25;
    return q.fact == RADICLE_CONDITION_FACT_GLOBAL_VALUE && q.form.local_id == k_glob_plain;
  };
  const auto live = fake.state();
  double value = -1;
  radicle_glob_live_reason reason = RADICLE_GLOB_LIVE_NO_VALUE;

  ASSERT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_plain), &live, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_LIVE_HOST);
  EXPECT_EQ(value, 7.25);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].form_plugin, kPlugin);
  EXPECT_EQ(fake.seen[0].form_id, k_glob_plain);

  ASSERT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_const), &live, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_LIVE_RECORD_CONSTANT);
  EXPECT_EQ(value, 0.5);

  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  ASSERT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_plain), &live, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_LIVE_NO_VALUE);
  EXPECT_EQ(value, 0.0);

  ASSERT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_empty), &live, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_LIVE_NO_VALUE);

  fake.seen.clear();
  ASSERT_EQ(radicle_glob_value_live(env_.get(), record(k_level), &live, &value, &reason), RADICLE_STATUS_OK);
  EXPECT_EQ(reason, RADICLE_GLOB_LIVE_NOT_GLOB);
  EXPECT_TRUE(fake.seen.empty());
}

TEST_F(ConditionLive, LiveGlobalRefusesBadArguments) {
  Fake fake;
  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  const auto live = fake.state();
  double value = 0;
  radicle_glob_live_reason reason = RADICLE_GLOB_LIVE_HOST;
  EXPECT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_plain), nullptr, &value, &reason),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_glob_value_live(env_.get(), nullptr, &live, &value, &reason), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_glob_value_live(env_.get(), record(k_glob_plain), &live, nullptr, &reason),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(ConditionLive, SexIsAskedOfTheSubjectAndComparedWithTheRowsSexParameter) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 1;
    return q.fact == RADICLE_CONDITION_FACT_IS_SEX;
  };
  EXPECT_EQ(evaluate(k_sex, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_IS_SEX);
  EXPECT_TRUE(fake.seen[0].form_plugin.empty());
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 0;
    return q.fact == RADICLE_CONDITION_FACT_IS_SEX;
  };
  EXPECT_EQ(evaluate(k_sex, fake).verdict, RADICLE_CONDITION_VERDICT_FALSE);
  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  EXPECT_EQ(evaluate(k_sex, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
}

TEST_F(ConditionLive, ItemCountPassesTheItemKeyAndAnUnknownOrNullItemIsUnevaluable) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 5;
    return q.fact == RADICLE_CONDITION_FACT_ITEM_COUNT;
  };
  EXPECT_EQ(evaluate(k_item_count, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_ITEM_COUNT);
  EXPECT_EQ(fake.seen[0].form_id, k_item);
  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  EXPECT_EQ(evaluate(k_item_count, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  Fake never;
  never.answer = [](const radicle_condition_query&, double& out) {
    out = 5;
    return true;
  };
  EXPECT_EQ(evaluate(k_null_item, never).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_TRUE(never.seen.empty());
}

TEST_F(ConditionLive, AliasRefPassesTheRawAliasInIntegerAndNoQuestForANonQuestRecord) {
  Fake fake;
  fake.answer = [](const radicle_condition_query& q, double& out) {
    out = 1;
    return q.fact == RADICLE_CONDITION_FACT_IS_ALIAS_REF;
  };
  EXPECT_EQ(evaluate(k_alias_ref, fake).verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(fake.seen.size(), 1u);
  EXPECT_EQ(fake.seen[0].fact, RADICLE_CONDITION_FACT_IS_ALIAS_REF);
  EXPECT_EQ(fake.seen[0].integer, 7);
  EXPECT_TRUE(fake.seen[0].form_plugin.empty());
  fake.answer = [](const radicle_condition_query&, double&) { return false; };
  EXPECT_EQ(evaluate(k_alias_ref, fake).verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
}

}  // namespace
