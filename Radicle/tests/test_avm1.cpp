#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::avm1::Avm1Status;

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

Bytes le16(std::uint16_t value) {
  return Bytes{static_cast<std::uint8_t>(value & 0xFFu), static_cast<std::uint8_t>(value >> 8)};
}

Bytes le32(std::uint32_t value) {
  Bytes out;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  return out;
}

Bytes str(std::string_view text) {
  Bytes out(text.begin(), text.end());
  out.push_back(0);
  return out;
}

Bytes rec(std::uint8_t code, const Bytes& payload) {
  Bytes out{code};
  if (code >= 0x80) {
    const Bytes length = le16(static_cast<std::uint16_t>(payload.size()));
    out.insert(out.end(), length.begin(), length.end());
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::vector<std::byte> to_span_bytes(const Bytes& values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const std::uint8_t value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

Bytes to_values(const std::vector<std::byte>& bytes) {
  Bytes out;
  out.reserve(bytes.size());
  for (const std::byte value : bytes) {
    out.push_back(std::to_integer<std::uint8_t>(value));
  }
  return out;
}

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  const auto bytes = to_span_bytes(input);
  return fo4::avm1::decode_actions(bytes);
}

void expect_round_trip(const fo4::avm1::Avm1Stream& stream, const Bytes& input) {
  EXPECT_EQ(to_values(fo4::avm1::encode_actions(stream)), input);
}

// Invariant: a single-byte action must advance exactly one byte, so the offsets after it stay exact.
TEST(Avm1, StackOperationsAreOneByteRecords) {
  const Bytes input = cat({Bytes{0x0A, 0x17, 0x12}, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 4u);
  for (std::size_t index = 0; index < 4; ++index) {
    EXPECT_EQ(stream.actions[index].offset, index);
    EXPECT_EQ(stream.actions[index].length, 1u);
    EXPECT_FALSE(stream.actions[index].unknown);
  }
  EXPECT_EQ(stream.actions[1].code, 0x17);
  expect_round_trip(stream, input);
}

// Invariant: each Push value type must consume its own width, so Float and Double keep their bit layout.
TEST(Avm1, PushEveryValueTypeKeepsItsWidthAndBitLayout) {
  const Bytes push = cat({
      Bytes{0x00}, str("a"), Bytes{0x01}, le32(0x3F800000), Bytes{0x02}, Bytes{0x03},
      Bytes{0x04, 0x04}, Bytes{0x05, 0x01}, Bytes{0x06, 0x00, 0x00, 0xF0, 0x3F, 0x00, 0x00, 0x00, 0x00},
      Bytes{0x07, 0xFE, 0xFF, 0xFF, 0xFF}, Bytes{0x08, 0x05}, Bytes{0x09, 0x2C, 0x01},
  });
  const Bytes input = cat({rec(0x96, push), Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto& values = std::get<fo4::avm1::Avm1Push>(stream.actions[0].payload).values;
  ASSERT_EQ(values.size(), 10u);
  EXPECT_EQ(values[0].text, "a");
  EXPECT_EQ(values[1].float_bits, 0x3F800000u);
  EXPECT_EQ(values[2].type, fo4::avm1::Avm1ValueType::Null);
  EXPECT_EQ(values[3].type, fo4::avm1::Avm1ValueType::Undefined);
  EXPECT_EQ(values[4].index, 4u);
  EXPECT_TRUE(values[5].boolean);
  EXPECT_EQ(values[6].double_bits, 0x3FF0000000000000ull);
  EXPECT_EQ(values[7].integer, -2);
  EXPECT_EQ(values[8].index, 5u);
  EXPECT_EQ(values[9].index, 300u);
  expect_round_trip(stream, input);
}

// Invariant: a branch target is measured from the end of its own record, so Jump +1 lands on the byte after the Pop.
TEST(Avm1, BranchTargetsAreMeasuredFromTheRecordEnd) {
  const Bytes forward = cat({rec(0x99, le16(1)), Bytes{0x17, 0x00}});
  const auto jump = decode(forward);
  ASSERT_TRUE(jump.ok()) << jump.message;
  EXPECT_EQ(std::get<fo4::avm1::Avm1Branch>(jump.actions[0].payload).target, 6u);
  expect_round_trip(jump, forward);

  const Bytes backward = cat({Bytes{0x17}, rec(0x9D, le16(0xFFFA)), Bytes{0x00}});
  const auto loop = decode(backward);
  ASSERT_TRUE(loop.ok()) << loop.message;
  const auto& branch = std::get<fo4::avm1::Avm1Branch>(loop.actions[1].payload);
  EXPECT_EQ(branch.offset, -6);
  EXPECT_EQ(branch.target, 0u);
  expect_round_trip(loop, backward);
}

// Invariant: a branch target past the stream must be refused at the branch offset, not kept as a dangling target.
TEST(Avm1, BranchPastTheStreamIsRefusedAtItsOffset) {
  const auto stream = decode(cat({rec(0x99, le16(100)), Bytes{0x00}}));
  EXPECT_EQ(stream.status, Avm1Status::BranchOutOfRange);
  EXPECT_EQ(stream.offset, 0u);
  EXPECT_NE(stream.message.find("offset 0"), std::string::npos);
  EXPECT_TRUE(stream.actions.empty());
}

// Invariant: a branch target must land on a record start, so a listing label never names a byte inside a record.
TEST(Avm1, BranchIntoTheMiddleOfARecordIsRefused) {
  const auto stream =
      decode(cat({rec(0x99, le16(1)), rec(0x96, Bytes{0x07, 0x00, 0x00, 0x00, 0x00}), Bytes{0x00}}));
  EXPECT_EQ(stream.status, Avm1Status::BranchNotRecord);
  EXPECT_EQ(stream.offset, 0u);
}

// Invariant: ConstantPool must read its count before the strings, so each string stays aligned to its own NUL.
TEST(Avm1, ConstantPoolReadsItsCountThenItsStrings) {
  const Bytes input = cat({rec(0x88, cat({le16(3), str("a"), str("bc"), str("")})), Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto& pool = std::get<fo4::avm1::Avm1ConstantPool>(stream.actions[0].payload);
  EXPECT_EQ(pool.strings, (std::vector<std::string>{"a", "bc", ""}));
  expect_round_trip(stream, input);
}

// Invariant: a ConstantPool count past the bytes left in its record is Truncated at the count, so removing the check (refusal moves to offset 7) fails.
TEST(Avm1, ConstantPoolCountPastTheRecordIsRefused) {
  const auto stream = decode(cat({rec(0x88, cat({le16(0xFFFF), str("a")})), Bytes{0x00}}));
  EXPECT_EQ(stream.status, Avm1Status::Truncated);
  EXPECT_EQ(stream.offset, 5u);
  EXPECT_TRUE(stream.actions.empty());
}

// Invariant: a DefineFunction2 body starts after its header and spans exactly body_size bytes, so later records keep their offsets.
TEST(Avm1, DefineFunction2BodyStartsAfterItsHeader) {
  const Bytes header = cat({str("f"), le16(1), Bytes{2}, le16(1), Bytes{1}, str("x"), le16(2)});
  const Bytes input = cat({rec(0x8E, header), Bytes{0x12, 0x17}, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 4u);
  EXPECT_EQ(stream.actions[0].length, 15u);
  EXPECT_EQ(stream.actions[1].offset, 15u);
  EXPECT_EQ(stream.actions[2].offset, 16u);
  EXPECT_EQ(stream.actions[3].offset, 17u);
  const auto& fn = std::get<fo4::avm1::Avm1Function>(stream.actions[0].payload);
  EXPECT_TRUE(fn.version2);
  EXPECT_EQ(fn.name, "f");
  EXPECT_EQ(fn.register_count, 2);
  EXPECT_EQ(fn.flags, 1);
  ASSERT_EQ(fn.parameters.size(), 1u);
  EXPECT_EQ(fn.parameters[0].register_index, 1);
  EXPECT_EQ(fn.parameters[0].name, "x");
  EXPECT_EQ(fn.body_size, 2);
  expect_round_trip(stream, input);
}

// Invariant: a function parameter count past the bytes left in its record is Truncated at the count, so removing the check (refusal moves to offset 9 or 13) fails.
TEST(Avm1, FunctionParameterCountPastTheRecordIsRefused) {
  const auto v1 = decode(cat({rec(0x9B, cat({str("f"), le16(5), str("x")})), Bytes{0x00}}));
  EXPECT_EQ(v1.status, Avm1Status::Truncated);
  EXPECT_EQ(v1.offset, 7u);
  const Bytes v2_header = cat({str("f"), le16(5), Bytes{0x00}, le16(0), Bytes{0x01}, str("x")});
  const auto v2 = decode(cat({rec(0x8E, v2_header), Bytes{0x00}}));
  EXPECT_EQ(v2.status, Avm1Status::Truncated);
  EXPECT_EQ(v2.offset, 10u);
}

// Invariant: a DefineFunction2 whose parameter count fits its record decodes every parameter and re-encodes byte-identically, so the count check never refuses a valid function.
TEST(Avm1, DefineFunction2WithParametersRoundTrips) {
  const Bytes header =
      cat({str("h"), le16(2), Bytes{4}, le16(0), Bytes{1}, str("p"), Bytes{2}, str(""), le16(1)});
  const Bytes input = cat({rec(0x8E, header), Bytes{0x17}, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 3u);
  EXPECT_EQ(stream.actions[1].offset, 17u);
  const auto& fn = std::get<fo4::avm1::Avm1Function>(stream.actions[0].payload);
  EXPECT_EQ(fn.register_count, 4);
  ASSERT_EQ(fn.parameters.size(), 2u);
  EXPECT_EQ(fn.parameters[0].register_index, 1);
  EXPECT_EQ(fn.parameters[0].name, "p");
  EXPECT_EQ(fn.parameters[1].register_index, 2);
  EXPECT_EQ(fn.parameters[1].name, "");
  EXPECT_EQ(fn.body_size, 1);
  expect_round_trip(stream, input);
}

// Invariant: a DefineFunction without parameters reads its body size right after the name, so the body starts at the header end.
TEST(Avm1, DefineFunctionWithoutParametersReadsItsBodySize) {
  const Bytes input = cat({rec(0x9B, cat({str("g"), le16(0), le16(1)})), Bytes{0x17}, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 3u);
  EXPECT_EQ(stream.actions[1].offset, 9u);
  EXPECT_EQ(stream.actions[2].offset, 10u);
  const auto& fn = std::get<fo4::avm1::Avm1Function>(stream.actions[0].payload);
  EXPECT_FALSE(fn.version2);
  EXPECT_EQ(fn.body_size, 1);
  expect_round_trip(stream, input);
}

// Invariant: With and Try bodies are sized blocks after their headers, so each body record keeps its true offset.
TEST(Avm1, WithAndTryBodiesFollowTheirHeadersAtTheirSizes) {
  const Bytes try_header = cat({Bytes{0x03}, le16(1), le16(1), le16(1), str("e")});
  const Bytes input =
      cat({rec(0x94, le16(1)), Bytes{0x17}, rec(0x8F, try_header), Bytes{0x17, 0x12, 0x0A, 0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 7u);
  EXPECT_EQ(stream.actions[1].offset, 5u);
  EXPECT_EQ(stream.actions[2].offset, 6u);
  EXPECT_EQ(stream.actions[2].length, 12u);
  EXPECT_EQ(stream.actions[3].offset, 18u);
  EXPECT_EQ(stream.actions[6].offset, 21u);
  EXPECT_EQ(std::get<fo4::avm1::Avm1With>(stream.actions[0].payload).body_size, 1);
  const auto& tr = std::get<fo4::avm1::Avm1Try>(stream.actions[2].payload);
  EXPECT_EQ(tr.flags, 3);
  EXPECT_EQ(tr.try_size, 1);
  EXPECT_EQ(tr.catch_name, "e");
  expect_round_trip(stream, input);
}

// Invariant: a Try with CatchInRegister set reads a register byte instead of a catch name, so the finally body keeps its offset.
TEST(Avm1, TryCatchInRegisterReadsARegisterNotAName) {
  const Bytes header = cat({Bytes{0x06}, le16(0), le16(0), le16(1), Bytes{0x07}});
  const Bytes input = cat({rec(0x8F, header), Bytes{0x0A, 0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 3u);
  EXPECT_EQ(stream.actions[1].offset, 11u);
  EXPECT_EQ(stream.actions[2].offset, 12u);
  const auto& tr = std::get<fo4::avm1::Avm1Try>(stream.actions[0].payload);
  EXPECT_EQ(tr.catch_register, 7);
  EXPECT_EQ(tr.finally_size, 1);
  expect_round_trip(stream, input);
}

// Invariant: a nested body that runs past the end of its block must be refused, not read into the record after it.
TEST(Avm1, BodyLongerThanItsBlockIsRefused) {
  const auto stream = decode(cat({rec(0x94, le16(9)), Bytes{0x17, 0x00}}));
  EXPECT_EQ(stream.status, Avm1Status::LengthPastEnd);
  EXPECT_EQ(stream.offset, 5u);
}

// Invariant: a stream with no End record must be refused at the stream end, so a truncated DoAction is never accepted.
TEST(Avm1, MissingEndIsRefusedAtTheStreamEnd) {
  const auto stream = decode(Bytes{0x17});
  EXPECT_EQ(stream.status, Avm1Status::MissingEnd);
  EXPECT_EQ(stream.offset, 1u);
}

// Invariant: a record length cut short by the stream end must be refused at the offset where the read failed.
TEST(Avm1, TruncatedLengthFieldIsRefusedAtItsOffset) {
  const auto stream = decode(Bytes{0x17, 0x81, 0x02});
  EXPECT_EQ(stream.status, Avm1Status::Truncated);
  EXPECT_EQ(stream.offset, 2u);
}

// Invariant: a declared record length past the end of the stream must be refused before any field is read.
TEST(Avm1, DeclaredLengthPastTheStreamIsRefused) {
  const auto stream = decode(Bytes{0x96, 0x10, 0x00, 0x00, 0x00});
  EXPECT_EQ(stream.status, Avm1Status::LengthPastEnd);
  EXPECT_EQ(stream.offset, 0u);
}

// Invariant: a record whose fields stop short of its declared length must be refused, because dropped bytes would break the exact re-encode.
TEST(Avm1, FieldsThatStopShortOfTheLengthAreRefused) {
  const auto stream = decode(cat({rec(0x81, cat({le16(1), Bytes{0xAB}})), Bytes{0x00}}));
  EXPECT_EQ(stream.status, Avm1Status::FieldsMismatch);
  EXPECT_EQ(stream.offset, 5u);
}

// Invariant: an unknown opcode must keep its bytes and length, so the record re-encodes exactly and later offsets stay true.
TEST(Avm1, UnknownOpcodesKeepTheirBytesAndLength) {
  const Bytes input = cat({Bytes{0x1A}, rec(0xA5, Bytes{1, 2, 3}), Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 3u);
  EXPECT_TRUE(stream.actions[0].unknown);
  EXPECT_EQ(stream.actions[0].length, 1u);
  EXPECT_TRUE(stream.actions[1].unknown);
  EXPECT_EQ(stream.actions[1].length, 6u);
  EXPECT_EQ(stream.actions[2].offset, 7u);
  EXPECT_EQ(std::get<fo4::avm1::Avm1Raw>(stream.actions[1].payload).bytes.size(), 3u);
  expect_round_trip(stream, input);
  EXPECT_NE(fo4::avm1::disassemble_actions(stream).find("unknown 0xA5"), std::string::npos);
}

// Invariant: bytes after the End record must be kept as trailing bytes, so the re-encode returns every input byte.
TEST(Avm1, BytesAfterEndAreKeptAsTrailing) {
  const Bytes input{0x00, 0x55, 0x66};
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_EQ(stream.actions.size(), 1u);
  EXPECT_EQ(stream.trailing.size(), 2u);
  expect_round_trip(stream, input);
}

// Invariant: the GotoFrame2 scene bias is read only when its flag bit is set, so a flag-only record keeps its one-byte length.
TEST(Avm1, GotoFrame2ReadsSceneBiasOnlyWithItsFlag) {
  const Bytes input = cat({rec(0x9F, Bytes{0x03, 0x05, 0x00}), rec(0x9F, Bytes{0x01}), Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions.size(), 3u);
  const auto& with_bias = std::get<fo4::avm1::Avm1GotoFrame2>(stream.actions[0].payload);
  ASSERT_TRUE(with_bias.scene_bias.has_value());
  EXPECT_EQ(*with_bias.scene_bias, 5);
  EXPECT_FALSE(std::get<fo4::avm1::Avm1GotoFrame2>(stream.actions[1].payload).scene_bias.has_value());
  expect_round_trip(stream, input);
}

// Invariant: an empty Push and a Push value of an undefined type or a Boolean other than 0 or 1 must be refused at their offset.
TEST(Avm1, EmptyPushAndUndefinedValueTypesAreRefused) {
  EXPECT_EQ(decode(cat({rec(0x96, Bytes{}), Bytes{0x00}})).status, Avm1Status::EmptyPush);
  const auto undefined_type = decode(Bytes{0x96, 0x01, 0x00, 0x0B, 0x00});
  EXPECT_EQ(undefined_type.status, Avm1Status::BadPushValue);
  EXPECT_EQ(undefined_type.offset, 3u);
  EXPECT_EQ(decode(Bytes{0x96, 0x02, 0x00, 0x05, 0x02, 0x00}).status, Avm1Status::BadPushValue);
}

// Invariant: the listing must print each record's offset and each branch target as its resolved offset, so the text matches the stream.
TEST(Avm1, DisassemblyPrintsOffsetsAndResolvedTargets) {
  const auto stream = decode(cat({rec(0x99, le16(1)), Bytes{0x17, 0x00}}));
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_EQ(fo4::avm1::disassemble_actions(stream), "0000  Jump -> 0006\n0005  Pop\n0006  End\n");
}

Bytes nested_withs(std::size_t depth) {
  Bytes body{0x17};
  for (std::size_t level = 0; level < depth; ++level) {
    body = cat({rec(0x94, le16(static_cast<std::uint16_t>(body.size()))), body});
  }
  return cat({body, Bytes{0x00}});
}

// Invariant: With bodies nest at most 64 deep, so a 1,000-level chain is refused at its 65th body and 8 levels still re-encode exactly.
TEST(Avm1, NestedWithBodiesStopAtSixtyFourLevels) {
  const Bytes deep = nested_withs(1000);
  const auto refused = decode(deep);
  EXPECT_EQ(refused.status, Avm1Status::TooDeep);
  EXPECT_EQ(refused.offset, 325u);
  EXPECT_TRUE(refused.actions.empty());
  const Bytes shallow = nested_withs(8);
  const auto stream = decode(shallow);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_EQ(stream.actions.size(), 10u);
  expect_round_trip(stream, shallow);
}

// Invariant: a quoted text value with a newline must print as a \x escape, so one listing line never breaks inside its quotes.
TEST(Avm1, DisassemblyEscapesControlBytesInQuotedText) {
  const auto stream = decode(cat({rec(0x8B, str("a\nb")), Bytes{0x00}}));
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_NE(fo4::avm1::disassemble_actions(stream).find("\"a\\x0Ab\""), std::string::npos);
}

Bytes push_text(std::string_view text) {
  return rec(0x96, cat({Bytes{0x00}, str(text)}));
}

Bytes push_int(std::uint32_t value) {
  return rec(0x96, cat({Bytes{0x07}, le32(value)}));
}

Bytes push_double(double value) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  return rec(0x96, cat({Bytes{0x06}, le32(static_cast<std::uint32_t>(bits >> 32)),
                        le32(static_cast<std::uint32_t>(bits))}));
}

// Invariant: Subtract and Divide must take the deeper operand first, so 10 - 4 traces 6 and 1 / 4 traces 0.25; a reversed pair would trace -6 and 4.
TEST(Avm1Interpreter, ArithmeticResultIsTracedInOperandOrder) {
  const Bytes input = cat({
      push_int(2), push_int(3), rec(0x0A, {}), push_int(4), rec(0x0C, {}), rec(0x26, {}),
      push_int(10), push_int(4), rec(0x0B, {}), rec(0x26, {}),
      push_int(1), push_int(4), rec(0x0D, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"20", "6", "0.25"}));
}

// Invariant: a Jump and an If must land on the records their offsets name, so a counted loop traces 0, 1, 2 and exits when i reaches 3; a wrong branch offset is refused at decode.
TEST(Avm1Interpreter, CountedLoopExitsThroughIfAndJump) {
  const Bytes init = cat({push_text("i"), push_int(0), rec(0x1D, {})});
  const Bytes test = cat({push_text("i"), rec(0x1C, {}), push_int(3), rec(0x48, {}), rec(0x12, {})});
  const Bytes body = cat({
      push_text("i"), rec(0x1C, {}), rec(0x26, {}), push_text("i"), push_text("i"),
      rec(0x1C, {}), push_int(1), rec(0x47, {}), rec(0x1D, {}),
  });
  const Bytes exit_if = rec(0x9D, le16(static_cast<std::uint16_t>(body.size() + 5)));
  const Bytes back_jump =
      rec(0x99, le16(static_cast<std::uint16_t>(-static_cast<int>(test.size() + body.size() + 10))));
  const Bytes input = cat({init, test, exit_if, body, back_jump, Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"0", "1", "2"}));
  EXPECT_EQ(result.variables.at("i").number, 3.0);
}

// Invariant: a loop with no exit must stop at the step limit with StepLimit after exactly 100 records; without the limit this test never returns.
TEST(Avm1Interpreter, SelfJumpStopsAtTheStepLimit) {
  const auto stream = decode(cat({rec(0x99, le16(0xFFFB)), Bytes{0x00}}));
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream, 100);
  EXPECT_EQ(result.status, fo4::avm1::Avm1RunStatus::StepLimit);
  EXPECT_EQ(result.steps, 100u);
  EXPECT_EQ(result.opcode, 0x99);
  EXPECT_EQ(result.offset, 0u);
}

// Invariant: SetVariable must pop the value above its name, so the name keys the variable and "hi" reads back; a swapped pop would key "hi" instead.
TEST(Avm1Interpreter, VariableRoundTripsThroughTheGlobalScope) {
  const Bytes input = cat({
      push_text("x"), push_text("hi"), rec(0x1D, {}), push_text("x"), rec(0x1C, {}), rec(0x26, {}),
      push_text("missing"), rec(0x1C, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"hi", "undefined"}));
  ASSERT_EQ(result.variables.count("x"), 1u);
  EXPECT_EQ(result.variables.at("x").kind, fo4::avm1::Avm1DataKind::String);
  EXPECT_EQ(result.variables.at("x").text, "hi");
  EXPECT_EQ(result.variables.count("hi"), 0u);
}

// Invariant: an opcode outside the slice must refuse at its own offset by name; a silent skip would run the Trace after it and trace "2".
TEST(Avm1Interpreter, UnsupportedOpcodeIsRefusedByName) {
  const auto stream =
      decode(cat({push_int(1), rec(0x26, {}), rec(0x34, {}), push_int(2), rec(0x26, {}), Bytes{0x00}}));
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  EXPECT_EQ(result.status, fo4::avm1::Avm1RunStatus::Unsupported);
  EXPECT_EQ(result.opcode, 0x34);
  EXPECT_EQ(result.offset, stream.actions[2].offset);
  EXPECT_NE(result.message.find("GetTime (0x34)"), std::string::npos);
  EXPECT_EQ(result.trace, (std::vector<std::string>{"1"}));
}

// Invariant: a Number traces in the ECMA-262 layout, so 1e20 prints in full, 1e21 and 1e-7 switch to exponent form, 0.000001 stays fixed, and 0.1 + 0.2 prints 0.3.
TEST(Avm1Interpreter, DoublesTraceInTheEcmaScriptLayout) {
  const Bytes input = cat({
      push_double(1e20), rec(0x26, {}), push_double(1e21), rec(0x26, {}), push_double(0.000001),
      rec(0x26, {}), push_double(1e-7), rec(0x26, {}), push_double(0.1), push_double(0.2),
      rec(0x47, {}), rec(0x26, {}), Bytes{0x00},
  });
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"100000000000000000000", "1e+21", "0.000001",
                                                    "1e-7", "0.3"}));
}

// Invariant: a string that doubles must stop with ResourceLimit once it passes the 16 MiB cap, long before the step limit, so bad_alloc never escapes.
TEST(Avm1Interpreter, DoublingStringStopsAtTheResourceCap) {
  const Bytes input =
      cat({push_text("aa"), rec(0x4C, {}), rec(0x21, {}), rec(0x99, le16(0xFFF9)), Bytes{0x00}});
  const auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream);
  EXPECT_EQ(result.status, fo4::avm1::Avm1RunStatus::ResourceLimit);
  EXPECT_EQ(result.opcode, 0x21);
  EXPECT_LT(result.steps, 1000u);
}

// Invariant: a Jump past the last record ends the run only when its target is exactly the stream size, so one byte further is BadBranch at the Jump offset.
TEST(Avm1Interpreter, JumpPastTheStreamEndIsRefusedUnlessItIsTheStreamSize) {
  auto stream = decode(cat({rec(0x99, le16(1)), Bytes{0x17, 0x00}}));
  ASSERT_TRUE(stream.ok()) << stream.message;
  auto& branch = std::get<fo4::avm1::Avm1Branch>(stream.actions[0].payload);
  branch.target = 7;
  EXPECT_TRUE(fo4::avm1::execute_actions(stream).ok());
  branch.target = 8;
  const auto result = fo4::avm1::execute_actions(stream);
  EXPECT_EQ(result.status, fo4::avm1::Avm1RunStatus::BadBranch);
  EXPECT_EQ(result.offset, 0u);
}

struct TableRow {
  const char* name;
  Bytes input;
  fo4::avm1::Avm1RunStatus status;
  std::vector<std::string> trace;
};

// Invariant: Equals2 follows ECMA-262 for null, undefined, text, Booleans and NaN, StringAdd keeps operand order, a constant inside the pool traces and one past it refuses, and an empty stack or an undecodable stream refuses by its own status.
TEST(Avm1Interpreter, OperatorsConstantsAndRefusalsMatchTheTable) {
  using fo4::avm1::Avm1RunStatus;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::vector<TableRow> rows = {
      {"null equals undefined",
       cat({rec(0x96, Bytes{0x02}), rec(0x96, Bytes{0x03}), rec(0x49, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"text 1 equals number 1",
       cat({push_text("1"), push_int(1), rec(0x49, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"true equals number 1",
       cat({rec(0x96, Bytes{0x05, 0x01}), push_int(1), rec(0x49, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"NaN equals NaN",
       cat({push_double(nan), push_double(nan), rec(0x49, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"StringAdd keeps operand order",
       cat({push_text("a"), push_text("b"), rec(0x21, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"ab"}},
      {"ConstantPool then Push constant8",
       cat({rec(0x88, cat({le16(2), str("x"), str("y")})), rec(0x96, Bytes{0x08, 0x01}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"y"}},
      {"constant past the pool",
       cat({rec(0x88, cat({le16(1), str("x")})), rec(0x96, Bytes{0x08, 0x05}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::ConstantOutOfRange,
       {}},
      {"Pop on an empty stack", cat({Bytes{0x17}, Bytes{0x00}}), Avm1RunStatus::StackUnderflow, {}},
      {"stream that does not decode", Bytes{0x17}, Avm1RunStatus::InvalidStream, {}},
  };
  for (const TableRow& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    EXPECT_EQ(stream.ok(), row.status != Avm1RunStatus::InvalidStream);
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, row.status) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

// Invariant: Less compares numbers, Less2 and StringLess compare text, and Greater with a number and text compares as numbers; a wrong coercion flips one of these rows, and Greater with undefined must be false rather than the negation of Less2.
TEST(Avm1Interpreter, ComparisonOpcodesUseTheirOwnCoercion) {
  using fo4::avm1::Avm1RunStatus;
  const std::vector<TableRow> rows = {
      {"Less compares numbers, so 10 is not less than 9",
       cat({push_text("10"), push_text("9"), rec(0x0F, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"Less2 compares two texts as text, so 10 is less than 9",
       cat({push_text("10"), push_text("9"), rec(0x48, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"StringLess compares texts, so 10 is less than 9",
       cat({push_text("10"), push_text("9"), rec(0x29, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"StringGreater compares texts, so 10 is not greater than 9",
       cat({push_text("10"), push_text("9"), rec(0x68, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"Greater with a number and a text compares as numbers",
       cat({push_int(10), push_text("9"), rec(0x67, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"Greater with undefined is false",
       cat({rec(0x96, Bytes{0x03}), push_int(1), rec(0x67, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
  };
  for (const TableRow& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, row.status) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

// Invariant: StringExtract reads the string, then the index, then the count from the top; the index is 1-based and clamped to the text, so "hello" at index 1 with count 3 traces "hel", and a swapped pair would trace "hel" for ("hello",3,1), which must trace "l".
TEST(Avm1Interpreter, SubstringReadsIndexThenCountAndClamps) {
  using fo4::avm1::Avm1RunStatus;
  const std::vector<TableRow> rows = {
      {"index 1 and count 3 take the first three characters",
       cat({push_text("hello"), push_int(1), push_int(3), rec(0x15, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"hel"}},
      {"index 0 starts at the first character too",
       cat({push_text("hello"), push_int(0), push_int(3), rec(0x15, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"hel"}},
      {"index 3 and count 1 take the third character",
       cat({push_text("hello"), push_int(3), push_int(1), rec(0x15, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"l"}},
      {"a count past the end stops at the end",
       cat({push_text("hello"), push_int(3), push_int(10), rec(0x15, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"llo"}},
      {"an index past the end is empty",
       cat({push_text("hello"), push_int(9), push_int(2), rec(0x15, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {""}},
      {"a negative count is empty",
       cat({push_text("hello"), push_int(1), push_int(static_cast<std::uint32_t>(-4)),
            rec(0x15, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {""}},
      {"StringExtract with two values is a stack underflow",
       cat({push_text("hello"), push_int(1), rec(0x15, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow,
       {}},
  };
  for (const TableRow& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, row.status) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

// Invariant: Modulo keeps the dividend's sign like fmod, so -7 % 3 traces -1 and not the floored 2; a zero divisor traces NaN as Divide does; Increment converts text first and Decrement steps a number.
TEST(Avm1Interpreter, ModuloAndIncrementDecrementMatchTheirTable) {
  using fo4::avm1::Avm1RunStatus;
  const std::vector<TableRow> rows = {
      {"-7 % 3 keeps the dividend's sign",
       cat({push_int(static_cast<std::uint32_t>(-7)), push_int(3), rec(0x3F, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-1"}},
      {"7 % -3 keeps the dividend's sign",
       cat({push_int(7), push_int(static_cast<std::uint32_t>(-3)), rec(0x3F, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1"}},
      {"5.5 % 2 is 1.5",
       cat({push_double(5.5), push_int(2), rec(0x3F, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1.5"}},
      {"a zero divisor is NaN",
       cat({push_int(4), push_int(0), rec(0x3F, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"NaN"}},
      {"Increment converts text to a number first",
       cat({push_text("4"), rec(0x50, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"5"}},
      {"Decrement steps a number down by one",
       cat({push_int(10), rec(0x51, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"9"}},
      {"Increment of undefined is NaN",
       cat({rec(0x96, Bytes{0x03}), rec(0x50, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"NaN"}},
  };
  for (const TableRow& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(row.input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, row.status) << result.message;
    EXPECT_EQ(result.trace, row.trace);
  }
}

// Invariant: CallFunction, GetMember and Enumerate must stay refused by their names, since the slice has no ActionScript runtime; a stub that silently succeeds would run the Trace after each and trace nothing.
TEST(Avm1Interpreter, ObjectAndFunctionOpcodesStayRefusedByName) {
  const std::vector<std::pair<std::uint8_t, std::string_view>> refused = {
      {0x3D, "CallFunction"}, {0x4E, "GetMember"}, {0x46, "Enumerate"}};
  for (const auto& [code, name] : refused) {
    SCOPED_TRACE(name);
    const auto stream = decode(cat({rec(code, {}), rec(0x26, {}), Bytes{0x00}}));
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, fo4::avm1::Avm1RunStatus::Unsupported);
    EXPECT_EQ(result.opcode, code);
    EXPECT_NE(result.message.find(name), std::string::npos) << result.message;
    EXPECT_TRUE(result.trace.empty());
  }
}

}  // namespace
