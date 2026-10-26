#include <fo4/avm2/class.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using fo4::avm2::Class;
using fo4::avm2::Key;
using fo4::avm2::LookupKind;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::NamespaceKind;
using fo4::avm2::Object;
using fo4::avm2::PropertyLookup;
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

std::shared_ptr<fo4::avm2::FunctionObject> function_named(std::string name) {
  return std::make_shared<fo4::avm2::FunctionObject>(std::move(name), nullptr,
                                                      fo4::avm2::kNoMethod);
}

Trait slot_trait(std::string name, std::uint32_t slot_id, bool is_const = false) {
  Trait trait;
  trait.multiname = qname(std::move(name));
  trait.kind = is_const ? TraitKind::Const : TraitKind::Slot;
  trait.slot_id = slot_id;
  return trait;
}

Trait method_trait(std::string name, std::string function_name) {
  Trait trait;
  trait.multiname = qname(std::move(name));
  trait.kind = TraitKind::Method;
  trait.function = function_named(std::move(function_name));
  return trait;
}

Trait accessor_trait(std::string name, bool getter, std::string function_name) {
  Trait trait;
  trait.multiname = qname(std::move(name));
  trait.kind = getter ? TraitKind::Getter : TraitKind::Setter;
  trait.function = function_named(std::move(function_name));
  return trait;
}

struct Hierarchy {
  Class base;
  Class derived;
  Class interface_class;

  Hierarchy() {
    interface_class.name = "IThing";
    interface_class.is_interface = true;

    base.name = "Base";
    base.interfaces.push_back(&interface_class);
    base.instance_traits.push_back(slot_trait("x", 0));
    base.instance_traits.push_back(slot_trait("k", 1, true));
    base.instance_traits.push_back(method_trait("foo", "Base::foo"));
    Trait secret;
    secret.multiname = qname("secret", "Base", NamespaceKind::Private);
    secret.kind = TraitKind::Slot;
    secret.slot_id = 2;
    base.instance_traits.push_back(secret);
    base.slot_count = base.compute_slot_count();

    derived.name = "Derived";
    derived.super = &base;
    derived.slot_count = derived.compute_slot_count();
    derived.instance_traits.push_back(slot_trait("y", 3));
    derived.instance_traits.push_back(method_trait("foo", "Derived::foo"));
    derived.instance_traits.push_back(accessor_trait("value", true, "Derived::get_value"));
    derived.instance_traits.push_back(accessor_trait("value", false, "Derived::set_value"));
    derived.slot_count = derived.compute_slot_count();
  }
};

TEST(Avm2Class, CountsSlotsOverTheChain) {
  Hierarchy hierarchy;
  EXPECT_EQ(hierarchy.base.compute_slot_count(), 3u);
  EXPECT_EQ(hierarchy.derived.compute_slot_count(), 4u);
  EXPECT_TRUE(hierarchy.derived.is_subclass_of(&hierarchy.base));
  EXPECT_TRUE(hierarchy.derived.implements_interface(&hierarchy.interface_class));
  EXPECT_FALSE(hierarchy.base.is_subclass_of(&hierarchy.derived));
}

TEST(Avm2Class, ResolvesDeclarationsFromMostDerivedClass) {
  Hierarchy hierarchy;
  const Trait* foo = hierarchy.derived.resolve_instance_trait(qname("foo"));
  ASSERT_NE(foo, nullptr);
  EXPECT_EQ(foo->function->name(), "Derived::foo");
  const Trait* inherited = hierarchy.derived.resolve_instance_trait(qname("x"));
  ASSERT_NE(inherited, nullptr);
  EXPECT_EQ(inherited->slot_id, 0u);
  EXPECT_EQ(hierarchy.derived.find_slot_trait(0), inherited);
  EXPECT_NE(hierarchy.derived.find_slot_trait(3), nullptr);
  EXPECT_EQ(hierarchy.derived.find_slot_trait(9), nullptr);
}

TEST(Avm2Class, MatchesTraitNamespacesAndKinds) {
  Hierarchy hierarchy;
  EXPECT_NE(hierarchy.base.resolve_instance_trait(
                qname("secret", "Base", NamespaceKind::Private)),
            nullptr);
  EXPECT_EQ(hierarchy.base.resolve_instance_trait(qname("secret")), nullptr);
  EXPECT_EQ(hierarchy.base.resolve_instance_trait(qname("secret", "Other",
                                                        NamespaceKind::Private)),
            nullptr);

  const Trait* accessor = hierarchy.derived.resolve_instance_trait(qname("value"));
  ASSERT_NE(accessor, nullptr);
  EXPECT_EQ(accessor->kind, TraitKind::Getter);
}

TEST(Avm2Object, ReadsAndWritesSlotTraits) {
  Hierarchy hierarchy;
  Object object;
  object.set_instance_class(&hierarchy.derived);
  object.resize_slots(hierarchy.derived.slot_count);

  Value value;
  EXPECT_TRUE(object.read_value(qname("x"), value));
  EXPECT_TRUE(value.is_undefined());
  EXPECT_TRUE(object.write_value(qname("x"), Value::int32(7)));
  ASSERT_TRUE(object.read_value(qname("x"), value));
  EXPECT_EQ(value.as_int32(), 7);

  EXPECT_TRUE(object.write_value(qname("y"), Value::string("hello")));
  ASSERT_TRUE(object.read_value(qname("y"), value));
  EXPECT_EQ(value.as_string(), "hello");

  EXPECT_FALSE(object.write_value(qname("k"), Value::int32(1))) << "const traits refuse writes";
}

TEST(Avm2Object, ReportsMethodAndAccessorLookups) {
  Hierarchy hierarchy;
  Object object;
  object.set_instance_class(&hierarchy.derived);
  object.resize_slots(hierarchy.derived.slot_count);

  const PropertyLookup method = object.lookup(qname("foo"));
  EXPECT_EQ(method.kind, LookupKind::Method);
  ASSERT_NE(method.trait, nullptr);
  EXPECT_EQ(method.trait->function->name(), "Derived::foo");
  Value ignored;
  EXPECT_FALSE(object.read_value(qname("foo"), ignored));

  const PropertyLookup accessor = object.lookup(qname("value"));
  EXPECT_EQ(accessor.kind, LookupKind::Accessor);
  ASSERT_TRUE(accessor.getter != nullptr);
  ASSERT_TRUE(accessor.setter != nullptr);
  EXPECT_EQ(accessor.getter->name(), "Derived::get_value");
  EXPECT_EQ(accessor.setter->name(), "Derived::set_value");
  Value ignored;
  EXPECT_FALSE(object.read_value(qname("value"), ignored));
  EXPECT_FALSE(object.write_value(qname("value"), Value::int32(1)));
}

TEST(Avm2Object, KeepsDynamicPropertiesSeparate) {
  Object object;
  EXPECT_FALSE(object.has_property(qname("dyn")));
  EXPECT_TRUE(object.write_value(qname("dyn"), Value::boolean(true)));
  EXPECT_TRUE(object.has_property(qname("dyn")));
  Value value;
  ASSERT_TRUE(object.read_value(qname("dyn"), value));
  EXPECT_TRUE(value.as_boolean());
  EXPECT_TRUE(object.delete_property(qname("dyn")));
  EXPECT_FALSE(object.has_property(qname("dyn")));
  EXPECT_TRUE(object.delete_property(qname("dyn"))) << "deleting a missing name succeeds";
}

TEST(Avm2Object, RefusesMissingWritesOnANonDynamicObject) {
  Object object;
  object.set_dynamic(false);
  EXPECT_FALSE(object.write_value(qname("dyn"), Value::int32(1)));
  EXPECT_FALSE(object.has_property(qname("dyn")));
}

TEST(Avm2Object, WalksThePrototypeChain) {
  auto parent = std::make_shared<Object>();
  auto child = std::make_shared<Object>();
  child->set_prototype(parent);
  ASSERT_TRUE(parent->write_value(qname("inherited"), Value::int32(3)));

  Value value;
  ASSERT_TRUE(child->read_value(qname("inherited"), value));
  EXPECT_EQ(value.as_int32(), 3);

  EXPECT_TRUE(child->write_value(qname("inherited"), Value::int32(4)));
  ASSERT_TRUE(parent->read_value(qname("inherited"), value));
  EXPECT_EQ(value.as_int32(), 4) << "an inherited value property must update where it lives";

  ASSERT_TRUE(child->write_value(qname("own"), Value::int32(5)));
  EXPECT_TRUE(parent->has_property(qname("own")) == false);
}

TEST(Avm2Name, RendersNamespacesAndMatchesSets) {
  EXPECT_EQ(fo4::avm2::render_multiname(qname("x")), "x");
  EXPECT_EQ(fo4::avm2::render_multiname(qname("Sprite", "flash.display")),
            "flash.display::Sprite");
  EXPECT_EQ(fo4::avm2::render_multiname(qname("secret", "Base", NamespaceKind::Private)),
            "private:Base::secret");

  Multiname set;
  set.kind = MultinameKind::QNameSet;
  set.name = "thing";
  set.namespaces.push_back(Namespace{NamespaceKind::Package, ""});
  set.namespaces.push_back(Namespace{NamespaceKind::Private, "Base"});
  EXPECT_TRUE(fo4::avm2::key_matches(set, Key{qname("").namespaces.front(), "thing"}));
  EXPECT_TRUE(fo4::avm2::key_matches(set, Key{Namespace{NamespaceKind::Private, "Base"},
                                              "thing"}));
  EXPECT_FALSE(fo4::avm2::key_matches(set, Key{Namespace{NamespaceKind::Private, "Other"},
                                               "thing"}));

  Multiname runtime_namespace = qname("thing");
  runtime_namespace.kind = MultinameKind::RuntimeNamespace;
  runtime_namespace.namespaces.clear();
  EXPECT_TRUE(fo4::avm2::key_matches(runtime_namespace,
                                     Key{Namespace{NamespaceKind::Private, "Any"}, "thing"}));
  EXPECT_FALSE(fo4::avm2::key_matches(runtime_namespace,
                                      Key{Namespace{NamespaceKind::Private, "Any"}, "other"}));
}

}  // namespace
