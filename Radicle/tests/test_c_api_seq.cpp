#include "fo4/c_api.h"
#include "fo4/c_api_seq.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint32_t> values) {
  std::vector<std::uint8_t> out;
  for (const auto value : values) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
  }
  return out;
}

class TempFile {
public:
  TempFile() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("radicle_c_api_seq_" + std::to_string(id) + ".seq");
  }

  ~TempFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

  void write(const std::vector<std::uint8_t>& data) const {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

private:
  std::filesystem::path path_;
};

}

TEST(CApiSeq, ReportsEntriesAndRoundTrips) {
  const auto input = bytes({0x07000800u, 0x07000802u, 0x01001733u});
  radicle_seq* seq = nullptr;
  ASSERT_EQ(radicle_seq_parse(input.data(), input.size(), &seq), RADICLE_STATUS_OK)
      << radicle_last_error();

  size_t count = 0;
  ASSERT_EQ(radicle_seq_entry_count(seq, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);

  const std::uint32_t expected[] = {0x07000800u, 0x07000802u, 0x01001733u};
  for (size_t index = 0; index < count; ++index) {
    std::uint32_t form_id = 0;
    ASSERT_EQ(radicle_seq_entry_form_id(seq, index, &form_id), RADICLE_STATUS_OK);
    EXPECT_EQ(form_id, expected[index]);
  }

  uint8_t* written = nullptr;
  size_t written_size = 0;
  ASSERT_EQ(radicle_seq_write_bytes(seq, &written, &written_size), RADICLE_STATUS_OK);
  ASSERT_EQ(written_size, input.size());
  ASSERT_NE(written, nullptr);
  EXPECT_EQ(std::memcmp(written, input.data(), input.size()), 0);
  radicle_free_bytes(written);

  radicle_seq_destroy(seq);
}

TEST(CApiSeq, EmptyInputIsAnEmptyList) {
  radicle_seq* seq = nullptr;
  ASSERT_EQ(radicle_seq_parse(nullptr, 0, &seq), RADICLE_STATUS_OK) << radicle_last_error();

  size_t count = 1;
  ASSERT_EQ(radicle_seq_entry_count(seq, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);

  uint8_t* written = reinterpret_cast<uint8_t*>(1);
  size_t written_size = 1;
  ASSERT_EQ(radicle_seq_write_bytes(seq, &written, &written_size), RADICLE_STATUS_OK);
  EXPECT_EQ(written, nullptr);
  EXPECT_EQ(written_size, 0u);

  radicle_seq_destroy(seq);
}

TEST(CApiSeq, ReadsFromFileAndRoundTrips) {
  const TempFile file;
  const auto input = bytes({0x00000010u, 0x0000000Fu});
  file.write(input);

  radicle_seq* seq = nullptr;
  ASSERT_EQ(radicle_seq_read_file(file.path().string().c_str(), &seq), RADICLE_STATUS_OK)
      << radicle_last_error();

  uint8_t* written = nullptr;
  size_t written_size = 0;
  ASSERT_EQ(radicle_seq_write_bytes(seq, &written, &written_size), RADICLE_STATUS_OK);
  ASSERT_EQ(written_size, input.size());
  EXPECT_EQ(std::memcmp(written, input.data(), input.size()), 0);
  radicle_free_bytes(written);

  radicle_seq_destroy(seq);
}

TEST(CApiSeq, PartialEntryReturnsParseError) {
  const std::vector<std::uint8_t> input(6, 0x41);
  radicle_seq* seq = nullptr;
  EXPECT_EQ(radicle_seq_parse(input.data(), input.size(), &seq), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(seq, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);
}

TEST(CApiSeq, MissingFileReturnsIoError) {
  radicle_seq* seq = nullptr;
  EXPECT_EQ(radicle_seq_read_file("radicle_no_such_directory/no_such_file.seq", &seq),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(seq, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);
}

TEST(CApiSeq, NullArgumentsReturnInvalidArgument) {
  const auto input = bytes({0x07000800u});
  radicle_seq* seq = nullptr;
  size_t count = 0;
  std::uint32_t form_id = 0;
  uint8_t* out_bytes = nullptr;
  size_t out_size = 0;

  EXPECT_EQ(radicle_seq_parse(nullptr, input.size(), &seq), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_parse(input.data(), input.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_read_file(nullptr, &seq), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_entry_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_entry_form_id(nullptr, 0, &form_id), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_write_bytes(nullptr, &out_bytes, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_seq_destroy(nullptr);

  ASSERT_EQ(radicle_seq_parse(input.data(), input.size(), &seq), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_seq_entry_count(seq, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_entry_form_id(seq, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_seq_write_bytes(seq, nullptr, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_seq_destroy(seq);
}

TEST(CApiSeq, OutOfRangeIndexReturnsInvalidArgument) {
  const auto input = bytes({0x07000800u});
  radicle_seq* seq = nullptr;
  ASSERT_EQ(radicle_seq_parse(input.data(), input.size(), &seq), RADICLE_STATUS_OK);

  size_t count = 0;
  ASSERT_EQ(radicle_seq_entry_count(seq, &count), RADICLE_STATUS_OK);

  std::uint32_t form_id = 0xFFFFFFFFu;
  EXPECT_EQ(radicle_seq_entry_form_id(seq, count, &form_id), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(form_id, 0u);

  radicle_seq_destroy(seq);
}
