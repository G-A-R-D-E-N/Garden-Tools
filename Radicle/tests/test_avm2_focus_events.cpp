#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::FunctionObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::Property;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

Multiname public_name(const char* name) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

void reset_log(Runtime& runtime) {
  Property& property =
      runtime.global_object()->define_own_property(fo4::avm2::primary_key(public_name("log")));
  property.kind = fo4::avm2::PropertyKind::Value;
  property.value = Value::string(std::string{});
}

std::string read_log(Runtime& runtime) {
  Value value;
  if (!runtime.global_object()->read_value(public_name("log"), value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

void define_global(Runtime& runtime, const char* name) {
  Property& property =
      runtime.global_object()->define_own_property(fo4::avm2::primary_key(public_name(name)));
  property.kind = fo4::avm2::PropertyKind::Value;
  property.value = Value::null();
}

const fo4::avm2::Object* related_of(Runtime& runtime, const char* holder) {
  Value event;
  if (!runtime.global_object()->read_value(public_name(holder), event) || !event.is_object()) {
    return nullptr;
  }
  Value related;
  if (!event.as_object()->read_value(public_name("relatedObject"), related) ||
      !related.is_object()) {
    return nullptr;
  }
  return related.as_object().get();
}

struct Fixture {
  avm2_test::ScriptBuilder script;
  std::vector<std::uint32_t> methods;
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  std::uint32_t append_body(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log_name = log_multiname();
    body.findpropstrict(log_name);
    body.dup();
    body.getproperty(log_name);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log_name);
    body.returnvoid();
    return script.method("append_" + tag, body.bytes(), 2);
  }

  std::uint32_t store_body(const std::string& tag, const std::string& holder) {
    avm2_test::CodeBuilder body;
    const std::uint32_t holder_name = script.multiname(holder);
    body.findpropstrict(holder_name);
    body.getlocal(1);
    body.initproperty(holder_name);
    body.returnvoid();
    return script.method("store_" + tag, body.bytes(), 2);
  }

  std::uint32_t log_multiname() { return script.multiname("log"); }

  void finish() {
    const std::uint32_t init = script.method("init", std::vector<std::uint8_t>{0x47}, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
    reset_log(runtime);
  }

  std::shared_ptr<FunctionObject> function(std::uint32_t index) {
    return std::make_shared<FunctionObject>("listener", &file, index);
  }
};

fo4::avm2::Class* class_for(Runtime& runtime, const char* name) {
  return const_cast<fo4::avm2::Class*>(runtime.find_class(name));
}

std::shared_ptr<DisplayObject> make_sprite(Runtime& runtime) {
  auto klass = class_for(runtime, "flash.display::Sprite");
  if (klass == nullptr) {
    return nullptr;
  }
  return std::static_pointer_cast<DisplayObject>(
      fo4::avm2::make_display_object(runtime, *klass));
}

using avm2_test::set_box_hit_area;

fo4::swf::SwfFile minimal_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;
  return file;
}

TEST(Avm2FocusEvents, RollOverAndRollOutFollowTheAncestorBoundary) {
  Fixture fixture;
  const std::uint32_t outer_over_body = fixture.append_body("o");
  const std::uint32_t inner_over_body = fixture.append_body("i");
  const std::uint32_t outer_out_body = fixture.append_body("O");
  const std::uint32_t inner_out_body = fixture.append_body("I");
  const std::uint32_t other_over_body = fixture.append_body("s");
  fixture.finish();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto* root = static_cast<DisplayObjectContainer*>(movie->root().get());
  auto outer = make_sprite(fixture.runtime);
  auto inner = make_sprite(fixture.runtime);
  auto other = make_sprite(fixture.runtime);
  ASSERT_NE(outer, nullptr);
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(other, nullptr);
  outer->set_width(100.0);
  outer->set_height(100.0);
  inner->set_width(50.0);
  inner->set_height(50.0);
  other->set_x(200.0);
  other->set_width(50.0);
  other->set_height(50.0);
  set_box_hit_area(*outer);
  set_box_hit_area(*inner);
  set_box_hit_area(*other);
  ASSERT_TRUE(root->add_child(outer));
  ASSERT_TRUE(static_cast<DisplayObjectContainer*>(outer.get())->add_child(inner));
  ASSERT_TRUE(root->add_child(other));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*outer, "rollOver",
                                            Value::object(fixture.function(outer_over_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*inner, "rollOver",
                                            Value::object(fixture.function(inner_over_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*outer, "rollOut",
                                            Value::object(fixture.function(outer_out_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*inner, "rollOut",
                                            Value::object(fixture.function(inner_out_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*other, "rollOver",
                                            Value::object(fixture.function(other_over_body)),
                                            false, 0));
  movie->mouse_move(*fixture.interpreter, 10.0, 10.0);
  EXPECT_EQ(read_log(fixture.runtime), "oi");
  movie->mouse_move(*fixture.interpreter, 20.0, 20.0);
  EXPECT_EQ(read_log(fixture.runtime), "oi");
  movie->mouse_move(*fixture.interpreter, 70.0, 70.0);
  EXPECT_EQ(read_log(fixture.runtime), "oiI");
  movie->mouse_move(*fixture.interpreter, 220.0, 10.0);
  EXPECT_EQ(read_log(fixture.runtime), "oiIOs");
}

TEST(Avm2FocusEvents, MouseDownOnATabEnabledObjectMovesStageFocus) {
  Fixture fixture;
  const std::uint32_t in_body = fixture.append_body("i");
  const std::uint32_t out_body = fixture.append_body("o");
  fixture.finish();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto* root = static_cast<DisplayObjectContainer*>(movie->root().get());
  auto first = make_sprite(fixture.runtime);
  auto second = make_sprite(fixture.runtime);
  auto plain = make_sprite(fixture.runtime);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(plain, nullptr);
  first->set_width(50.0);
  first->set_height(50.0);
  first->set_tab_enabled(true);
  second->set_x(100.0);
  second->set_width(50.0);
  second->set_height(50.0);
  second->set_tab_enabled(true);
  plain->set_x(200.0);
  plain->set_width(50.0);
  plain->set_height(50.0);
  set_box_hit_area(*first);
  set_box_hit_area(*second);
  set_box_hit_area(*plain);
  ASSERT_TRUE(root->add_child(first));
  ASSERT_TRUE(root->add_child(second));
  ASSERT_TRUE(root->add_child(plain));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*first, "focusIn",
                                            Value::object(fixture.function(in_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*first, "focusOut",
                                            Value::object(fixture.function(out_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*second, "focusIn",
                                            Value::object(fixture.function(in_body)), false, 0));
  movie->mouse_event(*fixture.interpreter, 10.0, 10.0, 0, true);
  EXPECT_EQ(movie->stage()->focus().get(), first.get());
  EXPECT_EQ(read_log(fixture.runtime), "i");
  movie->mouse_event(*fixture.interpreter, 110.0, 10.0, 0, true);
  EXPECT_EQ(movie->stage()->focus().get(), second.get());
  EXPECT_EQ(read_log(fixture.runtime), "ioi");
  movie->mouse_event(*fixture.interpreter, 210.0, 10.0, 0, true);
  EXPECT_EQ(movie->stage()->focus().get(), second.get());
  EXPECT_EQ(read_log(fixture.runtime), "ioi");
}

TEST(Avm2FocusEvents, RollStateSurvivesRemovingTheHoveredChild) {
  Fixture fixture;
  const std::uint32_t outer_over_body = fixture.append_body("o");
  const std::uint32_t inner_out_body = fixture.append_body("I");
  const std::uint32_t outer_out_body = fixture.append_body("O");
  fixture.finish();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto* root = static_cast<DisplayObjectContainer*>(movie->root().get());
  auto outer = make_sprite(fixture.runtime);
  auto inner = make_sprite(fixture.runtime);
  ASSERT_NE(outer, nullptr);
  ASSERT_NE(inner, nullptr);
  outer->set_width(100.0);
  outer->set_height(100.0);
  inner->set_width(50.0);
  inner->set_height(50.0);
  set_box_hit_area(*outer);
  set_box_hit_area(*inner);
  ASSERT_TRUE(root->add_child(outer));
  ASSERT_TRUE(static_cast<DisplayObjectContainer*>(outer.get())->add_child(inner));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*outer, "rollOver",
                                            Value::object(fixture.function(outer_over_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*outer, "rollOut",
                                            Value::object(fixture.function(outer_out_body)),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*inner, "rollOut",
                                            Value::object(fixture.function(inner_out_body)),
                                            false, 0));
  movie->mouse_move(*fixture.interpreter, 10.0, 10.0);
  EXPECT_EQ(read_log(fixture.runtime), "o");
  ASSERT_TRUE(static_cast<DisplayObjectContainer*>(outer.get())->remove_child(inner));
  movie->mouse_move(*fixture.interpreter, 70.0, 70.0);
  EXPECT_EQ(read_log(fixture.runtime), "oI");
  movie->mouse_move(*fixture.interpreter, 500.0, 500.0);
  EXPECT_EQ(read_log(fixture.runtime), "oIO");
}

TEST(Avm2FocusEvents, RollEventsCarryTheObjectMovedFromOrTo) {
  Fixture fixture;
  const std::uint32_t out_body = fixture.store_body("out", "out_event");
  const std::uint32_t over_body = fixture.store_body("over", "over_event");
  fixture.finish();
  define_global(fixture.runtime, "out_event");
  define_global(fixture.runtime, "over_event");
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto* root = static_cast<DisplayObjectContainer*>(movie->root().get());
  auto outer = make_sprite(fixture.runtime);
  auto other = make_sprite(fixture.runtime);
  ASSERT_NE(outer, nullptr);
  ASSERT_NE(other, nullptr);
  outer->set_width(100.0);
  outer->set_height(100.0);
  other->set_x(200.0);
  other->set_width(50.0);
  other->set_height(50.0);
  set_box_hit_area(*outer);
  set_box_hit_area(*other);
  ASSERT_TRUE(root->add_child(outer));
  ASSERT_TRUE(root->add_child(other));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*outer, "rollOut",
                                            Value::object(fixture.function(out_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*other, "rollOver",
                                            Value::object(fixture.function(over_body)), false, 0));
  movie->mouse_move(*fixture.interpreter, 10.0, 10.0);
  movie->mouse_move(*fixture.interpreter, 220.0, 10.0);
  EXPECT_EQ(related_of(fixture.runtime, "out_event"), other.get());
  EXPECT_EQ(related_of(fixture.runtime, "over_event"), outer.get());
}

}  // namespace
