#include <fo4/abc/abc.hpp>
#include <fo4/avm2/domain.hpp>
#include <fo4/avm2/host.hpp>
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
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ApplicationDomainObject;
using fo4::avm2::ExecutionResult;
using fo4::avm2::HostCallbacks;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

struct DomainHarness {
  std::map<std::string, std::vector<std::uint8_t>> assets;
  Runtime runtime;
  Interpreter interpreter{runtime};
  abc::AbcFile main_file;

  DomainHarness() {
    HostCallbacks callbacks;
    callbacks.resolve_asset = [this](std::string_view path)
        -> std::optional<std::vector<std::uint8_t>> {
      const auto found = assets.find(std::string{path});
      if (found == assets.end()) {
        return std::nullopt;
      }
      return found->second;
    };
    runtime.host().set_callbacks(std::move(callbacks));
  }

  ExecutionResult run(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                      std::uint32_t local_count) {
    const std::uint32_t method = script.method("init", code.bytes(), local_count);
    main_file = script.finish(method);
    runtime.add_file(main_file);
    if (!runtime.link().ok()) {
      ExecutionResult failure;
      failure.message = "runtime did not link";
      return failure;
    }
    return interpreter.run_script(0);
  }
};

std::vector<std::uint8_t> second_class_abc() {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder body;
  body.returnvoid();
  const std::uint32_t init = script.method("init", body.bytes(), 0);
  const std::uint32_t constructor = script.method("second", body.bytes(), 1);
  const std::uint32_t class_init = script.method("cinit", body.bytes(), 1);
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t class_name = script.multiname("Second");
  abc::AbcFile file = script.finish(init);
  abc::AbcInstanceInfo instance;
  instance.name = class_name;
  instance.super_index = object_name;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), abc::AbcStatus::Ok);
  return bytes;
}

std::vector<std::uint8_t> second_movie() {
  const std::vector<std::uint8_t> abc_bytes = second_class_abc();
  swf_test::SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("second");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));
  swf_test::SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kShowFrame);
  builder.tag(fo4::swf::tag::kEnd);
  const std::vector<std::byte> bytes = swf_test::wrap_body(builder.body, 10, false, false);
  return std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   reinterpret_cast<const std::uint8_t*>(bytes.data()) +
                                       bytes.size());
}

ExecutionResult run_domain_expression(DomainHarness& harness, avm2_test::ScriptBuilder& script,
                                      avm2_test::CodeBuilder& code) {
  return harness.run(script, code, 3);
}

TEST(Avm2Domain, CurrentDomainIsOneObject) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t current = script.multiname("currentDomain");
  code.getlex(domain_class);
  code.getproperty(current);
  code.getlex(domain_class);
  code.getproperty(current);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = run_domain_expression(harness, script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
  ASSERT_NE(harness.runtime.current_domain(), nullptr);
  EXPECT_EQ(harness.runtime.current_domain()->parent(), nullptr);
}

TEST(Avm2Domain, NewDomainDefaultsToTheCurrentDomainAsParent) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  code.getlex(domain_class);
  code.construct(0);
  code.returnvalue();
  const ExecutionResult result = run_domain_expression(harness, script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto child = std::dynamic_pointer_cast<ApplicationDomainObject>(result.value.as_object());
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(child->parent().get(), harness.runtime.current_domain().get());
}

TEST(Avm2Domain, ParentDomainPropertyReturnsTheParent) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t parent = script.multiname("parentDomain");
  code.getlex(domain_class);
  code.construct(0);
  code.getproperty(parent);
  code.returnvalue();
  const ExecutionResult result = run_domain_expression(harness, script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object().get(), harness.runtime.current_domain().get());
}

TEST(Avm2Domain, GetDefinitionResolvesABuiltinThroughTheDomain) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t current = script.multiname("currentDomain");
  const std::uint32_t definition = script.multiname("getDefinition");
  code.getlex(domain_class);
  code.getproperty(current);
  code.pushstring(script.string_of("flash.display::Sprite"));
  code.callproperty(definition, 1);
  code.returnvalue();
  const ExecutionResult result = run_domain_expression(harness, script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
  const auto& class_object = static_cast<const fo4::avm2::ClassObject&>(*result.value.as_object());
  ASSERT_NE(class_object.represented_class(), nullptr);
  EXPECT_EQ(class_object.represented_class()->name, "flash.display::Sprite");
}

TEST(Avm2Domain, HasDefinitionIsFalseForAnUnknownName) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t current = script.multiname("currentDomain");
  const std::uint32_t has = script.multiname("hasDefinition");
  code.getlex(domain_class);
  code.getproperty(current);
  code.pushstring(script.string_of("NoSuchClass"));
  code.callproperty(has, 1);
  code.returnvalue();
  const ExecutionResult result = run_domain_expression(harness, script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_FALSE(result.value.as_boolean());
}

ExecutionResult run_loader_script(DomainHarness& harness, bool with_context,
                                  bool compare_info = false) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t context_class = script.multiname("LoaderContext", "flash.system");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t loader_class = script.multiname("Loader", "flash.display");
  const std::uint32_t domain_name = script.multiname("applicationDomain");
  const std::uint32_t url_name = script.multiname("url");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t content_info = script.multiname("contentLoaderInfo");
  const std::uint32_t current_domain = script.multiname("currentDomain");
  if (with_context) {
    code.getlex(domain_class);
    code.construct(0);
    code.setlocal(1);
    code.getlex(context_class);
    code.construct(0);
    code.setlocal(2);
    code.getlocal(2);
    code.getlocal(1);
    code.setproperty(domain_name);
  }
  code.getlex(request_class);
  code.construct(0);
  code.setlocal(3);
  code.getlocal(3);
  code.pushstring(script.string_of("second.swf"));
  code.setproperty(url_name);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(4);
  code.getlocal(4);
  code.getlocal(3);
  if (with_context) {
    code.getlocal(2);
    code.callproperty(load_name, 2);
  } else {
    code.callproperty(load_name, 1);
  }
  code.pop();
  if (compare_info) {
    code.getlocal(4);
    code.getproperty(content_info);
    code.getproperty(domain_name);
    if (with_context) {
      code.getlocal(1);
    } else {
      code.getlex(domain_class);
      code.getproperty(current_domain);
    }
    code.strictequals();
    code.returnvalue();
  } else if (with_context) {
    code.getlocal(1);
    code.returnvalue();
  } else {
    code.returnvoid();
  }
  return harness.run(script, code, 5);
}

TEST(Avm2Domain, LoaderContextDomainReceivesTheSecondMovieDefinitions) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loader_script(harness, true);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto child = std::dynamic_pointer_cast<ApplicationDomainObject>(result.value.as_object());
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(child->parent().get(), harness.runtime.current_domain().get());
  const fo4::avm2::Class* second = harness.runtime.find_domain_definition(*child, "Second");
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->name, "Second");
  EXPECT_EQ(harness.runtime.find_domain_definition(*harness.runtime.current_domain(), "Second"),
            nullptr);
  EXPECT_EQ(harness.runtime.host().stats().asset_requests, 1u);
}

TEST(Avm2Domain, LoaderWithoutAContextLoadsIntoTheCurrentDomain) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loader_script(harness, false);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_NE(harness.runtime.find_domain_definition(*harness.runtime.current_domain(), "Second"),
            nullptr);
}

TEST(Avm2Domain, ContentLoaderInfoReportsTheContextDomain) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loader_script(harness, true, true);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Domain, ContentLoaderInfoReportsTheRootDomainWithoutAContext) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loader_script(harness, false, true);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Domain, LoadedContentLoaderInfoIsTheLoadersContentLoaderInfo) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t loader_class = script.multiname("Loader", "flash.display");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t url_name = script.multiname("url");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t child_at = script.multiname("getChildAt");
  const std::uint32_t content_info = script.multiname("contentLoaderInfo");
  const std::uint32_t loader_info = script.multiname("loaderInfo");
  code.getlex(request_class);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(script.string_of("second.swf"));
  code.setproperty(url_name);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(2);
  code.getlocal(1);
  code.callproperty(load_name, 1);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.callproperty(child_at, 1);
  code.getproperty(loader_info);
  code.getlocal(2);
  code.getproperty(content_info);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Domain, HeldContentLoaderInfoTakesTheDomainOfALaterLoad) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t context_class = script.multiname("LoaderContext", "flash.system");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t loader_class = script.multiname("Loader", "flash.display");
  const std::uint32_t domain_name = script.multiname("applicationDomain");
  const std::uint32_t url_name = script.multiname("url");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t content_info = script.multiname("contentLoaderInfo");
  code.getlex(domain_class);
  code.construct(0);
  code.setlocal(1);
  code.getlex(context_class);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(2);
  code.getlocal(1);
  code.setproperty(domain_name);
  code.getlex(request_class);
  code.construct(0);
  code.setlocal(3);
  code.getlocal(3);
  code.pushstring(script.string_of("second.swf"));
  code.setproperty(url_name);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(4);
  code.getlocal(4);
  code.getproperty(content_info);
  code.setlocal(5);
  code.getlocal(4);
  code.getlocal(3);
  code.getlocal(2);
  code.callproperty(load_name, 2);
  code.pop();
  code.getlocal(5);
  code.getproperty(domain_name);
  code.getlocal(1);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 6);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Domain, LoaderContextConstructorStoresTheDomainArgument) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t domain_class = script.multiname("ApplicationDomain", "flash.system");
  const std::uint32_t context_class = script.multiname("LoaderContext", "flash.system");
  const std::uint32_t domain_name = script.multiname("applicationDomain");
  code.getlex(domain_class);
  code.construct(0);
  code.setlocal(1);
  code.getlex(context_class);
  code.pushfalse();
  code.getlocal(1);
  code.construct(2);
  code.getproperty(domain_name);
  code.getlocal(1);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Domain, AddCallbackStoresAScriptFunctionThatTheHostCanInvoke) {
  DomainHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder callback;
  callback.pushbyte(3);
  callback.pushbyte(4);
  callback.add();
  callback.returnvalue();
  const std::uint32_t callback_method = script.method("onReady", callback.bytes(), 1);
  avm2_test::CodeBuilder code;
  const std::uint32_t external = script.multiname("ExternalInterface", "flash.external");
  const std::uint32_t add_callback = script.multiname("addCallback");
  code.getlex(external);
  code.pushstring(script.string_of("onReady"));
  code.newfunction(callback_method);
  code.callproperty(add_callback, 2);
  code.returnvoid();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  const std::shared_ptr<fo4::avm2::Object> stored = harness.runtime.external_callback("onReady");
  ASSERT_NE(stored, nullptr);
  ASSERT_TRUE(stored->is_callable());
  const ExecutionResult answer = harness.interpreter.call_function(
      static_cast<const fo4::avm2::FunctionObject&>(*stored), Value::undefined(), {}, 0);
  ASSERT_TRUE(answer.ok()) << answer.message;
  ASSERT_TRUE(answer.value.is_number_like());
  EXPECT_EQ(answer.value.as_number(), 7.0);
}

TEST(Avm2Domain, ExternalInterfaceCallRoutesToANamedHostFunction) {
  DomainHarness harness;
  harness.runtime.host().install_function(
      "Sum", [](std::vector<std::string> arguments) -> std::optional<std::string> {
        return "sum:" + arguments[0] + "," + arguments[1];
      });
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t external = script.multiname("ExternalInterface", "flash.external");
  const std::uint32_t call_name = script.multiname("call");
  code.getlex(external);
  code.pushstring(script.string_of("Sum"));
  code.pushbyte(2);
  code.pushbyte(5);
  code.callproperty(call_name, 3);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "sum:2,5");
  EXPECT_EQ(harness.runtime.host().stats().host_calls, 1u);
  EXPECT_EQ(harness.runtime.host().stats().host_call_misses, 0u);
}

TEST(Avm2Domain, LoaderCompleteIsSentToTheContentLoaderInfo) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  avm2_test::ScriptBuilder script;
  const std::uint32_t loader_class = script.multiname("Loader", "flash.display");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t content_info = script.multiname("contentLoaderInfo");
  const std::uint32_t listen = script.multiname("addEventListener");
  const std::uint32_t flag = script.multiname("completed");
  avm2_test::CodeBuilder handler_code;
  handler_code.getglobalscope();
  handler_code.pushtrue();
  handler_code.setproperty(flag);
  handler_code.returnvoid();
  const std::uint32_t handler = script.method("onComplete", handler_code.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.getglobalscope();
  code.pushfalse();
  code.setproperty(flag);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(content_info);
  code.setlocal(2);
  code.getlocal(2);
  code.pushstring(script.string_of("complete"));
  code.newfunction(handler);
  code.callpropvoid(listen, 2);
  code.getlex(request_class);
  code.pushstring(script.string_of("second.swf"));
  code.construct(1);
  code.setlocal(3);
  code.getlocal(1);
  code.getlocal(3);
  code.callpropvoid(load_name, 1);
  code.getglobalscope();
  code.getproperty(flag);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

ExecutionResult run_loaded_content(DomainHarness& harness, bool through_info) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t loader_class = script.multiname("Loader", "flash.display");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t info_name = script.multiname("contentLoaderInfo");
  const std::uint32_t content_name = script.multiname("content");
  avm2_test::CodeBuilder code;
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(1);
  code.getlex(request_class);
  code.pushstring(script.string_of("second.swf"));
  code.construct(1);
  code.setlocal(2);
  code.getlocal(1);
  code.getlocal(2);
  code.callpropvoid(load_name, 1);
  code.getlocal(1);
  if (through_info) {
    code.getproperty(info_name);
  }
  code.getproperty(content_name);
  code.returnvalue();
  return harness.run(script, code, 3);
}

TEST(Avm2Domain, LoaderContentIsTheLoadedMovie) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loaded_content(harness, false);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_object());
}

TEST(Avm2Domain, LoaderInfoContentIsTheLoadedMovie) {
  DomainHarness harness;
  harness.assets["second.swf"] = second_movie();
  const ExecutionResult result = run_loaded_content(harness, true);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_object());
}

}  // namespace
