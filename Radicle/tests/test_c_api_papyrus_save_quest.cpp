#include "fo4/c_api_papyrus_quest.h"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/c_api_papyrus_save.h"

#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

std::uint8_t opcodeOf(const std::string& mnemonic) {
  const std::size_t count = sizeof(PexFile::kOpCodes) / sizeof(PexFile::kOpCodes[0]);
  for (std::size_t i = 0; i < count; ++i) {
    if (mnemonic == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
  }
  return 0xFF;
}

PexValue id(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue literal(std::string text) {
  PexValue value;
  value.type = PexValueType::String;
  value.str = std::move(text);
  return value;
}

PexInstruction ins(std::string mnemonic, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcodeOf(mnemonic);
  instruction.mnemonic = std::move(mnemonic);
  instruction.args = std::move(args);
  return instruction;
}

int32_t allowGate(void*, const char*) { return 1; }

int64_t beginLatent(void*, const char*, const char*, const radicle_papyrus_value* const*, size_t) { return 3; }

class CApiPapyrusSaveQuestTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_.gate = allowGate;
    callbacks_.begin_latent = beginLatent;
    ASSERT_EQ(radicle_papyrus_runtime_create(&callbacks_, &runtime_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_runtime_destroy(runtime_);
    for (radicle_pex* pex : pex_) radicle_pex_destroy(pex);
  }

  void load(PexFile& model) {
    const std::vector<std::uint8_t> bytes = model.toBytes();
    radicle_pex* pex = nullptr;
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime_, pex), RADICLE_STATUS_OK);
    pex_.push_back(pex);
  }

  radicle_papyrus_runtime_callbacks callbacks_{};
  radicle_papyrus_runtime* runtime_{nullptr};
  std::vector<radicle_pex*> pex_;
};

PexFile latentModel() {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Actor.psc";
  PexObject actor;
  actor.name = "Actor";
  actor.autoStateName = "";
  PexState entry;
  entry.name = "";
  PexFunction onInit;
  onInit.name = "OnInit";
  onInit.returnType = "None";
  onInit.instructions = {ins("return", {id("::NoneVar")})};
  PexFunction run;
  run.name = "Run";
  run.returnType = "None";
  run.instructions = {ins("callstatic", {literal("Waiter"), literal("Wait"), id("::nonevar")}),
                      ins("return", {id("::NoneVar")})};
  entry.functions = {onInit, run};
  actor.states.push_back(entry);
  model.objects.push_back(actor);
  model.rebuildStringTable();
  return model;
}

TEST_F(CApiPapyrusSaveQuestTest, FingerprintsAndCapturesASuspendedThread) {
  PexFile model = latentModel();
  load(model);
  char* fingerprint = nullptr;
  ASSERT_EQ(radicle_papyrus_script_fingerprint(pex_[0], "Actor", &fingerprint), RADICLE_STATUS_OK);
  ASSERT_NE(fingerprint, nullptr);
  EXPECT_EQ(std::strlen(fingerprint), 64u);
  radicle_papyrus_string_free(fingerprint);

  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Waiter", "Wait",
                                                    RADICLE_PAPYRUS_NATIVE_STATIC, "", 1),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "0001:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Run", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 64, &step), RADICLE_STATUS_OK);

  char* json = nullptr;
  char* digest = nullptr;
  ASSERT_EQ(radicle_papyrus_save_capture(runtime_, &json, &digest), RADICLE_STATUS_OK);
  ASSERT_NE(json, nullptr);
  EXPECT_NE(std::strstr(json, "\"waitingToken\""), nullptr);
  EXPECT_EQ(std::strlen(digest), 64u);
  char* report = nullptr;
  int32_t resumed = -1;
  int32_t rejected = -1;
  const char* scripts[] = {"Actor"};
  const char* fingerprints[] = {"other"};
  ASSERT_EQ(radicle_papyrus_save_restore(json, scripts, fingerprints, 1, &report, &resumed, &rejected),
            RADICLE_STATUS_OK);
  EXPECT_EQ(resumed, 0);
  EXPECT_EQ(rejected, 1);
  EXPECT_NE(std::strstr(report, "rejectedlatentwait"), nullptr);
  radicle_papyrus_string_free(report);
  radicle_papyrus_string_free(json);
  radicle_papyrus_string_free(digest);
}

TEST_F(CApiPapyrusSaveQuestTest, IdentifiesStagesBindsValuesAndFillsAliases) {
  const radicle_papyrus_stage_input stages[] = {{0, 0, 0}, {4, 1, 0}};
  const radicle_papyrus_quest_stages quests[] = {{"Q1", stages, 2}};
  radicle_papyrus_stage_report stageReport{};
  ASSERT_EQ(radicle_papyrus_identify_startup_stages(quests, 1, &stageReport), RADICLE_STATUS_OK);
  EXPECT_EQ(stageReport.identified, 1);
  ASSERT_EQ(stageReport.count, 1u);
  EXPECT_EQ(stageReport.outcomes[0].has_stage, 1);
  EXPECT_EQ(stageReport.outcomes[0].stage, 4);
  radicle_papyrus_stage_report_destroy(&stageReport);

  radicle_papyrus_vmad_shape shapes[2]{};
  shapes[0].name = "Count";
  shapes[0].type = "Int";
  shapes[0].has_integer = 1;
  shapes[0].integer = 5;
  shapes[1].name = "List";
  shapes[1].type = "Array";
  shapes[1].has_element_count = 1;
  shapes[1].element_count = 2;
  radicle_papyrus_bind_report bindReport{};
  ASSERT_EQ(radicle_papyrus_bind_vmad_values(RADICLE_PAPYRUS_OWNER_FORM, "0001:Q.esp", "", shapes, 2,
                                             &bindReport),
            RADICLE_STATUS_OK);
  EXPECT_EQ(bindReport.bound, 1);
  EXPECT_EQ(bindReport.unsupported, 1);
  radicle_papyrus_bind_report_destroy(&bindReport);

  const radicle_papyrus_alias_input aliases[] = {{"Q", 1, 0, "0002:Ref.esp", nullptr},
                                                 {"Q", 2, 3, nullptr, nullptr}};
  radicle_papyrus_alias_report aliasReport{};
  ASSERT_EQ(radicle_papyrus_fill_quest_aliases(aliases, 2, &aliasReport), RADICLE_STATUS_OK);
  EXPECT_EQ(aliasReport.filled, 1);
  EXPECT_EQ(aliasReport.open, 1);
  radicle_papyrus_alias_report_destroy(&aliasReport);
}

}  // namespace
