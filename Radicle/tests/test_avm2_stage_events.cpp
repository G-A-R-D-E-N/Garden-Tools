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

  void start(const std::vector<std::uint8_t>& init_code) {
    const std::uint32_t init = script.method("init", init_code, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
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

}  // namespace
