#include "npc_visual_fixture.hpp"

#include "fo4/c_api_cell.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/environment/furniture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using npc_visual_fixture::Rec;
using npc_visual_fixture::build;
using npc_visual_fixture::key;
using npc_visual_fixture::make_env;
using npc_visual_fixture::words;
using npc_visual_fixture::zstr;

constexpr std::uint32_t k_npc_two = 0x910;
constexpr std::uint32_t k_race_one = 0x911;
constexpr std::uint32_t k_npc_none = 0x912;
constexpr std::uint32_t k_npc_bad_count = 0x913;
constexpr std::uint32_t k_npc_null_entry = 0x914;
constexpr std::uint32_t k_npc_twice = 0x915;
constexpr std::uint32_t k_furn = 0x916;
constexpr std::uint32_t k_quest = 0x917;
constexpr std::uint32_t k_keyword_a = 0x920;
constexpr std::uint32_t k_keyword_b = 0x921;

std::vector<Rec> records() {
  return {
      Rec{"NPC_", k_npc_two, {{"EDID", zstr("Two")}, {"KSIZ", words({2})}, {"KWDA", words({k_keyword_b, k_keyword_a})}}},
      Rec{"RACE", k_race_one, {{"EDID", zstr("Race")}, {"KSIZ", words({1})}, {"KWDA", words({k_keyword_a})}}},
      Rec{"NPC_", k_npc_none, {{"EDID", zstr("None")}}},
      Rec{"NPC_", k_npc_bad_count, {{"EDID", zstr("Bad")}, {"KSIZ", words({3})}, {"KWDA", words({k_keyword_a})}}},
      Rec{"NPC_", k_npc_null_entry, {{"EDID", zstr("Null")}, {"KSIZ", words({2})}, {"KWDA", words({0, k_keyword_a})}}},
      Rec{"NPC_", k_npc_twice,
          {{"EDID", zstr("Twice")}, {"KSIZ", words({1})}, {"KWDA", words({k_keyword_a})}, {"KWDA", words({k_keyword_b})}}},
      Rec{"QUST", k_quest, {{"EDID", zstr("Quest")}, {"KSIZ", words({1})}, {"KWDA", words({k_keyword_a})}}},
      Rec{"FURN", k_furn, {{"EDID", zstr("Furn")}, {"KSIZ", words({1})}, {"KWDA", words({k_keyword_a})}}},
  };
}

class RecordKeywordsCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_record_keywords_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto path = (root_ / "Base.esm").string();
    const auto bytes = build({}, 0x1, records());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const char* paths[] = {path.c_str()};
    radicle_env* env = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    env_.reset(env);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* winner(std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{"Base.esm", local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env_{nullptr, radicle_env_destroy};
};

}  // namespace

TEST(RecordKeywordsDecode, ReadsKwdaInStoredOrderForAnyRecordSignature) {
  const auto env = make_env({{"Base.esm", build({}, 0x1, records())}});
  const auto npc = fo4::env::decode_record_keywords(env, *env.winner(key(k_npc_two)));
  ASSERT_EQ(npc.size(), 2u);
  EXPECT_EQ(npc[0], key(k_keyword_b));
  EXPECT_EQ(npc[1], key(k_keyword_a));
  const auto race = fo4::env::decode_record_keywords(env, *env.winner(key(k_race_one)));
  ASSERT_EQ(race.size(), 1u);
  EXPECT_EQ(race[0], key(k_keyword_a));
  EXPECT_TRUE(fo4::env::decode_record_keywords(env, *env.winner(key(k_npc_none))).empty());
}

TEST(RecordKeywordsDecode, FailsClosedOnMismatchedKsizAndDuplicateKwda) {
  const auto env = make_env({{"Base.esm", build({}, 0x1, records())}});
  EXPECT_THROW(fo4::env::decode_record_keywords(env, *env.winner(key(k_npc_bad_count))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_record_keywords(env, *env.winner(key(k_npc_twice))), fo4::env::WorldDecodeError);
}

TEST(RecordKeywordsDecode, FurnitureReaderStillRejectsOtherSignatures) {
  const auto env = make_env({{"Base.esm", build({}, 0x1, records())}});
  EXPECT_EQ(fo4::env::decode_furniture_keywords(env, *env.winner(key(k_furn))).size(), 1u);
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, *env.winner(key(k_npc_two))), fo4::env::WorldDecodeError);
}

TEST_F(RecordKeywordsCApi, ReturnsKeysInStoredOrderAndFreeReleasesThem) {
  radicle_form_key* keys = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_two), &keys, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(keys[0].plugin, "Base.esm");
  EXPECT_EQ(keys[0].local_id, k_keyword_b);
  EXPECT_EQ(keys[1].local_id, k_keyword_a);
  radicle_free_form_keys(keys);
}

TEST_F(RecordKeywordsCApi, RecordWithoutKwdaAnswersAnEmptyArray) {
  radicle_form_key sentinel{};
  radicle_form_key* keys = &sentinel;
  size_t count = 7;
  ASSERT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_none), &keys, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);
}

TEST_F(RecordKeywordsCApi, NullKeywordEntriesAreDropped) {
  radicle_form_key* keys = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_null_entry), &keys, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(keys[0].local_id, k_keyword_a);
  radicle_free_form_keys(keys);
}

TEST_F(RecordKeywordsCApi, MalformedKeywordsAreParseErrorsAndArgumentsAreChecked) {
  radicle_form_key* keys = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_bad_count), &keys, &count), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_twice), &keys, &count), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_record_keywords(nullptr, winner(k_npc_two), &keys, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_keywords(env_.get(), nullptr, &keys, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_two), nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_keywords(env_.get(), winner(k_npc_two), &keys, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(RecordKeywordsCApi, QuestIsRejectedBecauseAliasBlocksRepeatKwda) {
  radicle_form_key* keys = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_record_keywords(env_.get(), winner(k_quest), &keys, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);
}
