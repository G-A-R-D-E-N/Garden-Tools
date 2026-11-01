#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "avm2_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::ExecutionResult;
using fo4::avm2::FunctionObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Namespace;
using fo4::avm2::Runtime;
using fo4::avm2::Value;

Multiname public_name(const char* name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

std::shared_ptr<DisplayObject> make_display(Runtime& runtime, const char* class_name) {
  auto* klass = const_cast<fo4::avm2::Class*>(runtime.find_class(class_name));
  return klass == nullptr ? nullptr : fo4::avm2::make_display_object(runtime, *klass);
}

struct Scene {
  avm2_test::ScriptBuilder script;
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  std::uint32_t logger(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log = script.multiname("log");
    body.findpropstrict(log);
    body.dup();
    body.getproperty(log);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log);
    body.returnvoid();
    return script.method("on_" + tag, body.bytes(), 2);
  }

  std::uint32_t thrower() {
    avm2_test::CodeBuilder body;
    body.pushbyte(1);
    body.throw_op();
    body.returnvoid();
    return script.method("throw_one", body.bytes(), 2);
  }

  void start(const std::vector<std::uint8_t>& init_code,
             fo4::avm2::InterpreterLimits limits = {}) {
    const std::uint32_t init = script.method("init", init_code, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime, limits);
    define_global("log", Value::string(std::string{}));
  }

  void define_global(const char* name, const Value& value) {
    fo4::avm2::Property& property = runtime.global_object()->define_own_property(
        fo4::avm2::primary_key(public_name(name)));
    property.kind = fo4::avm2::PropertyKind::Value;
    property.value = value;
  }

  void listen(DisplayObject& target, const char* type, std::uint32_t method) {
    const auto listener = std::make_shared<FunctionObject>("listener", &file, method);
    fo4::avm2::add_event_listener(target, type, Value::object(listener), false, 0);
  }

  std::string log() {
    Value value;
    return runtime.global_object()->read_value(public_name("log"), value) && value.is_string()
               ? value.as_string()
               : std::string{};
  }
};

TEST(Avm2StageEvents, AddChildAndRemoveChildSendTheStageEventsDownTheSubtree) {
  Scene scene;
  const std::uint32_t box_added = scene.logger("box.added,");
  const std::uint32_t stage_added = scene.logger("stage.added,");
  const std::uint32_t box_stage = scene.logger("box.stage,");
  const std::uint32_t piece_stage = scene.logger("piece.stage,");
  const std::uint32_t leaf_stage = scene.logger("leaf.stage,");
  const std::uint32_t box_removed = scene.logger("box.removed,");
  const std::uint32_t stage_removed = scene.logger("stage.removed,");
  const std::uint32_t box_rfs = scene.logger("box.rfs,");
  const std::uint32_t piece_rfs = scene.logger("piece.rfs,");
  const std::uint32_t leaf_rfs = scene.logger("leaf.rfs,");
  const std::uint32_t stage_name = scene.script.multiname("stage");
  const std::uint32_t box_name = scene.script.multiname("box");
  const std::uint32_t add_child = scene.script.multiname("addChild");
  const std::uint32_t remove_child = scene.script.multiname("removeChild");
  avm2_test::CodeBuilder init;
  init.getlex(stage_name);
  init.getlex(box_name);
  init.callpropvoid(add_child, 1);
  init.getlex(stage_name);
  init.getlex(box_name);
  init.callpropvoid(remove_child, 1);
  init.returnvoid();
  scene.start(init.bytes());

  auto stage = make_display(scene.runtime, "flash.display::Stage");
  auto box = make_display(scene.runtime, "flash.display::Sprite");
  auto piece = make_display(scene.runtime, "flash.display::Sprite");
  auto leaf = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(box, nullptr);
  ASSERT_NE(piece, nullptr);
  ASSERT_NE(leaf, nullptr);
  static_cast<DisplayObjectContainer*>(box.get())->add_child(piece);
  static_cast<DisplayObjectContainer*>(piece.get())->add_child(leaf);
  scene.listen(*box, "added", box_added);
  scene.listen(*stage, "added", stage_added);
  scene.listen(*box, "addedToStage", box_stage);
  scene.listen(*piece, "addedToStage", piece_stage);
  scene.listen(*leaf, "addedToStage", leaf_stage);
  scene.listen(*box, "removed", box_removed);
  scene.listen(*stage, "removed", stage_removed);
  scene.listen(*box, "removedFromStage", box_rfs);
  scene.listen(*piece, "removedFromStage", piece_rfs);
  scene.listen(*leaf, "removedFromStage", leaf_rfs);
  scene.define_global("stage", Value::object(stage));
  scene.define_global("box", Value::object(box));

  const ExecutionResult result = scene.interpreter->run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(scene.log(),
            "box.added,stage.added,box.stage,piece.stage,leaf.stage,"
            "box.removed,stage.removed,box.rfs,piece.rfs,leaf.rfs,");
  EXPECT_EQ(box->parent(), nullptr);
}

TEST(Avm2StageEvents, AddChildAtOffStageSendsAddedButNotAddedToStage) {
  Scene scene;
  const std::uint32_t piece_added = scene.logger("piece.added,");
  const std::uint32_t box_added = scene.logger("box.added,");
  const std::uint32_t piece_stage = scene.logger("piece.stage,");
  const std::uint32_t piece_removed = scene.logger("piece.removed,");
  const std::uint32_t box_removed = scene.logger("box.removed,");
  const std::uint32_t piece_rfs = scene.logger("piece.rfs,");
  const std::uint32_t box_name = scene.script.multiname("box");
  const std::uint32_t piece_name = scene.script.multiname("piece");
  const std::uint32_t add_child_at = scene.script.multiname("addChildAt");
  const std::uint32_t remove_child_at = scene.script.multiname("removeChildAt");
  avm2_test::CodeBuilder init;
  init.getlex(box_name);
  init.getlex(piece_name);
  init.pushbyte(0);
  init.callpropvoid(add_child_at, 2);
  init.getlex(box_name);
  init.pushbyte(0);
  init.callpropvoid(remove_child_at, 1);
  init.returnvoid();
  scene.start(init.bytes());

  auto box = make_display(scene.runtime, "flash.display::Sprite");
  auto piece = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(box, nullptr);
  ASSERT_NE(piece, nullptr);
  scene.listen(*piece, "added", piece_added);
  scene.listen(*box, "added", box_added);
  scene.listen(*piece, "addedToStage", piece_stage);
  scene.listen(*piece, "removed", piece_removed);
  scene.listen(*box, "removed", box_removed);
  scene.listen(*piece, "removedFromStage", piece_rfs);
  scene.define_global("box", Value::object(box));
  scene.define_global("piece", Value::object(piece));

  const ExecutionResult result = scene.interpreter->run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(scene.log(), "piece.added,box.added,piece.removed,box.removed,");
  EXPECT_EQ(piece->parent(), nullptr);
}

std::vector<std::byte> timeline_movie() {
  swf_test::SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(3);
  builder.tag(fo4::swf::tag::kShowFrame);
  swf_test::SwfBuilder place;
  place.u8(0x02);
  place.u16(1);
  place.u16(5);
  builder.tag(fo4::swf::tag::kPlaceObject2, place.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  swf_test::SwfBuilder remove;
  remove.u16(1);
  builder.tag(fo4::swf::tag::kRemoveObject2, remove.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> child_movie() {
  swf_test::SwfBuilder remove;
  remove.u16(1);
  const std::vector<std::byte> sprite = swf_test::sprite_tag(
      2, 2, {{fo4::swf::tag::kShowFrame, {}}, {fo4::swf::tag::kShowFrame, {}}});
  const std::vector<std::byte> place = swf_test::place_full(1, 2, {}, {});
  return swf_test::movie_of(3, {{fo4::swf::tag::kDefineSprite, sprite},
                                {fo4::swf::tag::kPlaceObject2, place},
                                {fo4::swf::tag::kShowFrame, {}},
                                {fo4::swf::tag::kShowFrame, {}},
                                {fo4::swf::tag::kRemoveObject2, remove.body},
                                {fo4::swf::tag::kShowFrame, {}}});
}

TEST(Avm2StageEvents, TimelinePlacementAndRemovalSendTheStageEvents) {
  Scene scene;
  const std::uint32_t added = scene.logger("added,");
  const std::uint32_t removed = scene.logger("removed,");
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(timeline_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  scene.listen(*movie->stage(), "added", added);
  scene.listen(*movie->stage(), "removed", removed);

  movie->advance(*scene.interpreter, 1.0 / 24.0);
  EXPECT_EQ(scene.log(), "added,");
  movie->advance(*scene.interpreter, 1.0 / 24.0);
  EXPECT_EQ(scene.log(), "added,removed,");
}

std::vector<std::byte> two_placements_movie() {
  swf_test::SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(3);
  builder.tag(fo4::swf::tag::kShowFrame);
  for (std::uint16_t depth = 1; depth <= 2; ++depth) {
    swf_test::SwfBuilder place;
    place.u8(0x02);
    place.u16(depth);
    place.u16(5);
    builder.tag(fo4::swf::tag::kPlaceObject2, place.body, false);
  }
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

TEST(Avm2StageEvents, TimelineRemovalWithAFailingRemovedListenerKeepsPlaying) {
  Scene scene;
  const std::uint32_t thrower = scene.thrower();
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(timeline_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  scene.listen(*movie->stage(), "removed", thrower);

  movie->advance(*scene.interpreter, 1.0 / 24.0);
  movie->advance(*scene.interpreter, 1.0 / 24.0);
  EXPECT_TRUE(movie->playing());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 1u);
}

TEST(Avm2StageEvents, TimelinePlacementWithAFailingAddedListenerSendsEveryAddedEvent) {
  Scene scene;
  const std::uint32_t thrower = scene.thrower();
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(two_placements_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  scene.listen(*movie->stage(), "added", thrower);

  movie->advance(*scene.interpreter, 1.0 / 24.0);
  EXPECT_TRUE(movie->playing());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

void log_tag(Scene& scene, avm2_test::CodeBuilder& code, const char* tag) {
  const std::uint32_t log = scene.script.multiname("log");
  code.findpropstrict(log);
  code.dup();
  code.getproperty(log);
  code.pushstring(scene.script.string_of(tag));
  code.add();
  code.initproperty(log);
}

void run_once(Scene& scene, avm2_test::CodeBuilder& code, const char* flag, const char* done) {
  const std::uint32_t name = scene.script.multiname(flag);
  code.getlex(name);
  code.iftrue(done);
  code.pushtrue();
  code.findpropstrict(name);
  code.swap();
  code.setproperty(name);
}

TEST(Avm2StageEvents, ReAddingToTheSameParentFromAnAddedListenerSendsOneEvent) {
  Scene scene;
  const std::uint32_t stage_name = scene.script.multiname("stage");
  const std::uint32_t child_name = scene.script.multiname("child");
  const std::uint32_t add_child = scene.script.multiname("addChild");
  avm2_test::CodeBuilder added_code;
  log_tag(scene, added_code, "added,");
  run_once(scene, added_code, "again", "done");
  added_code.getlex(stage_name);
  added_code.getlex(child_name);
  added_code.callpropvoid(add_child, 1);
  added_code.label("done");
  added_code.returnvoid();
  const std::uint32_t added = scene.script.method("onAdded", added_code.bytes(), 2);
  avm2_test::CodeBuilder init;
  init.getlex(stage_name);
  init.getlex(child_name);
  init.callpropvoid(add_child, 1);
  init.returnvoid();
  scene.start(init.bytes());
  auto stage = make_display(scene.runtime, "flash.display::Stage");
  auto child = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(child, nullptr);
  scene.define_global("again", Value::boolean(false));
  scene.define_global("stage", Value::object(stage));
  scene.define_global("child", Value::object(child));
  scene.listen(*child, "added", added);

  const ExecutionResult result = scene.interpreter->run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(scene.log(), "added,");
}

TEST(Avm2StageEvents, ReRemovingFromARemovedListenerIsANoOp) {
  Scene scene;
  const std::uint32_t stage_name = scene.script.multiname("stage");
  const std::uint32_t child_name = scene.script.multiname("child");
  const std::uint32_t remove_child = scene.script.multiname("removeChild");
  avm2_test::CodeBuilder removed_code;
  log_tag(scene, removed_code, "removed,");
  run_once(scene, removed_code, "again", "done");
  removed_code.getlex(stage_name);
  removed_code.getlex(child_name);
  removed_code.callpropvoid(remove_child, 1);
  removed_code.label("done");
  removed_code.returnvoid();
  const std::uint32_t removed = scene.script.method("onRemoved", removed_code.bytes(), 2);
  avm2_test::CodeBuilder init;
  init.getlex(stage_name);
  init.getlex(child_name);
  init.callpropvoid(remove_child, 1);
  init.returnvoid();
  scene.start(init.bytes());
  auto stage = make_display(scene.runtime, "flash.display::Stage");
  auto child = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(static_cast<DisplayObjectContainer*>(stage.get())->add_child(child));
  scene.define_global("again", Value::boolean(false));
  scene.define_global("stage", Value::object(stage));
  scene.define_global("child", Value::object(child));
  scene.listen(*child, "removed", removed);

  const ExecutionResult result = scene.interpreter->run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(scene.log(), "removed,");
  EXPECT_EQ(child->parent(), nullptr);
}

TEST(Avm2StageEvents, ReparentingSendsRemovedToTheOldParentAndAddedToTheNewOne) {
  Scene scene;
  const std::uint32_t box2_name = scene.script.multiname("box2");
  const std::uint32_t child_name = scene.script.multiname("child");
  const std::uint32_t add_child = scene.script.multiname("addChild");
  const std::uint32_t child_removed = scene.logger("child.removed,");
  const std::uint32_t box1_removed = scene.logger("box1.removed,");
  const std::uint32_t child_rfs = scene.logger("child.rfs,");
  const std::uint32_t child_added = scene.logger("child.added,");
  const std::uint32_t box2_added = scene.logger("box2.added,");
  const std::uint32_t child_stage = scene.logger("child.stage,");
  avm2_test::CodeBuilder init;
  init.getlex(box2_name);
  init.getlex(child_name);
  init.callpropvoid(add_child, 1);
  init.returnvoid();
  scene.start(init.bytes());
  auto stage = make_display(scene.runtime, "flash.display::Stage");
  auto box1 = make_display(scene.runtime, "flash.display::Sprite");
  auto box2 = make_display(scene.runtime, "flash.display::Sprite");
  auto child = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(child, nullptr);
  auto* container = static_cast<DisplayObjectContainer*>(stage.get());
  ASSERT_TRUE(container->add_child(box1));
  ASSERT_TRUE(container->add_child(box2));
  ASSERT_TRUE(static_cast<DisplayObjectContainer*>(box1.get())->add_child(child));
  scene.listen(*child, "removed", child_removed);
  scene.listen(*box1, "removed", box1_removed);
  scene.listen(*child, "removedFromStage", child_rfs);
  scene.listen(*child, "added", child_added);
  scene.listen(*box2, "added", box2_added);
  scene.listen(*child, "addedToStage", child_stage);
  scene.define_global("box2", Value::object(box2));
  scene.define_global("child", Value::object(child));

  const ExecutionResult result = scene.interpreter->run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(scene.log(),
            "child.removed,box1.removed,child.rfs,child.added,box2.added,child.stage,");
  EXPECT_EQ(child->parent(), box2.get());
}

TEST(Avm2StageEvents, ReparentingLoopFromAnAddedListenerStopsWithACallDepthError) {
  Scene scene;
  const std::uint32_t box1_name = scene.script.multiname("box1");
  const std::uint32_t box2_name = scene.script.multiname("box2");
  const std::uint32_t child_name = scene.script.multiname("child");
  const std::uint32_t parent_name = scene.script.multiname("parent");
  const std::uint32_t add_child = scene.script.multiname("addChild");
  avm2_test::CodeBuilder added_code;
  added_code.getlex(child_name);
  added_code.getproperty(parent_name);
  added_code.getlex(box1_name);
  added_code.strictequals();
  added_code.iffalse("to_box1");
  added_code.getlex(box2_name);
  added_code.getlex(child_name);
  added_code.callpropvoid(add_child, 1);
  added_code.returnvoid();
  added_code.label("to_box1");
  added_code.getlex(box1_name);
  added_code.getlex(child_name);
  added_code.callpropvoid(add_child, 1);
  added_code.returnvoid();
  const std::uint32_t added = scene.script.method("bounce", added_code.bytes(), 2);
  avm2_test::CodeBuilder init;
  init.getlex(box1_name);
  init.getlex(child_name);
  init.callpropvoid(add_child, 1);
  init.returnvoid();
  scene.start(init.bytes(), fo4::avm2::InterpreterLimits{.max_call_depth = 16});
  auto stage = make_display(scene.runtime, "flash.display::Stage");
  auto box1 = make_display(scene.runtime, "flash.display::Sprite");
  auto box2 = make_display(scene.runtime, "flash.display::Sprite");
  auto child = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(child, nullptr);
  auto* container = static_cast<DisplayObjectContainer*>(stage.get());
  ASSERT_TRUE(container->add_child(box1));
  ASSERT_TRUE(container->add_child(box2));
  scene.define_global("box1", Value::object(box1));
  scene.define_global("box2", Value::object(box2));
  scene.define_global("child", Value::object(child));
  scene.listen(*child, "added", added);

  const ExecutionResult result = scene.interpreter->run_script(0);
  EXPECT_EQ(result.status, fo4::avm2::InterpreterStatus::CallDepthExceeded) << result.message;
}

TEST(Avm2StageEvents, GotoAndStopInsideEnterFrameSendsNoFrameEvents) {
  Scene scene;
  const std::uint32_t goto_name = scene.script.multiname("gotoAndStop");
  avm2_test::CodeBuilder enter_code;
  log_tag(scene, enter_code, "e,");
  run_once(scene, enter_code, "went", "done");
  enter_code.getlocal0();
  enter_code.pushbyte(2);
  enter_code.callpropvoid(goto_name, 1);
  enter_code.label("done");
  enter_code.returnvoid();
  const std::uint32_t enter = scene.script.method("onEnter", enter_code.bytes(), 2);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());
  scene.define_global("went", Value::boolean(false));

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(timeline_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  scene.listen(*movie->root(), "enterFrame", enter);

  movie->advance(*scene.interpreter, 1.0 / 24.0);
  EXPECT_EQ(scene.log(), "e,");
}

TEST(Avm2StageEvents, FrameScriptThatGoesToItsOwnFrameStopsWithACallDepthError) {
  Scene scene;
  const std::uint32_t goto_name = scene.script.multiname("gotoAndStop");
  avm2_test::CodeBuilder frame_code;
  frame_code.getlocal0();
  frame_code.pushbyte(2);
  frame_code.callpropvoid(goto_name, 1);
  frame_code.returnvoid();
  const std::uint32_t frame_method = scene.script.method("frame2", frame_code.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes(), fo4::avm2::InterpreterLimits{.max_call_depth = 16});

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(timeline_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  movie->root()->set_frame_script(
      2, std::make_shared<FunctionObject>("frame2", &scene.file, frame_method));

  const ExecutionResult jumped = movie->root()->goto_frame(*scene.interpreter, 2, true);
  EXPECT_EQ(jumped.status, fo4::avm2::InterpreterStatus::CallDepthExceeded) << jumped.message;
}

TEST(Avm2StageEvents, ChildScriptThatGoesToAFrameRemovingItKeepsTheSpriteLoopIntact) {
  Scene scene;
  const std::uint32_t goto_name = scene.script.multiname("gotoAndStop");
  const std::uint32_t parent_name = scene.script.multiname("parent");
  avm2_test::CodeBuilder child_code;
  child_code.getlocal0();
  child_code.getproperty(parent_name);
  child_code.pushbyte(3);
  child_code.callpropvoid(goto_name, 1);
  child_code.returnvoid();
  const std::uint32_t child_frame2 = scene.script.method("childFrame2", child_code.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(child_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<DisplayObject> child = movie->root()->child_at(0);
  ASSERT_NE(child, nullptr);
  static_cast<fo4::avm2::MovieClip&>(*child).set_frame_script(
      2, std::make_shared<FunctionObject>("childFrame2", &scene.file, child_frame2));

  movie->advance(*scene.interpreter, 1.0 / 12.0);
  EXPECT_EQ(movie->root()->current_frame(), 3u);
  EXPECT_TRUE(movie->root()->children().empty());
}

TEST(Avm2StageEvents, GotoDoesNotAdvanceExistingChildren) {
  Scene scene;
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(child_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<DisplayObject> child = movie->root()->child_at(0);
  ASSERT_NE(child, nullptr);

  const ExecutionResult jumped = movie->root()->goto_frame(*scene.interpreter, 2, true);
  EXPECT_TRUE(jumped.ok()) << jumped.message;
  EXPECT_EQ(movie->root()->current_frame(), 2u);
  EXPECT_EQ(static_cast<fo4::avm2::MovieClip&>(*child).current_frame(), 1u);
}

std::vector<std::byte> two_sprites_movie() {
  const std::vector<std::byte> sprite = swf_test::sprite_tag(
      2, 3,
      {{fo4::swf::tag::kShowFrame, {}}, {fo4::swf::tag::kShowFrame, {}},
       {fo4::swf::tag::kShowFrame, {}}});
  const std::vector<std::byte> place_a = swf_test::place_full(1, 2, {}, {});
  const std::vector<std::byte> place_c = swf_test::place_full(2, 2, {}, {});
  return swf_test::movie_of(3, {{fo4::swf::tag::kDefineSprite, sprite},
                                {fo4::swf::tag::kPlaceObject2, place_a},
                                {fo4::swf::tag::kShowFrame, {}},
                                {fo4::swf::tag::kShowFrame, {}},
                                {fo4::swf::tag::kPlaceObject2, place_c},
                                {fo4::swf::tag::kShowFrame, {}}});
}

// Regression: a live sprite loop also visits a sibling a nested goto inserts, stepping it on its creation tick.
TEST(Avm2StageEvents, SiblingCreatedByANestedGotoIsNotAdvancedOnItsCreationTick) {
  Scene scene;
  const std::uint32_t goto_name = scene.script.multiname("gotoAndStop");
  const std::uint32_t parent_name = scene.script.multiname("parent");
  avm2_test::CodeBuilder child_code;
  child_code.getlocal0();
  child_code.getproperty(parent_name);
  child_code.pushbyte(3);
  child_code.callpropvoid(goto_name, 1);
  child_code.returnvoid();
  const std::uint32_t child_frame2 = scene.script.method("childFrame2", child_code.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(two_sprites_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<DisplayObject> first = movie->root()->child_at(0);
  ASSERT_NE(first, nullptr);
  static_cast<fo4::avm2::MovieClip&>(*first).set_frame_script(
      2, std::make_shared<FunctionObject>("childFrame2", &scene.file, child_frame2));

  movie->advance(*scene.interpreter, 1.0 / 12.0);
  EXPECT_EQ(movie->root()->current_frame(), 3u);
  ASSERT_EQ(movie->root()->children().size(), 2u);
  const std::shared_ptr<DisplayObject> created = movie->root()->child_at(1);
  EXPECT_EQ(static_cast<fo4::avm2::MovieClip&>(*created).current_frame(), 1u);
}

TEST(Avm2StageEvents, StoppedParentStepsItsPlayingChildOnceAndRerunsNoFrameScript) {
  Scene scene;
  const std::uint32_t stop_name = scene.script.multiname("stop");
  avm2_test::CodeBuilder root_code;
  log_tag(scene, root_code, "root.1,");
  root_code.getlocal0();
  root_code.callpropvoid(stop_name, 0);
  root_code.returnvoid();
  const std::uint32_t root_frame1 = scene.script.method("rootFrame1", root_code.bytes(), 1);
  avm2_test::CodeBuilder child_code;
  log_tag(scene, child_code, "child.2,");
  child_code.returnvoid();
  const std::uint32_t child_frame2 = scene.script.method("childFrame2", child_code.bytes(), 1);
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(child_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<DisplayObject> child = movie->root()->child_at(0);
  ASSERT_NE(child, nullptr);
  movie->root()->set_frame_script(1, std::make_shared<FunctionObject>("rootFrame1", &scene.file,
                                                                      root_frame1));
  static_cast<fo4::avm2::MovieClip&>(*child).set_frame_script(
      2, std::make_shared<FunctionObject>("childFrame2", &scene.file, child_frame2));
  ASSERT_TRUE(movie->root()->run_frame_actions(*scene.interpreter).ok());
  ASSERT_FALSE(movie->root()->playing());

  movie->advance(*scene.interpreter, 1.0 / 12.0);
  EXPECT_EQ(movie->root()->current_frame(), 1u);
  EXPECT_EQ(static_cast<fo4::avm2::MovieClip&>(*child).current_frame(), 2u);
  EXPECT_EQ(scene.log(), "root.1,child.2,");
}

TEST(Avm2StageEvents, StoppedParentDoesNotResetAChildTheScriptMoved) {
  Scene scene;
  avm2_test::CodeBuilder init;
  init.returnvoid();
  scene.start(init.bytes());

  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(child_movie());
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(scene.runtime, *scene.interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<DisplayObject> child = movie->root()->child_at(0);
  ASSERT_NE(child, nullptr);
  movie->root()->set_playing(false);
  child->set_x(99.0);

  movie->advance(*scene.interpreter, 1.0 / 12.0);
  EXPECT_EQ(static_cast<fo4::avm2::MovieClip&>(*child).current_frame(), 2u);
  EXPECT_EQ(child->x(), 99.0);
}

TEST(Avm2StageEvents, StageInvalidateIsAcceptedByTheStage) {
  Scene scene;
  const std::uint32_t stage_name = scene.script.multiname("stage");
  const std::uint32_t invalidate = scene.script.multiname("invalidate");
  avm2_test::CodeBuilder init;
  init.getlex(stage_name);
  init.callpropvoid(invalidate, 0);
  init.returnvoid();
  scene.start(init.bytes());
  auto stage = make_display(scene.runtime, "flash.display::Stage");
  ASSERT_NE(stage, nullptr);
  scene.define_global("stage", Value::object(stage));
  const ExecutionResult result = scene.interpreter->run_script(0);
  EXPECT_TRUE(result.ok()) << result.message;
}

}  // namespace
