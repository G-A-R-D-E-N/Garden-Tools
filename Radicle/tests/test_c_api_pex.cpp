#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    path_ = std::filesystem::temp_directory_path() / "radicle_c_api_pex_test.pex";
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

TEST(CApiPex, AbiVersionIsAtLeastThePexExports) {
  EXPECT_GE(radicle_abi_version(), 5u);
  EXPECT_EQ(RADICLE_ABI_VERSION, radicle_abi_version());
}
