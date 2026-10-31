#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

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

}  // namespace

TEST(Avm2Arguments, OptionalParameterTakesItsDefaultWhenTheArgumentIsOmitted) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t seven = script.add_int(7);
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(2);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("pick", function_code.bytes(), 3);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.pushbyte(5);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& pick = file.methods[function_method];
  pick.param_count = 2;
  pick.flags = 0x08;
  pick.options = {abc::AbcOptionalValue{abc::AbcValueKind::Int, seven}};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

TEST(Avm2Arguments, CallWithTooFewArgumentsIsAnArgumentError) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("need", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.call(0);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& need = file.methods[function_method];
  need.flags = 0;
  need.options.clear();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::InvalidArgument);
  EXPECT_NE(result.message.find("argument count mismatch"), std::string::npos) << result.message;
}

TEST(Avm2Arguments, DeclaredParameterTypeCoercesTheArgument) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t int_type = script.multiname("int");
  const std::uint32_t fraction = script.add_double(3.7);
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("typed", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.pushdouble(fraction);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& typed = file.methods[function_method];
  typed.flags = 0;
  typed.options.clear();
  typed.param_types = {int_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 3);
}

TEST(Avm2Arguments, NullForAStringParameterStaysNull) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_type = script.multiname("String");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("echo", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.pushnull();
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& echo = file.methods[function_method];
  echo.flags = 0;
  echo.options.clear();
  echo.param_types = {string_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
}

TEST(Avm2Arguments, NeedArgumentsBuildsAnArgumentsObjectWithEveryArgument) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(2);
  function_code.getproperty(length);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("count", function_code.bytes(), 3);
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
  abc::AbcMethodInfo& count = file.methods[function_method];
  count.param_count = 1;
  count.flags = 0x01;
  count.options.clear();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2Arguments, ClassParameterAcceptsAClassObject) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t class_type = script.multiname("Class");
  const std::uint32_t object_class = script.multiname("Object");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("take", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.getlex(object_class);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& take = file.methods[function_method];
  take.flags = 0;
  take.options.clear();
  take.param_types = {class_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
}

TEST(Avm2Arguments, FunctionParameterAcceptsAClassObject) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t function_type = script.multiname("Function");
  const std::uint32_t object_class = script.multiname("Object");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("take", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.getlex(object_class);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& take = file.methods[function_method];
  take.flags = 0;
  take.options.clear();
  take.param_types = {function_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
}

TEST(Avm2Arguments, ObjectParameterAcceptsAClassObject) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t object_type = script.multiname("Object");
  const std::uint32_t object_class = script.multiname("Object");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("take", function_code.bytes(), 2);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.getlex(object_class);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& take = file.methods[function_method];
  take.flags = 0;
  take.options.clear();
  take.param_types = {object_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
}

TEST(Avm2Arguments, FunctionParameterAcceptsAClosure) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t function_type = script.multiname("Function");
  avm2_test::CodeBuilder function_code;
  function_code.getlocal(1);
  function_code.returnvalue();
  const std::uint32_t function_method = script.method("take", function_code.bytes(), 2);
  avm2_test::CodeBuilder closure_code;
  closure_code.pushbyte(1);
  closure_code.returnvalue();
  const std::uint32_t closure_method = script.method("closure", closure_code.bytes(), 1);
  avm2_test::CodeBuilder script_code;
  script_code.pushnull();
  script_code.newfunction(function_method);
  script_code.newfunction(closure_method);
  script_code.call(1);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcMethodInfo& take = file.methods[function_method];
  take.flags = 0;
  take.options.clear();
  take.param_types = {function_type};
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Function);
}
