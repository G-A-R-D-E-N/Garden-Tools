#include "fo4/papyrus/runtime.hpp"
#include "fo4/papyrus/save_state.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

using fo4::papyrus::OwnerId;
using fo4::papyrus::Runtime;
using fo4::papyrus::SavedObjective;
using fo4::papyrus::SavedRegistration;
using fo4::papyrus::SavedThread;
using fo4::papyrus::SavedTimer;
using fo4::papyrus::SaveState;
using fo4::papyrus::ThreadRestoreState;
using fo4::papyrus::captureSaveState;
using fo4::papyrus::restoreSaveState;
using fo4::papyrus::scriptFingerprint;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

SavedThread thread(const std::string& owner, const std::string& script, const std::string& fingerprint) {
  SavedThread saved;
  saved.ownerKey = owner;
  saved.script = script;
  saved.fingerprint = fingerprint;
  saved.function = "Run";
  saved.pc = 3;
  saved.locals = {"1", "text"};
  return saved;
}


std::uint8_t opcodeOf(const std::string& mnemonic) {
  const std::size_t count = sizeof(PexFile::kOpCodes) / sizeof(PexFile::kOpCodes[0]);
  for (std::size_t i = 0; i < count; ++i) {
    if (mnemonic == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
  }
  return 0xFF;
}

PexValue idValue(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue textValue(std::string text) {
  PexValue value;
  value.type = PexValueType::String;
  value.str = std::move(text);
  return value;
}
PexObject scriptObject(const std::string& name) {
  PexFunction function;
  function.name = "Run";
  function.returnType = "None";
  PexInstruction wait;
  wait.opCode = opcodeOf("callstatic");
  wait.mnemonic = "callstatic";
  wait.args = {textValue("Waiter"), textValue("Wait"), idValue("::nonevar")};
  function.instructions.push_back(wait);
  PexInstruction ret;
  ret.opCode = opcodeOf("return");
  ret.mnemonic = "return";
  ret.args = {idValue("::NoneVar")};
  function.instructions.push_back(ret);
  PexState state;
  state.name = "";
  state.functions.push_back(function);
  PexObject object;
  object.name = name;
  object.autoStateName = "";
  object.states.push_back(state);
  return object;
}

TEST(PapyrusSaveState, SerializeRoundTripsAndDigestIsStable) {
  SavedRegistration registration;
  registration.ownerKey = "form:0001:Q.esp#";
  registration.kind = "CustomEvent";
  registration.eventName = "OnPing";
  SavedTimer timer;
  timer.ownerKey = "form:0001:Q.esp#";
  timer.eventName = "Tick";
  timer.elapsed = 0.5;
  timer.fires = 2;
  SavedObjective objective;
  objective.ownerKey = "alias:0001:Q.esp#0";
  objective.index = 1;
  objective.state = 2;
  const SaveState state = captureSaveState({thread("b", "B", "fpb"), thread("a", "A", "fpa")},
                                           {registration}, {timer}, {objective});
  ASSERT_EQ(state.threads.size(), 2u);
  EXPECT_EQ(state.threads[0].ownerKey, "a");
  const std::string text = state.serialize();
  EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
  EXPECT_EQ(state.digest(), state.digest());
  std::string error;
  const auto parsed = SaveState::deserialize(text, error);
  ASSERT_TRUE(parsed.has_value()) << error;
  EXPECT_EQ(parsed->serialize(), text);
  EXPECT_EQ(parsed->digest(), state.digest());
  EXPECT_EQ(parsed->registrations.size(), 1u);
  EXPECT_EQ(parsed->timers.size(), 1u);
  EXPECT_EQ(parsed->objectives.size(), 1u);
}

TEST(PapyrusSaveState, RestoreDecidesPerThreadAndRejectsLatentWaits) {
  SavedThread resumed = thread("a", "A", "fp");
  SavedThread unknown = thread("b", "B", "fp");
  SavedThread changed = thread("c", "C", "old");
  SavedThread latent = thread("d", "A", "fp");
  latent.waitingToken = 4;
  latent.waitingNative = "Wait";
  const SaveState state = captureSaveState({resumed, unknown, changed, latent}, {}, {}, {});
  const std::map<std::string, std::string> current = {{"A", "fp"}, {"C", "new"}};
  const auto report = restoreSaveState(state, current);
  EXPECT_EQ(report.resumed, 1);
  EXPECT_EQ(report.rejected, 3);
  ASSERT_EQ(report.threads.size(), 4u);
  EXPECT_EQ(report.threads[0].state, ThreadRestoreState::Resumed);
  EXPECT_EQ(report.threads[1].state, ThreadRestoreState::RejectedUnknownScript);
  EXPECT_EQ(report.threads[2].state, ThreadRestoreState::RejectedChangedScript);
  EXPECT_EQ(report.threads[3].state, ThreadRestoreState::RejectedLatentWait);
  EXPECT_NE(report.toReport().find("resumed=1 rejected=3"), std::string::npos);
  EXPECT_EQ(report.digest(), report.digest());
}

TEST(PapyrusSaveState, RestoreRejectsAnUnsupportedVersion) {
  std::string error;
  const auto parsed = SaveState::deserialize("{\"version\": 1, \"threads\": [], \"registrations\": [], "
                                             "\"timers\": [], \"objectives\": []}",
                                             error);
  EXPECT_FALSE(parsed.has_value());
  EXPECT_NE(error.find("unsupported Papyrus save version"), std::string::npos);
}

TEST(PapyrusSaveState, FingerprintChangesWhenTheScriptChanges) {
  PexObject object = scriptObject("Actor");
  const std::string first = scriptFingerprint(object);
  EXPECT_EQ(first, scriptFingerprint(object));
  object.states[0].functions[0].instructions[0].opCode = 1;
  EXPECT_NE(first, scriptFingerprint(object));
}

TEST(PapyrusSaveState, RuntimeCapturesASuspendedLatentThread) {
  PexObject object = scriptObject("Actor");
  Runtime runtime({&object});
  runtime.registerNative("Waiter", "Wait",
                         fo4::papyrus::NativeRegistration{fo4::papyrus::NativeTargetKind::Static, "", {}, true,
                                                          [](const std::vector<fo4::papyrus::Value>&,
                                                             fo4::papyrus::Status&) { return std::uint64_t{5}; }});
  const auto instance = runtime.attach("Actor", OwnerId::forForm("0001:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime.initialize(*instance), fo4::papyrus::Status::Ok);
  EXPECT_EQ(runtime.sendEvent(*instance, "Run"), fo4::papyrus::Status::Ok);
  runtime.runStep(64);
  const std::vector<SavedThread> threads = runtime.captureThreads();
  ASSERT_FALSE(threads.empty());
  EXPECT_EQ(threads[0].script, "Actor");
  EXPECT_EQ(threads[0].fingerprint, scriptFingerprint(object));
  ASSERT_TRUE(threads[0].waitingToken.has_value());
}

}  // namespace
