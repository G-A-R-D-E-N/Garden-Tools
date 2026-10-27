#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "abc_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::abc::AbcFile;
using fo4::abc::AbcStatus;
using fo4::avm2::Class;
using fo4::avm2::LookupKind;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::NamespaceKind;
using fo4::avm2::Object;
using fo4::avm2::Runtime;
using fo4::avm2::RuntimeStatus;
using fo4::avm2::Trait;
using fo4::avm2::TraitKind;
using fo4::avm2::Value;

Multiname qname(std::string name, std::string name_space = {},
                NamespaceKind kind = NamespaceKind::Package) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = std::move(name);
  multiname.namespaces.push_back(Namespace{kind, std::move(name_space)});
  return multiname;
}

abc::AbcFile build_parsed_abc() {
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  return file;
}

abc::AbcFile build_script_function_abc() {
  AbcFile file;
  file.minor_version = fo4::abc::kAbcMinorVersion;
  file.major_version = fo4::abc::kAbcMajorVersion;
  file.pool.strings = {"", "helper", "Object", "void"};
  file.pool.namespaces = {abc::AbcNamespace{abc::AbcNamespaceKind::Namespace, 0},
                          abc::AbcNamespace{abc::AbcNamespaceKind::Package, 0}};
  file.pool.ns_sets = {abc::AbcNsSet{}};
  file.pool.multinames = {
      abc::AbcMultiname{},
      abc::AbcMultiname{abc::AbcMultinameKind::QName, 1, 1, 0, {}},
      abc::AbcMultiname{abc::AbcMultinameKind::QName, 2, 1, 0, {}},
      abc::AbcMultiname{abc::AbcMultinameKind::QName, 3, 1, 0, {}},
  };
  file.pool.ints = {0};
  file.pool.uints = {0};
  file.pool.doubles = {0.0};
  file.methods = {abc::AbcMethodInfo{0, 3, {}, 0, 0, {}, {}},
                  abc::AbcMethodInfo{0, 3, {}, 1, 0, {}, {}}};
  const std::vector<std::uint8_t> return_void{0x47};
  file.bodies = {abc::AbcMethodBody{0, 1, 1, 0, 1, return_void, {}, {}},
                 abc::AbcMethodBody{1, 1, 1, 0, 1, return_void, {}, {}}};
  abc::AbcTrait function_trait;
  function_trait.name = 1;
  function_trait.kind = abc::AbcTraitKind::Function;
  function_trait.class_index = 1;
  abc::AbcScriptInfo script;
  script.init = 0;
  script.traits.push_back(function_trait);
  file.scripts.push_back(script);
  return file;
}

TEST(Avm2Runtime, ResolvesMultinamesToTheirRuntimeShape) {
  const AbcFile file = build_parsed_abc();
  Multiname multiname;
  ASSERT_EQ(fo4::avm2::resolve_multiname(file, 1, multiname), RuntimeStatus::Ok);
  EXPECT_EQ(multiname.kind, MultinameKind::QName);
  EXPECT_EQ(multiname.name, "ExternalInterface");
  ASSERT_EQ(multiname.namespaces.size(), 1u);
  EXPECT_EQ(multiname.namespaces[0].kind, NamespaceKind::Package);
  EXPECT_EQ(multiname.namespaces[0].name, "flash.external");

  ASSERT_EQ(fo4::avm2::resolve_multiname(file, 11, multiname), RuntimeStatus::Ok);
  EXPECT_EQ(multiname.kind, MultinameKind::TypeName);
  EXPECT_EQ(multiname.name, "Object");
  ASSERT_EQ(multiname.type_parameters.size(), 1u);
  EXPECT_EQ(multiname.type_parameters[0], "Test");
  EXPECT_EQ(fo4::avm2::render_multiname(multiname), "Object<Test>");

  ASSERT_EQ(fo4::avm2::resolve_multiname(file, 14, multiname), RuntimeStatus::Ok);
  EXPECT_EQ(multiname.kind, MultinameKind::RuntimeName);
  EXPECT_FALSE(multiname.has_fixed_name());
  EXPECT_EQ(fo4::avm2::render_multiname(multiname), "*");
}

TEST(Avm2Runtime, LinksAClassWithBuiltinSuperAndExternalInterface) {
  const AbcFile file = build_parsed_abc();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  const fo4::avm2::RuntimeLoad load = runtime.link();
  ASSERT_TRUE(load.ok()) << fo4::avm2::runtime_status_name(load.status) << " " << load.message;
  EXPECT_TRUE(runtime.linked());

  const Class* test_class = runtime.find_class("Test");
  ASSERT_NE(test_class, nullptr);
  EXPECT_EQ(test_class->name, "Test");
  EXPECT_FALSE(test_class->external);
  ASSERT_NE(test_class->super, nullptr);
  EXPECT_EQ(test_class->super->name, "Object");
  EXPECT_FALSE(test_class->super->external);
  ASSERT_EQ(test_class->interfaces.size(), 1u);
  EXPECT_EQ(test_class->interfaces[0]->name, "click");
  EXPECT_EQ(test_class->super, runtime.find_class(qname("Object")));
  EXPECT_EQ(test_class->slot_count, 3u);

  ASSERT_EQ(test_class->instance_traits.size(), 5u);
  EXPECT_EQ(test_class->instance_traits[0].kind, TraitKind::Method);
  EXPECT_EQ(test_class->instance_traits[0].function->name(), "onClick");
  EXPECT_NE(test_class->instance_traits[0].function->body(), nullptr);

  const Trait* constant = test_class->find_slot_trait(2);
  ASSERT_NE(constant, nullptr);
  EXPECT_EQ(constant->kind, TraitKind::Const);
  EXPECT_TRUE(constant->has_default);
  ASSERT_TRUE(constant->default_value.is_int32());
  EXPECT_EQ(constant->default_value.as_int32(), 7);

  ASSERT_NE(test_class->prototype_object, nullptr);
  ASSERT_NE(test_class->class_object, nullptr);
  EXPECT_EQ(test_class->prototype_object->prototype(), test_class->super->prototype_object);
  EXPECT_EQ(test_class->class_object->prototype(), test_class->super->class_object);
  EXPECT_EQ(test_class->class_object->represented_class(), test_class);
}

TEST(Avm2Runtime, AppliesSlotDefaultsAndReadsClassTraits) {
  const AbcFile file = build_parsed_abc();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  const Class* test_class = runtime.find_class("Test");
  ASSERT_NE(test_class, nullptr);

  Object instance;
  instance.set_instance_class(const_cast<Class*>(test_class));
  test_class->apply_slot_defaults(instance);
  ASSERT_EQ(instance.slots().size(), 3u);
  EXPECT_EQ(instance.slots()[2].as_int32(), 7);

  const fo4::avm2::PropertyLookup nested = instance.lookup(qname("Inner"));
  EXPECT_EQ(nested.kind, LookupKind::Slot);
  ASSERT_TRUE(nested.value.is_object());
  EXPECT_EQ(nested.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);

  const fo4::avm2::PropertyLookup helper = instance.lookup(qname("helper"));
  EXPECT_EQ(helper.kind, LookupKind::Slot);
  ASSERT_TRUE(helper.value.is_object());
  ASSERT_EQ(helper.value.as_object()->kind(), fo4::avm2::ObjectKind::Function);
  const auto& helper_function =
      static_cast<const fo4::avm2::FunctionObject&>(*helper.value.as_object());
  EXPECT_EQ(helper_function.name(), "helper");
}

TEST(Avm2Runtime, RegistersTheClassObjectOnTheGlobalScope) {
  const AbcFile file = build_parsed_abc();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Object* global = runtime.global_object();
  ASSERT_NE(global, nullptr);
  Value value;
  ASSERT_TRUE(global->read_value(qname("Test"), value));
  ASSERT_TRUE(value.is_object());
  EXPECT_EQ(value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
  const auto& class_object = static_cast<const fo4::avm2::ClassObject&>(*value.as_object());
  EXPECT_EQ(class_object.represented_class(), runtime.find_class("Test"));
}

TEST(Avm2Runtime, RegistersScriptFunctionTraitsOnTheGlobalScope) {
  const AbcFile file = build_script_function_abc();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  const fo4::avm2::RuntimeLoad load = runtime.link();
  ASSERT_TRUE(load.ok()) << load.message;
  ASSERT_EQ(runtime.scripts().size(), 1u);
  EXPECT_EQ(runtime.scripts()[0].file, &file);
  EXPECT_EQ(runtime.scripts()[0].init_method, 0u);

  Object* global = runtime.global_object();
  Value value;
  ASSERT_TRUE(global->read_value(qname("helper"), value));
  ASSERT_TRUE(value.is_object());
  ASSERT_EQ(value.as_object()->kind(), fo4::avm2::ObjectKind::Function);
  const auto& function = static_cast<const fo4::avm2::FunctionObject&>(*value.as_object());
  EXPECT_EQ(function.name(), "helper");
  EXPECT_NE(function.body(), nullptr);
}

TEST(Avm2Runtime, AddsFilesAfterLinkByAliasingTheirClasses) {
  const AbcFile file = build_parsed_abc();
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  const std::size_t classes = runtime.class_count();
  EXPECT_EQ(runtime.link().status, RuntimeStatus::AlreadyLinked);
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link_added_file(1).ok());
  EXPECT_EQ(runtime.class_count(), classes);
}

TEST(Avm2Runtime, ReportsABrokenLinkWithoutKeepingHalfTheState) {
  AbcFile file = build_script_function_abc();
  file.scripts[0].traits[0].class_index = 9;
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  const fo4::avm2::RuntimeLoad load = runtime.link();
  EXPECT_EQ(load.status, RuntimeStatus::InvalidTrait);
  EXPECT_FALSE(runtime.linked());
  EXPECT_EQ(runtime.link().status, RuntimeStatus::AlreadyLinked);
}

}  // namespace
