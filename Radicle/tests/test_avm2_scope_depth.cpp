#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_two_scope_pushes(std::uint32_t max_scope_depth) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getglobalscope();
  code.pushscope();
  code.getglobalscope();
  code.pushscope();
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  file.bodies[0].max_scope_depth = max_scope_depth;
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

}  // namespace

TEST(Avm2ScopeDepth, PushscopeRunsWhileTheDepthStaysWithinMaxScopeDepth) {
  const ExecutionResult result = run_two_scope_pushes(2);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
}

TEST(Avm2ScopeDepth, PushscopePastMaxScopeDepthRaisesAnErrorInsteadOfGrowing) {
  const ExecutionResult result = run_two_scope_pushes(1);
  EXPECT_EQ(result.status, InterpreterStatus::ScopeDepthExceeded);
}

TEST(Avm2OuterScope, GetouterscopeReadsTheScopeCapturedByNewfunction) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t value = script.multiname("value");
  avm2_test::CodeBuilder function_code;
  function_code.op_u30(0x67, 1);
  function_code.getproperty(value);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("read", function_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("value"));
  code.pushbyte(7);
  code.newobject(1);
  code.pushscope();
  code.pushnull();
  code.newfunction(function_method);
  code.pushstring(script.string_of("value"));
  code.pushbyte(9);
  code.newobject(1);
  code.pushscope();
  code.call(0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

TEST(Avm2OuterScope, GetouterscopeRefusesAnIndexPastTheCapturedScope) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder function_code;
  function_code.op_u30(0x67, 1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("read", function_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.newfunction(function_method);
  code.call(0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::ScopeUnderflow);
  EXPECT_NE(result.message.find("getouterscope"), std::string::npos) << result.message;
}

TEST(Avm2OuterScope, GetouterscopeRefusesAMethodWithoutACapturedScope) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.op_u30(0x67, 0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::NotImplemented);
  EXPECT_NE(result.message.find("getouterscope"), std::string::npos) << result.message;
}
