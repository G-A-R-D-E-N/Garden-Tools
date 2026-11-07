#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
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

abc::AbcFile holder_class_file(avm2_test::ScriptBuilder& script, std::uint32_t init,
                               std::uint32_t constructor) {
  const std::uint32_t holder = script.multiname("Holder");
  const std::uint32_t object = script.multiname("Object");
  abc::AbcFile file = script.finish(init);
  abc::AbcInstanceInfo instance;
  instance.name = holder;
  instance.super_index = object;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);
  return file;
}

}  // namespace

TEST(Avm2ClassCall, CallingAUserClassCoercesItsArgument) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder = script.multiname("Holder");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.findpropstrict(holder);
  code.pushnull();
  code.callproperty(holder, 1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
}

TEST(Avm2ClassCall, CallingAUserClassReturnsAnInstanceOfThatClassUnchanged) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder = script.multiname("Holder");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.findpropstrict(holder);
  code.constructprop(holder, 0);
  code.setlocal(1);
  code.findpropstrict(holder);
  code.getlocal(1);
  code.callproperty(holder, 1);
  code.getlocal(1);
  code.strictequals();
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 2);
  abc::AbcFile file = holder_class_file(script, method, constructor);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2ClassCall, CallingAUserClassRefusesAValueOfAnotherType) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder = script.multiname("Holder");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.findpropstrict(holder);
  code.pushbyte(5);
  code.callproperty(holder, 1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2ClassCall, CallOpcodeConvertsThroughAUserClass) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder = script.multiname("Holder");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.getlex(holder);
  code.pushnull();
  code.pushnull();
  code.call(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
}

TEST(Avm2ClassCall, CallingAnUndefinedClassNamesTheMissingClass) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t missing = script.multiname("Missing", "flash.nowhere");
  avm2_test::CodeBuilder code;
  code.findpropstrict(missing);
  code.pushnull();
  code.callproperty(missing, 1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::CannotCall);
  EXPECT_NE(result.message.find("Missing is not defined"), std::string::npos) << result.message;
}

TEST(Avm2ClassCall, CallingAVectorClassCopiesAnArrayWithItsElementsCoerced) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t vector = script.multiname("Vector", "__AS3__.vec");
  const std::uint32_t int_class = script.multiname("int");
  avm2_test::CodeBuilder code;
  code.getlex(vector);
  code.getlex(int_class);
  code.applytype(1);
  code.pushnull();
  code.pushdouble(script.add_double(1.5));
  code.pushstring(script.string_of("2"));
  code.newarray(2);
  code.call(1);
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
  ASSERT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Array);
  const auto& converted = static_cast<const fo4::avm2::ArrayObject&>(*result.value.as_object());
  EXPECT_EQ(converted.instance_class()->name, "__AS3__.vec::Vector.<int>");
  ASSERT_EQ(converted.elements().size(), 2u);
  EXPECT_EQ(converted.elements()[0].as_int32(), 1);
  EXPECT_EQ(converted.elements()[1].as_int32(), 2);
}

TEST(Avm2ClassCall, CallingAVectorClassRefusesAnElementOfAnotherType) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t vector = script.multiname("Vector", "__AS3__.vec");
  const std::uint32_t holder = script.multiname("Holder");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.getlex(vector);
  code.getlex(holder);
  code.applytype(1);
  code.pushnull();
  code.pushstring(script.string_of("x"));
  code.newarray(1);
  code.call(1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}
