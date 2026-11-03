#include "fo4/localization/strings.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  return out;
}

std::string text(const fo4::localization::LocalizedStringEntry& entry) {
  std::string out;
  out.reserve(entry.text.size());
  for (const auto value : entry.text) {
    out.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }
  return out;
}

std::uint32_t u32(const std::vector<std::byte>& data, std::size_t offset) {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
}

void set_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  data[offset] = std::byte{static_cast<std::uint8_t>(value)};
  data[offset + 1] = std::byte{static_cast<std::uint8_t>(value >> 8)};
  data[offset + 2] = std::byte{static_cast<std::uint8_t>(value >> 16)};
  data[offset + 3] = std::byte{static_cast<std::uint8_t>(value >> 24)};
}

fo4::localization::LocalizedStringTable sample(fo4::localization::StringTableKind kind) {
  return {
      kind,
      {
          {20, bytes("Shared")},
          {10, bytes("Other")},
          {30, bytes("Shared")},
      },
  };
}

}  // namespace

// Detects: bounded held-source parsing must reject row, record and duplicate-offset text amplification before allocation.
TEST(LocalizedStrings, BoundedParsingRejectsAmplificationAndMalformedOffsets) {
  using namespace fo4::localization;
  for (const auto kind : {StringTableKind::strings, StringTableKind::dlstrings, StringTableKind::ilstrings}) {
    const auto encoded = write_string_table(sample(kind));
    EXPECT_THROW(read_string_table_bounded(encoded, kind, {2, 8, 100}), StringTableError);
    EXPECT_THROW(read_string_table_bounded(encoded, kind, {3, 1, 100}), StringTableError);
    EXPECT_THROW(read_string_table_bounded(encoded, kind, {3, 2, 16}), StringTableError);
    EXPECT_EQ(read_string_table_bounded(encoded, kind, {3, 2, 17}).entries.size(), 3u);
    auto malformed = encoded; set_u32(malformed, 12, 1);
    EXPECT_THROW(read_string_table_bounded(malformed, kind, {3, 2, 100}), StringTableError);
  }
}

TEST(LocalizedStrings, StringsSortsIdsButPreservesIndependentDataOffsets) {
  const auto encoded = fo4::localization::write_string_table(
      sample(fo4::localization::StringTableKind::strings));

  ASSERT_GE(encoded.size(), 8u + 3u * 8u);
  EXPECT_EQ(u32(encoded, 0), 3u);
  EXPECT_EQ(encoded.size(), 8u + 3u * 8u + u32(encoded, 4));

  EXPECT_EQ(u32(encoded, 8), 10u);
  EXPECT_EQ(u32(encoded, 16), 20u);
  EXPECT_EQ(u32(encoded, 24), 30u);
  EXPECT_EQ(u32(encoded, 12), 7u);  // "Shared\0" was inserted first.
  EXPECT_EQ(u32(encoded, 20), 0u);
  EXPECT_EQ(u32(encoded, 28), 0u);  // identical text shares one data offset.

  const auto decoded = fo4::localization::read_string_table(
      encoded, fo4::localization::StringTableKind::strings);
  ASSERT_EQ(decoded.entries.size(), 3u);
  EXPECT_EQ(decoded.entries[0].id, 10u);
  EXPECT_EQ(text(decoded.entries[0]), "Other");
  EXPECT_EQ(decoded.entries[1].id, 20u);
  EXPECT_EQ(text(decoded.entries[1]), "Shared");
  EXPECT_EQ(decoded.entries[2].id, 30u);
  EXPECT_EQ(text(decoded.entries[2]), "Shared");

  const auto* found = fo4::localization::find_string(decoded, 20u);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(text(*found), "Shared");
  EXPECT_EQ(fo4::localization::find_string(decoded, 999u), nullptr);
}

TEST(LocalizedStrings, DlAndIlLengthsIncludeTheTrailingNul) {
  for (const auto kind : {fo4::localization::StringTableKind::dlstrings,
                          fo4::localization::StringTableKind::ilstrings}) {
    const fo4::localization::LocalizedStringTable table{
        kind,
        {
            {20, bytes("abc")},
            {10, bytes("x")},
        },
    };
    const auto encoded = fo4::localization::write_string_table(table);
    const auto data_offset = 8u + 2u * 8u;

    EXPECT_EQ(u32(encoded, data_offset), 4u);      // 3 text bytes + NUL.
    EXPECT_EQ(encoded[data_offset + 7], std::byte{0});
    EXPECT_EQ(u32(encoded, data_offset + 8), 2u);  // 1 text byte + NUL.
    EXPECT_EQ(encoded[data_offset + 13], std::byte{0});

    // The ID table is sorted even though the data block stayed insertion ordered.
    EXPECT_EQ(u32(encoded, 8), 10u);
    EXPECT_EQ(u32(encoded, 12), 8u);
    EXPECT_EQ(u32(encoded, 16), 20u);
    EXPECT_EQ(u32(encoded, 20), 0u);

    const auto decoded = fo4::localization::read_string_table(encoded, kind);
    ASSERT_EQ(decoded.entries.size(), 2u);
    EXPECT_EQ(decoded.entries[0].id, 10u);
    EXPECT_EQ(text(decoded.entries[0]), "x");
    EXPECT_EQ(decoded.entries[1].id, 20u);
    EXPECT_EQ(text(decoded.entries[1]), "abc");
  }
}

TEST(LocalizedStrings, EmptyTableRoundTripsAsEightByteHeader) {
  const fo4::localization::LocalizedStringTable table{
      fo4::localization::StringTableKind::strings, {}};
  const auto encoded = fo4::localization::write_string_table(table);
  ASSERT_EQ(encoded.size(), 8u);
  EXPECT_EQ(u32(encoded, 0), 0u);
  EXPECT_EQ(u32(encoded, 4), 0u);
  EXPECT_TRUE(fo4::localization::read_string_table(
                  encoded, fo4::localization::StringTableKind::strings)
                  .entries.empty());
}

TEST(LocalizedStrings, RejectsSizeMismatchAndUnsortedIds) {
  auto encoded = fo4::localization::write_string_table(
      sample(fo4::localization::StringTableKind::strings));
  encoded.push_back(std::byte{0});
  EXPECT_THROW(
      fo4::localization::read_string_table(
          encoded, fo4::localization::StringTableKind::strings),
      fo4::localization::StringTableError);

  encoded = fo4::localization::write_string_table(
      sample(fo4::localization::StringTableKind::strings));
  set_u32(encoded, 8, 20u);
  set_u32(encoded, 16, 10u);
  EXPECT_THROW(
      fo4::localization::read_string_table(
          encoded, fo4::localization::StringTableKind::strings),
      fo4::localization::StringTableError);
}

TEST(LocalizedStrings, RejectsOffsetsThatAreNotRecordBoundaries) {
  auto encoded = fo4::localization::write_string_table(
      sample(fo4::localization::StringTableKind::strings));
  set_u32(encoded, 12, 1u);
  EXPECT_THROW(
      fo4::localization::read_string_table(
          encoded, fo4::localization::StringTableKind::strings),
      fo4::localization::StringTableError);
}

TEST(LocalizedStrings, RejectsMalformedLengthPrefixedRecords) {
  const fo4::localization::LocalizedStringTable table{
      fo4::localization::StringTableKind::dlstrings,
      {{10, bytes("hello")}}};
  auto encoded = fo4::localization::write_string_table(table);
  const auto data_offset = 16u;

  auto zero_length = encoded;
  set_u32(zero_length, data_offset, 0u);
  EXPECT_THROW(
      fo4::localization::read_string_table(
          zero_length, fo4::localization::StringTableKind::dlstrings),
      fo4::localization::StringTableError);

  auto missing_nul = encoded;
  missing_nul.back() = std::byte{1};
  EXPECT_THROW(
      fo4::localization::read_string_table(
          missing_nul, fo4::localization::StringTableKind::dlstrings),
      fo4::localization::StringTableError);
}

TEST(LocalizedStrings, WriterRejectsDuplicateIdsAndEmbeddedNuls) {
  fo4::localization::LocalizedStringTable duplicate{
      fo4::localization::StringTableKind::strings,
      {{10, bytes("a")}, {10, bytes("b")}}};
  EXPECT_THROW(fo4::localization::write_string_table(duplicate),
               fo4::localization::StringTableError);

  auto invalid = bytes("ab");
  invalid.insert(invalid.begin() + 1, std::byte{0});
  fo4::localization::LocalizedStringTable embedded{
      fo4::localization::StringTableKind::strings,
      {{10, std::move(invalid)}}};
  EXPECT_THROW(fo4::localization::write_string_table(embedded),
               fo4::localization::StringTableError);
}

TEST(LocalizedStrings, FileIoRoundTripsRawLegacyBytesWithoutUtf8Validation) {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("radicle_strings_" + std::to_string(unique) + ".STRINGS");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  } cleanup{path};

  const fo4::localization::LocalizedStringTable table{
      fo4::localization::StringTableKind::strings,
      {{10, {std::byte{0x93}, std::byte{'X'}, std::byte{0x94}}}}};
  fo4::localization::write_string_table_file(table, path);
  const auto reread = fo4::localization::read_string_table_file(
      path, fo4::localization::StringTableKind::strings);
  ASSERT_EQ(reread.entries.size(), 1u);
  EXPECT_EQ(reread.entries[0].text, table.entries[0].text);
}

TEST(LocalizedStrings, InsertKeepsExplicitIdOrderAndRejectsDuplicateIds) {
  fo4::localization::LocalizedStringTable table{
      fo4::localization::StringTableKind::strings,
      {{10, bytes("Ten")}, {30, bytes("Thirty")}}};
  fo4::localization::insert_string(table, 20, "Twenty");
  ASSERT_EQ(table.entries.size(), 3u);
  EXPECT_EQ(table.entries[1].id, 20u);
  EXPECT_EQ(text(table.entries[1]), "Twenty");
  EXPECT_THROW(fo4::localization::insert_string(table, 20, "Again"),
               fo4::localization::StringTableError);
  EXPECT_THROW(fo4::localization::insert_string(table, 40, std::string_view("a\0b", 3)),
               fo4::localization::StringTableError);
  EXPECT_EQ(table.entries.size(), 3u);
}

TEST(LocalizedStrings, SetAndRemoveReportMissingIdsWithoutChangingTheTable) {
  fo4::localization::LocalizedStringTable table{
      fo4::localization::StringTableKind::strings,
      {{10, bytes("Ten")}}};
  EXPECT_FALSE(fo4::localization::set_string(table, 11, "Eleven"));
  EXPECT_FALSE(fo4::localization::remove_string(table, 11));
  EXPECT_EQ(table.entries.size(), 1u);

  EXPECT_TRUE(fo4::localization::set_string(table, 10, "Edited"));
  EXPECT_EQ(text(table.entries[0]), "Edited");
  EXPECT_TRUE(fo4::localization::remove_string(table, 10));
  EXPECT_TRUE(table.entries.empty());
}
