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

TEST_F(CApiPapyrusSaveQuestTest, BindsArraysAndStructsFromNestedShapesAndListsRefusals) {
  radicle_papyrus_vmad_shape elements[2]{};
  elements[0].type = "Int";
  elements[0].has_integer = 1;
  elements[0].integer = 4;
  elements[1].type = "Int";
  elements[1].has_integer = 1;
  elements[1].integer = 9;
  radicle_papyrus_vmad_shape members[2]{};
  members[0].name = "Speed";
  members[0].type = "Int";
  members[0].has_integer = 1;
  members[0].integer = 7;
  members[1].name = "Label";
  members[1].type = "String";
  members[1].text = "go";

  radicle_papyrus_vmad_shape shapes[4]{};
  shapes[0].name = "Counts";
  shapes[0].type = "Array";
  shapes[0].element_type = "Int";
  shapes[0].has_element_count = 1;
  shapes[0].element_count = 2;
  shapes[0].elements = elements;
  shapes[0].elements_len = 2;
  shapes[1].name = "Config";
  shapes[1].type = "Struct";
  shapes[1].has_element_count = 1;
  shapes[1].element_count = 2;
  shapes[1].members = members;
  shapes[1].members_len = 2;
  shapes[2].name = "Pair";
  shapes[2].type = "Struct";
  shapes[2].has_element_count = 1;
  shapes[2].element_count = 2;
  shapes[3].name = "Big";
  shapes[3].type = "Int";
  shapes[3].has_integer = 1;
  shapes[3].integer = 4294967296LL;

  radicle_papyrus_bind_report report{};
  ASSERT_EQ(radicle_papyrus_bind_vmad_values(RADICLE_PAPYRUS_OWNER_FORM, "0001:Q.esp", "", shapes, 4, &report),
            RADICLE_STATUS_OK);
  EXPECT_EQ(report.bound, 2);
  EXPECT_EQ(report.unsupported, 2);
  ASSERT_EQ(report.unsupported_shape_count, 2u);
  EXPECT_NE(std::strstr(report.unsupported_shapes[0], "Big: Int: "), nullptr);
  EXPECT_NE(std::strstr(report.unsupported_shapes[1], "Pair: Struct: the struct's 2 members"), nullptr);
  ASSERT_EQ(report.count, 4u);
  EXPECT_EQ(report.properties[2].kind, 6);
  ASSERT_NE(report.properties[2].value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_array_count(report.properties[2].value), 2u);
  radicle_papyrus_value* second = nullptr;
  ASSERT_EQ(radicle_papyrus_value_array_get(report.properties[2].value, 1, &second), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(second), 9);
  radicle_papyrus_value_destroy(second);
  EXPECT_EQ(report.properties[1].kind, 7);
  radicle_papyrus_value* label = nullptr;
  ASSERT_EQ(radicle_papyrus_value_struct_get(report.properties[1].value, "label", &label), RADICLE_STATUS_OK);
  EXPECT_STREQ(radicle_papyrus_value_text(label), "go");
  radicle_papyrus_value_destroy(label);
  radicle_papyrus_bind_report_destroy(&report);
  EXPECT_EQ(report.unsupported_shapes, nullptr);
}

TEST_F(CApiPapyrusSaveQuestTest, RefusesAShapeNestedPastTheLimitOrWithANullChildArray) {
  radicle_papyrus_vmad_shape chain[20]{};
  for (int i = 0; i < 20; ++i) {
    chain[i].name = "Nest";
    chain[i].type = "Struct";
    if (i + 1 < 20) {
      chain[i].members = &chain[i + 1];
      chain[i].members_len = 1;
    }
  }
  radicle_papyrus_bind_report report{};
  EXPECT_EQ(radicle_papyrus_bind_vmad_values(RADICLE_PAPYRUS_OWNER_FORM, "0001:Q.esp", "", chain, 1, &report),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_papyrus_vmad_shape broken{};
  broken.name = "Broken";
  broken.type = "Array";
  broken.elements_len = 3;
  EXPECT_EQ(radicle_papyrus_bind_vmad_values(RADICLE_PAPYRUS_OWNER_FORM, "0001:Q.esp", "", &broken, 1, &report),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

}  // namespace
