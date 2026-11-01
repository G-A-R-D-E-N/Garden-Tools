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
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::test::put_string;
using fo4::test::put_u16;
using fo4::test::put_u30;
using fo4::test::put_u8;
using swf_test::SwfBuilder;
using swf_test::sprite_tag;
using swf_test::wrap_body;

/* A document-class ABC must extend MovieClip and its iinit must record that it ran. */
std::vector<std::uint8_t> document_class_abc() {
  std::vector<std::uint8_t> out;
  put_u16(out, 16);
  put_u16(out, 46);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 6);
  put_string(out, "DocumentClass");
  put_string(out, "flash.display");
  put_string(out, "MovieClip");
  put_string(out, "documentReady");
  put_string(out, "yes");
  put_u30(out, 3);
  put_u8(out, 0x16);
  put_u30(out, 0);
  put_u8(out, 0x16);
  put_u30(out, 2);
  put_u30(out, 0);
  put_u30(out, 4);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, 3);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, 4);
  put_u30(out, 2);
  for (int method = 0; method < 2; ++method) {
    put_u30(out, 0);
    put_u30(out, 0);
    put_u30(out, 0);
    put_u8(out, 0);
  }
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 2);
  put_u8(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 2);

  std::vector<std::uint8_t> class_init;
  class_init.push_back(0x47);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, static_cast<std::uint32_t>(class_init.size()));
  out.insert(out.end(), class_init.begin(), class_init.end());
  put_u30(out, 0);
  put_u30(out, 0);

  std::vector<std::uint8_t> constructor_code;
  constructor_code.push_back(0x62);
  put_u30(constructor_code, 0);
  constructor_code.push_back(0x30);
  constructor_code.push_back(0x5D);
  put_u30(constructor_code, 3);
  constructor_code.push_back(0x2C);
  put_u30(constructor_code, 5);
  constructor_code.push_back(0x68);
  put_u30(constructor_code, 3);
  constructor_code.push_back(0x47);
  put_u30(out, 1);
  put_u30(out, 2);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, static_cast<std::uint32_t>(constructor_code.size()));
  out.insert(out.end(), constructor_code.begin(), constructor_code.end());
  put_u30(out, 0);
  put_u30(out, 0);
  return out;
}

std::vector<std::byte> document_class_movie() {
  const std::vector<std::uint8_t> abc_bytes = document_class_abc();
  SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("document");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));

  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(0);
  symbols.text("DocumentClass");

  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

std::string global_string(fo4::avm2::Runtime& runtime, const std::string& name) {
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value value;
  if (!runtime.global_object()->read_value(multiname, value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

}  // namespace

TEST(Avm2DocumentClass, ConstructorRunsOnTheRootClipBeforeFrameOne) {
  const std::vector<std::byte> bytes = document_class_movie();
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);

  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(movie->root(), nullptr);
  ASSERT_NE(movie->root()->display_class(), nullptr);
  EXPECT_EQ(movie->root()->display_class()->name, "DocumentClass");
  EXPECT_EQ(global_string(runtime, "documentReady"), "yes");
}

namespace {

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

std::vector<std::byte> movie_with_blocks(const std::vector<abc::AbcFile>& blocks,
                                         bool document_symbol) {
  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  for (const abc::AbcFile& block : blocks) {
    add_abc_block(builder, block, "block");
  }
  if (document_symbol) {
    SwfBuilder symbols;
    symbols.u16(1);
    symbols.u16(0);
    symbols.text("DocumentClass");
    builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  }
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

abc::AbcFile document_file(const std::string& super_package, const std::string& super_name,
                           const std::string& read_name, const std::string& store_name,
                           const std::string& slot_package = {},
                           const std::string& slot_name = {}) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t document = script.multiname("DocumentClass");
  const std::uint32_t super = script.multiname(super_name, super_package);
  const std::uint32_t read = script.multiname(read_name);
  const std::uint32_t store = script.multiname(store_name);
  const std::uint32_t ping = script.multiname("Ping");
  const std::uint32_t slot_type =
      slot_name.empty() ? 0u : script.multiname(slot_name, slot_package);
  avm2_test::CodeBuilder constructor_code;
  constructor_code.getlocal0();
  constructor_code.pushscope();
  if (!read_name.empty()) {
    constructor_code.getlex(read);
    constructor_code.getglobalscope();
    constructor_code.swap();
    constructor_code.setproperty(store);
  }
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("iinit", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder ping_code;
  ping_code.pushbyte(7);
  ping_code.returnvalue();
  const std::uint32_t ping_method = script.method("Ping", ping_code.bytes(), 1);
  avm2_test::CodeBuilder init_code;
  init_code.returnvoid();
  const std::uint32_t init = script.method("init", init_code.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcTrait ping_trait;
  ping_trait.name = ping;
  ping_trait.kind = abc::AbcTraitKind::Method;
  ping_trait.method_index = ping_method;
  abc::AbcInstanceInfo instance;
  instance.name = document;
  instance.super_index = super;
  instance.iinit = constructor;
  instance.traits.push_back(ping_trait);
  if (!slot_name.empty()) {
    abc::AbcTrait slot;
    slot.name = read;
    slot.kind = abc::AbcTraitKind::Slot;
    slot.type_name = slot_type;
    instance.traits.push_back(slot);
  }
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);
  return file;
}

fo4::avm2::Value global_value(const fo4::avm2::Object& global, const std::string& name) {
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value value;
  global.read_value(multiname, value);
  return value;
}

}  // namespace

TEST(Avm2DocumentClass, StageIsReadableInTheDocumentConstructor) {
  const std::vector<std::byte> bytes = movie_with_blocks(
      {document_file("flash.display", "MovieClip", "stage", "stageSeen")}, true);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const fo4::avm2::Value seen = global_value(*runtime.global_object(), "stageSeen");
  ASSERT_TRUE(seen.is_object());
  EXPECT_EQ(seen.as_object().get(), static_cast<fo4::avm2::Object*>(movie->stage().get()));
}

TEST(Avm2DocumentClass, DocumentClassExtendingSpriteBecomesTheRootAndKeepsItsMethods) {
  const std::vector<std::byte> bytes =
      movie_with_blocks({document_file("flash.display", "Sprite", "", "")}, true);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(movie->root()->display_class(), nullptr);
  EXPECT_EQ(movie->root()->display_class()->name, "DocumentClass");
  const fo4::avm2::ExecutionResult ping = movie->call_root(interpreter, "Ping", {});
  ASSERT_TRUE(ping.ok()) << ping.message;
  ASSERT_TRUE(ping.value.is_int32());
  EXPECT_EQ(ping.value.as_int32(), 7);
}

TEST(Avm2DocumentClass, ScriptOfAnEarlierBlockCanNameAClassFromALaterBlock) {
  avm2_test::ScriptBuilder earlier;
  const std::uint32_t later = earlier.multiname("Later");
  const std::uint32_t seen = earlier.multiname("laterSeen");
  avm2_test::CodeBuilder earlier_code;
  earlier_code.getlex(later);
  earlier_code.getglobalscope();
  earlier_code.swap();
  earlier_code.setproperty(seen);
  earlier_code.returnvoid();
  const std::uint32_t earlier_init = earlier.method("init", earlier_code.bytes(), 1);
  const abc::AbcFile earlier_file = earlier.finish(earlier_init);

  avm2_test::ScriptBuilder later_builder;
  const std::uint32_t later_name = later_builder.multiname("Later");
  const std::uint32_t object = later_builder.multiname("Object");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = later_builder.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder later_init_code;
  later_init_code.returnvoid();
  const std::uint32_t later_init = later_builder.method("init", later_init_code.bytes(), 1);
  abc::AbcFile later_file = later_builder.finish(later_init);
  abc::AbcInstanceInfo instance;
  instance.name = later_name;
  instance.super_index = object;
  instance.iinit = constructor;
  later_file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  later_file.classes.push_back(klass);

  const std::vector<std::byte> bytes = movie_with_blocks({earlier_file, later_file}, false);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  ASSERT_NE(runtime.scripts()[0].global, nullptr);
  const fo4::avm2::Value value = global_value(*runtime.scripts()[0].global, "laterSeen");
  ASSERT_TRUE(value.is_object());
  ASSERT_EQ(value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
  const fo4::avm2::Class* found =
      static_cast<const fo4::avm2::ClassObject&>(*value.as_object()).represented_class();
  ASSERT_NE(found, nullptr);
  EXPECT_FALSE(found->external);
  EXPECT_EQ(found->name, "Later");
}

namespace {

std::vector<std::byte> movie_with_frame_one_child(const abc::AbcFile& document) {
  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  add_abc_block(builder, document, "document");
  builder.tag(fo4::swf::tag::kDefineSprite, sprite_tag(2, 1, {}), false);
  SwfBuilder place;
  place.u8(0x22);
  place.u16(1);
  place.u16(2);
  place.text("List_mc");
  builder.tag(fo4::swf::tag::kPlaceObject2, place.body, false);
  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(0);
  symbols.text("DocumentClass");
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

abc::AbcFile constructed_child_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t marked = script.multiname("MarkedClip");
  const std::uint32_t super = script.multiname("MovieClip", "flash.display");
  const std::uint32_t store = script.multiname("markedChild");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.getlocal0();
  constructor_code.pushscope();
  constructor_code.getlocal0();
  constructor_code.getglobalscope();
  constructor_code.swap();
  constructor_code.setproperty(store);
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("iinit", constructor_code.bytes(), 1);
  const std::uint32_t initialized = script.multiname("classInitRan");
  avm2_test::CodeBuilder class_init;
  class_init.getlocal0();
  class_init.pushscope();
  class_init.pushtrue();
  class_init.getglobalscope();
  class_init.swap();
  class_init.setproperty(initialized);
  class_init.returnvoid();
  const std::uint32_t cinit = script.method("cinit", class_init.bytes(), 1);
  avm2_test::CodeBuilder script_init;
  script_init.returnvoid();
  const std::uint32_t init = script.method("init", script_init.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcInstanceInfo instance;
  instance.name = marked;
  instance.super_index = super;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = cinit;
  file.classes.push_back(klass);
  return file;
}

std::vector<std::byte> movie_with_placed_class(const abc::AbcFile& child) {
  SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  add_abc_block(builder, child, "child");
  builder.tag(fo4::swf::tag::kDefineSprite, sprite_tag(2, 1, {}), false);
  SwfBuilder place;
  place.u8(0x22);
  place.u16(1);
  place.u16(2);
  place.text("List_mc");
  builder.tag(fo4::swf::tag::kPlaceObject2, place.body, false);
  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(2);
  symbols.text("MarkedClip");
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  return wrap_body(builder.body, 10, false, false);
}

}  // namespace

TEST(Avm2DocumentClass, FrameOneChildrenExistWhenTheDocumentConstructorRuns) {
  const std::vector<std::byte> bytes = movie_with_frame_one_child(
      document_file("flash.display", "MovieClip", "List_mc", "childSeen"));
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> placed = movie->root()->child_by_name("List_mc");
  ASSERT_NE(placed, nullptr);
  const fo4::avm2::Value seen = global_value(*runtime.global_object(), "childSeen");
  ASSERT_TRUE(seen.is_object());
  EXPECT_EQ(seen.as_object().get(), static_cast<fo4::avm2::Object*>(placed.get()));
}

TEST(Avm2DocumentClass, PlacedChildFillsADeclaredSlotOfTheSameName) {
  const std::vector<std::byte> bytes = movie_with_frame_one_child(
      document_file("flash.display", "MovieClip", "List_mc", "childSeen", "flash.display",
                    "MovieClip"));
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> placed = movie->root()->child_by_name("List_mc");
  ASSERT_NE(placed, nullptr);
  const fo4::avm2::Value seen = global_value(*runtime.global_object(), "childSeen");
  ASSERT_TRUE(seen.is_object());
  EXPECT_EQ(seen.as_object().get(), static_cast<fo4::avm2::Object*>(placed.get()));
}

TEST(Avm2DocumentClass, PlacedChildIsNotStoredInASlotOfAnUnrelatedType) {
  const std::vector<std::byte> bytes = movie_with_frame_one_child(
      document_file("flash.display", "MovieClip", "List_mc", "childSeen", "flash.display",
                    "Shape"));
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  EXPECT_FALSE(global_value(*runtime.global_object(), "childSeen").is_object());
}

TEST(Avm2DocumentClass, PlacedChildRunsItsClassConstructor) {
  const std::vector<std::byte> bytes = movie_with_placed_class(constructed_child_file());
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  ASSERT_TRUE(runtime.link().ok());
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file);
  ASSERT_NE(movie, nullptr) << fo4::avm2::SwfRuntime::last_error();
  const std::shared_ptr<fo4::avm2::DisplayObject> placed = movie->root()->child_by_name("List_mc");
  ASSERT_NE(placed, nullptr);
  const fo4::avm2::Value marked = global_value(*runtime.global_object(), "markedChild");
  ASSERT_TRUE(marked.is_object());
  EXPECT_EQ(marked.as_object().get(), static_cast<fo4::avm2::Object*>(placed.get()));
  const fo4::avm2::Value initialized = global_value(*runtime.global_object(), "classInitRan");
  EXPECT_TRUE(initialized.is_boolean() && initialized.as_boolean());
}
