#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using fo4::pex::PexDebugFunction;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexPropertyGroup;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
using fo4::pex::PexStructOrder;
using fo4::pex::PexTypedName;
using fo4::pex::PexUserFlag;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex::PexVariable;

namespace {

PexValue identifier(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue integer(std::int32_t number) {
  PexValue value;
  value.type = PexValueType::Integer;
  value.int_ = number;
  return value;
}

PexValue none() { return PexValue{}; }

PexTypedName typed(std::string name, std::string type) {
  return PexTypedName{std::move(name), std::move(type)};
}

/* Synthetic .pex content. Every fixture in this file is project-authored; no
   shipped or third-party script is read, stored or derived from. */
PexFile syntheticFile() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.compilationTime = 0x0102030405060708LL;
  pex.sourceFileName = "Synthetic.psc";
  pex.userName = "garden";
  pex.computerName = "garden-box";
  pex.hasDebugInfo = true;
  pex.modificationTime = 0x1122334455667788LL;

  PexDebugFunction debug;
  debug.objectName = "SyntheticObject";
  debug.stateName = "";
  debug.functionName = "Run";
  debug.functionType = 0;
  debug.lineNumbers = {1, 2, 3};
  pex.debugFunctions.push_back(debug);

  PexPropertyGroup group;
  group.objectName = "SyntheticObject";
  group.groupName = "Group";
  group.docString = "group doc";
  group.userFlags = 7;
  group.propertyNames = {"Count"};
  pex.propertyGroups.push_back(group);

  PexStructOrder order;
  order.objectName = "SyntheticObject";
  order.orderName = "Order";
  order.memberNames = {"X"};
  pex.structOrders.push_back(order);

  pex.userFlags.push_back(PexUserFlag{"Hidden", 0});
  pex.userFlags.push_back(PexUserFlag{"Conditional", 1});

  PexStruct point;
  point.name = "Point";
  PexStructMember member;
  member.name = "X";
  member.type = "Int";
  member.userFlags = 0;
  member.defaultValue = integer(3);
  member.isConst = false;
  member.docString = "member doc";
  point.members.push_back(member);

  PexVariable variable;
  variable.name = "Count";
  variable.type = "Int";
  variable.userFlags = 0;
  variable.defaultValue = integer(0x11223344);
  variable.isConst = false;

  PexProperty property;
  property.name = "Count";
  property.type = "Int";
  property.docString = "property doc";
  property.userFlags = 0;
  property.flags = 0x07;
  property.autoVarName = "::Count_var";

  PexFunction function;
  function.name = "Run";
  function.returnType = "None";
  function.docString = "function doc";
  function.userFlags = 0;
  function.isGlobal = false;
  function.isNative = false;
  function.params.push_back(typed("a", "Int"));
  function.locals.push_back(typed("t", "Float"));

  PexInstruction assign;
  assign.opCode = 13;
  assign.args = {identifier("Count"), integer(5)};
  function.instructions.push_back(assign);

  PexInstruction call;
  call.opCode = 23;
  call.args = {identifier("Count"), identifier("Run"), identifier("Count"), integer(1)};
  function.instructions.push_back(call);

  PexInstruction ret;
  ret.opCode = 26;
  ret.args = {none()};
  function.instructions.push_back(ret);

  PexState state;
  state.name = "";
  state.functions.push_back(function);

  PexObject object;
  object.name = "SyntheticObject";
  object.parentClassName = "ObjectReference";
  object.docString = "object doc";
  object.isConst = false;
  object.userFlags = 0;
  object.autoStateName = "";
  object.sizeIncludesItself = true;
  object.structs.push_back(point);
  object.variables.push_back(variable);
  object.properties.push_back(property);
  object.states.push_back(state);
  pex.objects.push_back(object);

  pex.rebuildStringTable();
  return pex;
}

std::vector<std::uint8_t> encoded(const PexFile& pex) { return pex.toBytes(); }

std::size_t findSequence(const std::vector<std::uint8_t>& haystack,
                         const std::vector<std::uint8_t>& needle) {
  if (needle.empty() || needle.size() > haystack.size()) return std::string::npos;
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool match = true;
    for (std::size_t k = 0; k < needle.size(); ++k) {
      if (haystack[i + k] != needle[k]) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return std::string::npos;
}

void expectParseError(const std::vector<std::uint8_t>& bytes, const std::string& expected) {
  try {
    static_cast<void>(PexFile::fromBytes(bytes));
    FAIL() << "expected a parse error";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
  } catch (...) {
    FAIL() << "expected std::runtime_error";
  }
}

void expectAnyParseError(const std::vector<std::uint8_t>& bytes) {
  try {
    static_cast<void>(PexFile::fromBytes(bytes));
    FAIL() << bytes.size() << "-byte prefix parsed";
  } catch (const std::runtime_error&) {
  } catch (...) {
    FAIL() << bytes.size() << "-byte prefix threw a non-runtime_error exception";
  }
}

}

TEST(Pex, HeaderFieldsSurviveARoundTrip) {
  const auto bytes = encoded(syntheticFile());
  const PexFile decoded = PexFile::fromBytes(bytes);
  EXPECT_EQ(decoded.majorVersion, 3);
  EXPECT_EQ(decoded.minorVersion, 9);
  EXPECT_EQ(decoded.gameId, 2);
  EXPECT_EQ(decoded.compilationTime, 0x0102030405060708LL);
  EXPECT_EQ(decoded.sourceFileName, "Synthetic.psc");
  EXPECT_EQ(decoded.userName, "garden");
  EXPECT_EQ(decoded.computerName, "garden-box");
  EXPECT_TRUE(decoded.hasDebugInfo);
  EXPECT_EQ(decoded.modificationTime, 0x1122334455667788LL);
}

TEST(Pex, DebugSectionsSurviveARoundTrip) {
  const auto bytes = encoded(syntheticFile());
  const PexFile decoded = PexFile::fromBytes(bytes);
  ASSERT_EQ(decoded.debugFunctions.size(), 1u);
  EXPECT_EQ(decoded.debugFunctions[0].objectName, "SyntheticObject");
  EXPECT_EQ(decoded.debugFunctions[0].functionName, "Run");
  EXPECT_EQ(decoded.debugFunctions[0].lineNumbers, (std::vector<std::uint16_t>{1, 2, 3}));
  ASSERT_EQ(decoded.propertyGroups.size(), 1u);
  EXPECT_EQ(decoded.propertyGroups[0].groupName, "Group");
  EXPECT_EQ(decoded.propertyGroups[0].propertyNames, (std::vector<std::string>{"Count"}));
  ASSERT_EQ(decoded.structOrders.size(), 1u);
  EXPECT_EQ(decoded.structOrders[0].memberNames, (std::vector<std::string>{"X"}));
  ASSERT_EQ(decoded.userFlags.size(), 2u);
  EXPECT_EQ(decoded.userFlags[0].name, "Hidden");
  EXPECT_EQ(decoded.userFlags[1].index, 1);
}

TEST(Pex, ObjectModelSurvivesARoundTrip) {
  const auto bytes = encoded(syntheticFile());
  const PexFile decoded = PexFile::fromBytes(bytes);
  ASSERT_EQ(decoded.objects.size(), 1u);
  const PexObject& object = decoded.objects[0];
  EXPECT_EQ(object.name, "SyntheticObject");
  EXPECT_EQ(object.parentClassName, "ObjectReference");
  EXPECT_EQ(object.docString, "object doc");
  ASSERT_EQ(object.structs.size(), 1u);
  EXPECT_EQ(object.structs[0].name, "Point");
  ASSERT_EQ(object.structs[0].members.size(), 1u);
  EXPECT_EQ(object.structs[0].members[0].docString, "member doc");
  ASSERT_EQ(object.variables.size(), 1u);
  ASSERT_TRUE(object.variables[0].defaultValue.has_value());
  EXPECT_EQ(object.variables[0].defaultValue->int_, 0x11223344);
  ASSERT_EQ(object.properties.size(), 1u);
  EXPECT_TRUE(object.properties[0].isAutoVar());
  EXPECT_TRUE(object.properties[0].canRead());
  EXPECT_TRUE(object.properties[0].canWrite());
  EXPECT_EQ(object.properties[0].autoVarName, "::Count_var");
}

TEST(Pex, InstructionsSurviveARoundTrip) {
  const auto bytes = encoded(syntheticFile());
  const PexFile decoded = PexFile::fromBytes(bytes);
  ASSERT_EQ(decoded.objects.size(), 1u);
  ASSERT_EQ(decoded.objects[0].states.size(), 1u);
  ASSERT_EQ(decoded.objects[0].states[0].functions.size(), 1u);
  const PexFunction& function = decoded.objects[0].states[0].functions[0];
  EXPECT_EQ(function.name, "Run");
  EXPECT_EQ(function.returnType, "None");
  ASSERT_EQ(function.params.size(), 1u);
  EXPECT_EQ(function.params[0].type, "Int");
  ASSERT_EQ(function.locals.size(), 1u);
  EXPECT_EQ(function.locals[0].type, "Float");
  ASSERT_EQ(function.instructions.size(), 3u);
  EXPECT_EQ(function.instructions[0].opCode, 13);
  EXPECT_EQ(function.instructions[0].mnemonic, "assign");
  EXPECT_EQ(function.instructions[0].fixedArgCount, 2);
  ASSERT_EQ(function.instructions[0].args.size(), 2u);
  EXPECT_EQ(function.instructions[0].args[0].str, "Count");
  EXPECT_EQ(function.instructions[0].args[1].int_, 5);
  EXPECT_EQ(function.instructions[1].opCode, 23);
  EXPECT_TRUE(function.instructions[1].hasVarArgs);
  EXPECT_EQ(function.instructions[1].args.size(), 4u);
  EXPECT_EQ(function.instructions[2].opCode, 26);
  EXPECT_EQ(function.instructions[2].args[0].type, PexValueType::None);
}

TEST(Pex, OpcodeTableIsTheFallout4Table) {
  EXPECT_EQ(std::size(PexFile::kOpCodes), 47u);
  EXPECT_STREQ(PexFile::kOpCodes[0].name, "nop");
  EXPECT_STREQ(PexFile::kOpCodes[46].name, "array_clear");
  EXPECT_TRUE(PexFile::kOpCodes[23].varargs);
  EXPECT_TRUE(PexFile::kOpCodes[24].varargs);
  EXPECT_TRUE(PexFile::kOpCodes[25].varargs);
  for (std::size_t i = 0; i < std::size(PexFile::kOpCodes); ++i) {
    if (i != 23u && i != 24u && i != 25u) {
      EXPECT_FALSE(PexFile::kOpCodes[i].varargs);
    }
  }
}

TEST(Pex, LatentValueOperandsPreserveEveryEightBitCharacter) {
  PexFile pex = syntheticFile();
  const std::string high = "\x80\xA9\xFE\xFF";
  PexValue stringValue;
  stringValue.type = PexValueType::String;
  stringValue.str = high;
  pex.objects[0].states[0].functions[0].instructions[0].args[0] = stringValue;
  pex.rebuildStringTable();

  const auto bytes = encoded(pex);
  const PexFile decoded = PexFile::fromBytes(bytes);
  EXPECT_EQ(decoded.objects[0].states[0].functions[0].instructions[0].args[0].str, high);
  EXPECT_EQ(decoded.toBytes(), bytes);
}

TEST(Pex, TrailingBytesArePreserved) {
  PexFile pex = syntheticFile();
  pex.trailingBytes = {0x00, 0x7F, 0x80, 0xFF};
  const auto bytes = encoded(pex);
  const PexFile decoded = PexFile::fromBytes(bytes);
  EXPECT_EQ(decoded.trailingBytes, pex.trailingBytes);
  EXPECT_EQ(decoded.toBytes(), bytes);
}

TEST(Pex, ObjectSizeConventionIsRecordedAndReproduced) {
  PexFile withItself = syntheticFile();
  PexFile bodyOnly = syntheticFile();
  bodyOnly.objects[0].sizeIncludesItself = false;

  const auto itselfBytes = encoded(withItself);
  const auto bodyBytes = encoded(bodyOnly);

  const PexFile decodedItself = PexFile::fromBytes(itselfBytes);
  const PexFile decodedBody = PexFile::fromBytes(bodyBytes);
  EXPECT_TRUE(decodedItself.objects[0].sizeIncludesItself);
  EXPECT_FALSE(decodedBody.objects[0].sizeIncludesItself);
  EXPECT_EQ(decodedItself.toBytes(), itselfBytes);
  EXPECT_EQ(decodedBody.toBytes(), bodyBytes);
  EXPECT_NE(itselfBytes, bodyBytes);
}

TEST(Pex, UnusedStringTableEntriesAreNotRebuiltOnWrite) {
  PexFile pex = syntheticFile();
  pex.stringTable.insert(pex.stringTable.begin(), "UnusedLeadingEntry");
  const auto bytes = encoded(pex);
  const PexFile decoded = PexFile::fromBytes(bytes);
  EXPECT_EQ(decoded.stringTable.front(), "UnusedLeadingEntry");
  EXPECT_EQ(decoded.toBytes(), bytes);
}

TEST(Pex, EmptyAndBadMagicFailClosed) {
  expectParseError({}, "Unexpected end of .pex data");
  expectParseError({0x00, 0x00, 0x00, 0x00}, "Not a .pex file");
  expectParseError({0xFA, 0x57, 0xC0, 0xDE}, "big-endian");
}

TEST(Pex, TruncatedFileFailsClosed) {
  const auto bytes = encoded(syntheticFile());
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    expectAnyParseError(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + length));
  }
}

TEST(Pex, ForeignGameIdFailsClosed) {
  std::vector<std::uint8_t> bytes = encoded(syntheticFile());
  ASSERT_GE(bytes.size(), 8u);
  bytes[6] = 1;
  bytes[7] = 0;
  expectParseError(bytes, "is for game id");
}

TEST(Pex, ValueTypeTagBeyondFiveFailsClosed) {
  std::vector<std::uint8_t> bytes = encoded(syntheticFile());
  const std::vector<std::uint8_t> integerMarker = {0x03, 0x44, 0x33, 0x22, 0x11};
  const std::size_t at = findSequence(bytes, integerMarker);
  ASSERT_NE(at, std::string::npos);
  bytes[at] = 0x06;
  expectParseError(bytes, "Invalid value type tag");
}

TEST(Pex, WritingAnUnknownOpcodeFailsClosed) {
  PexFile pex = syntheticFile();
  pex.objects[0].states[0].functions[0].instructions[0].opCode = 0xFF;
  EXPECT_THROW(static_cast<void>(pex.toBytes()), std::runtime_error);
}

TEST(Pex, WritingWrongOperandCountFailsClosed) {
  PexFile pex = syntheticFile();
  pex.objects[0].states[0].functions[0].instructions[0].args = {identifier("Count")};
  EXPECT_THROW(static_cast<void>(pex.toBytes()), std::runtime_error);
}

TEST(Pex, WritingAStringOutsideTheTableFailsClosed) {
  PexFile pex = syntheticFile();
  PexValue missing;
  missing.type = PexValueType::Identifier;
  missing.str = "NotInTheTable";
  pex.objects[0].states[0].functions[0].instructions[0].args[0] = missing;
  EXPECT_THROW(static_cast<void>(pex.toBytes()), std::runtime_error);
}
