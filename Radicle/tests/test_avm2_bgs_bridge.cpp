#include "fo4/abc/abc.hpp"
#include "fo4/avm2/host.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/object.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "avm2_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::HostCallbacks;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::Namespace;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::SwfRuntimeOptions;
using fo4::avm2::Value;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

Multiname public_name(const std::string& name) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

abc::AbcFile document_class_file(bool sets_code_object) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder constructor;
  if (sets_code_object) {
    constructor.getlocal0();
    constructor.pushscope();
    constructor.getlocal0();
    constructor.newobject(0);
    constructor.initproperty(script.multiname("BGSCodeObj"));
  }
  constructor.returnvoid();
  const std::uint32_t document = script.multiname("DocumentClass");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t constructor_method =
      script.method("DocumentClass", constructor.bytes(), 1);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);

  abc::AbcInstanceInfo instance;
  instance.name = document;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = document;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

std::vector<std::byte> document_movie(bool sets_code_object) {
  abc::AbcFile file = document_class_file(sets_code_object);
  std::vector<std::uint8_t> abc_bytes;
  EXPECT_EQ(file.serialize(abc_bytes), abc::AbcStatus::Ok);
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

abc::AbcFile call_script(bool absent) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t code_object = script.multiname("codeObj");
  const std::uint32_t name = script.multiname(absent ? "noSuchFunction" : "playSound");
  const std::uint32_t apply = script.multiname("apply");
  const std::uint32_t argument = script.string_of("x");
  avm2_test::CodeBuilder code;
  code.getlex(code_object);
  code.getproperty(name);
  if (!absent) {
    code.pushnull();
    code.pushstring(argument);
    code.newarray(1);
    code.callproperty(apply, 2);
  }
  code.returnvalue();
  const std::uint32_t init = script.method("init", code.bytes(), 1);
  return script.finish(init);
}

struct BgsHarness {
  Runtime runtime;
  Interpreter interpreter{runtime};
  std::vector<std::pair<std::string, std::vector<std::string>>> calls;

  BgsHarness() {
    HostCallbacks callbacks;
    callbacks.host_call = [this](std::string_view method, std::vector<std::string> arguments)
        -> std::optional<std::string> {
      calls.emplace_back(std::string{method}, std::move(arguments));
      return std::string{"answer"};
    };
    runtime.host().set_callbacks(std::move(callbacks));
  }

  std::shared_ptr<SwfRuntime> open(bool sets_code_object, const SwfRuntimeOptions& options) {
    EXPECT_TRUE(runtime.link().ok());
    const std::vector<std::byte> bytes = document_movie(sets_code_object);
    const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
    EXPECT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
    return SwfRuntime::create(runtime, interpreter, read.file, options);
  }

  void publish_code_object(const SwfRuntime& movie) {
    Value code_object;
    ASSERT_TRUE(movie.root()->read_value(public_name("BGSCodeObj"), code_object));
    fo4::avm2::Key key;
    key.name = "codeObj";
    runtime.global_object()->define_own_property(key).value = code_object;
  }

  ExecutionResult run(const abc::AbcFile& file) {
    if (!runtime.add_file(file).ok() ||
        !runtime.link_added_file(runtime.file_count() - 1).ok()) {
      ExecutionResult failure;
      failure.message = "the call script did not link";
      return failure;
    }
    return interpreter.run_script(runtime.scripts().size() - 1);
  }
};

SwfRuntimeOptions bgs_options() {
  SwfRuntimeOptions options;
  options.bgs_host = true;
  return options;
}

TEST(Avm2BgsBridge, BgsCodeObjApplyReachesTheHostWithItsName) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const abc::AbcFile call = call_script(false);
  const ExecutionResult result = harness.run(call);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(harness.calls.size(), 1u);
  EXPECT_EQ(harness.calls[0].first, "bgs.playSound");
  EXPECT_EQ(harness.calls[0].second, std::vector<std::string>{"x"});
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "answer");
}

TEST(Avm2BgsBridge, AbsentBgsFunctionReadsUndefinedWithoutThrowing) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  harness.publish_code_object(*movie);

  const abc::AbcFile call = call_script(true);
  const ExecutionResult result = harness.run(call);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
  EXPECT_TRUE(harness.calls.empty());
}

TEST(Avm2BgsBridge, MovieWithoutBgsCodeObjCreatesWithNoCodeObject) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(false, bgs_options());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  Value code_object;
  EXPECT_FALSE(movie->root()->read_value(public_name("BGSCodeObj"), code_object));
}

TEST(Avm2BgsBridge, DisabledHostLeavesBgsCodeObjWithoutFunctions) {
  BgsHarness harness;
  const std::shared_ptr<SwfRuntime> movie = harness.open(true, SwfRuntimeOptions{});
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  Value code_object;
  ASSERT_TRUE(movie->root()->read_value(public_name("BGSCodeObj"), code_object));
  ASSERT_TRUE(code_object.is_object());
  EXPECT_TRUE(code_object.as_object()->own_properties().empty());
}

}  // namespace
