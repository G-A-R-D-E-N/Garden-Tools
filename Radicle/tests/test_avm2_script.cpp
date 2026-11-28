#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/abc/abc.hpp>
#include <fo4/swf/swf.hpp>

#include "abc_test_support.hpp"
#include "avm2_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Namespace;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

std::vector<std::uint8_t> serialize_script(avm2_test::ScriptBuilder& script,
                                           avm2_test::CodeBuilder& code) {
  const std::uint32_t method = script.method("init", code.bytes(), 0);
  abc::AbcFile file = script.finish(method);
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), abc::AbcStatus::Ok);
  return bytes;
}

std::string global_string(const fo4::avm2::Object& global, const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!global.read_value(multiname, value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

std::size_t global_count(const fo4::avm2::Object& global, const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!global.read_value(multiname, value) || !value.is_number_like()) {
    return 0;
  }
  return static_cast<std::size_t>(value.as_number());
}

TEST(Avm2Script, LinksAddedFilesIncrementallyAndRunsTheirScripts) {
  Runtime runtime;
  Interpreter interpreter{runtime};

  avm2_test::ScriptBuilder first_script;
  avm2_test::CodeBuilder first_code;
  first_code.findpropstrict(first_script.multiname("phase"));
  first_code.pushstring(first_script.string_of("one"));
  first_code.initproperty(first_script.multiname("phase"));
  first_code.returnvoid();
  const std::vector<std::uint8_t> first_method = serialize_script(first_script, first_code);
  abc::AbcFile first_file;
  ASSERT_EQ(abc::AbcFile::parse(first_method, first_file), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(first_file).ok());
  ASSERT_TRUE(runtime.link().ok());
  ASSERT_EQ(runtime.scripts().size(), 1u);
  ASSERT_TRUE(interpreter.run_script(0).ok());
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  EXPECT_EQ(global_string(*runtime.scripts()[0].global, "phase"), "one");

  avm2_test::ScriptBuilder second_script;
  avm2_test::CodeBuilder second_code;
  second_code.findpropstrict(second_script.multiname("phase"));
  second_code.pushstring(second_script.string_of("two"));
  second_code.initproperty(second_script.multiname("phase"));
  second_code.findpropstrict(second_script.multiname("boots"));
  second_code.pushbyte(1);
  second_code.initproperty(second_script.multiname("boots"));
  second_code.returnvoid();
  const std::vector<std::uint8_t> second_method = serialize_script(second_script, second_code);
  abc::AbcFile second_file;
  ASSERT_EQ(abc::AbcFile::parse(second_method, second_file), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(second_file).ok());
  ASSERT_EQ(runtime.file_count(), 2u);
  ASSERT_TRUE(runtime.link_added_file(1).ok());
  ASSERT_EQ(runtime.scripts().size(), 2u);
  ASSERT_TRUE(interpreter.run_script(1).ok());
  ASSERT_NE(runtime.scripts()[1].global, nullptr);
  EXPECT_EQ(global_string(*runtime.scripts()[0].global, "phase"), "two");
  EXPECT_EQ(global_count(*runtime.scripts()[1].global, "boots"), 1u);
  EXPECT_EQ(runtime.link_added_file(1).status, fo4::avm2::RuntimeStatus::AlreadyLinked);
}

TEST(Avm2Script, SwfRuntimeLinksAndRunsTheMoviesAbcBlocks) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.findpropstrict(script.multiname("booted"));
  code.pushstring(script.string_of("yes"));
  code.initproperty(script.multiname("booted"));
  code.returnvoid();
  const std::vector<std::uint8_t> abc_bytes = serialize_script(script, code);

  swf_test::SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kShowFrame);
  swf_test::SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("boot");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kEnd);
  const std::vector<std::byte> bytes = swf_test::wrap_body(builder.body, 10, false, false);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);

  Runtime runtime;
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const std::shared_ptr<SwfRuntime> movie = SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  EXPECT_EQ(runtime.file_count(), 1u);
  EXPECT_EQ(runtime.scripts().size(), 1u);
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  EXPECT_EQ(global_string(*runtime.scripts()[0].global, "booted"), "yes");
}

}  // namespace

TEST(Avm2Script, AliasesDuplicateClassesFromASecondMovieFile) {
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  abc::AbcFile first;
  ASSERT_EQ(abc::AbcFile::parse(bytes, first), abc::AbcStatus::Ok);
  Runtime runtime;
  Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.link().ok());
  const std::size_t classes_after_first = runtime.class_count();

  abc::AbcFile second;
  ASSERT_EQ(abc::AbcFile::parse(bytes, second), abc::AbcStatus::Ok);
  ASSERT_TRUE(runtime.add_file(second).ok());
  ASSERT_TRUE(runtime.link_added_file(1).ok());
  EXPECT_EQ(runtime.class_count(), classes_after_first);
  EXPECT_EQ(runtime.class_at(first, 0), runtime.class_at(second, 0));
}

namespace {

Multiname public_name(const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{fo4::avm2::NamespaceKind::Package, {}});
  return multiname;
}

abc::AbcFile class_script_file(const std::string& name, std::uint32_t slot) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t class_name = script.multiname(name);
  const std::uint32_t read_name = script.multiname("readOwn");
  const std::uint32_t object_name = script.multiname("Object");
  avm2_test::CodeBuilder read_code;
  read_code.getglobalscope();
  read_code.getslot(slot);
  read_code.returnvalue();
  const std::uint32_t read_own = script.method("readOwn", read_code.bytes(), 1);
  avm2_test::CodeBuilder empty;
  empty.returnvoid();
  const std::uint32_t constructor = script.method("ctor", empty.bytes(), 1);
  const std::uint32_t cinit = script.method("cinit", empty.bytes(), 1);
  const std::uint32_t init = script.method("init", empty.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcInstanceInfo instance;
  instance.name = class_name;
  instance.super_index = object_name;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcTrait read_trait;
  read_trait.name = read_name;
  read_trait.kind = abc::AbcTraitKind::Method;
  read_trait.method_index = read_own;
  abc::AbcClassInfo klass;
  klass.cinit = cinit;
  klass.traits.push_back(read_trait);
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = class_name;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.slot_id = slot;
  file.scripts[0].traits.push_back(class_trait);
  return file;
}

abc::AbcTrait int_slot_trait(std::uint32_t name, std::uint32_t slot, std::uint32_t int_index) {
  abc::AbcTrait trait;
  trait.name = name;
  trait.kind = abc::AbcTraitKind::Slot;
  trait.slot_id = slot;
  trait.value_index = int_index;
  trait.value_kind = abc::AbcValueKind::Int;
  return trait;
}

abc::AbcTrait function_trait(std::uint32_t name, abc::AbcTraitKind kind, std::uint32_t method) {
  abc::AbcTrait trait;
  trait.name = name;
  trait.kind = kind;
  if (kind == abc::AbcTraitKind::Method) {
    trait.method_index = method;
  } else {
    trait.class_index = method;
  }
  return trait;
}

abc::AbcFile helper_script_file(abc::AbcTraitKind kind, std::int32_t own_value) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t helper = script.multiname("helper");
  const std::uint32_t own = script.multiname("own");
  const std::uint32_t value = script.add_int(own_value);
  avm2_test::CodeBuilder read;
  read.getglobalscope();
  read.getslot(1);
  read.returnvalue();
  const std::uint32_t helper_method = script.method("helper", read.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  file.scripts[0].traits.push_back(int_slot_trait(own, 1, value));
  file.scripts[0].traits.push_back(function_trait(helper, kind, helper_method));
  return file;
}

abc::AbcFile caller_script_file(std::int32_t own_value) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t helper = script.multiname("helper");
  const std::uint32_t own = script.multiname("own");
  const std::uint32_t value = script.add_int(own_value);
  avm2_test::CodeBuilder init;
  init.findpropstrict(helper);
  init.callproperty(helper, 0);
  init.returnvalue();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  file.scripts[0].traits.push_back(int_slot_trait(own, 1, value));
  return file;
}

void expect_helper_reads_the_global_of_its_own_script(abc::AbcTraitKind kind) {
  abc::AbcFile helper = helper_script_file(kind, 5);
  abc::AbcFile caller = caller_script_file(7);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(helper).ok());
  ASSERT_TRUE(runtime.add_file(caller).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 5);
}

abc::AbcFile class_then_bad_function_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t late = script.multiname("Late");
  const std::uint32_t bad = script.multiname("bad");
  const std::uint32_t object_name = script.multiname("Object");
  avm2_test::CodeBuilder empty;
  empty.returnvoid();
  const std::uint32_t constructor = script.method("ctor", empty.bytes(), 1);
  const std::uint32_t cinit = script.method("cinit", empty.bytes(), 1);
  const std::uint32_t init = script.method("init", empty.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcInstanceInfo instance;
  instance.name = late;
  instance.super_index = object_name;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = cinit;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = late;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.slot_id = 1;
  file.scripts[0].traits.push_back(class_trait);
  file.scripts[0].traits.push_back(function_trait(bad, abc::AbcTraitKind::Function, 999));
  return file;
}

}  // namespace

TEST(Avm2Script, StaticMethodReadsItsOwnScriptGlobalSlotWhenAnotherScriptUsesTheSameSlot) {
  abc::AbcFile first = class_script_file("First", 1);
  abc::AbcFile second = class_script_file("Second", 1);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.add_file(second).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  ASSERT_TRUE(interpreter.run_script(0).ok());
  ASSERT_TRUE(interpreter.run_script(1).ok());
  for (const char* name : {"First", "Second"}) {
    const fo4::avm2::Class* klass = runtime.find_class(std::string{name});
    ASSERT_NE(klass, nullptr);
    const fo4::avm2::Trait* method = klass->resolve_class_trait(public_name("readOwn"));
    ASSERT_NE(method, nullptr);
    ASSERT_NE(method->function, nullptr);
    const ExecutionResult result = interpreter.call_function(
        *method->function, Value::object(klass->class_object), {}, 0);
    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_TRUE(result.value.is_object()) << name;
    EXPECT_EQ(static_cast<const fo4::avm2::ClassObject&>(*result.value.as_object()).represented_class(),
              klass)
        << name;
  }
}

TEST(Avm2Script, AliasedClassKeepsTheScriptGlobalOfItsFirstDefinition) {
  abc::AbcFile first = class_script_file("Dup", 1);
  abc::AbcFile second = class_script_file("Dup", 1);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.add_file(second).ok());
  ASSERT_TRUE(runtime.link().ok());
  ASSERT_EQ(runtime.scripts().size(), 2u);
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  const fo4::avm2::Class* klass = runtime.find_class(std::string{"Dup"});
  ASSERT_NE(klass, nullptr);
  EXPECT_EQ(runtime.class_at(second, 0), klass);
  EXPECT_EQ(klass->script_global, runtime.scripts()[0].global);
}

TEST(Avm2Script, InitpropertyOfAScriptClassStoresTheClassObjectInItsSlot) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t class_name = script.multiname("Second");
  const std::uint32_t object_name = script.multiname("Object");
  avm2_test::CodeBuilder empty;
  empty.returnvoid();
  const std::uint32_t constructor = script.method("ctor", empty.bytes(), 1);
  const std::uint32_t cinit = script.method("cinit", empty.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.getlocal0();
  init.pushnull();
  init.newclass(0);
  init.initproperty(class_name);
  init.returnvoid();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  abc::AbcInstanceInfo instance;
  instance.name = class_name;
  instance.super_index = object_name;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = cinit;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = class_name;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.slot_id = 1;
  file.scripts[0].traits.push_back(class_trait);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  ASSERT_TRUE(interpreter.run_script(0).ok());
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  const Value& stored = runtime.scripts()[0].global->slots()[1];
  ASSERT_TRUE(stored.is_object());
  EXPECT_EQ(stored.as_object().get(), runtime.find_class(std::string{"Second"})->class_object.get());
}

TEST(Avm2Script, SetslotIsReadByNameInTheSameScript) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t shared = script.multiname("shared");
  const std::uint32_t five = script.add_int(5);
  avm2_test::CodeBuilder init;
  init.getlocal0();
  init.pushbyte(7);
  init.setslot(1);
  init.getlex(shared);
  init.returnvalue();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  file.scripts[0].traits.push_back(int_slot_trait(shared, 1, five));
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

TEST(Avm2Script, SetpropertyIsReadBySlotInTheSameScript) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t shared = script.multiname("shared");
  const std::uint32_t five = script.add_int(5);
  avm2_test::CodeBuilder init;
  init.getlocal0();
  init.pushbyte(9);
  init.setproperty(shared);
  init.getglobalscope();
  init.getslot(1);
  init.returnvalue();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  file.scripts[0].traits.push_back(int_slot_trait(shared, 1, five));
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2Script, ANameDefinedInScriptAIsVisibleByNameFromScriptB) {
  avm2_test::ScriptBuilder first_script;
  const std::uint32_t first_shared = first_script.multiname("shared");
  const std::uint32_t nine = first_script.add_int(9);
  avm2_test::CodeBuilder first_code;
  first_code.getlocal0();
  first_code.pushbyte(9);
  first_code.setproperty(first_shared);
  first_code.returnvoid();
  const std::uint32_t first_init = first_script.method("init", first_code.bytes(), 1);
  abc::AbcFile first = first_script.finish(first_init);
  first.scripts[0].traits.push_back(int_slot_trait(first_shared, 1, nine));

  avm2_test::ScriptBuilder second_script;
  const std::uint32_t second_shared = second_script.multiname("shared");
  avm2_test::CodeBuilder second_code;
  second_code.getlex(second_shared);
  second_code.returnvalue();
  const std::uint32_t second_init = second_script.method("init", second_code.bytes(), 1);
  abc::AbcFile second = second_script.finish(second_init);

  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.add_file(second).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  ASSERT_TRUE(interpreter.run_script(0).ok());
  const ExecutionResult result = interpreter.run_script(1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2Script, FindpropertyMissCreatesTheNameOnTheScriptGlobalNotTheRegistry) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t fresh = script.multiname("fresh");
  avm2_test::CodeBuilder init;
  init.findpropstrict(fresh);
  init.pushbyte(4);
  init.initproperty(fresh);
  init.returnvoid();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  ASSERT_TRUE(interpreter.run_script(0).ok());
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  Value stored;
  ASSERT_TRUE(runtime.scripts()[0].global->read_value(public_name("fresh"), stored));
  ASSERT_TRUE(stored.is_int32());
  EXPECT_EQ(stored.as_int32(), 4);
  EXPECT_FALSE(runtime.global_object()->read_value(public_name("fresh"), stored));
}

TEST(Avm2Script, MethodTraitIsCallableByNameFromAnotherScriptWithItsOwnGlobal) {
  expect_helper_reads_the_global_of_its_own_script(abc::AbcTraitKind::Method);
}

TEST(Avm2Script, FunctionTraitIsCallableByNameFromAnotherScriptWithItsOwnGlobal) {
  expect_helper_reads_the_global_of_its_own_script(abc::AbcTraitKind::Function);
}

TEST(Avm2Script, ReleasesAScriptGlobalThatHoldsItsFunctionAfterTheRuntimeIsDestroyed) {
  abc::AbcFile file = helper_script_file(abc::AbcTraitKind::Function, 5);
  std::weak_ptr<fo4::avm2::Object> global;
  {
    auto runtime = std::make_unique<Runtime>();
    ASSERT_TRUE(runtime->add_file(file).ok());
    ASSERT_TRUE(runtime->link().ok());
    ASSERT_EQ(runtime->scripts().size(), 1u);
    global = runtime->scripts()[0].global;
    ASSERT_FALSE(global.expired());
  }
  EXPECT_TRUE(global.expired());
}

TEST(Avm2Script, FailedLinkKeepsTheClassOfAScriptGlobalThatItAlreadyPublished) {
  abc::AbcFile first = class_script_file("First", 1);
  abc::AbcFile second = class_then_bad_function_file();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(first).ok());
  ASSERT_TRUE(runtime.link().ok());
  ASSERT_TRUE(runtime.add_file(second).ok());
  EXPECT_EQ(runtime.link_added_file(1).status, fo4::avm2::RuntimeStatus::InvalidTrait);
  const fo4::avm2::Class* late = runtime.find_class(std::string{"Late"});
  ASSERT_NE(late, nullptr);
  ASSERT_NE(late->script_global, nullptr);
  EXPECT_FALSE(late->script_global->has_property(public_name("Late")));
}

TEST(Avm2Script, InitpropertyWritesADeclaredInstanceSlot) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t class_name = script.multiname("K");
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t field = script.multiname("field");
  const std::uint32_t three = script.add_int(3);
  avm2_test::CodeBuilder empty;
  empty.returnvoid();
  avm2_test::CodeBuilder constructor_code;
  constructor_code.getlocal0();
  constructor_code.pushbyte(9);
  constructor_code.initproperty(field);
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  const std::uint32_t cinit = script.method("cinit", empty.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.getlex(class_name);
  init.construct(0);
  init.getslot(1);
  init.returnvalue();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  abc::AbcInstanceInfo instance;
  instance.name = class_name;
  instance.super_index = object_name;
  instance.iinit = constructor;
  instance.traits.push_back(int_slot_trait(field, 1, three));
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = cinit;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = class_name;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.slot_id = 1;
  file.scripts[0].traits.push_back(class_trait);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2Script, WritingAScriptFunctionNameFromItsOwnScriptIsRefused) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t fn = script.multiname("fn");
  avm2_test::CodeBuilder body;
  body.returnvoid();
  const std::uint32_t fn_method = script.method("fn", body.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.findproperty(fn);
  init.pushbyte(1);
  init.setproperty(fn);
  init.returnvoid();
  const std::uint32_t init_method = script.method("init", init.bytes(), 1);
  abc::AbcFile file = script.finish(init_method);
  file.scripts[0].traits.push_back(function_trait(fn, abc::AbcTraitKind::Function, fn_method));
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(0);
  EXPECT_EQ(result.status, fo4::avm2::InterpreterStatus::TypeError);
  EXPECT_NE(result.message.find("cannot write property"), std::string::npos) << result.message;
}
