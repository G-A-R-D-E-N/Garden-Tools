#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/opcodes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexVariable;

/* Bytes a .pex string table may hold that are not ASCII. The C ABI must hand
   them back untouched: the container stores one byte per character and the
   character encoding above 0x7F is UNPROVEN, so nothing may be transcoded. */
const std::string kHighBytes = "\x80\xA9\xFE\xFF";

PexFile syntheticPex() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.compilationTime = 0x0102030405060708LL;
  pex.sourceFileName = "Synthetic.psc";
  pex.userName = "garden";
  pex.computerName = "garden-box";

  PexStruct structValue;
  structValue.name = "Point";

  PexVariable variable;
  variable.name = "Count";
  variable.type = "Int";

  PexProperty property;
  property.name = "Count";
  property.type = "Int";
  property.flags = 0;

  PexFunction function;
  function.name = "Run";

  PexState state;
  state.name = "";
  state.functions.push_back(function);

  PexObject object;
  object.name = "SyntheticObject";
  object.parentClassName = "ObjectReference";
  object.structs.push_back(structValue);
  object.variables.push_back(variable);
  object.properties.push_back(property);
  object.states.push_back(state);
  pex.objects.push_back(object);

  pex.rebuildStringTable();
  pex.stringTable.push_back(kHighBytes);
  return pex;
}

std::vector<std::uint8_t> syntheticBytes() { return syntheticPex().toBytes(); }

std::string asString(const uint8_t* bytes, size_t size) {
  if (!bytes || size == 0) return std::string();
  return std::string(reinterpret_cast<const char*>(bytes), size);
}

class TempFile {
public:
  TempFile() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("radicle_c_api_pex_" + std::to_string(id) + ".pex");
  }

  ~TempFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

  void write(const std::vector<std::uint8_t>& bytes) const {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

private:
  std::filesystem::path path_;
};

}

TEST(CApiPex, ParsesASyntheticFileAndReportsHeaderFacts) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_pex_header header{};
  ASSERT_EQ(radicle_pex_get_header(pex, &header), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(header.major_version, 3);
  EXPECT_EQ(header.minor_version, 9);
  EXPECT_EQ(header.game_id, 2);
  EXPECT_EQ(header.compilation_time, 0x0102030405060708LL);
  EXPECT_EQ(header.object_count, 1u);
  EXPECT_GE(header.string_count, 8u);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, ReportsObjectCountsAndNames) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  radicle_pex_object_counts counts{};
  ASSERT_EQ(radicle_pex_object_get_counts(pex, 0, &counts), RADICLE_STATUS_OK);
  EXPECT_EQ(counts.struct_count, 1u);
  EXPECT_EQ(counts.variable_count, 1u);
  EXPECT_EQ(counts.property_count, 1u);
  EXPECT_EQ(counts.state_count, 1u);
  EXPECT_EQ(counts.function_count, 1u);

  uint8_t* name = nullptr;
  size_t name_size = 0;
  ASSERT_EQ(radicle_pex_object_name(pex, 0, &name, &name_size), RADICLE_STATUS_OK);
  EXPECT_EQ(asString(name, name_size), "SyntheticObject");
  radicle_free_bytes(name);

  uint8_t* parent = nullptr;
  size_t parent_size = 0;
  ASSERT_EQ(radicle_pex_object_parent_name(pex, 0, &parent, &parent_size), RADICLE_STATUS_OK);
  EXPECT_EQ(asString(parent, parent_size), "ObjectReference");
  radicle_free_bytes(parent);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, StringBytesAreReturnedVerbatimWithoutTranscoding) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  radicle_pex_header header{};
  ASSERT_EQ(radicle_pex_get_header(pex, &header), RADICLE_STATUS_OK);
  ASSERT_GE(header.string_count, 1u);

  uint8_t* entry = nullptr;
  size_t entry_size = 0;
  ASSERT_EQ(radicle_pex_string_table_entry(pex, header.string_count - 1, &entry, &entry_size),
            RADICLE_STATUS_OK);
  EXPECT_EQ(asString(entry, entry_size), kHighBytes);
  EXPECT_EQ(entry_size, kHighBytes.size());
  radicle_free_bytes(entry);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, EmptyStringTableEntryIsNotNullAndZeroLength) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  radicle_pex_header header{};
  ASSERT_EQ(radicle_pex_get_header(pex, &header), RADICLE_STATUS_OK);

  bool sawEmpty = false;
  for (size_t index = 0; index < header.string_count; ++index) {
    uint8_t* entry = nullptr;
    size_t entry_size = 1;
    ASSERT_EQ(radicle_pex_string_table_entry(pex, index, &entry, &entry_size), RADICLE_STATUS_OK);
    if (entry_size == 0) {
      sawEmpty = true;
      EXPECT_EQ(entry, nullptr);
    } else {
      ASSERT_NE(entry, nullptr);
      radicle_free_bytes(entry);
    }
  }
  EXPECT_TRUE(sawEmpty);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, RoundTripsThroughWriteBytes) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  uint8_t* written = nullptr;
  size_t written_size = 0;
  ASSERT_EQ(radicle_pex_write_bytes(pex, &written, &written_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(written_size, bytes.size());
  ASSERT_NE(written, nullptr);
  EXPECT_EQ(std::memcmp(written, bytes.data(), bytes.size()), 0);
  radicle_free_bytes(written);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, ReadsFromFileAndRoundTrips) {
  const TempFile file;
  const auto bytes = syntheticBytes();
  file.write(bytes);

  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_read_file(file.path().string().c_str(), &pex), RADICLE_STATUS_OK)
      << radicle_last_error();

  uint8_t* written = nullptr;
  size_t written_size = 0;
  ASSERT_EQ(radicle_pex_write_bytes(pex, &written, &written_size), RADICLE_STATUS_OK);
  ASSERT_EQ(written_size, bytes.size());
  EXPECT_EQ(std::memcmp(written, bytes.data(), bytes.size()), 0);
  radicle_free_bytes(written);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, MissingFileReturnsIoError) {
  radicle_pex* pex = nullptr;
  EXPECT_EQ(radicle_pex_read_file("radicle_no_such_directory/no_such_file.pex", &pex),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(pex, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);
}

TEST(CApiPex, MalformedBytesReturnParseError) {
  const std::vector<std::uint8_t> empty;
  radicle_pex* pex = nullptr;
  EXPECT_EQ(radicle_pex_parse(empty.data(), empty.size(), &pex), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(pex, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  const std::vector<std::uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
  EXPECT_EQ(radicle_pex_parse(garbage.data(), garbage.size(), &pex), RADICLE_STATUS_PARSE_ERROR);

  auto truncated = syntheticBytes();
  truncated.resize(truncated.size() / 2);
  EXPECT_EQ(radicle_pex_parse(truncated.data(), truncated.size(), &pex), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(pex, nullptr);
}

TEST(CApiPex, NullArgumentsReturnInvalidArgument) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  uint8_t* out_bytes = nullptr;
  size_t out_size = 0;
  radicle_pex_header header{};
  radicle_pex_object_counts counts{};

  EXPECT_EQ(radicle_pex_parse(nullptr, bytes.size(), &pex), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_read_file(nullptr, &pex), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_get_header(nullptr, &header), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_get_counts(nullptr, 0, &counts), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_name(nullptr, 0, &out_bytes, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_parent_name(nullptr, 0, &out_bytes, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_string_table_entry(nullptr, 0, &out_bytes, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_write_bytes(nullptr, &out_bytes, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_pex_destroy(nullptr);

  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_pex_get_header(pex, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_get_counts(pex, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_name(pex, 0, nullptr, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_write_bytes(pex, nullptr, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_pex_destroy(pex);
}

TEST(CApiPex, OutOfRangeIndexesReturnInvalidArgument) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  radicle_pex_header header{};
  ASSERT_EQ(radicle_pex_get_header(pex, &header), RADICLE_STATUS_OK);

  radicle_pex_object_counts counts{};
  uint8_t* out_bytes = reinterpret_cast<uint8_t*>(1);
  size_t out_size = 99;

  EXPECT_EQ(radicle_pex_object_get_counts(pex, header.object_count, &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_name(pex, header.object_count, &out_bytes, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_bytes, nullptr);
  EXPECT_EQ(out_size, 0u);

  EXPECT_EQ(radicle_pex_string_table_entry(pex, header.string_count, &out_bytes, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_bytes, nullptr);
  EXPECT_EQ(out_size, 0u);

  radicle_pex_destroy(pex);
}

TEST(CApiPex, HandlesAreIndependent) {
  const auto bytes = syntheticBytes();
  radicle_pex* first = nullptr;
  radicle_pex* second = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &first), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &second), RADICLE_STATUS_OK);
  EXPECT_NE(first, second);

  radicle_pex_destroy(first);

  radicle_pex_header header{};
  EXPECT_EQ(radicle_pex_get_header(second, &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.object_count, 1u);
  radicle_pex_destroy(second);
}

TEST(CApiPex, PublicStructLayoutKeepsFieldOrderAndNaturalAlignment) {
  EXPECT_EQ(offsetof(radicle_pex_header, major_version), 0u);
  EXPECT_EQ(offsetof(radicle_pex_header, minor_version), 1u);
  EXPECT_EQ(offsetof(radicle_pex_header, game_id), 2u);
  EXPECT_EQ(offsetof(radicle_pex_header, compilation_time) % alignof(std::int64_t), 0u);
  EXPECT_EQ(offsetof(radicle_pex_header, string_count) % alignof(size_t), 0u);
  EXPECT_EQ(offsetof(radicle_pex_header, object_count) % alignof(size_t), 0u);
  EXPECT_EQ(sizeof(radicle_pex_header) % alignof(std::int64_t), 0u);

  EXPECT_LT(offsetof(radicle_pex_header, major_version), offsetof(radicle_pex_header, game_id));
  EXPECT_LT(offsetof(radicle_pex_header, game_id), offsetof(radicle_pex_header, compilation_time));
  EXPECT_LT(offsetof(radicle_pex_header, compilation_time), offsetof(radicle_pex_header, string_count));
  EXPECT_LT(offsetof(radicle_pex_header, string_count), offsetof(radicle_pex_header, object_count));
  EXPECT_LT(offsetof(radicle_pex_header, object_count), sizeof(radicle_pex_header));

  EXPECT_EQ(offsetof(radicle_pex_object_counts, struct_count), 0u);
  EXPECT_EQ(offsetof(radicle_pex_object_counts, function_count), sizeof(size_t) * 4);
  EXPECT_EQ(sizeof(radicle_pex_object_counts), sizeof(size_t) * 5);
}

namespace {

fo4::pex::PexValue pexValue(fo4::pex::PexValueType type, const std::string& text = {}, std::int32_t integer = 0,
                            float real = 0.0F, bool flag = false) {
  fo4::pex::PexValue value;
  value.type = type;
  value.str = text;
  value.int_ = integer;
  value.float_ = real;
  value.bool_ = flag;
  return value;
}

fo4::pex::PexInstruction pexInstruction(std::uint8_t opcode, std::vector<fo4::pex::PexValue> args) {
  fo4::pex::PexInstruction instruction;
  instruction.opCode = opcode;
  instruction.args = std::move(args);
  return instruction;
}

PexFile codePex() {
  using fo4::pex::PexValueType;
  PexFile pex = syntheticPex();
  auto& object = pex.objects[0];
  object.autoStateName = "";

  auto& run = object.states[0].functions[0];
  run.returnType = "Int";
  run.isGlobal = true;
  run.params.push_back({"akTarget", "ObjectReference"});
  run.locals.push_back({"::temp0", "Int"});
  run.locals.push_back({"::NoneVar", "None"});
  run.instructions.push_back(pexInstruction(13, {pexValue(PexValueType::Identifier, "::temp0"),
                                                 pexValue(PexValueType::Integer, {}, -7)}));
  run.instructions.push_back(pexInstruction(23, {pexValue(PexValueType::Identifier, "Activate"),
                                                 pexValue(PexValueType::Identifier, "akTarget"),
                                                 pexValue(PexValueType::Identifier, "::NoneVar"),
                                                 pexValue(PexValueType::Float, {}, 0, 1.5F),
                                                 pexValue(PexValueType::Bool, {}, 0, 0.0F, true)}));
  run.instructions.push_back(pexInstruction(25, {pexValue(PexValueType::Identifier, "Debug"),
                                                 pexValue(PexValueType::Identifier, "Trace"),
                                                 pexValue(PexValueType::Identifier, "::NoneVar"),
                                                 pexValue(PexValueType::String, "hello")}));
  run.instructions.push_back(pexInstruction(26, {pexValue(PexValueType::Identifier, "::temp0")}));

  fo4::pex::PexFunction busy;
  busy.name = "OnBusy";
  fo4::pex::PexState waiting;
  waiting.name = "Waiting";
  waiting.functions.push_back(busy);
  object.states.push_back(waiting);

  object.variables[0].defaultValue = pexValue(PexValueType::Integer, {}, 3);
  object.properties[0].flags = 0x03;
  fo4::pex::PexFunction getter;
  getter.name = "";
  getter.returnType = "Int";
  getter.instructions.push_back(pexInstruction(26, {pexValue(PexValueType::Integer, {}, 9)}));
  object.properties[0].readHandler = getter;
  fo4::pex::PexFunction setter;
  setter.params.push_back({"value", "Int"});
  object.properties[0].writeHandler = setter;

  fo4::pex::PexStructMember member;
  member.name = "X";
  member.type = "Float";
  member.defaultValue = pexValue(PexValueType::Float, {}, 0, 2.0F);
  object.structs[0].members.push_back(member);

  pex.rebuildStringTable();
  return pex;
}

std::string tableEntry(const radicle_pex* pex, size_t index) {
  if (index == RADICLE_PEX_NO_STRING) return "<none>";
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_pex_string_table_entry(pex, index, &bytes, &size), RADICLE_STATUS_OK);
  auto text = asString(bytes, size);
  radicle_free_bytes(bytes);
  return text;
}

}

TEST(CApiPex, ObjectCodeFlattensStatesFunctionsAndInstructions) {
  const auto bytes = codePex().toBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);

  radicle_pex_object_code code{};
  ASSERT_EQ(radicle_pex_object_code_get(pex, 0, &code), RADICLE_STATUS_OK);

  ASSERT_EQ(code.state_count, 2u);
  EXPECT_EQ(tableEntry(pex, code.states[1].name), "Waiting");
  ASSERT_EQ(code.function_count, 4u);
  EXPECT_EQ(code.states[0].first_function, 0u);
  EXPECT_EQ(code.states[1].first_function, 1u);

  const auto& run = code.functions[0];
  EXPECT_EQ(run.state, 0u);
  EXPECT_EQ(tableEntry(pex, run.name), "Run");
  EXPECT_EQ(tableEntry(pex, run.return_type), "Int");
  EXPECT_EQ(run.is_global, 1);
  ASSERT_EQ(run.parameter_count, 1u);
  EXPECT_EQ(tableEntry(pex, code.typed_names[run.first_parameter].name), "akTarget");
  EXPECT_EQ(tableEntry(pex, code.typed_names[run.first_parameter].type), "ObjectReference");
  ASSERT_EQ(run.local_count, 2u);
  EXPECT_EQ(tableEntry(pex, code.typed_names[run.first_local + 1].name), "::NoneVar");
  ASSERT_EQ(run.instruction_count, 4u);

  const auto& assign = code.instructions[run.first_instruction];
  EXPECT_EQ(assign.opcode, 13);
  ASSERT_EQ(assign.argument_count, 2u);
  EXPECT_EQ(code.values[assign.first_argument + 1].type, 3);
  EXPECT_EQ(code.values[assign.first_argument + 1].integer, -7);

  const auto& callmethod = code.instructions[run.first_instruction + 1];
  EXPECT_EQ(callmethod.opcode, 23);
  ASSERT_EQ(callmethod.argument_count, 5u);
  const auto* args = &code.values[callmethod.first_argument];
  EXPECT_EQ(tableEntry(pex, args[0].string), "Activate");
  EXPECT_EQ(tableEntry(pex, args[1].string), "akTarget");
  EXPECT_EQ(args[3].type, 4);
  EXPECT_FLOAT_EQ(args[3].real, 1.5F);
  EXPECT_EQ(args[4].type, 5);
  EXPECT_EQ(args[4].boolean, 1);

  const auto& callstatic = code.instructions[run.first_instruction + 2];
  EXPECT_EQ(callstatic.opcode, 25);
  ASSERT_EQ(callstatic.argument_count, 4u);
  EXPECT_EQ(code.values[callstatic.first_argument + 3].type, 2);
  EXPECT_EQ(tableEntry(pex, code.values[callstatic.first_argument + 3].string), "hello");

  EXPECT_EQ(code.functions[1].state, 1u);
  EXPECT_EQ(tableEntry(pex, code.functions[1].name), "OnBusy");

  ASSERT_EQ(code.property_count, 1u);
  const auto& property = code.properties[0];
  EXPECT_EQ(property.flags, 0x03);
  ASSERT_EQ(property.read_function, 2u);
  ASSERT_EQ(property.write_function, 3u);
  EXPECT_EQ(code.functions[2].state, RADICLE_PEX_NO_INDEX);
  EXPECT_EQ(code.functions[2].instruction_count, 1u);
  EXPECT_EQ(code.functions[3].parameter_count, 1u);

  ASSERT_EQ(code.variable_count, 1u);
  EXPECT_EQ(code.variables[0].has_default, 1);
  EXPECT_EQ(code.variables[0].default_value.integer, 3);
  ASSERT_EQ(code.struct_count, 1u);
  ASSERT_EQ(code.structs[0].member_count, 1u);
  EXPECT_EQ(tableEntry(pex, code.struct_members[0].name), "X");
  EXPECT_FLOAT_EQ(code.struct_members[0].default_value.real, 2.0F);

  radicle_pex_free_object_code(&code);
  EXPECT_EQ(code.functions, nullptr);
  EXPECT_EQ(code.function_count, 0u);
  radicle_pex_destroy(pex);
}

TEST(CApiPex, ObjectCodeRejectsBadArgumentsAndFreesZeroedStructs) {
  const auto bytes = syntheticBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
  radicle_pex_object_code code{};
  EXPECT_EQ(radicle_pex_object_code_get(nullptr, 0, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_code_get(pex, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_object_code_get(pex, 1, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(code.states, nullptr);
  radicle_pex_free_object_code(&code);
  radicle_pex_free_object_code(nullptr);
  radicle_pex_destroy(pex);
}

TEST(CApiPex, SnapshotCountsExcludeBothPropertyHandlers) {
  const auto bytes = codePex().toBytes();
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
  radicle_pex_object_counts counts{};
  radicle_pex_object_code code{};
  ASSERT_EQ(radicle_pex_object_get_counts(pex, 0, &counts), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_pex_object_code_get(pex, 0, &code), RADICLE_STATUS_OK);
  EXPECT_EQ(counts.function_count, 2u);
  EXPECT_EQ(code.function_count, 4u);
  EXPECT_EQ(code.properties[0].read_function, 2u);
  EXPECT_EQ(code.properties[0].write_function, 3u);
  radicle_pex_free_object_code(&code);
  radicle_pex_destroy(pex);
}

TEST(CApiPex, DuplicateStringsCanonicalizeToTheFirstMatchingEntry) {
  auto model = syntheticPex();
  model.objects[0].states[0].functions[0].instructions.push_back(
      pexInstruction(26, {pexValue(fo4::pex::PexValueType::String, kHighBytes)}));
  const auto first = model.stringTable.size() - 1;
  const auto repeated = model.stringTable;
  model.stringTable.insert(model.stringTable.end(), repeated.begin(), repeated.end());
  const auto duplicate = model.stringTable.size() - 1;
  auto bytes = model.toBytes();
  bytes[bytes.size() - 2] = static_cast<uint8_t>(duplicate);
  bytes.back() = static_cast<uint8_t>(duplicate >> 8);
  ASSERT_EQ(fo4::pex::PexFile::fromBytes(bytes).objects[0].states[0].functions[0].instructions[0].args[0].str,
            kHighBytes);
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
  radicle_pex_object_code code{};
  ASSERT_EQ(radicle_pex_object_code_get(pex, 0, &code), RADICLE_STATUS_OK);
  EXPECT_EQ(tableEntry(pex, duplicate), kHighBytes);
  EXPECT_EQ(code.values[0].string, first);
  EXPECT_NE(code.values[0].string, duplicate);
  EXPECT_EQ(tableEntry(pex, code.values[0].string), kHighBytes);
  EXPECT_EQ(code.functions[0].name,
            static_cast<size_t>(std::find(model.stringTable.begin(), model.stringTable.end(), "Run") - model.stringTable.begin()));
  EXPECT_EQ(tableEntry(pex, code.functions[0].name), "Run");
  radicle_pex_free_object_code(&code);
  radicle_pex_destroy(pex);
}

TEST(CApiPex, ScriptResolveReturnsWinnerIndexesAndReplacedCounts) {
  const std::string name = "foo";
  const std::string archive = "Fallout4 - Main.ba2";
  const std::string loose = "Data/Scripts/foo.pex";
  const radicle_script_candidate candidates[] = {
      {reinterpret_cast<const uint8_t*>(name.data()), name.size(), RADICLE_PEX_SOURCE_ARCHIVE,
       reinterpret_cast<const uint8_t*>(archive.data()), archive.size(), 5, 10},
      {reinterpret_cast<const uint8_t*>(name.data()), name.size(), RADICLE_PEX_SOURCE_LOOSE,
       reinterpret_cast<const uint8_t*>(loose.data()), loose.size(), 0, 12},
  };
  radicle_script_winner* winners = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_script_resolve(candidates, 2, &winners, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(winners[0].candidate_index, 1u);
  EXPECT_EQ(winners[0].replaced, 1u);
  radicle_script_free_winners(winners);
}

TEST(CApiPex, ScriptResolveRejectsAnUnknownSourceKind) {
  const std::string name = "foo";
  const radicle_script_candidate candidate{reinterpret_cast<const uint8_t*>(name.data()), name.size(), 7,
                                           nullptr, 0, 0, 1};
  radicle_script_winner* winners = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_script_resolve(&candidate, 1, &winners, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(winners, nullptr);
  EXPECT_EQ(count, 0u);
}

TEST(CApiPex, OpcodeNameMatchesTheTableForEveryTableEntry) {
  constexpr std::size_t kOpcodeCount = std::size(PexFile::kOpCodes);
  ASSERT_EQ(kOpcodeCount, static_cast<std::size_t>(47));
  for (std::size_t index = 0; index < kOpcodeCount; ++index) {
    const auto opcode = static_cast<std::uint8_t>(index);
    uint8_t* bytes = nullptr;
    size_t size = 0;
    ASSERT_EQ(radicle_pex_opcode_name(opcode, &bytes, &size), RADICLE_STATUS_OK)
        << "opcode " << static_cast<int>(opcode);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), size),
              fo4::pex_analysis::pex_op_name(opcode))
        << "opcode " << static_cast<int>(opcode);
    radicle_free_bytes(bytes);
  }
}

namespace {

std::string opcode_name(std::uint8_t opcode) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  if (radicle_pex_opcode_name(opcode, &bytes, &size) != RADICLE_STATUS_OK) return std::string();
  std::string name(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return name;
}

}  // namespace

TEST(CApiPex, OpcodeNamePinsTheFirstAndLastTableEntries) {
  EXPECT_EQ(opcode_name(0), "nop");
  EXPECT_EQ(opcode_name(46), "array_clear");
}

TEST(CApiPex, OpcodeNameRefusesAByteOutsideTheTable) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(1);
  size_t size = 9;
  EXPECT_EQ(radicle_pex_opcode_name(47, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_STREQ(radicle_last_error(), "opcode 47 is outside the 0..46 name table");

  EXPECT_EQ(radicle_pex_opcode_name(255, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_STREQ(radicle_last_error(), "opcode 255 is outside the 0..46 name table");
}

TEST(CApiPex, OpcodeNameRequiresItsOutputParameters) {
  EXPECT_EQ(radicle_pex_opcode_name(0, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "out_bytes and out_size are required");
}

TEST(CApiPex, ScriptNormalizeNameReturnsTheLibraryKey) {
  const std::string path = "Data\\Scripts\\Quest\\Foo.PEX";
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_normalize_name(reinterpret_cast<const uint8_t*>(path.data()), path.size(), &bytes, &size),
            RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), size), "quest/foo");
  radicle_free_bytes(bytes);
}

TEST(CApiPex, ScriptNormalizeNameReportsInvalidArgumentForParentComponent) {
  const std::string path = "Scripts/../escape.pex";
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_script_normalize_name(reinterpret_cast<const uint8_t*>(path.data()), path.size(), &bytes, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
}
