#include "fo4/abc/abc.hpp"
#include "fo4/avm2/display.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/object.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "abc_test_support.hpp"
#include "avm2_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using swf_test::SwfBuilder;
using swf_test::sprite_tag;
using swf_test::wrap_body;

using ConstructorBody = void (*)(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder&);
using FrameBody = void (*)(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder&);

void add_abc_block(SwfBuilder& builder, const abc::AbcFile& file, const char* name) {
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), abc::AbcStatus::Ok);
  SwfBuilder payload;
  payload.u32(0);
  payload.text(name);
  payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                           bytes.size()));
  builder.tag(fo4::swf::tag::kDoAbc2, payload.body, true);
}

fo4::avm2::Value global_value(const fo4::avm2::Object& global, const std::string& name) {
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value value;
  global.read_value(multiname, value);
  return value;
}

double number_property(const fo4::avm2::Object& object, const std::string& name) {
  const fo4::avm2::Value value = global_value(object, name);
  return value.is_number_like() ? value.as_number() : -1.0;
}

void count_on_this(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                   const std::string& name) {
  const std::uint32_t counter = script.multiname(name);
  code.getlocal0();
  code.getlocal0();
  code.getproperty(counter);
  code.convert_i();
  code.pushbyte(1);
  code.add();
  code.setproperty(counter);
}

void store_property(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                    const std::string& property, const std::string& name) {
  code.getlocal0();
  code.getproperty(script.multiname(property));
  code.getglobalscope();
  code.swap();
  code.setproperty(script.multiname(name));
}

void record_placement(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  store_property(script, code, "name", "recordedName");
  store_property(script, code, "x", "recordedX");
  store_property(script, code, "parent", "recordedParent");
  store_property(script, code, "stage", "recordedStage");
  store_property(script, code, "numChildren", "recordedChildren");
}

void write_placement(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.getlocal0();
  code.pushbyte(37);
  code.setproperty(script.multiname("x"));
  code.getlocal0();
  code.pushfalse();
  code.setproperty(script.multiname("visible"));
}

void throw_on_construction(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder& code) {
  code.pushbyte(1);
  code.throw_op();
}

void count_then_throw_two(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  count_on_this(script, code, "frameRuns");
  code.pushbyte(2);
  code.throw_op();
}

void no_constructor_body(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder&) {}

void goto_frame_one_on_construction(avm2_test::ScriptBuilder& script,
                                    avm2_test::CodeBuilder& code) {
  code.getlocal0();
  code.pushbyte(1);
  code.callpropvoid(script.multiname("gotoAndStop"), 1);
}

void listen_for_added_to_stage(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  avm2_test::CodeBuilder handler;
  const std::uint32_t added_runs = script.multiname("addedRuns");
  handler.getglobalscope();
  handler.dup();
  handler.getproperty(added_runs);
  handler.convert_i();
  handler.pushbyte(1);
  handler.add();
  handler.setproperty(added_runs);
  handler.returnvoid();
  const std::uint32_t on_added = script.method("onAdded", handler.bytes(), 1);
  code.getlocal0();
  code.pushstring(script.string_of("addedToStage"));
  code.newfunction(on_added);
  code.callpropvoid(script.multiname("addEventListener"), 2);
}

void count_frame(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  count_on_this(script, code, "frameRuns");
}

void record_parent_in_frame(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  count_on_this(script, code, "frameRuns");
  store_property(script, code, "parent", "frameParent");
}

abc::AbcFile clip_file(const std::string& name, ConstructorBody body,
                       FrameBody frame_body = count_frame) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t klass = script.multiname(name);
  const std::uint32_t super = script.multiname("MovieClip", "flash.display");
  const std::uint32_t frame_name = script.multiname("frame1");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.getlocal0();
  constructor_code.pushscope();
  count_on_this(script, constructor_code, "constructedRuns");
  body(script, constructor_code);
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("iinit", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder frame_code;
  frame_code.getlocal0();
  frame_code.pushscope();
  frame_body(script, frame_code);
  frame_code.returnvoid();
  const std::uint32_t frame = script.method("frame1", frame_code.bytes(), 1);
  avm2_test::CodeBuilder class_init;
  class_init.returnvoid();
  const std::uint32_t cinit = script.method("cinit", class_init.bytes(), 1);
  avm2_test::CodeBuilder script_init;
  script_init.returnvoid();
  const std::uint32_t init = script.method("init", script_init.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcTrait frame_trait;
  frame_trait.name = frame_name;
  frame_trait.kind = abc::AbcTraitKind::Method;
  frame_trait.method_index = frame;
  abc::AbcInstanceInfo instance;
  instance.name = klass;
  instance.super_index = super;
  instance.iinit = constructor;
  instance.traits.push_back(frame_trait);
  file.instances.push_back(instance);
  abc::AbcClassInfo class_info;
  class_info.cinit = cinit;
  file.classes.push_back(class_info);
  return file;
}

std::vector<std::byte> placement(std::uint16_t depth, std::uint16_t character,
                                 const std::string& name = {},
                                 const std::vector<std::byte>& matrix = {}) {
  SwfBuilder place;
  std::uint32_t flags = 0x02;
  if (!matrix.empty()) {
    flags |= 0x04;
  }
  if (!name.empty()) {
    flags |= 0x20;
  }
  place.u8(flags);
  place.u16(depth);
  place.u16(character);
  place.bytes(matrix);
  if (!name.empty()) {
    place.text(name);
  }
  return place.body;
}

std::vector<std::byte> sprite_with_frame(std::uint16_t id,
                                         const std::vector<std::vector<std::byte>>& places) {
  std::vector<swf_test::TagPayload> tags;
  for (const std::vector<std::byte>& place : places) {
    tags.emplace_back(fo4::swf::tag::kPlaceObject2, place);
  }
  tags.emplace_back(fo4::swf::tag::kShowFrame, std::vector<std::byte>{});
  return sprite_tag(id, 1, tags);
}

std::vector<std::byte> clip_movie(
    const std::vector<abc::AbcFile>& blocks, const std::vector<std::vector<std::byte>>& sprites,
    const std::vector<std::vector<std::vector<std::byte>>>& frames,
    const std::vector<std::pair<std::uint16_t, std::string>>& symbols) {
  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(static_cast<std::uint32_t>(frames.size()));
  for (const abc::AbcFile& block : blocks) {
    add_abc_block(builder, block, "block");
  }
  for (const std::vector<std::byte>& sprite : sprites) {
    builder.tag(fo4::swf::tag::kDefineSprite, sprite, false);
  }
  SwfBuilder table;
  table.u16(static_cast<std::uint32_t>(symbols.size()));
  for (const auto& [id, name] : symbols) {
    table.u16(id);
    table.text(name);
  }
  builder.tag(fo4::swf::tag::kSymbolClass, table.body, false);
  for (const auto& frame : frames) {
    for (const std::vector<std::byte>& place : frame) {
      builder.tag(fo4::swf::tag::kPlaceObject2, place, false);
    }
    builder.tag(fo4::swf::tag::kShowFrame);
  }
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

}  // namespace

TEST(Avm2PlacedConstruction, ConstructorSeesItsNameAndXFromThePlacement) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Recorder", record_placement)}, {},
      {{placement(1, 2, "hero", swf_test::matrix_full(65536, 65536, 0, 0, 800, 0))}},
      {{2, "Recorder"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(movie->root()->child_by_name("hero"), nullptr);
  const fo4::avm2::Value name_value = global_value(*runtime.global_object(), "recordedName");
  EXPECT_TRUE(name_value.is_string() && name_value.as_string() == "hero");
  EXPECT_EQ(number_property(*runtime.global_object(), "recordedX"), 40.0);
  EXPECT_TRUE(global_value(*runtime.global_object(), "recordedParent").is_null());
  EXPECT_TRUE(global_value(*runtime.global_object(), "recordedStage").is_null());
}

TEST(Avm2PlacedConstruction, ConstructorWriteSurvivesOnARowWithoutMatrixOrVisibility) {
  const std::vector<std::byte> bytes = clip_movie({clip_file("Writer", write_placement)}, {},
                                                  {{placement(1, 2, "plain")}, {}},
                                                  {{2, "Writer"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> placed = movie->root()->child_by_name("plain");
  ASSERT_NE(placed, nullptr);
  EXPECT_EQ(placed->x(), 37.0);
  EXPECT_FALSE(placed->visible());
  movie->advance(interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->current_frame(), 2u);
  EXPECT_EQ(placed->x(), 37.0);
  EXPECT_FALSE(placed->visible());
}

TEST(Avm2PlacedConstruction, ConstructorSeesItsFrameOneChild) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Recorder", record_placement)},
      {sprite_with_frame(2, {placement(1, 3, "inner")}), sprite_with_frame(3, {})},
      {{placement(1, 2, "outer")}}, {{2, "Recorder"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> outer = movie->root()->child_by_name("outer");
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(number_property(*runtime.global_object(), "recordedChildren"), 1.0);
  const auto container = std::dynamic_pointer_cast<fo4::avm2::DisplayObjectContainer>(outer);
  ASSERT_NE(container, nullptr);
  EXPECT_NE(container->child_by_name("inner"), nullptr);
}

TEST(Avm2PlacedConstruction, PlacedClipIsConstructedOncePerPlacementAcrossFrames) {
  const std::vector<std::byte> bytes = clip_movie({clip_file("Clip", no_constructor_body)},
                                                  {sprite_with_frame(2, {})},
                                                  {{placement(1, 2), placement(2, 2)}},
                                                  {{2, "Clip"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const auto placed = movie->root()->children();
  ASSERT_EQ(placed.size(), 2u);
  for (const auto& child : placed) {
    EXPECT_EQ(number_property(*child, "constructedRuns"), 1.0);
    EXPECT_EQ(number_property(*child, "frameRuns"), 1.0);
  }
  movie->advance(interpreter, 1.0);
  for (const auto& child : placed) {
    EXPECT_EQ(number_property(*child, "constructedRuns"), 1.0);
    EXPECT_EQ(number_property(*child, "frameRuns"), 25.0);
  }
}

TEST(Avm2PlacedConstruction, NestedClipIsConstructedAndScriptedOnceWhenItsParentIsPlaced) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Clip", no_constructor_body)},
      {sprite_with_frame(2, {placement(1, 3, "inner")}), sprite_with_frame(3, {})},
      {{placement(1, 2, "outer")}}, {{2, "Clip"}, {3, "Clip"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> outer = movie->root()->child_by_name("outer");
  ASSERT_NE(outer, nullptr);
  const auto container = std::dynamic_pointer_cast<fo4::avm2::DisplayObjectContainer>(outer);
  ASSERT_NE(container, nullptr);
  const std::shared_ptr<fo4::avm2::DisplayObject> inner = container->child_by_name("inner");
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(number_property(*outer, "constructedRuns"), 1.0);
  EXPECT_EQ(number_property(*outer, "frameRuns"), 1.0);
  EXPECT_EQ(number_property(*inner, "constructedRuns"), 1.0);
  EXPECT_EQ(number_property(*inner, "frameRuns"), 1.0);
  movie->advance(interpreter, 1.0);
  EXPECT_EQ(number_property(*outer, "constructedRuns"), 1.0);
  EXPECT_EQ(number_property(*inner, "constructedRuns"), 1.0);
}

TEST(Avm2PlacedConstruction, FailingConstructorLeavesNothingAttached) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Clip", no_constructor_body), clip_file("Thrower", throw_on_construction)},
      {sprite_with_frame(2, {}), sprite_with_frame(4, {})},
      {{placement(1, 2, "ok")}, {placement(2, 4, "bad")}}, {{2, "Clip"}, {4, "Thrower"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(movie->root()->child_by_name("ok"), nullptr);
  EXPECT_EQ(movie->root()->child_by_name("bad"), nullptr);
  movie->advance(interpreter, 1.0);
  EXPECT_FALSE(movie->frame_error().empty());
  EXPECT_NE(movie->root()->child_by_name("ok"), nullptr);
  EXPECT_EQ(movie->root()->child_by_name("bad"), nullptr);
}

TEST(Avm2PlacedConstruction, PlacedChildFrameOneScriptSeesItsParentAfterAttach) {
  const std::vector<std::byte> bytes =
      clip_movie({clip_file("Clip", no_constructor_body, record_parent_in_frame)},
                 {sprite_with_frame(2, {})}, {{placement(1, 2, "hero")}}, {{2, "Clip"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> hero = movie->root()->child_by_name("hero");
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(number_property(*hero, "frameRuns"), 1.0);
  const fo4::avm2::Value parent = global_value(*runtime.global_object(), "frameParent");
  EXPECT_TRUE(parent.is_object() && parent.as_object() == movie->root());
}

TEST(Avm2PlacedConstruction, ConstructorGotoRunsThePlacedFrameScriptOnce) {
  const std::vector<std::byte> bytes =
      clip_movie({clip_file("Clip", goto_frame_one_on_construction)}, {sprite_with_frame(2, {})},
                 {{placement(1, 2, "hero")}}, {{2, "Clip"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> hero = movie->root()->child_by_name("hero");
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(number_property(*hero, "constructedRuns"), 1.0);
  EXPECT_EQ(number_property(*hero, "frameRuns"), 1.0);
}

TEST(Avm2PlacedConstruction, DocumentConstructorGotoRunsFrameOneScriptOnce) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Document", goto_frame_one_on_construction)}, {}, {{}}, {{0, "Document"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  EXPECT_EQ(number_property(*movie->root(), "constructedRuns"), 1.0);
  EXPECT_EQ(number_property(*movie->root(), "frameRuns"), 1.0);
}

TEST(Avm2PlacedConstruction, FailingSiblingStillFinishesTheEarlierChildInTheSameFrame) {
  const std::vector<std::byte> bytes = clip_movie(
      {clip_file("Listener", listen_for_added_to_stage, count_then_throw_two),
       clip_file("Thrower", throw_on_construction)},
      {sprite_with_frame(2, {}), sprite_with_frame(4, {})},
      {{}, {placement(1, 2, "ok"), placement(2, 4, "bad")}}, {{2, "Listener"}, {4, "Thrower"}});
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  movie->advance(interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->current_frame(), 2u);
  const std::shared_ptr<fo4::avm2::DisplayObject> ok = movie->root()->child_by_name("ok");
  ASSERT_NE(ok, nullptr);
  EXPECT_EQ(movie->root()->child_by_name("bad"), nullptr);
  EXPECT_EQ(movie->frame_error(), "uncaught exception: 1");
  EXPECT_EQ(number_property(*runtime.global_object(), "addedRuns"), 1.0);
  EXPECT_EQ(number_property(*ok, "frameRuns"), 1.0);
}
