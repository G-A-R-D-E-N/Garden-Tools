#include "fo4/abc/abc.hpp"
#include "fo4/avm2/coercion.hpp"
#include "fo4/avm2/host.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/object.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "avm2_test_support.hpp"
#include "bgs_test_movies.hpp"
#include "fake_bgs_host.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using bgs_test::document_movie;
using bgs_test::movie_of;
using bgs_test::score_root_file;
using fo4::avm2::ExecutionResult;
using fo4::avm2::HostCallbacks;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Namespace;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::SwfRuntimeOptions;
using fo4::avm2::Value;

Multiname public_name(const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

abc::AbcFile call_script(bool absent) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t code_object = script.multiname("codeObj");
  const std::uint32_t name = script.multiname(absent ? "noSuchFunction" : "playSound");
  const std::uint32_t apply = script.multiname("apply");
  const std::uint32_t argument = script.string_of("x");
  avm2_test::CodeBuilder code;
  code.getlex(code_object);
  code.getproperty(name);
  if (!absent) {
    code.pushnull();
    code.pushstring(argument);
    code.newarray(1);
    code.callproperty(apply, 2);
  }
  code.returnvalue();
  const std::uint32_t init = script.method("init", code.bytes(), 1);
  return script.finish(init);
}

abc::AbcFile direct_call_script(const std::string& name, const std::vector<std::string>& arguments) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t code_object = script.multiname("codeObj");
  const std::uint32_t function = script.multiname(name);
  avm2_test::CodeBuilder code;
  code.getlex(code_object);
  for (const std::string& argument : arguments) {
    code.pushstring(script.string_of(argument));
  }
  code.callproperty(function, static_cast<std::uint32_t>(arguments.size()));
  code.returnvalue();
  const std::uint32_t init = script.method("init", code.bytes(), 1);
  return script.finish(init);
}

struct BgsHarness {
  Runtime runtime;
  Interpreter interpreter{runtime};
  std::vector<std::pair<std::string, std::vector<std::string>>> calls;

  BgsHarness() {
    HostCallbacks callbacks;
    callbacks.host_call = [this](std::string_view method, std::vector<std::string> arguments)
        -> std::optional<std::string> {
      calls.emplace_back(std::string{method}, std::move(arguments));
      return std::string{"answer"};
    };
    runtime.host().set_callbacks(std::move(callbacks));
  }

  std::shared_ptr<SwfRuntime> open(bool sets_code_object, const SwfRuntimeOptions& options) {
    EXPECT_TRUE(runtime.link().ok());
    const std::vector<std::byte> bytes = document_movie(sets_code_object);
    const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
    EXPECT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
    return SwfRuntime::create(runtime, interpreter, read.file, options);
  }

  void publish_code_object(const SwfRuntime& movie) {
    Value code_object;
    ASSERT_TRUE(movie.root()->read_value(public_name("BGSCodeObj"), code_object));
    fo4::avm2::Key key;
    key.name = "codeObj";
    runtime.global_object()->define_own_property(key).value = code_object;
  }

  ExecutionResult run(const abc::AbcFile& file) {
    if (!runtime.add_file(file).ok() ||
        !runtime.link_added_file(runtime.file_count() - 1).ok()) {
      ExecutionResult failure;
      failure.message = "the call script did not link";
      return failure;
    }
    return interpreter.run_script(runtime.scripts().size() - 1);
  }
};

SwfRuntimeOptions bgs_options() {
  SwfRuntimeOptions options;
  options.bgs_host = true;
  return options;
}

TEST(Avm2BgsBridge, BgsCodeObjApplyReachesTheHostWithItsName) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const abc::AbcFile call = call_script(false);
  const ExecutionResult result = harness.run(call);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.playSound");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"x"});
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "answer");
}

TEST(Avm2BgsBridge, AbsentBgsFunctionReadsUndefinedWithoutThrowing) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const abc::AbcFile call = call_script(true);
  const ExecutionResult result = harness.run(call);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
  EXPECT_TRUE(harness.calls.empty());
}

TEST(Avm2BgsBridge, MovieWithoutBgsCodeObjCreatesWithNoCodeObject) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(false, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  Value code_object;
  EXPECT_FALSE(movie->root()->read_value(public_name("BGSCodeObj"), code_object));
}

// Catches a missing notifyScripts entry: the call must reach the host as bgs.notifyScripts.
TEST(Avm2BgsBridge, NotifyScriptsReachesTheHostWithItsArgument) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result =
      harness.run(direct_call_script("notifyScripts", {"SetTurretStatusOff"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.notifyScripts");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"SetTurretStatusOff"});
}

// Catches a text lookup whose host answer is dropped: the answer must be the call's return value.
TEST(Avm2BgsBridge, TextReplaceIdReturnsTheHostAnswerToTheScript) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result = harness.run(direct_call_script("getTextReplaceID", {"OnStatus"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.getTextReplaceID");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"OnStatus"});
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "answer");
}

// Catches an anim call that forwards the SWF's arguments: the host must receive none.
TEST(Avm2BgsBridge, AnimCallsSendNoArgumentsEvenWhenTheScriptPassesSome) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result = harness.run(direct_call_script("playLeftAnim", {"a", "b"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.playLeftAnim");
  EXPECT_TRUE(harness.calls[0].second.empty());
}

// Catches playRightAnim forwarding the SWF's arguments: the host must receive none.
TEST(Avm2BgsBridge, PlayRightAnimSendsNoArgumentsEvenWhenTheScriptPassesSome) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result = harness.run(direct_call_script("playRightAnim", {"a", "b"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.playRightAnim");
  EXPECT_TRUE(harness.calls[0].second.empty());
}

// Catches playActionAnim forwarding the SWF's arguments: the host must receive none.
TEST(Avm2BgsBridge, PlayActionAnimSendsNoArgumentsEvenWhenTheScriptPassesSome) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result = harness.run(direct_call_script("playActionAnim", {"a", "b"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.playActionAnim");
  EXPECT_TRUE(harness.calls[0].second.empty());
}

// Catches a getHighscore that returns the host answer: the answer must arrive by callback.
TEST(Avm2BgsBridge, GetHighscoreAnswersByCallbackNotAsAReturnValue) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result =
      harness.run(direct_call_script("getHighscore", {"GAME_HIGH_SCORE_KEY"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.getHighscore");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"GAME_HIGH_SCORE_KEY"});
  EXPECT_TRUE(result.value.is_null());
}

// Catches a getSaveData that returns the host answer: the answer must arrive by callback.
TEST(Avm2BgsBridge, GetSaveDataAnswersByCallbackNotAsAReturnValue) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const ExecutionResult result =
      harness.run(direct_call_script("getSaveData", {"GAME_SAVE_DATA_KEY"}));
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.getSaveData");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"GAME_SAVE_DATA_KEY"});
  EXPECT_TRUE(result.value.is_null());
}

// Catches a host store that keeps the last score: the callback must carry the greater score.
TEST(Avm2BgsBridge, HighscoreCallbackCarriesTheGreaterStoredScore) {
  fake_bgs::FakeBgsHost host;
  Runtime runtime;
  Interpreter interpreter{runtime};
  HostCallbacks callbacks;
  callbacks.host_call = [&host](std::string_view method, std::vector<std::string> arguments) {
    return host.host_call(method, std::move(arguments));
  };
  runtime.host().set_callbacks(std::move(callbacks));
  ASSERT_TRUE(runtime.link().ok());
  const std::vector<std::byte> bytes = movie_of(score_root_file());
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(runtime, interpreter, read.file, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  ASSERT_TRUE(movie->call_root(interpreter, "SetScore", {Value::int32(50)}).ok());
  ASSERT_TRUE(movie->call_root(interpreter, "SetScore", {Value::int32(20)}).ok());
  ASSERT_TRUE(movie->call_root(interpreter, "RequestScore", {}).ok());
  ASSERT_FALSE(host.calls().empty());
  EXPECT_EQ(host.calls().back().method, "bgs.getHighscore");
  EXPECT_EQ(host.calls().back().arguments, std::vector<std::string>{"GAME_HIGH_SCORE_KEY"});
  const std::optional<std::string> answer =
      host.host_call("bgs.getHighscore", {"GAME_HIGH_SCORE_KEY"});
  ASSERT_TRUE(answer.has_value());
  ASSERT_TRUE(
      movie->call_root(interpreter, "SetHighscore", {Value::int32(std::stoi(*answer))}).ok());
  Value stored;
  ASSERT_TRUE(movie->root()->read_value(public_name("lastHighscore"), stored));
  EXPECT_EQ(fo4::avm2::to_string(stored), "50");
}

TEST(Avm2BgsBridge, DisabledHostLeavesBgsCodeObjWithoutFunctions) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, SwfRuntimeOptions{});
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  Value code_object;
  ASSERT_TRUE(movie->root()->read_value(public_name("BGSCodeObj"), code_object));
  ASSERT_TRUE(code_object.is_object());
  EXPECT_TRUE(code_object.as_object()->own_properties().empty());
}

}  // namespace
