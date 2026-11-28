#include <fo4/avm2/bytearray.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/domain.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ByteArrayObject;
using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;
using fo4::avm2::to_number;

constexpr std::uint8_t kLi8 = 0x35;
constexpr std::uint8_t kLi16 = 0x36;
constexpr std::uint8_t kLi32 = 0x37;
constexpr std::uint8_t kLf32 = 0x38;
constexpr std::uint8_t kLf64 = 0x39;
constexpr std::uint8_t kSi8 = 0x3A;
constexpr std::uint8_t kSi16 = 0x3B;
constexpr std::uint8_t kSi32 = 0x3C;
constexpr std::uint8_t kSf32 = 0x3D;
constexpr std::uint8_t kSf64 = 0x3E;

using MemoryPtr = std::shared_ptr<ByteArrayObject>;

MemoryPtr make_memory(std::size_t size) {
  MemoryPtr memory = std::make_shared<ByteArrayObject>(nullptr);
  memory->resize(size);
  return memory;
}

ExecutionResult run_with_memory(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                                MemoryPtr memory) {
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  runtime.current_domain()->set_domain_memory(std::move(memory));
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

ExecutionResult run_load(std::uint8_t opcode, std::int32_t address, const MemoryPtr& memory) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(address));
  code.op(opcode);
  code.returnvalue();
  return run_with_memory(script, code, memory);
}

ExecutionResult run_store_int(std::uint8_t opcode, std::int32_t value, std::int32_t address,
                              const MemoryPtr& memory) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(value));
  code.pushint(script.add_int(address));
  code.op(opcode);
  code.returnvoid();
  return run_with_memory(script, code, memory);
}

ExecutionResult run_store_number(std::uint8_t opcode, double value, std::int32_t address,
                                 const MemoryPtr& memory) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushdouble(script.add_double(value));
  code.pushint(script.add_int(address));
  code.op(opcode);
  code.returnvoid();
  return run_with_memory(script, code, memory);
}

TEST(Avm2DomainMemory, SiThirtyTwoThenLiThirtyTwoRoundTripsANegativeInt) {
  const MemoryPtr memory = make_memory(8);
  const ExecutionResult stored = run_store_int(kSi32, -2, 4, memory);
  ASSERT_TRUE(stored.ok()) << stored.message;
  EXPECT_EQ(memory->byte_at(4), 0xFE);
  EXPECT_EQ(memory->byte_at(5), 0xFF);
  EXPECT_EQ(memory->byte_at(6), 0xFF);
  EXPECT_EQ(memory->byte_at(7), 0xFF);
  const ExecutionResult loaded = run_load(kLi32, 4, memory);
  ASSERT_TRUE(loaded.ok()) << loaded.message;
  EXPECT_EQ(loaded.value.as_int32(), -2);
}

TEST(Avm2DomainMemory, StoresAreLittleEndianSoLiEightReadsTheBytesInOrder) {
  const MemoryPtr memory = make_memory(8);
  const ExecutionResult stored = run_store_int(kSi32, 0x01020304, 0, memory);
  ASSERT_TRUE(stored.ok()) << stored.message;
  EXPECT_EQ(memory->byte_at(0), 0x04);
  EXPECT_EQ(memory->byte_at(1), 0x03);
  EXPECT_EQ(memory->byte_at(2), 0x02);
  EXPECT_EQ(memory->byte_at(3), 0x01);
  const std::int32_t expected[] = {4, 3, 2, 1};
  for (std::int32_t address = 0; address < 4; ++address) {
    const ExecutionResult loaded = run_load(kLi8, address, memory);
    ASSERT_TRUE(loaded.ok()) << loaded.message;
    EXPECT_EQ(loaded.value.as_int32(), expected[address]);
  }
  const ExecutionResult low_half = run_load(kLi16, 0, memory);
  ASSERT_TRUE(low_half.ok()) << low_half.message;
  EXPECT_EQ(low_half.value.as_int32(), 0x0304);
  const ExecutionResult high_half = run_load(kLi16, 2, memory);
  ASSERT_TRUE(high_half.ok()) << high_half.message;
  EXPECT_EQ(high_half.value.as_int32(), 0x0102);
}

TEST(Avm2DomainMemory, SiEightAndSiSixteenStoreOnlyTheLowBytesAndLiZeroExtends) {
  const MemoryPtr memory = make_memory(8);
  ASSERT_TRUE(run_store_int(kSi8, 0x1FF, 2, memory).ok());
  EXPECT_EQ(memory->byte_at(2), 0xFF);
  EXPECT_EQ(memory->byte_at(3), 0x00);
  const ExecutionResult byte = run_load(kLi8, 2, memory);
  ASSERT_TRUE(byte.ok()) << byte.message;
  EXPECT_EQ(byte.value.as_int32(), 255);

  ASSERT_TRUE(run_store_int(kSi16, 0x12345, 0, memory).ok());
  EXPECT_EQ(memory->byte_at(0), 0x45);
  EXPECT_EQ(memory->byte_at(1), 0x23);
  const ExecutionResult half = run_load(kLi16, 0, memory);
  ASSERT_TRUE(half.ok()) << half.message;
  EXPECT_EQ(half.value.as_int32(), 9029);

  ASSERT_TRUE(run_store_int(kSi16, -1, 4, memory).ok());
  const ExecutionResult all_ones = run_load(kLi16, 4, memory);
  ASSERT_TRUE(all_ones.ok()) << all_ones.message;
  EXPECT_EQ(all_ones.value.as_int32(), 65535);
}

TEST(Avm2DomainMemory, SfThirtyTwoNarrowsToFloatAndLfThirtyTwoWidensItBack) {
  const MemoryPtr memory = make_memory(8);
  ASSERT_TRUE(run_store_number(kSf32, 0.1, 0, memory).ok());
  EXPECT_EQ(memory->byte_at(0), 0xCD);
  EXPECT_EQ(memory->byte_at(1), 0xCC);
  EXPECT_EQ(memory->byte_at(2), 0xCC);
  EXPECT_EQ(memory->byte_at(3), 0x3D);
  const ExecutionResult loaded = run_load(kLf32, 0, memory);
  ASSERT_TRUE(loaded.ok()) << loaded.message;
  EXPECT_EQ(to_number(loaded.value), 0.100000001490116119384765625);
  EXPECT_NE(to_number(loaded.value), 0.1);
}

TEST(Avm2DomainMemory, SfSixtyFourThenLfSixtyFourRoundTripsADouble) {
  const MemoryPtr memory = make_memory(8);
  ASSERT_TRUE(run_store_number(kSf64, 0.1, 0, memory).ok());
  EXPECT_EQ(memory->byte_at(0), 0x9A);
  EXPECT_EQ(memory->byte_at(6), 0xB9);
  EXPECT_EQ(memory->byte_at(7), 0x3F);
  const ExecutionResult loaded = run_load(kLf64, 0, memory);
  ASSERT_TRUE(loaded.ok()) << loaded.message;
  EXPECT_EQ(to_number(loaded.value), 0.1);
}

TEST(Avm2DomainMemory, AccessOutsideTheDomainMemoryIsRefused) {
  const MemoryPtr memory = make_memory(4);
  ASSERT_TRUE(run_load(kLi32, 0, memory).ok());
  const ExecutionResult past_end = run_load(kLi8, 4, memory);
  EXPECT_EQ(past_end.status, InterpreterStatus::InvalidArgument);
  EXPECT_TRUE(past_end.message.starts_with("domain memory")) << past_end.message;
  const ExecutionResult straddles = run_load(kLi32, 1, memory);
  EXPECT_EQ(straddles.status, InterpreterStatus::InvalidArgument);
  const ExecutionResult negative = run_load(kLi8, -1, memory);
  EXPECT_EQ(negative.status, InterpreterStatus::InvalidArgument);
  const ExecutionResult store = run_store_int(kSi32, 1, 1, memory);
  EXPECT_EQ(store.status, InterpreterStatus::InvalidArgument);
  EXPECT_TRUE(store.message.starts_with("domain memory")) << store.message;
}

TEST(Avm2DomainMemory, AccessWithoutDomainMemoryIsRefused) {
  const ExecutionResult load = run_load(kLi8, 0, nullptr);
  EXPECT_EQ(load.status, InterpreterStatus::NullReference);
  EXPECT_TRUE(load.message.starts_with("domain memory")) << load.message;
  const ExecutionResult store = run_store_int(kSi8, 1, 0, nullptr);
  EXPECT_EQ(store.status, InterpreterStatus::NullReference);
  EXPECT_TRUE(store.message.starts_with("domain memory")) << store.message;
}

TEST(Avm2DomainMemory, MemoryOpcodesRefuseAnEmptyStackWithStackUnderflow) {
  const std::uint8_t opcodes[] = {kLi8,  kLi16, kLi32, kLf32, kLf64,
                                  kSi8,  kSi16, kSi32, kSf32, kSf64};
  for (const std::uint8_t opcode : opcodes) {
    avm2_test::ScriptBuilder script;
    avm2_test::CodeBuilder code;
    code.op(opcode);
    code.returnvoid();
    const ExecutionResult result = run_with_memory(script, code, make_memory(8));
    EXPECT_EQ(result.status, InterpreterStatus::StackUnderflow) << result.message;
  }
}

}  // namespace
