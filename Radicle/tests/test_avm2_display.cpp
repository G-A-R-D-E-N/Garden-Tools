#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
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

namespace {

namespace abc = fo4::abc;

using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::MovieClip;
using fo4::avm2::Runtime;
using fo4::avm2::TimelineDefinition;
using fo4::avm2::TimelineFrameLabel;

ExecutionResult run_initializer(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                                std::uint32_t local_count = 1) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

struct LinkedRuntime {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  explicit LinkedRuntime(std::uint32_t local_count = 1) {
    const std::uint32_t method = script.method("init", code.bytes(), local_count);
    file = script.finish(method);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
  }
};

std::shared_ptr<MovieClip> make_clip(Runtime& runtime, std::shared_ptr<TimelineDefinition> timeline) {
  const fo4::avm2::Class* klass = runtime.find_class("flash.display::MovieClip");
  if (klass == nullptr) {
    return nullptr;
  }
  auto object = fo4::avm2::make_display_object(runtime, *const_cast<fo4::avm2::Class*>(klass));
  auto clip = std::dynamic_pointer_cast<MovieClip>(object);
  if (clip != nullptr) {
    clip->set_timeline(std::move(timeline));
  }
  return clip;
}

TEST(Avm2Display, SpriteXPropertyRoundTrips) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  const std::uint32_t x_name = script.multiname("x");
  avm2_test::CodeBuilder code;
  code.getlex(sprite);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushbyte(12);
  code.setproperty(x_name);
  code.getlocal(1);
  code.getproperty(x_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 12.0);
}

TEST(Avm2Display, AddChildIncrementsNumChildren) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  const std::uint32_t add_child = script.multiname("addChild");
  const std::uint32_t num_children = script.multiname("numChildren");
  avm2_test::CodeBuilder code;
  code.getlex(sprite);
  code.construct(0);
  code.setlocal(1);
  code.getlex(sprite);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(1);
  code.getlocal(2);
  code.callproperty(add_child, 1);
  code.pop();
  code.getlocal(1);
  code.getproperty(num_children);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2Display, GetChildAtReturnsTheAddedChild) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  const std::uint32_t add_child_at = script.multiname("addChildAt");
  const std::uint32_t get_child_at = script.multiname("getChildAt");
  avm2_test::CodeBuilder code;
  code.getlex(sprite);
  code.construct(0);
  code.setlocal(1);
  code.getlex(sprite);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(1);
  code.getlocal(2);
  code.pushbyte(0);
  code.callproperty(add_child_at, 2);
  code.pop();
  code.getlocal(1);
  code.pushbyte(0);
  code.callproperty(get_child_at, 1);
  code.getlocal(2);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Display, NewMovieClipStartsAtFrameOneOfOne) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t current_frame = script.multiname("currentFrame");
  const std::uint32_t total_frames = script.multiname("totalFrames");
  avm2_test::CodeBuilder code;
  code.getlex(movie_clip);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(current_frame);
  code.getlocal(1);
  code.getproperty(total_frames);
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 2.0);
}

TEST(Avm2Display, PointConstructionKeepsCoordinates) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t point = script.multiname("Point", "flash.geom");
  const std::uint32_t x_name = script.multiname("x");
  avm2_test::CodeBuilder code;
  code.getlex(point);
  code.pushbyte(3);
  code.pushbyte(4);
  code.construct(2);
  code.getproperty(x_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2Display, DictionaryKeepsDynamicEntries) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t dictionary = script.multiname("Dictionary", "flash.utils");
  const std::uint32_t answer = script.multiname("answer");
  avm2_test::CodeBuilder code;
  code.getlex(dictionary);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushbyte(7);
  code.setproperty(answer);
  code.getlocal(1);
  code.getproperty(answer);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

TEST(Avm2Display, MovieClipAdvancesAndWrapsOverItsTimeline) {
  LinkedRuntime linked;
  auto timeline = std::make_shared<TimelineDefinition>();
  timeline->frame_count = 3;
  timeline->labels.push_back(TimelineFrameLabel{"start", 1});
  timeline->labels.push_back(TimelineFrameLabel{"middle", 2});
  auto clip = make_clip(linked.runtime, timeline);
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->current_frame(), 1u);
  EXPECT_EQ(clip->total_frames(), 3u);

  ExecutionResult advanced = clip->advance_frame(*linked.interpreter);
  ASSERT_TRUE(advanced.ok()) << advanced.message;
  EXPECT_EQ(clip->current_frame(), 2u);
  EXPECT_EQ(timeline->frame_for_label("middle"), 2u);

  advanced = clip->advance_frame(*linked.interpreter);
  ASSERT_TRUE(advanced.ok());
  EXPECT_EQ(clip->current_frame(), 3u);

  advanced = clip->advance_frame(*linked.interpreter);
  ASSERT_TRUE(advanced.ok());
  EXPECT_EQ(clip->current_frame(), 1u);

  ExecutionResult jumped = clip->goto_frame(*linked.interpreter, 2, false);
  ASSERT_TRUE(jumped.ok());
  EXPECT_EQ(clip->current_frame(), 2u);
  EXPECT_FALSE(clip->playing());

  jumped = clip->goto_frame(*linked.interpreter, 3, true);
  ASSERT_TRUE(jumped.ok());
  EXPECT_TRUE(clip->playing());
}

TEST(Avm2Display, FrameScriptRunsWhenItsFrameBecomesCurrent) {
  LinkedRuntime linked;
  avm2_test::CodeBuilder frame_code;
  frame_code.pushbyte(42);
  frame_code.returnvalue();
  const std::uint32_t frame_method = linked.script.method("frame2", frame_code.bytes(), 1);
  const std::uint32_t init_method = linked.script.method("init", linked.code.bytes(), 1);
  abc::AbcFile file = linked.script.finish(init_method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);

  auto timeline = std::make_shared<TimelineDefinition>();
  timeline->frame_count = 3;
  timeline->frame_script_file = &file;
  timeline->frame_scripts.emplace(2, frame_method);
  auto clip = make_clip(runtime, timeline);
  ASSERT_NE(clip, nullptr);

  ExecutionResult first = clip->advance_frame(interpreter);
  ASSERT_TRUE(first.ok()) << first.message;
  ASSERT_TRUE(first.value.is_int32());
  EXPECT_EQ(first.value.as_int32(), 42);
}

TEST(Avm2Display, ChildKnowsItsParentAndRoot) {
  LinkedRuntime linked;
  const fo4::avm2::Class* sprite_class = linked.runtime.find_class("flash.display::Sprite");
  ASSERT_NE(sprite_class, nullptr);
  auto container = std::dynamic_pointer_cast<DisplayObjectContainer>(
      fo4::avm2::make_display_object(linked.runtime, *const_cast<fo4::avm2::Class*>(sprite_class)));
  auto child = std::dynamic_pointer_cast<DisplayObject>(
      fo4::avm2::make_display_object(linked.runtime, *const_cast<fo4::avm2::Class*>(sprite_class)));
  ASSERT_NE(container, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(container->add_child(child));
  EXPECT_EQ(child->parent(), container.get());
  EXPECT_EQ(child->root(), container.get());
  EXPECT_EQ(container->children().size(), 1u);
  EXPECT_TRUE(container->contains(child.get()));
}

}  // namespace
