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

abc::AbcFile holder_class_file(avm2_test::ScriptBuilder& script, std::uint32_t init,
                               std::uint32_t constructor, std::uint32_t instance_method,
                               std::uint32_t static_method) {
  const std::uint32_t holder = script.multiname("Holder");
  const std::uint32_t object = script.multiname("Object");
  const std::uint32_t scale = script.multiname("scale");
  const std::uint32_t make = script.multiname("make");
  abc::AbcFile file = script.finish(init);
  abc::AbcTrait scale_trait;
  scale_trait.name = scale;
  scale_trait.kind = abc::AbcTraitKind::Method;
  scale_trait.disp_id = 1;
  scale_trait.method_index = instance_method;
  abc::AbcInstanceInfo instance;
  instance.name = holder;
  instance.super_index = object;
  instance.iinit = constructor;
  instance.traits.push_back(scale_trait);
  file.instances.push_back(instance);
  abc::AbcTrait make_trait;
  make_trait.name = make;
  make_trait.kind = abc::AbcTraitKind::Method;
  make_trait.method_index = static_method;
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  klass.traits.push_back(make_trait);
  file.classes.push_back(klass);
  return file;
}

ExecutionResult run_holder_callmethod(std::uint32_t scale_dispatch_id,
                                      std::uint32_t call_dispatch_id) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder scale_code;
  scale_code.getlocal(1);
  scale_code.returnvalue();
  const std::uint32_t scale = script.method("scale", scale_code.bytes(), 2);
  avm2_test::CodeBuilder code;
  code.newclass(0);
  code.construct(0);
  code.pushbyte(4);
  code.callmethod(call_dispatch_id, 1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor, scale, constructor);
  file.instances[0].traits[0].disp_id = scale_dispatch_id;
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

}  // namespace

TEST(Avm2CallIndexed, CallstaticRunsTheMethodWithTheReceiverAsThis) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder make_code;
  make_code.getlocal0();
  make_code.returnvalue();
  const std::uint32_t make = script.method("make", make_code.bytes(), 1);
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder scale_code;
  scale_code.getlocal(1);
  scale_code.returnvalue();
  const std::uint32_t scale = script.method("scale", scale_code.bytes(), 2);
  avm2_test::CodeBuilder code;
  code.newclass(0);
  code.construct(0);
  code.callstatic(make, 0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor, scale, make);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  ASSERT_NE(runtime.find_class("Holder"), nullptr);
  EXPECT_EQ(result.value.as_object()->instance_class(), runtime.find_class("Holder"));
}

TEST(Avm2CallIndexed, CallstaticRefusesAMethodIndexPastTheMethodTable) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.callstatic(7, 0);
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::MethodIndexOutOfRange);
  EXPECT_NE(result.message.find("callstatic"), std::string::npos) << result.message;
}

TEST(Avm2CallIndexed, CallstaticRefusesANullReceiver) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.callstatic(0, 0);
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::NullReference);
  EXPECT_NE(result.message.find("callstatic"), std::string::npos) << result.message;
}

TEST(Avm2CallIndexed, CallmethodDispatchesByDispatchIdOnTheInstance) {
  const ExecutionResult result = run_holder_callmethod(1, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 4);
}

TEST(Avm2CallIndexed, CallmethodFindsTheDispatchIdOnTheSuperClass) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder_name = script.multiname("Holder");
  const std::uint32_t derived_name = script.multiname("Derived");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder scale_code;
  scale_code.getlocal(1);
  scale_code.returnvalue();
  const std::uint32_t scale = script.method("scale", scale_code.bytes(), 2);
  avm2_test::CodeBuilder code;
  code.newclass(1);
  code.construct(0);
  code.pushbyte(4);
  code.callmethod(1, 1);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor, scale, constructor);
  abc::AbcInstanceInfo derived;
  derived.name = derived_name;
  derived.super_index = holder_name;
  derived.iinit = constructor;
  file.instances.push_back(derived);
  abc::AbcClassInfo derived_class;
  derived_class.cinit = constructor;
  file.classes.push_back(derived_class);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 4);
}

TEST(Avm2CallIndexed, CallmethodRefusesDispatchIdZero) {
  const ExecutionResult result = run_holder_callmethod(0, 0);
  EXPECT_EQ(result.status, InterpreterStatus::CannotCall);
  EXPECT_NE(result.message.find("callmethod"), std::string::npos) << result.message;
}

TEST(Avm2CallIndexed, CallmethodRefusesAnUnknownDispatchId) {
  const ExecutionResult result = run_holder_callmethod(1, 9);
  EXPECT_EQ(result.status, InterpreterStatus::CannotCall);
  EXPECT_NE(result.message.find("callmethod"), std::string::npos) << result.message;
}

TEST(Avm2CallIndexed, CallmethodRefusesANullReceiver) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.pushbyte(4);
  code.callmethod(1, 1);
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, InterpreterStatus::NullReference);
  EXPECT_NE(result.message.find("callmethod"), std::string::npos) << result.message;
}

// Invariant: callstatic pops its arguments so that the first pushed value is local 1, so 10 then 3 subtracts to 7; reversed arguments would give -7.
TEST(Avm2CallIndexed, CallstaticConsumesItsArgumentsInPushOrder) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder diff_code;
  diff_code.getlocal(1);
  diff_code.getlocal(2);
  diff_code.subtract();
  diff_code.returnvalue();
  const std::uint32_t diff = script.method("diff", diff_code.bytes(), 3);
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder scale_code;
  scale_code.getlocal(1);
  scale_code.returnvalue();
  const std::uint32_t scale = script.method("scale", scale_code.bytes(), 2);
  avm2_test::CodeBuilder code;
  code.newclass(0);
  code.construct(0);
  code.pushbyte(10);
  code.pushbyte(3);
  code.callstatic(diff, 2);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor, scale, diff);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

// Invariant: callstatic boxes an int receiver into an object of the builtin int class holding 5 and passes it as this, so getlocal0 returns that box; a refused box would end in NullReference instead.
TEST(Avm2CallIndexed, CallstaticBoxesAnIntReceiverAsThis) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder make_code;
  make_code.getlocal0();
  make_code.returnvalue();
  const std::uint32_t make = script.method("make", make_code.bytes(), 1);
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder scale_code;
  scale_code.getlocal(1);
  scale_code.returnvalue();
  const std::uint32_t scale = script.method("scale", scale_code.bytes(), 2);
  avm2_test::CodeBuilder code;
  code.pushbyte(5);
  code.callstatic(make, 0);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = holder_class_file(script, method, constructor, scale, make);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& boxed = result.value.as_object();
  EXPECT_EQ(boxed->instance_class(), runtime.find_class("int"));
  ASSERT_TRUE(boxed->has_primitive());
  EXPECT_TRUE(boxed->primitive().is_int32());
  EXPECT_EQ(boxed->primitive().as_int32(), 5);
}
