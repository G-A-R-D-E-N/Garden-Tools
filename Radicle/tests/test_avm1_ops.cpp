#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::avm1::Avm1RunStatus;

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

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  const auto bytes = to_span_bytes(input);
  return fo4::avm1::decode_actions(bytes);
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

struct TableRow {
  const char* name;
  Bytes input;
  Avm1RunStatus status;
  std::vector<std::string> trace;
};

// Invariant: BitAnd, BitOr and BitXor take ToInt32 of each operand, so 2^32+1 is 1 and not the saturated 2147483647 that a naive cast gives, 2^31 wraps to -2147483648, and NaN is 0; a missing wrap traces a different number on each row.
TEST(Avm1Interpreter, BitwiseOperatorsWrapTheirOperandsToInt32) {
  const std::vector<TableRow> rows = {
      {"4294967297 wraps to 1 under BitAnd",
       cat({push_double(4294967297.0), push_int(3), rec(0x60, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1"}},
      {"4294967297 wraps to 1 under BitOr, so 1 | 2 is 3",
       cat({push_double(4294967297.0), push_int(2), rec(0x61, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"3"}},
      {"2^31 wraps to -2147483648 under BitOr with 0",
       cat({push_double(2147483648.0), push_int(0), rec(0x61, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-2147483648"}},
      {"-1 XOR 255 is -256",
       cat({push_int(static_cast<std::uint32_t>(-1)), push_int(255), rec(0x62, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-256"}},
      {"BitAnd truncates 5.9 toward zero before masking, so 5.9 & 7 is 5",
       cat({push_double(5.9), push_int(7), rec(0x60, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"5"}},
      {"text that is not a number is NaN, which is 0 under BitOr",
       cat({push_text("abc"), push_int(5), rec(0x61, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"5"}},
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

// Invariant: the shift count is taken modulo 32, so 1 << 32 is 1 and 1 << 33 is 2, a left shift into bit 31 is -2147483648, >> keeps the sign, and >>> reads -1 as 4294967295; a count that is not masked gives a different row.
TEST(Avm1Interpreter, ShiftCountsAreMaskedToFiveBits) {
  const std::vector<TableRow> rows = {
      {"1 << 32 uses count 0",
       cat({push_int(1), push_int(32), rec(0x63, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1"}},
      {"1 << 33 uses count 1",
       cat({push_int(1), push_int(33), rec(0x63, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"2"}},
      {"1 << 31 wraps to -2147483648",
       cat({push_int(1), push_int(31), rec(0x63, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-2147483648"}},
      {"-8 >> 1 keeps the sign",
       cat({push_int(static_cast<std::uint32_t>(-8)), push_int(1), rec(0x64, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-4"}},
      {"-1 >> 33 uses count 1 and stays -1",
       cat({push_int(static_cast<std::uint32_t>(-1)), push_int(33), rec(0x64, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-1"}},
      {"-16 >> 32 uses count 0",
       cat({push_int(static_cast<std::uint32_t>(-16)), push_int(32), rec(0x64, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"-16"}},
      {"-1 >>> 0 is 4294967295",
       cat({push_int(static_cast<std::uint32_t>(-1)), push_int(0), rec(0x65, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"4294967295"}},
      {"-1 >>> 1 is 2147483647",
       cat({push_int(static_cast<std::uint32_t>(-1)), push_int(1), rec(0x65, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"2147483647"}},
      {"-1 >>> 32 uses count 0",
       cat({push_int(static_cast<std::uint32_t>(-1)), push_int(32), rec(0x65, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"4294967295"}},
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

// Invariant: TypeOf names each data kind, so null and undefined trace different names and a Boolean is "boolean", not "number"; a swapped branch traces the other name on its row.
TEST(Avm1Interpreter, TypeOfNamesEachDataKind) {
  const std::vector<TableRow> rows = {
      {"undefined",
       cat({rec(0x96, Bytes{0x03}), rec(0x44, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"undefined"}},
      {"null",
       cat({rec(0x96, Bytes{0x02}), rec(0x44, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"null"}},
      {"boolean",
       cat({rec(0x96, Bytes{0x05, 0x01}), rec(0x44, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"boolean"}},
      {"number",
       cat({push_int(1), rec(0x44, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"number"}},
      {"string",
       cat({push_text("1"), rec(0x44, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"string"}},
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

// Invariant: CharToAscii reads the first byte with no codepage, so "A" is 65, "ABC" is 65, a 0xFF byte is 255 and "" is 0; AsciiToChar keeps the low byte, so 65 and -191 give "A" and 322 gives "B".
TEST(Avm1Interpreter, CharToAsciiAndAsciiToCharAreByteWise) {
  const std::vector<TableRow> rows = {
      {"A is 65",
       cat({push_text("A"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"65"}},
      {"only the first character counts",
       cat({push_text("ABC"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"65"}},
      {"the empty text is 0",
       cat({push_text(""), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"0"}},
      {"a high byte is 255 and not decoded as UTF-8",
       cat({push_text("\xFF"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"255"}},
      {"65 is A",
       cat({push_int(65), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"A"}},
      {"322 keeps its low byte 66, which is B",
       cat({push_int(322), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"B"}},
      {"-191 keeps its low byte 65, which is A",
       cat({push_int(static_cast<std::uint32_t>(-191)), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"A"}},
      {"0 is one NUL byte",
       cat({push_int(0), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {std::string(1, '\0')}},
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

// Invariant: Equals compares ToNumber of its operands, so "1.0" equals 1 where a text compare would not, null equals 0, null and undefined differ, and NaN equals nothing; an Equals2-style rule would make null equal undefined.
TEST(Avm1Interpreter, EqualsComparesNumbersAndNeverMatchesNaN) {
  const std::vector<TableRow> rows = {
      {"1.0 equals 1 as numbers",
       cat({push_text("1.0"), push_int(1), rec(0x0E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"null equals 0",
       cat({rec(0x96, Bytes{0x02}), push_int(0), rec(0x0E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"null does not equal undefined",
       cat({rec(0x96, Bytes{0x02}), rec(0x96, Bytes{0x03}), rec(0x0E, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"undefined is NaN and does not equal 0",
       cat({rec(0x96, Bytes{0x03}), push_int(0), rec(0x0E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"text that is not a number is NaN and does not equal itself",
       cat({push_text("abc"), push_text("abc"), rec(0x0E, {}), rec(0x26, {}), Bytes{0x00}}),
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

// Invariant: ToNumber trims and parses only whole text, so "12abc" is NaN and "" is 0, undefined is NaN and null is 0; a ToNumber that accepted a prefix would trace 12 on the first row.
TEST(Avm1Interpreter, ToNumberAndToStringConvertEachKind) {
  const std::vector<TableRow> rows = {
      {"12abc is NaN",
       cat({push_text("12abc"), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"NaN"}},
      {"the empty text is 0",
       cat({push_text(""), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"0"}},
      {"undefined is NaN",
       cat({rec(0x96, Bytes{0x03}), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"NaN"}},
      {"null is 0",
       cat({rec(0x96, Bytes{0x02}), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"0"}},
      {"true is 1",
       cat({rec(0x96, Bytes{0x05, 0x01}), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1"}},
      {"0.5 is the text 0.5",
       cat({push_double(0.5), rec(0x4B, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"0.5"}},
      {"null is the text null",
       cat({rec(0x96, Bytes{0x02}), rec(0x4B, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"null"}},
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

// Invariant: each new operator refuses a short stack with StackUnderflow and traces nothing, so a missing operand is never read from an empty vector and a later Trace never runs.
TEST(Avm1Interpreter, NewOperatorsRefuseAShortStack) {
  const std::vector<TableRow> rows = {
      {"Equals with one value", cat({push_int(1), rec(0x0E, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
      {"BitAnd with one value", cat({push_int(1), rec(0x60, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
      {"BitURShift with one value", cat({push_int(1), rec(0x65, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
      {"TypeOf on an empty stack", cat({rec(0x44, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
      {"CharToAscii on an empty stack", cat({rec(0x32, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
      {"AsciiToChar on an empty stack", cat({rec(0x33, {}), Bytes{0x00}}),
       Avm1RunStatus::StackUnderflow, {}},
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

// Invariant: the multibyte string opcodes stay refused by name, since the slice holds raw bytes with no codepage; a byte-wise stand-in would count UTF-8 text wrongly and trace a value instead of refusing.
TEST(Avm1Interpreter, MultibyteStringOpcodesStayRefusedByName) {
  const std::vector<std::pair<std::uint8_t, std::string_view>> refused = {
      {0x31, "MBStringLength"}, {0x35, "MBStringExtract"}, {0x36, "MBCharToAscii"},
      {0x37, "MBAsciiToChar"}};
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

std::vector<std::string> traced(const Bytes& input, std::uint8_t swf_version) {
  const auto stream = decode(input);
  EXPECT_TRUE(stream.ok()) << stream.message;
  const auto result = fo4::avm1::execute_actions(stream, 1000000, swf_version);
  EXPECT_EQ(result.status, Avm1RunStatus::Ok) << result.message;
  return result.trace;
}

struct VersionRow {
  const char* name;
  std::uint8_t version;
  Bytes input;
  std::vector<std::string> trace;
};

// Invariant: from SWF 6 CharToAscii reads the first UTF-8 code point and AsciiToChar encodes a 16-bit unit as UTF-8, so chr(256) is U+0100 and ord of it is 256; SWF 5 and version 0 stay byte-wise, so a version-blind UTF-8 rule would trace 233 on the SWF 5 e-acute row.
TEST(Avm1Interpreter, SwfSixStringsAreUtf8CodePoints) {
  const std::vector<VersionRow> rows = {
      {"chr 256 is U+0100 from SWF 6", 6,
       cat({push_int(256), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}), {"\xC4\x80"}},
      {"chr 256 is one NUL byte on SWF 5", 5,
       cat({push_int(256), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}), {std::string(1, '\0')}},
      {"chr keeps the low 16 bits from SWF 6", 6,
       cat({push_int(65601), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}), {"A"}},
      {"chr keeps the low byte on version 0", 0,
       cat({push_int(322), rec(0x33, {}), rec(0x26, {}), Bytes{0x00}}), {"B"}},
      {"ord of U+0100 is 256 from SWF 6", 6,
       cat({push_text("\xC4\x80"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"256"}},
      {"ord of e-acute is 233 from SWF 6", 6,
       cat({push_text("\xC3\xA9"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"233"}},
      {"ord of the euro sign is 8364 from SWF 6", 6,
       cat({push_text("\xE2\x82\xAC"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"8364"}},
      {"ord of e-acute is its lead byte 195 on SWF 5", 5,
       cat({push_text("\xC3\xA9"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"195"}},
      {"ord of e-acute is its lead byte 195 on version 0", 0,
       cat({push_text("\xC3\xA9"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"195"}},
      {"an invalid lead byte is its own value from SWF 6", 6,
       cat({push_text("\xFF"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"255"}},
      {"a truncated sequence is its lead byte from SWF 6", 6,
       cat({push_text("\xC3"), rec(0x32, {}), rec(0x26, {}), Bytes{0x00}}), {"195"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

// Invariant: from SWF 7 ToNumber(null) is NaN, while version 0 and SWF 6 keep null as 0, so a version-blind null of 0 would trace 0 on the SWF 7 row.
TEST(Avm1Interpreter, SwfSevenMakesNullNaN) {
  const Bytes null_to_number =
      cat({rec(0x96, Bytes{0x02}), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes null_plus_one =
      cat({rec(0x96, Bytes{0x02}), push_int(1), rec(0x0A, {}), rec(0x26, {}), Bytes{0x00}});
  const std::vector<VersionRow> rows = {
      {"version 0 keeps null as 0", 0, null_to_number, {"0"}},
      {"SWF 6 keeps null as 0", 6, null_to_number, {"0"}},
      {"SWF 7 makes null NaN", 7, null_to_number, {"NaN"}},
      {"version 0 adds 1 to null as 1", 0, null_plus_one, {"1"}},
      {"SWF 7 adds 1 to null as NaN", 7, null_plus_one, {"NaN"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

// Invariant: from SWF 5 undefined converts to 0 and from SWF 7 to NaN, and Less, Greater and Equals (0x0E) follow that conversion, so an undefined compared with 1 or 0 flips with the version; a version-blind NaN gives false on the SWF 5 rows.
TEST(Avm1Interpreter, UndefinedIsZeroBeforeSevenAndComparesFollow) {
  const Bytes undefined_to_number =
      cat({rec(0x96, Bytes{0x03}), rec(0x4A, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes undefined_less_one =
      cat({rec(0x96, Bytes{0x03}), push_int(1), rec(0x0F, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes one_greater_undefined =
      cat({push_int(1), rec(0x96, Bytes{0x03}), rec(0x67, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes undefined_equals_zero =
      cat({rec(0x96, Bytes{0x03}), push_int(0), rec(0x0E, {}), rec(0x26, {}), Bytes{0x00}});
  const std::vector<VersionRow> rows = {
      {"version 0 makes undefined NaN", 0, undefined_to_number, {"NaN"}},
      {"SWF 5 makes undefined 0", 5, undefined_to_number, {"0"}},
      {"SWF 7 makes undefined NaN", 7, undefined_to_number, {"NaN"}},
      {"SWF 5 has undefined less than 1", 5, undefined_less_one, {"true"}},
      {"SWF 7 has NaN not less than 1", 7, undefined_less_one, {"false"}},
      {"SWF 5 has 1 greater than undefined", 5, one_greater_undefined, {"true"}},
      {"SWF 7 has 1 not greater than NaN", 7, one_greater_undefined, {"false"}},
      {"SWF 5 has undefined equal to 0", 5, undefined_equals_zero, {"true"}},
      {"SWF 7 has undefined not equal to 0", 7, undefined_equals_zero, {"false"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

}  // namespace
