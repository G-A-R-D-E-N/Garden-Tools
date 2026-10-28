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

// Invariant: before SWF 7 undefined converts to 0 and from SWF 7 to NaN, and Less, Greater and Equals (0x0E) follow that conversion, so an undefined compared with 1 or 0 flips with the version; a version-blind NaN gives false on the SWF 5 rows.
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
      {"SWF 6 makes undefined 0", 6, undefined_to_number, {"0"}},
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

// Invariant: StrictEquals compares type before value, so "1" is not 1, null is not undefined, undefined and null each equal themselves, NaN equals nothing and equal text is true; a coercing Equals2 would trace true on the "1" row and on the null and undefined row.
TEST(Avm1Interpreter, StrictEqualsRequiresTheSameTypeAndValue) {
  const Bytes null_value = rec(0x96, Bytes{0x02});
  const Bytes undefined_value = rec(0x96, Bytes{0x03});
  const Bytes nan_value = push_double(std::numeric_limits<double>::quiet_NaN());
  const Bytes one_and_text_one =
      cat({push_int(1), push_text("1"), rec(0x66, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes one_and_one =
      cat({push_int(1), push_int(1), rec(0x66, {}), rec(0x26, {}), Bytes{0x00}});
  const std::vector<VersionRow> rows = {
      {"1 is not the text 1 from SWF 6", 6, one_and_text_one, {"false"}},
      {"1 is not the text 1 on version 0", 0, one_and_text_one, {"false"}},
      {"null is not undefined", 6,
       cat({null_value, undefined_value, rec(0x66, {}), rec(0x26, {}), Bytes{0x00}}), {"false"}},
      {"undefined is undefined", 6,
       cat({undefined_value, undefined_value, rec(0x66, {}), rec(0x26, {}), Bytes{0x00}}),
       {"true"}},
      {"NaN is not NaN", 6,
       cat({nan_value, nan_value, rec(0x66, {}), rec(0x26, {}), Bytes{0x00}}), {"false"}},
      {"1 is 1 from SWF 6", 6, one_and_one, {"true"}},
      {"1 is 1 on version 0", 0, one_and_one, {"true"}},
      {"equal text is equal", 6,
       cat({push_text("ab"), push_text("ab"), rec(0x66, {}), rec(0x26, {}), Bytes{0x00}}),
       {"true"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

// Invariant: StrictEquals is unknown from SWF 1 to 5, so it refuses by name at its own offset and traces nothing; a version-blind StrictEquals would trace true on the 1 and 1 row.
TEST(Avm1Interpreter, StrictEqualsIsRefusedBelowSwfSix) {
  const Bytes input = cat({push_int(1), push_int(1), rec(0x66, {}), rec(0x26, {}), Bytes{0x00}});
  for (const std::uint8_t version : {std::uint8_t{1}, std::uint8_t{5}}) {
    SCOPED_TRACE("SWF " + std::to_string(version));
    const auto stream = decode(input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream, 1000000, version);
    EXPECT_EQ(result.status, Avm1RunStatus::Unsupported) << result.message;
    EXPECT_EQ(result.opcode, 0x66);
    EXPECT_NE(result.message.find("StrictEquals"), std::string::npos) << result.message;
    EXPECT_TRUE(result.trace.empty());
  }
}

// Invariant: from SWF 6 StringLength and StringExtract count UTF-8 code points, so an e-acute is one unit and extracting one unit gives its two bytes, while SWF 5 and version 0 count bytes and give the lone lead byte; a byte count at SWF 6 would trace 2 on the e-acute row and split the e-acute on the extract row.
TEST(Avm1Interpreter, StringLengthAndExtractCountCodePointsFromSwfSix) {
  const Bytes text = push_text("a\xC3\xA9z");
  const Bytes e_acute_length =
      cat({push_text("\xC3\xA9"), rec(0x14, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes text_length = cat({text, rec(0x14, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes extract_index_2_count_1 =
      cat({text, push_int(2), push_int(1), rec(0x15, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes extract_index_3_count_2 =
      cat({text, push_int(3), push_int(2), rec(0x15, {}), rec(0x26, {}), Bytes{0x00}});
  const std::vector<VersionRow> rows = {
      {"e-acute is 2 bytes on SWF 5", 5, e_acute_length, {"2"}},
      {"e-acute is 1 code point on SWF 6", 6, e_acute_length, {"1"}},
      {"e-acute is 2 bytes on version 0", 0, e_acute_length, {"2"}},
      {"a e-acute z is 3 code points on SWF 6", 6, text_length, {"3"}},
      {"a e-acute z is 4 bytes on SWF 5", 5, text_length, {"4"}},
      {"SWF 6 extract at 2 for 1 is the e-acute", 6, extract_index_2_count_1, {"\xC3\xA9"}},
      {"SWF 5 extract at 2 for 1 is the lead byte", 5, extract_index_2_count_1, {"\xC3"}},
      {"version 0 extract at 2 for 1 is the lead byte", 0, extract_index_2_count_1, {"\xC3"}},
      {"SWF 6 extract at 3 for 2 is z, clamped by code points", 6, extract_index_3_count_2,
       {"z"}},
      {"SWF 5 extract at 3 for 2 is the last two bytes", 5, extract_index_3_count_2, {"\xA9z"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

// Invariant: from SWF 1 to 6 undefined converts to the empty text in Add2, StringAdd and StringEquals, while SWF 7 and version 0 keep the word undefined, so undefined plus "x" traces x on SWF 6 and undefinedx on SWF 7; a version-blind "undefined" would trace undefinedx on the SWF 1 and SWF 6 rows.
TEST(Avm1Interpreter, UndefinedIsEmptyTextFromSwfOneToSix) {
  const Bytes add2 =
      cat({rec(0x96, Bytes{0x03}), push_text("x"), rec(0x47, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes string_add =
      cat({rec(0x96, Bytes{0x03}), push_text("x"), rec(0x21, {}), rec(0x26, {}), Bytes{0x00}});
  const Bytes string_equals =
      cat({rec(0x96, Bytes{0x03}), push_text(""), rec(0x13, {}), rec(0x26, {}), Bytes{0x00}});
  const std::vector<VersionRow> rows = {
      {"SWF 1 makes undefined empty text", 1, add2, {"x"}},
      {"SWF 6 makes undefined empty text", 6, add2, {"x"}},
      {"SWF 7 keeps the word undefined", 7, add2, {"undefinedx"}},
      {"version 0 keeps the word undefined", 0, add2, {"undefinedx"}},
      {"SWF 6 StringAdd of undefined and x is x", 6, string_add, {"x"}},
      {"SWF 6 undefined equals the empty text as StringEquals", 6, string_equals, {"true"}},
      {"SWF 7 undefined does not equal the empty text as StringEquals", 7, string_equals,
       {"false"}},
  };
  for (const VersionRow& row : rows) {
    SCOPED_TRACE(row.name);
    EXPECT_EQ(traced(row.input, row.version), row.trace);
  }
}

}  // namespace
