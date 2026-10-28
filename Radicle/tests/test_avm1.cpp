#include <fo4/avm1/avm1.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
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

}  // namespace
