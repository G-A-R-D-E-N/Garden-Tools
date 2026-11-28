#include "fo4/papyrus/runtime.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"
#include "fo4/pex_analysis/native_catalogue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::NativeRegistration;
using fo4::papyrus::NativeTargetKind;
using fo4::papyrus::Runtime;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex_analysis::build_call_graph;
using fo4::pex_analysis::build_native_catalogue;
using fo4::pex_analysis::format_native_binding_report;
using fo4::pex_analysis::report_native_binding;

constexpr std::uint8_t kCallStatic = 25;
constexpr std::uint8_t kReturn = 26;

PexValue ident(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexFunction native(std::string name, bool global, std::string returnType = "None",
                   std::vector<PexTypedName> params = {}) {
  PexFunction function;
  function.name = std::move(name);
  function.isNative = true;
  function.isGlobal = global;
  function.returnType = std::move(returnType);
  function.params = std::move(params);
  return function;
}

PexFunction caller(std::string name, std::string target, std::string callee) {
  PexFunction function;
  function.name = std::move(name);
  PexInstruction call;
  call.opCode = kCallStatic;
  call.args = {ident(std::move(target)), ident(std::move(callee)), ident("::nonevar")};
  PexInstruction done;
  done.opCode = kReturn;
  done.args = {ident("::nonevar")};
  function.instructions = {call, done};
  return function;
}

PexFile file(std::string objectName, std::vector<PexFunction> functions, std::string parent = {}) {
  PexFile result;
  PexObject object;
  object.name = std::move(objectName);
  object.parentClassName = std::move(parent);
  PexState state;
  state.functions = std::move(functions);
  object.states.push_back(std::move(state));
  result.objects.push_back(std::move(object));
  return result;
}

NativeRegistration noopNative() {
  NativeRegistration registration;
  registration.kind = NativeTargetKind::Static;
  registration.body = [](const fo4::papyrus::Value&, const std::vector<fo4::papyrus::Value>&, fo4::papyrus::Value&,
                         fo4::papyrus::Status&) { return true; };
  return registration;
}

TEST(NativeCatalogue, ListsOnlyNativeFunctionsSortedAndCarriesTheirSignature) {
  const PexFile game = file("Game", {native("GetPlayer", true, "Actor"),
                                     native("AddItem", false, "None", {PexTypedName{"item", "Form"}}),
                                     caller("Helper", "Debug", "Trace")});
  const PexFile debug = file("debug", {native("Trace", true, "None", {PexTypedName{"text", "String"}})});
  const std::vector<const PexFile*> files{&game, nullptr, &debug};

  const auto catalogue = build_native_catalogue(files);

  ASSERT_EQ(catalogue.entries.size(), 3u);
  EXPECT_EQ(catalogue.entries[0].script, "debug");
  EXPECT_EQ(catalogue.entries[0].fileIndex, 2u);
  EXPECT_EQ(catalogue.entries[1].script, "Game");
  EXPECT_EQ(catalogue.entries[1].function, "AddItem");
  EXPECT_FALSE(catalogue.entries[1].global);
  ASSERT_EQ(catalogue.entries[1].params.size(), 1u);
  EXPECT_EQ(catalogue.entries[1].params[0].type, "Form");
  EXPECT_EQ(catalogue.entries[2].function, "GetPlayer");
  EXPECT_TRUE(catalogue.entries[2].global);
  EXPECT_EQ(catalogue.entries[2].returnType, "Actor");
  EXPECT_EQ(catalogue.calledEntries, 0u);
}

TEST(NativeCatalogue, KeepsOneEntryWhenTheSameNativeIsDeclaredTwiceIgnoringCase) {
  const PexFile first = file("Game", {native("GetPlayer", true)});
  const PexFile second = file("GAME", {native("getplayer", true)});
  const std::vector<const PexFile*> files{&first, &second};

  const auto catalogue = build_native_catalogue(files);

  ASSERT_EQ(catalogue.entries.size(), 1u);
  EXPECT_EQ(catalogue.entries[0].fileIndex, 0u);
}

TEST(NativeCatalogue, CountsResolvedCallSitesFromTheGraph) {
  const PexFile game = file("Game", {native("GetPlayer", true), native("Unused", true)});
  const PexFile user = file("User", {caller("A", "Game", "GetPlayer"), caller("B", "Game", "GetPlayer")});
  const std::vector<const PexFile*> files{&game, &user};
  const auto graph = build_call_graph(files);

  const auto catalogue = build_native_catalogue(files, &graph);

  ASSERT_EQ(catalogue.entries.size(), 2u);
  EXPECT_EQ(catalogue.entries[0].function, "GetPlayer");
  EXPECT_EQ(catalogue.entries[0].callSites, 2u);
  EXPECT_EQ(catalogue.entries[1].function, "Unused");
  EXPECT_EQ(catalogue.entries[1].callSites, 0u);
  EXPECT_EQ(catalogue.calledEntries, 1u);
}

TEST(NativeCatalogue, EmptySetGivesEmptyCatalogue) {
  const auto catalogue = build_native_catalogue({});
  EXPECT_TRUE(catalogue.entries.empty());
  EXPECT_EQ(catalogue.calledEntries, 0u);
}

TEST(NativeBindingReport, SplitsBoundFromUnboundAndCountsCalledUnbound) {
  const PexFile game = file("Game", {native("GetPlayer", true), native("Unused", true)});
  const PexFile user = file("User", {caller("A", "Game", "GetPlayer")});
  const std::vector<const PexFile*> files{&game, &user};
  const auto graph = build_call_graph(files);
  const auto catalogue = build_native_catalogue(files, &graph);

  const auto report = report_native_binding(
      catalogue, [](const std::string&, const std::string& function) { return function == "Unused"; });

  ASSERT_EQ(report.bound.size(), 1u);
  EXPECT_EQ(report.bound[0].function, "Unused");
  ASSERT_EQ(report.unbound.size(), 1u);
  EXPECT_EQ(report.unbound[0].function, "GetPlayer");
  EXPECT_EQ(report.unboundCalled, 1u);
  EXPECT_EQ(format_native_binding_report(report),
            "natives bound 1, unbound 1 (called 1)\nunbound Game.GetPlayer calls 1\n");
}

TEST(NativeBindingReport, NoPredicateLeavesEverythingUnbound) {
  const PexFile game = file("Game", {native("GetPlayer", true)});
  const std::vector<const PexFile*> files{&game};

  const auto report = report_native_binding(build_native_catalogue(files), {});

  EXPECT_TRUE(report.bound.empty());
  EXPECT_EQ(report.unbound.size(), 1u);
}

TEST(NativeBindingReport, RuntimeRegistryAnswersBoundIncludingAnAncestorRegistration) {
  const PexFile actorBase = file("ActorBase", {native("Wave", false)});
  const PexFile actor = file("Actor", {native("Heal", false), native("GetPlayer", true)}, "ActorBase");
  const std::vector<const PexFile*> files{&actorBase, &actor};
  Runtime runtime({&actorBase.objects[0], &actor.objects[0]});
  runtime.registerNative("Actor", "Heal", noopNative());
  runtime.registerNative("ActorBase", "Wave", noopNative());

  const auto report = report_native_binding(build_native_catalogue(files),
                                            [&runtime](const std::string& script, const std::string& function) {
                                              return runtime.isNativeBound(script, function);
                                            });

  ASSERT_EQ(report.bound.size(), 2u);
  EXPECT_EQ(report.bound[0].function, "Heal");
  EXPECT_EQ(report.bound[1].function, "Wave");
  ASSERT_EQ(report.unbound.size(), 1u);
  EXPECT_EQ(report.unbound[0].function, "GetPlayer");
  EXPECT_TRUE(runtime.isNativeBound("actor", "WAVE"));
  EXPECT_FALSE(runtime.isNativeBound("ActorBase", "Heal"));
}

}  // namespace
