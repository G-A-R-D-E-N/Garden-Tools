#include <fo4/avm2/amf.hpp>
#include <fo4/avm2/host.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
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

struct StorageHarness {
  std::map<std::string, std::vector<std::uint8_t>> storage;
  Runtime runtime;
  Interpreter interpreter{runtime};

  StorageHarness() {
    HostCallbacks callbacks;
    callbacks.read_storage = [this](std::string_view name)
        -> std::optional<std::vector<std::uint8_t>> {
      const auto found = storage.find(std::string{name});
      if (found == storage.end()) return std::nullopt;
      return found->second;
    };
    callbacks.write_storage = [this](std::string_view name, std::vector<std::uint8_t> bytes) {
      storage[std::string{name}] = std::move(bytes);
      return true;
    };
    runtime.host().set_callbacks(std::move(callbacks));
  }

  ExecutionResult run(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                      std::uint32_t local_count) {
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

TEST(Avm2Shared, FlushWritesAnAmf3BodyAndGetLocalReadsItBack) {
  StorageHarness writer;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shared_class = script.multiname("SharedObject", "flash.net");
  const std::uint32_t get_local = script.multiname("getLocal");
  const std::uint32_t data_name = script.multiname("data");
  const std::uint32_t count_name = script.multiname("count");
  const std::uint32_t flush_name = script.multiname("flush");
  code.getlex(shared_class);
  code.pushstring(script.string_of("save"));
  code.callproperty(get_local, 1);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(data_name);
  code.pushbyte(5);
  code.setproperty(count_name);
  code.getlocal(1);
  code.callproperty(flush_name, 0);
  code.pop();
  code.returnvoid();
  const ExecutionResult write_result = writer.run(script, code, 2);
  ASSERT_TRUE(write_result.ok()) << write_result.message;
  ASSERT_EQ(writer.storage.count("save"), 1u);
  EXPECT_GT(writer.storage["save"].size(), 0u);
  EXPECT_EQ(writer.runtime.host().stats().storage_writes, 1u);

  fo4::avm2::Value decoded;
  ASSERT_TRUE(fo4::avm2::decode_amf3(writer.storage["save"], decoded));
  ASSERT_TRUE(decoded.is_object());
  fo4::avm2::Multiname count;
  count.name = "count";
  count.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value value;
  ASSERT_TRUE(decoded.as_object()->read_value(count, value));
  ASSERT_TRUE(value.is_number_like());
  EXPECT_DOUBLE_EQ(value.as_number(), 5.0);

  StorageHarness reader;
  reader.storage = writer.storage;
  avm2_test::ScriptBuilder read_script;
  avm2_test::CodeBuilder read_code;
  const std::uint32_t reader_shared = read_script.multiname("SharedObject", "flash.net");
  const std::uint32_t reader_get_local = read_script.multiname("getLocal");
  const std::uint32_t reader_data = read_script.multiname("data");
  const std::uint32_t reader_count = read_script.multiname("count");
  read_code.getlex(reader_shared);
  read_code.pushstring(read_script.string_of("save"));
  read_code.callproperty(reader_get_local, 1);
  read_code.getproperty(reader_data);
  read_code.getproperty(reader_count);
  read_code.returnvalue();
  const ExecutionResult read_result = reader.run(read_script, read_code, 1);
  ASSERT_TRUE(read_result.ok()) << read_result.message;
  ASSERT_TRUE(read_result.value.is_number_like());
  EXPECT_DOUBLE_EQ(read_result.value.as_number(), 5.0);
  EXPECT_EQ(reader.runtime.host().stats().storage_reads, 1u);
}

TEST(Avm2Shared, RejectsAnUnsafeStorageName) {
  StorageHarness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shared_class = script.multiname("SharedObject", "flash.net");
  const std::uint32_t get_local = script.multiname("getLocal");
  code.getlex(shared_class);
  code.pushstring(script.string_of("bad/name"));
  code.callproperty(get_local, 1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_null());
  EXPECT_EQ(harness.runtime.host().stats().storage_reads, 0u);
}

}  // namespace
