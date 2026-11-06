#include "fo4/c_api_papyrus_lifecycle.h"

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
using fo4::pex::PexTypedName;
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

PexInstruction ins(std::string mnemonic, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcodeOf(mnemonic);
  instruction.mnemonic = std::move(mnemonic);
  instruction.fixedArgCount = static_cast<int>(args.size());
  instruction.hasVarArgs = false;
  instruction.args = std::move(args);
  return instruction;
}

PexFunction method(std::string name, std::vector<PexTypedName> params, std::vector<PexInstruction> body) {
  PexFunction function;
  function.name = std::move(name);
  function.returnType = "None";
  function.params = std::move(params);
  function.instructions = std::move(body);
  return function;
}

PexObject script(std::string name, std::vector<PexFunction> functions) {
  PexState entry;
  entry.name = "";
  entry.functions = std::move(functions);
  PexObject object;
  object.name = std::move(name);
  object.autoStateName = "";
  object.states.push_back(std::move(entry));
  return object;
}

PexFile model() {
  const std::vector<PexInstruction> done{ins("return", {id("::NoneVar")})};
  PexFile file;
  file.majorVersion = 3;
  file.minorVersion = 9;
  file.gameId = 2;
  file.sourceFileName = "Lifecycle.psc";
  file.objects.push_back(script("Plain", {method("OnInit", {}, done)}));
  file.objects.push_back(script(
      "Listener", {method("OnInit", {}, done),
                   method("::remote_Actor_OnDeath", {PexTypedName{"amount", "Int"}},
                          {ins("assign", {id("seen"), id("amount")}), ins("return", {id("::NoneVar")})})}));
  file.rebuildStringTable();
  return file;
}

struct GateState {
  std::string blocked;
};

int32_t gateCallback(void* user, const char* key) {
  return static_cast<GateState*>(user)->blocked == key ? 0 : 1;
}

class CApiLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_.user = &gate_;
    callbacks_.gate = gateCallback;
    ASSERT_EQ(radicle_papyrus_runtime_create(&callbacks_, &runtime_), RADICLE_STATUS_OK);
    const std::vector<std::uint8_t> bytes = model().toBytes();
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex_), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime_, pex_), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_lifecycle_create(runtime_, &lifecycle_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_lifecycle_destroy(lifecycle_);
    radicle_papyrus_runtime_destroy(runtime_);
    radicle_pex_destroy(pex_);
  }

  static radicle_papyrus_quest_startup_input quest(const char* key, const char* const* scripts, size_t count) {
    radicle_papyrus_quest_startup_input input{};
    input.quest = key;
    input.start_game_enabled = 1;
    input.scripts = scripts;
    input.script_count = count;
    return input;
  }

  GateState gate_;
  radicle_papyrus_runtime_callbacks callbacks_{};
  radicle_papyrus_runtime* runtime_{nullptr};
  radicle_pex* pex_{nullptr};
  radicle_papyrus_lifecycle_handle* lifecycle_{nullptr};
};

// Detects: the C boundary that must start a quest, expose its instances and counters, and stop it for a restart.
TEST_F(CApiLifecycleTest, StartsAQuestExposesItsInstancesAndRestartsAfterStop) {
  const char* scripts[] = {"Plain", "Listener"};
  const radicle_papyrus_quest_startup_input input = quest("QA", scripts, 2);

  radicle_papyrus_quest_startup_plan plan{};
  ASSERT_EQ(radicle_papyrus_lifecycle_preflight(lifecycle_, &input, 1, &plan), RADICLE_STATUS_OK);
  EXPECT_EQ(plan.ready, 1);
  EXPECT_NE(std::strstr(plan.report, "QA ready"), nullptr);
  EXPECT_EQ(std::strlen(plan.digest), 64u);
  radicle_papyrus_quest_startup_plan_destroy(&plan);

  radicle_papyrus_quest_startup_result result{};
  ASSERT_EQ(radicle_papyrus_lifecycle_start(lifecycle_, &input, 1, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.applied, 1);
  EXPECT_EQ(result.started, 1);
  EXPECT_EQ(result.attached_scripts, 2);
  EXPECT_EQ(result.reason_count, 0u);
  radicle_papyrus_quest_startup_result_destroy(&result);

  EXPECT_EQ(radicle_papyrus_lifecycle_is_started(lifecycle_, "QA"), 1);
  EXPECT_EQ(radicle_papyrus_lifecycle_instance_count(lifecycle_, "QA"), 2u);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_lifecycle_instance_at(lifecycle_, "QA", 1, &instance), 1);
  EXPECT_EQ(radicle_papyrus_runtime_lifecycle(runtime_, instance), RADICLE_PAPYRUS_LIFECYCLE_READY);
  EXPECT_EQ(radicle_papyrus_lifecycle_instance_at(lifecycle_, "QA", 2, &instance), 0);
  radicle_papyrus_lifecycle_counters counters{};
  ASSERT_EQ(radicle_papyrus_lifecycle_get_counters(lifecycle_, &counters), RADICLE_STATUS_OK);
  EXPECT_EQ(counters.started, 1);
  EXPECT_EQ(counters.attached_scripts, 2);

  int32_t wasStarted = 0;
  ASSERT_EQ(radicle_papyrus_lifecycle_stop(lifecycle_, "QA", &wasStarted), RADICLE_STATUS_OK);
  EXPECT_EQ(wasStarted, 1);
  EXPECT_EQ(radicle_papyrus_lifecycle_is_started(lifecycle_, "QA"), 0);
  ASSERT_EQ(radicle_papyrus_lifecycle_start(lifecycle_, &input, 1, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.applied, 1);
  radicle_papyrus_quest_startup_result_destroy(&result);
}

// Detects: the gate callback that must block a quest and report the reason strings across the C boundary.
TEST_F(CApiLifecycleTest, ABlockedQuestReportsItsReasonsAndAttachesNothing) {
  gate_.blocked = "quest.runtime.fragment_dispatch_timing";
  const char* scripts[] = {"Plain"};
  radicle_papyrus_quest_startup_input fragmented = quest("QB", scripts, 1);
  fragmented.fragments = 3;

  radicle_papyrus_quest_startup_result result{};
  ASSERT_EQ(radicle_papyrus_lifecycle_start(lifecycle_, &fragmented, 1, &result), RADICLE_STATUS_OK);

  EXPECT_EQ(result.applied, 0);
  ASSERT_EQ(result.reason_count, 1u);
  EXPECT_STREQ(result.reasons[0], "QB: the dispatch timing of stage fragments is unproven");
  EXPECT_EQ(radicle_papyrus_lifecycle_instance_count(lifecycle_, "QB"), 0u);
  radicle_papyrus_quest_startup_result_destroy(&result);
}

// Detects: alias and actor inputs that must cross the C boundary with their objectives, targets and counts intact.
TEST_F(CApiLifecycleTest, AliasesAttachBindToActorsAndClear) {
  const int32_t objectives[] = {4, 2};
  radicle_papyrus_alias_script_input plain{};
  plain.quest = "Q";
  plain.alias_id = 1;
  plain.script = "Plain";
  plain.objectives = objectives;
  plain.objective_count = 2;
  radicle_papyrus_alias_script_input none{};
  none.quest = "Q";
  none.alias_id = 2;
  const radicle_papyrus_alias_script_input aliases[] = {plain, none};

  radicle_papyrus_alias_attach_report report{};
  ASSERT_EQ(radicle_papyrus_lifecycle_attach_aliases(lifecycle_, aliases, 2, &report), RADICLE_STATUS_OK);
  EXPECT_EQ(report.attached, 1);
  EXPECT_EQ(report.skipped, 1);
  EXPECT_EQ(report.natives, 1);
  EXPECT_EQ(report.objectives, 2);
  radicle_papyrus_alias_attach_report_destroy(&report);
  EXPECT_EQ(radicle_papyrus_lifecycle_objective(lifecycle_, "Q", 1, 4), 1);
  EXPECT_EQ(radicle_papyrus_lifecycle_objective(lifecycle_, "Q", 1, 3), -1);
  uint64_t instance = 0;
  EXPECT_EQ(radicle_papyrus_lifecycle_alias_instance(lifecycle_, "Q", 1, &instance), 1);
  EXPECT_EQ(radicle_papyrus_lifecycle_alias_instance(lifecycle_, "Q", 2, &instance), 0);

  radicle_papyrus_actor_alias_input bound{};
  bound.alias.quest = "Q";
  bound.alias.alias_id = 7;
  bound.alias.script = "Plain";
  bound.bound = 1;
  bound.target = "Actor:0A";
  radicle_papyrus_actor_alias_input unbound{};
  unbound.alias.quest = "Q";
  unbound.alias.alias_id = 8;
  unbound.alias.script = "Plain";
  const radicle_papyrus_actor_alias_input actors[] = {bound, unbound};
  radicle_papyrus_actor_bind_report bind{};
  ASSERT_EQ(radicle_papyrus_lifecycle_bind_actors(lifecycle_, actors, 2, &bind), RADICLE_STATUS_OK);
  EXPECT_EQ(bind.bound, 1);
  EXPECT_EQ(bind.unbound, 1);
  EXPECT_EQ(bind.attached, 1);
  radicle_papyrus_actor_bind_report_destroy(&bind);

  int32_t cleared = 0;
  ASSERT_EQ(radicle_papyrus_lifecycle_clear_alias(lifecycle_, "Q", 1, &cleared), RADICLE_STATUS_OK);
  EXPECT_EQ(cleared, 1);
  ASSERT_EQ(radicle_papyrus_lifecycle_clear_quest_aliases(lifecycle_, "Q", &cleared), RADICLE_STATUS_OK);
  EXPECT_EQ(cleared, 1);
}

// Detects: fan-out that must deliver arguments and report per-script outcomes, and park state that must gate delivery.
TEST_F(CApiLifecycleTest, FanOutDeliversWithArgumentsAndHonoursParking) {
  const char* scripts[] = {"Listener"};
  const radicle_papyrus_quest_startup_input input = quest("QA", scripts, 1);
  radicle_papyrus_quest_startup_result started{};
  ASSERT_EQ(radicle_papyrus_lifecycle_start(lifecycle_, &input, 1, &started), RADICLE_STATUS_OK);
  radicle_papyrus_quest_startup_result_destroy(&started);

  radicle_papyrus_value* amount = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(7, &amount), RADICLE_STATUS_OK);
  const radicle_papyrus_value* args[] = {amount};

  radicle_papyrus_fanout_report report{};
  ASSERT_EQ(radicle_papyrus_runtime_fan_out_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "QA", "", "Actor.OnDeath",
                                                  args, 1, &report),
            RADICLE_STATUS_OK);
  EXPECT_EQ(report.delivered, 1);
  ASSERT_EQ(report.count, 1u);
  EXPECT_STREQ(report.handler, "::remote_Actor_OnDeath");
  EXPECT_STREQ(report.outcomes[0].script, "Listener");
  EXPECT_EQ(report.outcomes[0].result, RADICLE_PAPYRUS_FANOUT_DELIVERED);
  const uint64_t instance = report.outcomes[0].instance;
  radicle_papyrus_fanout_report_destroy(&report);

  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 64, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);
  radicle_papyrus_value* seen = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_get_variable(runtime_, instance, "seen", &seen), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(seen), 7);
  radicle_papyrus_value_destroy(seen);

  ASSERT_EQ(radicle_papyrus_runtime_park_owner(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "QA", "", 1), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_broadcast_event(runtime_, "Actor.OnDeath", args, 1, &report), RADICLE_STATUS_OK);
  EXPECT_EQ(report.skipped_parked, 1);
  EXPECT_EQ(report.delivered, 0);
  radicle_papyrus_fanout_report_destroy(&report);

  gate_.blocked = "event.Actor.OnDeath";
  ASSERT_EQ(radicle_papyrus_runtime_park_owner(runtime_, RADICLE_PAPYRUS_OWNER_FORM, "QA", "", 0), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_broadcast_event(runtime_, "Actor.OnDeath", args, 1, &report), RADICLE_STATUS_OK);
  EXPECT_EQ(report.refused, 1);
  radicle_papyrus_fanout_report_destroy(&report);
  radicle_papyrus_value_destroy(amount);
}

// Detects: missing handles and null arguments that must be refused instead of dereferenced.
TEST_F(CApiLifecycleTest, MissingHandlesAreRefused) {
  radicle_papyrus_lifecycle_handle* none = nullptr;
  EXPECT_EQ(radicle_papyrus_lifecycle_create(nullptr, &none), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(none, nullptr);
  radicle_papyrus_quest_startup_result result{};
  EXPECT_EQ(radicle_papyrus_lifecycle_start(nullptr, nullptr, 0, &result), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_papyrus_fanout_report report{};
  EXPECT_EQ(radicle_papyrus_runtime_broadcast_event(runtime_, nullptr, nullptr, 0, &report),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_fan_out_event(runtime_, RADICLE_PAPYRUS_OWNER_FORM, nullptr, "", "OnHit", nullptr, 0,
                                                  &report),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_lifecycle_is_started(nullptr, "QA"), 0);
}

}  // namespace
