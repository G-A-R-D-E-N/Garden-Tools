#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterLimits;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::NamespaceKind;
using fo4::avm2::Runtime;
using fo4::avm2::Value;

TEST(Avm2Interpreter, AddsTwoPushedInts) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(2);
  code.pushbyte(3);
  code.add();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 5);
  EXPECT_GT(result.instructions, 0u);
}

TEST(Avm2Interpreter, SumsOneToThreeWithLocalsAndBranches) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(0);
  code.setlocal(1);
  code.pushbyte(1);
  code.setlocal(2);
  code.label("loop");
  code.getlocal(2);
  code.pushbyte(3);
  code.ifgt("done");
  code.getlocal(1);
  code.getlocal(2);
  code.add();
  code.setlocal(1);
  code.getlocal(2);
  code.increment();
  code.setlocal(2);
  code.jump("loop");
  code.label("done");
  code.getlocal(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 3);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 6);
}

TEST(Avm2Interpreter, ConcatenatesStringsWithAdd) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("hello "));
  code.pushstring(script.string_of("world"));
  code.add();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "hello world");
}

TEST(Avm2Interpreter, BuildsAnObjectAndReadsThePropertyBack) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t value_name = script.multiname("value");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("value"));
  code.pushbyte(7);
  code.newobject(1);
  code.dup();
  code.getproperty(value_name);
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

TEST(Avm2Interpreter, ReadsAnArrayElement) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t second = script.multiname("1");
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.pushbyte(8);
  code.newarray(2);
  code.getproperty(second);
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
  EXPECT_EQ(result.value.as_int32(), 8);
}

TEST(Avm2Interpreter, CallsAFunctionCreatedWithNewfunction) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder function_code;
  function_code.pushbyte(42);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("answer", function_code.bytes(), 1);

  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.call(0);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 42);
}

TEST(Avm2Interpreter, WritesAndReadsAGlobalThroughTheScopeChain) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t counter = script.multiname("counter");
  avm2_test::CodeBuilder code;
  code.findpropstrict(counter);
  code.pushbyte(5);
  code.setproperty(counter);
  code.getlex(counter);
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
  EXPECT_EQ(result.value.as_int32(), 5);

  Multiname global_name;
  global_name.kind = MultinameKind::QName;
  global_name.name = "counter";
  global_name.namespaces.push_back(Namespace{NamespaceKind::Package, ""});
  Value stored;
  ASSERT_TRUE(runtime.global_object()->read_value(global_name, stored));
  EXPECT_EQ(stored.as_int32(), 5);
}

TEST(Avm2Interpreter, CoercesAStringToIntOnTheStack) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t int_type = script.multiname("int");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("42"));
  code.coerce(int_type);
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
  EXPECT_EQ(result.value.as_int32(), 42);
}

TEST(Avm2Interpreter, ReportsAnUnimplementedOpcodeWithItsName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t helper = script.multiname("helper");
  avm2_test::CodeBuilder code;
  code.callstatic(0, 0);
  code.getdescendants(helper);
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::NotImplemented);
  EXPECT_NE(result.message.find("callstatic"), std::string::npos) << result.message;
}

TEST(Avm2Interpreter, EnforcesTheInstructionBudget) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(1);
  code.pop();
  code.pushbyte(2);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  InterpreterLimits limits;
  limits.max_instructions = 2;
  Interpreter interpreter(runtime, limits);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::InstructionBudget);
}

TEST(Avm2Interpreter, RunsTheSameScriptToTheSameValueTwice) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(10);
  code.pushbyte(4);
  code.subtract();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult first = interpreter.run_script(0);
  const ExecutionResult second = interpreter.run_script(0);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_TRUE(fo4::avm2::strict_equals(first.value, second.value));
}



TEST(Avm2Interpreter, HandsTheGlobalObjectToAScriptInitializer) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlocal0();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object().get(), runtime.global_object());
}

TEST(Avm2Interpreter, CreatesAnExternalClassForAnUnresolvedPackageName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  avm2_test::CodeBuilder code;
  code.findpropstrict(sprite);
  code.getproperty(sprite);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
}

TEST(Avm2Interpreter, RefusesAnUnboundNativeUnlessTheProbeIsLenient) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t native_method = script.method_native("nativeHelper");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.newfunction(native_method);
  code.call(0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());

  Interpreter strict_interpreter(runtime);
  const ExecutionResult strict = strict_interpreter.run_script(0);
  EXPECT_EQ(strict.status, InterpreterStatus::NotImplemented);
  EXPECT_NE(strict.message.find("nativeHelper"), std::string::npos) << strict.message;

  fo4::avm2::InterpreterLimits lenient_limits;
  lenient_limits.lenient_builtins = true;
  Interpreter lenient_interpreter(runtime, lenient_limits);
  const ExecutionResult lenient = lenient_interpreter.run_script(0);
  ASSERT_TRUE(lenient.ok()) << lenient.message;
  EXPECT_TRUE(lenient.value.is_undefined());
}

TEST(Avm2Interpreter, CatchesAThrownValueAndRunsTheHandler) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.label("try");
  code.pushstring(script.string_of("boom"));
  code.throw_op();
  code.label("after");
  code.pushbyte(1);
  code.returnvalue();
  code.label("handler");
  code.newcatch(0);
  code.swap();
  code.pop();
  code.pop();
  code.pushbyte(9);
  code.returnvalue();
  std::vector<fo4::abc::AbcException> exceptions(1);
  exceptions[0].from = static_cast<std::uint32_t>(code.offset_of("try"));
  exceptions[0].to = static_cast<std::uint32_t>(code.offset_of("handler"));
  exceptions[0].target = static_cast<std::uint32_t>(code.offset_of("handler"));
  const std::uint32_t method =
      script.method_with_exceptions("init", code.bytes(), 1, exceptions);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2Interpreter, LeavesAnExceptionUncaughtWhenTheHandlerTypeDoesNotMatch) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t type_name = script.multiname("TypeError", "flash.errors");
  avm2_test::CodeBuilder code;
  code.findpropstrict(type_name);
  code.getproperty(type_name);
  code.pop();
  code.label("try");
  code.pushstring(script.string_of("boom"));
  code.throw_op();
  code.label("handler");
  code.pushbyte(9);
  code.returnvalue();
  std::vector<fo4::abc::AbcException> exceptions(1);
  exceptions[0].from = static_cast<std::uint32_t>(code.offset_of("try"));
  exceptions[0].to = static_cast<std::uint32_t>(code.offset_of("handler"));
  exceptions[0].target = static_cast<std::uint32_t>(code.offset_of("handler"));
  exceptions[0].exc_type = type_name;
  const std::uint32_t method =
      script.method_with_exceptions("init", code.bytes(), 1, exceptions);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::UncaughtException);
  EXPECT_NE(result.message.find("boom"), std::string::npos) << result.message;
}

}  // namespace
