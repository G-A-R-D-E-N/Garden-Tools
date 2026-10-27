#include <fo4/avm2/class.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;

ExecutionResult run_program(Runtime& runtime, avm2_test::ScriptBuilder& script,
                            avm2_test::CodeBuilder& code, std::uint32_t local_count) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

ExecutionResult read_static(Runtime& runtime, const char* klass, const char* package,
                            const char* name) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlex(script.multiname(klass, package));
  code.getproperty(script.multiname(name));
  code.returnvalue();
  return run_program(runtime, script, code, 1);
}

TEST(Avm2SystemClasses, TextFieldAutoSizeConstantsAreTheFlashStrings) {
  const char* names[] = {"NONE", "LEFT", "RIGHT", "CENTER"};
  const char* values[] = {"none", "left", "right", "center"};
  for (std::size_t index = 0; index < 4; ++index) {
    Runtime runtime;
    const ExecutionResult result = read_static(runtime, "TextFieldAutoSize", "flash.text",
                                               names[index]);
    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_TRUE(result.value.is_string());
    EXPECT_EQ(result.value.as_string(), values[index]);
  }
}

TEST(Avm2SystemClasses, CapabilitiesReportsAFixedWindowsStandalonePlayer) {
  Runtime runtime;
  const ExecutionResult language = read_static(runtime, "Capabilities", "flash.system", "language");
  ASSERT_TRUE(language.ok()) << language.message;
  EXPECT_EQ(language.value.as_string(), "en");
  Runtime os_runtime;
  const ExecutionResult os = read_static(os_runtime, "Capabilities", "flash.system", "os");
  ASSERT_TRUE(os.ok()) << os.message;
  EXPECT_EQ(os.value.as_string(), "Windows");
  Runtime player_runtime;
  const ExecutionResult player_type =
      read_static(player_runtime, "Capabilities", "flash.system", "playerType");
  ASSERT_TRUE(player_type.ok()) << player_type.message;
  EXPECT_EQ(player_type.value.as_string(), "StandAlone");
}

TEST(Avm2SystemClasses, SecuritySandboxTypeIsTheApplicationSandbox) {
  Runtime runtime;
  const ExecutionResult sandbox =
      read_static(runtime, "Security", "flash.system", "sandboxType");
  ASSERT_TRUE(sandbox.ok()) << sandbox.message;
  EXPECT_EQ(sandbox.value.as_string(), "application");
  Runtime remote_runtime;
  const ExecutionResult remote = read_static(remote_runtime, "Security", "flash.system", "REMOTE");
  ASSERT_TRUE(remote.ok()) << remote.message;
  EXPECT_EQ(remote.value.as_string(), "remote");
}

TEST(Avm2SystemClasses, BitmapFilterQualityConstantsAreOneTwoThree) {
  const char* names[] = {"LOW", "MEDIUM", "HIGH"};
  const double values[] = {1.0, 2.0, 3.0};
  for (std::size_t index = 0; index < 3; ++index) {
    Runtime runtime;
    const ExecutionResult result =
        read_static(runtime, "BitmapFilterQuality", "flash.filters", names[index]);
    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_TRUE(result.value.is_number_like());
    EXPECT_EQ(result.value.as_number(), values[index]);
  }
}

TEST(Avm2SystemClasses, FontEnumerateFontsReturnsAnEmptyArray) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlex(script.multiname("Font", "flash.text"));
  code.callproperty(script.multiname("enumerateFonts"), 0);
  code.getproperty(script.multiname("length"));
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 0.0);
}

TEST(Avm2SystemClasses, FontRegisterFontAndSecurityAllowDomainAreNoOps) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlex(script.multiname("Font", "flash.text"));
  code.pushstring(script.string_of("font.ttf"));
  code.callproperty(script.multiname("registerFont"), 1);
  code.pop();
  code.getlex(script.multiname("Security", "flash.system"));
  code.pushstring(script.string_of("*"));
  code.callproperty(script.multiname("allowDomain"), 1);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
}

TEST(Avm2SystemClasses, TextFieldAutoSizeAcceptsTheConstantAndReadsItBack) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t auto_size = script.multiname("autoSize");
  code.getlex(script.multiname("TextField", "flash.text"));
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getlex(script.multiname("TextFieldAutoSize", "flash.text"));
  code.getproperty(script.multiname("LEFT"));
  code.setproperty(auto_size);
  code.getlocal(1);
  code.getproperty(auto_size);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "left");
}

TEST(Avm2SystemClasses, EventDispatcherImplementsTheIEventDispatcherInterface) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  const fo4::avm2::Class* interface_class = runtime.find_class("flash.events::IEventDispatcher");
  const fo4::avm2::Class* dispatcher = runtime.find_class("flash.events::EventDispatcher");
  ASSERT_NE(interface_class, nullptr);
  ASSERT_NE(dispatcher, nullptr);
  EXPECT_TRUE(interface_class->is_interface);
  EXPECT_TRUE(dispatcher->implements_interface(interface_class));
}

}  // namespace
