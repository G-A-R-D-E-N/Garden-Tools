#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
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
  EXPECT_NE(result.message.find("getdescendants"), std::string::npos) << result.message;
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

// Detects: inclocal/declocal (0x92/0x94) missing; a Number local ++/-- must not be NotImplemented.
TEST(Avm2Interpreter, IncrementsAndDecrementsANumberLocal) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushdouble(script.add_double(2.5));
  code.setlocal(1);
  code.inclocal(1);
  code.declocal(1);
  code.inclocal(1);
  code.getlocal(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 2);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << fo4::avm2::interpreter_status_name(result.status) << ": "
                           << result.message;
  EXPECT_EQ(fo4::avm2::to_number(result.value), 3.5);
}

// Detects: negate_i (0xC4) missing, so negating an int must not return NotImplemented.
TEST(Avm2Interpreter, NegatesAnIntWithNegateI) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(5);
  code.negate_i();
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
  EXPECT_EQ(result.value.as_int32(), -5);
}

// Detects: generic add wrapping int32 overflow; INT_MAX + 1 must yield the Number 2147483648.
TEST(Avm2Interpreter, AddOfIntsPromotesToANumberOnOverflow) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(2147483647));
  code.pushbyte(1);
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
  EXPECT_FALSE(result.value.is_int32());
  EXPECT_EQ(fo4::avm2::to_number(result.value), 2147483648.0);
}

// Detects: generic increment wrapping INT_MAX; an overflowing ++ must yield the Number 2147483648.
TEST(Avm2Interpreter, IncrementOfIntMaxPromotesToANumber) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(2147483647));
  code.increment();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_FALSE(result.value.is_int32());
  EXPECT_EQ(fo4::avm2::to_number(result.value), 2147483648.0);
}

// Detects: generic subtract of two uints wrapping to 4294967295; 0u - 1u must yield the Number -1.
TEST(Avm2Interpreter, SubtractOfUintsPromotesToANumberBelowZero) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(0);
  code.convert_u();
  code.pushbyte(1);
  code.convert_u();
  code.subtract();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_FALSE(result.value.is_uint32());
  EXPECT_EQ(fo4::avm2::to_number(result.value), -1.0);
}

// Detects: generic uint add wrapping past UINT_MAX; UINT_MAX + 1 must yield the Number 4294967296.
TEST(Avm2Interpreter, AddOfUintsPromotesToANumberAboveUintMax) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(-1));
  code.convert_u();
  code.pushbyte(1);
  code.convert_u();
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
  EXPECT_FALSE(result.value.is_uint32());
  EXPECT_EQ(fo4::avm2::to_number(result.value), 4294967296.0);
}

// Detects: generic negate of INT_MIN wrapping to INT_MIN; the result must be the Number 2147483648.
TEST(Avm2Interpreter, NegateOfIntMinPromotesToANumber) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(std::numeric_limits<std::int32_t>::min()));
  code.negate();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_FALSE(result.value.is_int32());
  EXPECT_EQ(fo4::avm2::to_number(result.value), 2147483648.0);
}

// Detects: inclocal_i signed INT_MAX + 1 overflow; the int32 local must wrap to INT_MIN.
TEST(Avm2Interpreter, InclocalIWrapsIntMaxToIntMin) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(std::numeric_limits<std::int32_t>::max()));
  code.setlocal(1);
  code.inclocal_i(1);
  code.getlocal(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 2);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), std::numeric_limits<std::int32_t>::min());
}

// Detects: declocal_i signed INT_MIN - 1 overflow; the int32 local must wrap to INT_MAX.
TEST(Avm2Interpreter, DeclocalIWrapsIntMinToIntMax) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(std::numeric_limits<std::int32_t>::min()));
  code.setlocal(1);
  code.declocal_i(1);
  code.getlocal(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 2);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), std::numeric_limits<std::int32_t>::max());
}

TEST(Avm2Interpreter, RestParameterCollectsTheArgumentsAfterTheDeclaredOnes) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(2);
  function_code.getproperty(length);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("collect", function_code.bytes(), 3);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.pushbyte(1);
  script_code.pushbyte(2);
  script_code.pushbyte(3);
  script_code.call(3);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  file.methods[function_method].param_count = 1;
  file.methods[function_method].flags = 0x04;
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 2.0);
}

TEST(Avm2Interpreter, ArgumentsPastTheDeclaredParametersDoNotFillOrdinaryLocals) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(2);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("local", function_code.bytes(), 3);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.pushbyte(1);
  script_code.pushbyte(2);
  script_code.call(2);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  file.methods[function_method].param_count = 1;
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
}

TEST(Avm2Interpreter, NewclassPopsItsBaseWhenTheInterfaceHasNone) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t iface = script.multiname("IFace");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder script_code;
  script_code.pushbyte(5);
  script_code.pushnull();
  script_code.newclass(0);
  script_code.pop();
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcInstanceInfo instance;
  instance.name = iface;
  instance.super_index = 0;
  instance.flags = 0x04;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 5);
}

TEST(Avm2Interpreter, ClassObjectIsAnInstanceOfClassSoAsClassKeepsIt) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t class_name = script.multiname("Class");
  code.getlex(object_name);
  code.getlex(class_name);
  code.op(0x87);
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

TEST(Avm2Interpreter, ClassObjectIsAFunction) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t function_name = script.multiname("Function");
  code.getlex(object_name);
  code.getlex(function_name);
  code.op(0xB3);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

}  // namespace
