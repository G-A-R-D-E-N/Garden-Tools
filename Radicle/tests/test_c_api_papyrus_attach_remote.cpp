#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/c_api_papyrus_save.h"
#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::pex::PexFile;
using fo4::pex::PexObject;

class CApiAttachRemoteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    PexFile model;
    model.majorVersion = 3;
    model.minorVersion = 9;
    model.gameId = 2;
    model.sourceFileName = "Actor.psc";
    PexObject actor;
    actor.name = "Actor";
    actor.autoStateName = "";
    model.objects.push_back(std::move(actor));
    model.rebuildStringTable();
    const std::vector<std::uint8_t> bytes = model.toBytes();
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex_), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_create(nullptr, &runtime_), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime_, pex_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_runtime_destroy(runtime_);
    radicle_pex_destroy(pex_);
  }

  uint64_t attachPlain(const char* base) {
    uint64_t instance = 0;
    EXPECT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, base, "", &instance),
              RADICLE_STATUS_OK);
    return instance;
  }

  radicle_pex* pex_{nullptr};
  radicle_papyrus_runtime* runtime_{nullptr};
};

TEST_F(CApiAttachRemoteTest, AttachWithPropertiesStoresEveryValueAndReadsThemBack) {
  radicle_papyrus_value* count = nullptr;
  radicle_papyrus_value* label = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(5, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_string("a", 1, &label), RADICLE_STATUS_OK);
  const char* names[] = {"Count", "Label"};
  const radicle_papyrus_value* values[] = {count, label};
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", names, values, 2, &instance),
            RADICLE_STATUS_OK);
  EXPECT_NE(instance, 0u);
  radicle_papyrus_value_destroy(count);
  radicle_papyrus_value_destroy(label);

  radicle_papyrus_value* read = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_get_property(runtime_, instance, "COUNT", &read), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(read), 5);
  radicle_papyrus_value_destroy(read);
  ASSERT_EQ(radicle_papyrus_runtime_get_property(runtime_, instance, "label", &read), RADICLE_STATUS_OK);
  EXPECT_STREQ(radicle_papyrus_value_text(read), "a");
  radicle_papyrus_value_destroy(read);
  EXPECT_EQ(radicle_papyrus_runtime_get_property(runtime_, instance, "Missing", &read), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(read, nullptr);
}

TEST_F(CApiAttachRemoteTest, ABadPropertyRefusesTheWholeAttachAndLeavesNothingBehind) {
  radicle_papyrus_value* one = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(1, &one), RADICLE_STATUS_OK);
  uint64_t instance = 99;

  const char* duplicate[] = {"Count", "COUNT"};
  const radicle_papyrus_value* twice[] = {one, one};
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", duplicate, twice, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(instance, 0u);

  const char* blank[] = {"Count", " "};
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", blank, twice, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const char* withNull[] = {"Count", nullptr};
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", withNull, twice, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_papyrus_value* nullValue[] = {one, nullptr};
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", duplicate, nullValue, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Nothing", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", nullptr, nullptr, 0, &instance),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "", "",
                                                           nullptr, nullptr, 0, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_attach_with_properties(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                                           "000800:Actor.esp", "", duplicate, nullptr, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_papyrus_value_destroy(one);

  EXPECT_EQ(attachPlain("000800:Actor.esp"), 1u);
}

TEST_F(CApiAttachRemoteTest, RemoteEventsRegisterMatchUnregisterAndListTheirTargets) {
  const uint64_t first = attachPlain("000800:Actor.esp");
  const uint64_t second = attachPlain("000900:Actor.esp");
  int64_t firstHandle = 0;
  int64_t secondHandle = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, first, RADICLE_PAPYRUS_OWNER_FORM,
                                                          "000801:Quests.esp", "", "Quest.OnStageSet", &firstHandle),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, second, RADICLE_PAPYRUS_OWNER_FORM,
                                                          "000801:Quests.esp", "", "quest.onstageset", &secondHandle),
            RADICLE_STATUS_OK);
  int64_t otherHandle = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, first, RADICLE_PAPYRUS_OWNER_DYNAMIC_REFERENCE,
                                                          "000801:Quests.esp", "token", "Quest.OnStageSet",
                                                          &otherHandle),
            RADICLE_STATUS_OK);
  int64_t customHandle = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_custom_event(runtime_, first, "Quest.OnStageSet", &customHandle),
            RADICLE_STATUS_OK);

  size_t count = 99;
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "QUEST.ONSTAGESET", nullptr, nullptr, 0, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  int64_t handles[1] = {0};
  uint64_t instances[1] = {0};
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", handles, instances, 1, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(handles[0], firstHandle);
  EXPECT_EQ(instances[0], first);

  int64_t both[4] = {0};
  uint64_t owners[4] = {0};
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", both, owners, 4, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(both[1], secondHandle);
  EXPECT_EQ(owners[1], second);

  int32_t removed = 0;
  ASSERT_EQ(radicle_papyrus_runtime_unregister(runtime_, firstHandle, &removed), RADICLE_STATUS_OK);
  EXPECT_EQ(removed, 1);
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", nullptr, nullptr, 0, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  (void)otherHandle;
  (void)customHandle;

  EXPECT_EQ(radicle_papyrus_runtime_detach(runtime_, second), 1);
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", nullptr, nullptr, 0, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
}

TEST_F(CApiAttachRemoteTest, InvalidRemoteArgumentsAreRefusedAndRegisterNothing) {
  const uint64_t instance = attachPlain("000800:Actor.esp");
  int64_t handle = 7;
  EXPECT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, instance, RADICLE_PAPYRUS_OWNER_FORM, "", "",
                                                          "Quest.OnStageSet", &handle),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(handle, 0);
  EXPECT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, instance, RADICLE_PAPYRUS_OWNER_FORM,
                                                          "000801:Quests.esp", "", " ", &handle),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, 999, RADICLE_PAPYRUS_OWNER_FORM,
                                                          "000801:Quests.esp", "", "Quest.OnStageSet", &handle),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, instance, RADICLE_PAPYRUS_OWNER_PLACED_REFERENCE,
                                                          "000801:Quests.esp", "", "Quest.OnStageSet", &handle),
            RADICLE_STATUS_INVALID_ARGUMENT);
  size_t count = 5;
  ASSERT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", nullptr, nullptr, 0, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_papyrus_runtime_match_remote_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "000801:Quests.esp", "",
                                                       "Quest.OnStageSet", nullptr, nullptr, 1, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiAttachRemoteTest, RemoteRegistrationsRoundTripThroughTheSaveState) {
  const uint64_t instance = attachPlain("000800:Actor.esp");
  int64_t handle = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_remote_event(runtime_, instance, RADICLE_PAPYRUS_OWNER_FORM,
                                                          "000801:Quests.esp", "", "Quest.OnStageSet", &handle),
            RADICLE_STATUS_OK);
  char* json = nullptr;
  char* digest = nullptr;
  ASSERT_EQ(radicle_papyrus_save_capture(runtime_, &json, &digest), RADICLE_STATUS_OK);
  const std::string text(json);
  EXPECT_NE(text.find("\"kind\": \"RemoteEvent\""), std::string::npos);
  EXPECT_NE(text.find("\"source\": \"form:000801:Quests.esp#\""), std::string::npos);

  char* report = nullptr;
  int32_t resumed = -1;
  int32_t rejected = -1;
  ASSERT_EQ(radicle_papyrus_save_restore(json, nullptr, nullptr, 0, &report, &resumed, &rejected), RADICLE_STATUS_OK);
  EXPECT_NE(std::string(report).find("registrations=1"), std::string::npos);

  radicle_papyrus_string_free(report);
  report = nullptr;

  std::string stripped = text;
  const std::string marker = "\"source\": \"form:000801:Quests.esp#\"";
  stripped.replace(stripped.find(marker), marker.size(), "\"source\": \"\"");
  EXPECT_EQ(radicle_papyrus_save_restore(stripped.c_str(), nullptr, nullptr, 0, &report, &resumed, &rejected),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(report, nullptr);
  radicle_papyrus_string_free(json);
  radicle_papyrus_string_free(digest);
}

}  // namespace
