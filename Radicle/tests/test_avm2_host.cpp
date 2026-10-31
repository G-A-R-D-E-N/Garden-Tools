#include <fo4/avm2/display.hpp>
#include <fo4/avm2/host.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/sound.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::HostCallbacks;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::SnapshotSoundEvent;
using fo4::avm2::SwfRuntime;

struct HostHarness {
  std::map<std::string, std::vector<std::uint8_t>> assets;
  std::vector<std::string> host_calls;
  Runtime runtime;
  Interpreter interpreter{runtime};

  HostHarness() {
    HostCallbacks callbacks;
    callbacks.resolve_asset = [this](std::string_view path)
        -> std::optional<std::vector<std::uint8_t>> {
      const auto found = assets.find(std::string{path});
      if (found == assets.end()) {
        return std::nullopt;
      }
      return found->second;
    };
    callbacks.host_call = [this](std::string_view method,
                                 std::vector<std::string> arguments)
        -> std::optional<std::string> {
      host_calls.push_back(std::string{method});
      host_calls.insert(host_calls.end(), arguments.begin(), arguments.end());
      return std::string{"answer"};
    };
    runtime.host().set_callbacks(std::move(callbacks));
  }

  ExecutionResult run(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                      std::uint32_t local_count = 1) {
    const std::uint32_t method = script.method("init", code.bytes(), local_count);
    abc::AbcFile file = script.finish(method);
    runtime.add_file(file);
    if (!runtime.link().ok()) {
      ExecutionResult failure;
      failure.message = "runtime did not link";
      return failure;
    }
    return interpreter.run_script(0);
  }
};

fo4::swf::SwfFile minimal_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;
  return file;
}

TEST(Avm2Host, ExternalInterfaceForwardsCallsToTheHost) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t external = script.multiname("ExternalInterface", "flash.external");
  const std::uint32_t call_name = script.multiname("call");
  code.getlex(external);
  code.pushstring(script.string_of("GetFoo"));
  code.pushbyte(5);
  code.callproperty(call_name, 2);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "answer");
  ASSERT_EQ(harness.host_calls.size(), 2u);
  EXPECT_EQ(harness.host_calls[0], "GetFoo");
  EXPECT_EQ(harness.host_calls[1], "5");
  EXPECT_EQ(harness.runtime.host().stats().host_calls, 1u);
}

TEST(Avm2Host, ExternalInterfaceAvailableReflectsTheCallback) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t external = script.multiname("ExternalInterface", "flash.external");
  const std::uint32_t available = script.multiname("available");
  code.getlex(external);
  code.getproperty(available);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Host, GetDefinitionByNameResolvesABuiltinClass) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t definition = script.multiname("getDefinitionByName");
  code.pushnull();
  code.getlex(definition);
  code.pushstring(script.string_of("flash.display::Sprite"));
  code.call(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
  const auto& class_object = static_cast<const fo4::avm2::ClassObject&>(*result.value.as_object());
  ASSERT_NE(class_object.represented_class(), nullptr);
  EXPECT_EQ(class_object.represented_class()->name, "flash.display::Sprite");
}

TEST(Avm2Host, QualifiedClassNameReportsTheClass) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t qualified = script.multiname("getQualifiedClassName");
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  code.pushnull();
  code.getlex(qualified);
  code.getlex(sprite);
  code.call(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "flash.display::Sprite");
}

TEST(Avm2Host, UrlLoaderReadsThroughTheAssetResolver) {
  HostHarness harness;
  harness.assets["config.xml"] = std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'};
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t loader_class = script.multiname("URLLoader", "flash.net");
  const std::uint32_t url_name = script.multiname("url");
  const std::uint32_t data_name = script.multiname("data");
  const std::uint32_t load_name = script.multiname("load");
  code.getlex(request_class);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(script.string_of("config.xml"));
  code.setproperty(url_name);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(2);
  code.getlocal(1);
  code.callproperty(load_name, 1);
  code.pop();
  code.getlocal(2);
  code.getproperty(data_name);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "hello");
  EXPECT_EQ(harness.runtime.host().stats().asset_requests, 1u);
}

TEST(Avm2Host, UrlRequestConstructorArgumentIsTheRequestUrl) {
  HostHarness harness;
  harness.assets["config.xml"] = std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'};
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t loader_class = script.multiname("URLLoader", "flash.net");
  const std::uint32_t data_name = script.multiname("data");
  const std::uint32_t load_name = script.multiname("load");
  code.getlex(request_class);
  code.pushstring(script.string_of("config.xml"));
  code.construct(1);
  code.setlocal(1);
  code.getlex(loader_class);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(2);
  code.getlocal(1);
  code.callproperty(load_name, 1);
  code.pop();
  code.getlocal(2);
  code.getproperty(data_name);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "hello");
}

TEST(Avm2Host, UrlRequestWithoutAUrlHasANullUrl) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t url_name = script.multiname("url");
  code.getlex(request_class);
  code.construct(0);
  code.getproperty(url_name);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
}

TEST(Avm2Host, UrlRequestWithANullUrlHasANullUrl) {
  HostHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t url_name = script.multiname("url");
  code.getlex(request_class);
  code.pushnull();
  code.construct(1);
  code.getproperty(url_name);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
}

TEST(Avm2Host, SoundPlayQueuesASnapshotSoundEvent) {
  HostHarness harness;
  harness.assets["sound"] = std::vector<std::uint8_t>{0x52, 0x49, 0x46, 0x58};
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t sound_class = script.multiname("Sound", "flash.media");
  const std::uint32_t request_class = script.multiname("URLRequest", "flash.net");
  const std::uint32_t url_name = script.multiname("url");
  const std::uint32_t load_name = script.multiname("load");
  const std::uint32_t play_name = script.multiname("play");
  code.getlex(sound_class);
  code.construct(0);
  code.setlocal(1);
  code.getlex(request_class);
  code.construct(0);
  code.setlocal(2);
  code.getlocal(2);
  code.pushstring(script.string_of("sound"));
  code.setproperty(url_name);
  code.getlocal(1);
  code.getlocal(2);
  code.callproperty(load_name, 1);
  code.pop();
  code.getlocal(1);
  code.callproperty(play_name, 0);
  code.pop();
  code.returnvoid();

  const std::uint32_t method = script.method("init", code.bytes(), 3);
  abc::AbcFile file = script.finish(method);
  harness.runtime.add_file(file);
  ASSERT_TRUE(harness.runtime.link().ok());
  const fo4::swf::SwfFile movie_file = minimal_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, movie_file);
  ASSERT_NE(movie, nullptr);
  const ExecutionResult result = harness.interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(harness.runtime.sound_events().events().size(), 1u);
  const fo4::avm2::FrameSnapshot snapshot = movie->take_snapshot();
  ASSERT_EQ(snapshot.sounds.size(), 1u);
  EXPECT_EQ(snapshot.sounds[0].kind, SnapshotSoundEvent::Kind::Play);
  EXPECT_NE(snapshot.sounds[0].sound_id, 0);
  EXPECT_NE(harness.runtime.find_sound(snapshot.sounds[0].sound_id), nullptr);
}

}  // namespace
