#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
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

Bytes rec(std::uint8_t code, const Bytes& payload) {
  Bytes out{code};
  if (code >= 0x80) {
    out.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Bytes push_text(std::string_view text) {
  Bytes payload{0x00};
  payload.insert(payload.end(), text.begin(), text.end());
  payload.push_back(0);
  return rec(0x96, payload);
}

Bytes push_int(std::uint32_t value) {
  Bytes payload{0x07};
  for (unsigned shift = 0; shift < 32; shift += 8) {
    payload.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  return rec(0x96, payload);
}

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t value : input) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return fo4::avm1::decode_actions(bytes);
}

struct TableRow {
  const char* name;
  Bytes input;
  Avm1RunStatus status;
  std::vector<std::string> trace;
};

// Invariant: InitObject reads each (value, name) pair in stack order and GetMember reads it back, so a swapped pair traces the other value and a name that was never stored reads undefined.
TEST(Avm1Interpreter, InitObjectThenGetMemberReadsEachPair) {
  const std::vector<TableRow> rows = {
      {"two pairs read back in order",
       cat({push_int(1), push_text("a"), push_int(2), push_text("b"), push_int(2), rec(0x43, {}),
            rec(0x4C, {}), push_text("a"), rec(0x4E, {}), rec(0x26, {}), push_text("b"),
            rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"1", "2"}},
      {"a name never stored reads undefined",
       cat({push_int(7), push_text("a"), push_int(1), rec(0x43, {}), push_text("b"), rec(0x4E, {}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"undefined"}},
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

// Invariant: SetMember writes through the shared object so a write made on a copy reads back from the original, an array write at index 4 grows length to 5, and a write at index 0 keeps length 2; a write that did not grow the length would trace 0 or 2 on the wrong rows.
TEST(Avm1Interpreter, SetMemberAndArrayLengthFollowTheWrittenKey) {
  const std::vector<TableRow> rows = {
      {"a write through a copy reads back",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), rec(0x4C, {}), push_text("k"),
            push_int(9), rec(0x4F, {}), push_text("k"), rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"9"}},
      {"index 4 grows an empty array to length 5",
       cat({push_int(0), rec(0x42, {}), rec(0x4C, {}), push_int(4), push_int(1), rec(0x4F, {}),
            push_text("length"), rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"5"}},
      {"index 0 keeps a length of 2",
       cat({push_int(5), push_int(6), push_int(2), rec(0x42, {}), rec(0x4C, {}), push_int(0),
            push_int(9), rec(0x4F, {}), push_text("length"), rec(0x4E, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"2"}},
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

// Invariant: InitArray stores elements in push order and sets length to their count, so [10, 20] traces length 2 and element 1 is 20, and an empty array traces length 0; a reversed element order would trace 10 for element 1.
TEST(Avm1Interpreter, InitArrayLengthAndElementOrder) {
  const std::vector<TableRow> rows = {
      {"two elements",
       cat({push_int(10), push_int(20), push_int(2), rec(0x42, {}), rec(0x4C, {}),
            push_text("length"), rec(0x4E, {}), rec(0x26, {}), rec(0x4C, {}), push_int(1),
            rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"2", "20"}},
      {"no elements",
       cat({push_int(0), rec(0x42, {}), push_text("length"), rec(0x4E, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"0"}},
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

// Invariant: a member of undefined reads undefined, while a member of a number, a new of another class, a write to an array length and an InitArray count past the stack each refuse and trace nothing, so the slice never guesses at an AS2 result it does not model.
TEST(Avm1Interpreter, MemberAndConstructorRefusalsTraceNothing) {
  const std::vector<TableRow> rows = {
      {"a member of undefined reads undefined",
       cat({rec(0x96, Bytes{0x03}), push_text("x"), rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"undefined"}},
      {"a member of a number refuses",
       cat({push_int(1), push_text("x"), rec(0x4E, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       {}},
      {"setting the length of an array refuses",
       cat({push_int(0), rec(0x42, {}), push_text("length"), push_int(5), rec(0x4F, {}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       {}},
      {"new of another class refuses",
       cat({push_int(0), push_text("Array"), rec(0x40, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Unsupported,
       {}},
      {"InitArray with a count past the stack underflows",
       cat({push_int(1), push_int(3), rec(0x42, {}), rec(0x26, {}), Bytes{0x00}}),
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

// Invariant: Delete and Delete2 push true only when a property or variable was removed, so a second delete of the same name traces false, an array length is not removable, and a delete on a number traces false; a stub that always pushes true traces true on the rows that must read false.
TEST(Avm1Interpreter, DeleteReturnsWhetherSomethingWasRemoved) {
  const std::vector<TableRow> rows = {
      {"a property and a variable are removed once",
       cat({push_int(1), push_text("k"), push_int(1), rec(0x43, {}), rec(0x4C, {}),
            push_text("k"), rec(0x3A, {}), rec(0x26, {}), rec(0x4C, {}), push_text("k"),
            rec(0x3A, {}), rec(0x26, {}), push_text("v"), push_int(5), rec(0x1D, {}),
            push_text("v"), rec(0x3B, {}), rec(0x26, {}), push_text("v"), rec(0x3B, {}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true", "false", "true", "false"}},
      {"delete on a number is false",
       cat({push_int(1), push_text("k"), rec(0x3A, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"delete of an array length is false",
       cat({push_int(0), rec(0x42, {}), push_text("length"), rec(0x3A, {}), rec(0x26, {}),
            Bytes{0x00}}),
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

// Invariant: an object converts to the text "[object Object]" and to NaN, two new objects never compare equal, a copy compares strictly equal to its original, and object plus text concatenates; a version-blind identity test would make two new objects equal and trace true on the third row.
TEST(Avm1Interpreter, ObjectsConvertToTheObjectTextAndCompareByIdentity) {
  const std::vector<TableRow> rows = {
      {"ToString of an object is its text",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), rec(0x4B, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"[object Object]"}},
      {"ToNumber of an object is NaN",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), rec(0x4A, {}), rec(0x26, {}),
            Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"NaN"}},
      {"two new objects are not equal",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), push_int(0), push_text("Object"),
            rec(0x40, {}), rec(0x49, {}), rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"false"}},
      {"a copy of an object is strictly equal to it",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), rec(0x4C, {}), rec(0x66, {}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"true"}},
      {"object plus text concatenates its text",
       cat({push_int(0), push_text("Object"), rec(0x40, {}), push_text("x"), rec(0x47, {}),
            rec(0x26, {}), Bytes{0x00}}),
       Avm1RunStatus::Ok,
       {"[object Object]x"}},
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

}  // namespace
