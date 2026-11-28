#include "fo4/c_api_duplicate_formid.h"
#include "fo4/checks/duplicate_formid.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

void append_u32_le(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
}

void append_sig(std::vector<std::byte>& out, const char (&sig)[5]) {
  for (std::size_t i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>(sig[i]));
  }
}

void append_record(std::vector<std::byte>& out,
                   const char (&sig)[5],
                   std::uint32_t form_id,
                   std::size_t payload_size = 0) {
  append_sig(out, sig);
  append_u32_le(out, static_cast<std::uint32_t>(payload_size));
  append_u32_le(out, 0u);
  append_u32_le(out, form_id);
  append_u32_le(out, 0u);
  append_u32_le(out, 0u);
  for (std::size_t i = 0; i < payload_size; ++i) {
    out.push_back(std::byte{0});
  }
}

std::vector<std::byte> wrap_grup(const std::vector<std::byte>& inner, const char (&label)[5] = "KYWD") {
  std::vector<std::byte> out;
  out.reserve(24 + inner.size());
  append_sig(out, "GRUP");
  append_u32_le(out, static_cast<std::uint32_t>(24 + inner.size()));
  append_sig(out, label);
  append_u32_le(out, 0u);
  append_u32_le(out, 0u);
  append_u32_le(out, 0u);
  out.insert(out.end(), inner.begin(), inner.end());
  return out;
}

std::vector<std::byte> build_plugin(const std::vector<std::byte>& body) {
  std::vector<std::byte> out;
  append_record(out, "TES4", 0u, 0u);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const char* name) {
    path = std::filesystem::temp_directory_path() /
           ("radicle_dup_test_" + std::string(name) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void write_bytes(const std::filesystem::path& file, const std::vector<std::byte>& data) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  if (!data.empty()) {
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  ASSERT_TRUE(out.good());
}

TEST(DuplicateFormIdTests, DetectsDuplicateFormIdsInPluginBytesAndFile) {
  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000800u);
  append_record(body, "KYWD", 0x00000800u);
  const auto bytes = build_plugin(wrap_grup(body));

  const auto mem_result = fo4::scan_duplicate_form_ids(bytes);
  EXPECT_FALSE(mem_result.error.has_value());
  ASSERT_EQ(mem_result.duplicates.size(), 1u);
  EXPECT_EQ(mem_result.duplicates[0].raw_form_id, 0x00000800u);
  EXPECT_EQ(mem_result.duplicates[0].count, 2u);
  EXPECT_EQ(mem_result.duplicates[0].record_types, (std::vector<std::string>{"KYWD", "KYWD"}));

  TempDir dir("detect");
  const auto file = dir.path / "dup.esp";
  write_bytes(file, bytes);
  const auto file_result = fo4::scan_duplicate_form_ids_file(file);
  EXPECT_FALSE(file_result.error.has_value());
  ASSERT_EQ(file_result.duplicates.size(), 1u);
  EXPECT_EQ(file_result.duplicates[0].raw_form_id, 0x00000800u);
}

TEST(DuplicateFormIdTests, SkipsTes4HeaderAndDetectsDuplicateZeroFormIdsOutsideTes4) {
  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000000u);
  append_record(body, "BOOK", 0x00000000u);
  const auto bytes = build_plugin(body);

  const auto result = fo4::scan_duplicate_form_ids(bytes);
  EXPECT_FALSE(result.error.has_value());
  ASSERT_EQ(result.duplicates.size(), 1u);
  EXPECT_EQ(result.duplicates[0].raw_form_id, 0x00000000u);
  EXPECT_EQ(result.duplicates[0].count, 2u);
  EXPECT_EQ(result.duplicates[0].record_types, (std::vector<std::string>{"KYWD", "BOOK"}));
}

TEST(DuplicateFormIdTests, RejectsExcessiveGrupNesting) {
  std::vector<std::byte> nested;
  for (int i = 0; i < 130; ++i) {
    nested = wrap_grup(nested);
  }
  const auto bytes = build_plugin(nested);

  const auto result = fo4::scan_duplicate_form_ids(bytes);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_NE(result.error->find("nesting"), std::string::npos);
  EXPECT_FALSE(result.is_io_error);
  EXPECT_TRUE(result.duplicates.empty());
}

TEST(DuplicateFormIdTests, CachesParseErrorForBrokenPlugin) {
  TempDir dir("broken");
  const auto file = dir.path / "broken.esp";
  const std::vector<std::byte> broken{
      std::byte{'b'}, std::byte{'r'}, std::byte{'o'}, std::byte{'k'}, std::byte{'e'}, std::byte{'n'}};
  write_bytes(file, broken);

  fo4::DuplicateFormIdCache cache;
  const auto result = cache.scan(file);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_FALSE(result.is_io_error);
  EXPECT_TRUE(result.duplicates.empty());

  const auto cached = cache.try_get_cached(file);
  ASSERT_TRUE(cached.has_value());
  EXPECT_TRUE(cached->error.has_value());
}

TEST(DuplicateFormIdTests, DoesNotCacheTransientIoErrorForMissingFile) {
  TempDir dir("missing");
  const auto file = dir.path / "missing.esp";

  fo4::DuplicateFormIdCache cache;
  const auto missing_result = cache.scan(file);
  ASSERT_TRUE(missing_result.error.has_value());
  EXPECT_TRUE(missing_result.is_io_error);
  EXPECT_FALSE(cache.try_get_cached(file).has_value());

  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000801u);
  write_bytes(file, build_plugin(body));

  const auto valid_result = cache.scan(file);
  EXPECT_FALSE(valid_result.error.has_value());
  EXPECT_TRUE(valid_result.duplicates.empty());
  EXPECT_TRUE(cache.try_get_cached(file).has_value());
}

TEST(DuplicateFormIdTests, InvalidateAndRewriteWithIdenticalSizeAndTimestampRescans) {
  TempDir dir("invalidate");
  const auto file = dir.path / "rewrite.esp";

  std::vector<std::byte> dup_body;
  append_record(dup_body, "KYWD", 0x00000800u, 4);
  append_record(dup_body, "KYWD", 0x00000800u, 4);
  const auto dup_bytes = build_plugin(dup_body);

  std::vector<std::byte> clean_body;
  append_record(clean_body, "KYWD", 0x00000800u, 4);
  append_record(clean_body, "KYWD", 0x00000801u, 4);
  const auto clean_bytes = build_plugin(clean_body);
  ASSERT_EQ(dup_bytes.size(), clean_bytes.size());

  write_bytes(file, dup_bytes);
  const auto fixed_time = std::filesystem::last_write_time(file);

  fo4::DuplicateFormIdCache cache;
  const auto first = cache.scan(file);
  ASSERT_EQ(first.duplicates.size(), 1u);

  write_bytes(file, clean_bytes);
  std::filesystem::last_write_time(file, fixed_time);

  const auto stale = cache.scan(file);
  ASSERT_EQ(stale.duplicates.size(), 1u);

  cache.invalidate(file);
  EXPECT_FALSE(cache.try_get_cached(file).has_value());

  const auto rescanned = cache.scan(file);
  EXPECT_FALSE(rescanned.error.has_value());
  EXPECT_TRUE(rescanned.duplicates.empty());
}

#ifdef FO4_TEST_HOOKS
TEST(DuplicateFormIdTests, InvalidationDuringInFlightScanPreventsPublishingStaleResult) {
  TempDir dir("inflight");
  const auto file = dir.path / "inflight.esp";
  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000800u);
  append_record(body, "KYWD", 0x00000800u);
  write_bytes(file, build_plugin(body));

  fo4::DuplicateFormIdCache cache;
  cache.set_before_publish_hook_for_test([&]() { cache.invalidate(file); });

  const auto result = cache.scan(file);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_NE(result.error->find("invalidated"), std::string::npos);
  EXPECT_FALSE(cache.try_get_cached(file).has_value());
}
#endif

TEST(DuplicateFormIdTests, CacheKeysRespectPlatformCaseSensitivity) {
  TempDir dir("case");
  const auto upper = dir.path / "Mods" / "Same.esp";
  const auto lower = dir.path / "mods" / "Same.esp";
  std::filesystem::create_directories(upper.parent_path());

  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000800u);
  append_record(body, "KYWD", 0x00000800u);
  write_bytes(upper, build_plugin(body));

  fo4::DuplicateFormIdCache cache;
  const auto res_upper = cache.scan(upper);
  ASSERT_EQ(res_upper.duplicates.size(), 1u);

#if defined(_WIN32)
  EXPECT_TRUE(cache.try_get_cached(lower).has_value());
#else
  EXPECT_FALSE(cache.try_get_cached(lower).has_value());
#endif
}

TEST(DuplicateFormIdTests, CAbiScannerRoundTripAndRevertProof) {
  std::vector<std::byte> body;
  append_record(body, "BOOK", 0x00000900u);
  append_record(body, "KYWD", 0x00000800u);
  append_record(body, "BOOK", 0x00000900u);
  append_record(body, "ALCH", 0x00000800u);
  const auto bytes = build_plugin(wrap_grup(body));

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_duplicate_formid_scan_bytes(
                reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &rows, &count),
            RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(rows[0], "00000800\t2\tKYWD,ALCH");
  EXPECT_STREQ(rows[1], "00000900\t2\tBOOK,BOOK");
  radicle_free_string_array(rows, count);

  TempDir dir("cabi");
  const auto file = dir.path / "cabi.esp";
  write_bytes(file, bytes);

  radicle_duplicate_formid_scanner* scanner = nullptr;
  ASSERT_EQ(radicle_duplicate_formid_scanner_create(&scanner), RADICLE_STATUS_OK);
  ASSERT_NE(scanner, nullptr);

  rows = nullptr;
  count = 0;
  ASSERT_EQ(radicle_duplicate_formid_scanner_scan(scanner, file.string().c_str(), &rows, &count),
            RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  radicle_free_string_array(rows, count);

  ASSERT_EQ(radicle_duplicate_formid_scanner_invalidate(scanner, file.string().c_str()),
            RADICLE_STATUS_OK);
  radicle_duplicate_formid_scanner_destroy(scanner);

  const std::vector<std::byte> broken{std::byte{'b'}, std::byte{'a'}, std::byte{'d'}};
  EXPECT_EQ(radicle_duplicate_formid_scan_bytes(
                reinterpret_cast<const uint8_t*>(broken.data()), broken.size(), &rows, &count),
            RADICLE_STATUS_PARSE_ERROR);
}

std::vector<std::byte> raw_header(const char (&sig)[5], std::uint32_t size) {
  std::vector<std::byte> out;
  append_sig(out, sig);
  append_u32_le(out, size);
  for (int i = 0; i < 4; ++i) {
    append_u32_le(out, 0u);
  }
  return out;
}

std::vector<std::byte> plugin_with_tail(const std::vector<std::byte>& tail) {
  auto bytes = build_plugin({});
  bytes.insert(bytes.end(), tail.begin(), tail.end());
  return bytes;
}

std::string error_of(const std::vector<std::byte>& bytes) {
  const auto result = fo4::scan_duplicate_form_ids(bytes);
  EXPECT_TRUE(result.error.has_value());
  EXPECT_FALSE(result.is_io_error);
  EXPECT_TRUE(result.duplicates.empty());
  return result.error.value_or(std::string());
}

TEST(DuplicateFormIdTests, ThreeWayDuplicateCountsThreeWithAllTypes) {
  std::vector<std::byte> body;
  append_record(body, "KYWD", 0x00000800u);
  append_record(body, "BOOK", 0x00000800u);
  append_record(body, "ALCH", 0x00000800u);
  append_record(body, "KYWD", 0x00000801u);
  const auto result = fo4::scan_duplicate_form_ids(build_plugin(wrap_grup(body)));
  EXPECT_FALSE(result.error.has_value());
  ASSERT_EQ(result.duplicates.size(), 1u);
  EXPECT_EQ(result.duplicates[0].count, 3u);
  EXPECT_EQ(result.duplicates[0].record_types, (std::vector<std::string>{"KYWD", "BOOK", "ALCH"}));
}

TEST(DuplicateFormIdTests, MalformedGrupAndRecordSizesReportErrors) {
  EXPECT_NE(error_of(plugin_with_tail(raw_header("GRUP", 23u))).find("Invalid GRUP size"), std::string::npos);
  EXPECT_NE(error_of(plugin_with_tail(raw_header("GRUP", 0xFFFFFFFFu))).find("GRUP extends"), std::string::npos);
  EXPECT_NE(error_of(plugin_with_tail(raw_header("GRUP", 48u))).find("GRUP extends"), std::string::npos);
  EXPECT_NE(error_of(plugin_with_tail(raw_header("KYWD", 0xFFFFFFFFu))).find("Record extends"), std::string::npos);
  EXPECT_NE(error_of(plugin_with_tail(raw_header("KYWD", 8u))).find("Record extends"), std::string::npos);

  auto truncated_header = plugin_with_tail(raw_header("KYWD", 0u));
  truncated_header.resize(truncated_header.size() - 10);
  EXPECT_NE(error_of(truncated_header).find("Truncated"), std::string::npos);

  const auto grup_with_partial_header = wrap_grup({std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  EXPECT_NE(error_of(plugin_with_tail(grup_with_partial_header)).find("Truncated"), std::string::npos);
}

TEST(DuplicateFormIdTests, TrailingByteIsRejectedAndEmptyInputIsClean) {
  auto bytes = build_plugin({});
  EXPECT_FALSE(fo4::scan_duplicate_form_ids(bytes).error.has_value());
  bytes.push_back(std::byte{0});
  EXPECT_NE(error_of(bytes).find("Truncated"), std::string::npos);

  const auto empty = fo4::scan_duplicate_form_ids(std::span<const std::byte>{});
  EXPECT_FALSE(empty.error.has_value());
  EXPECT_TRUE(empty.duplicates.empty());

  TempDir dir("emptyfile");
  const auto empty_file = dir.path / "empty.esp";
  write_bytes(empty_file, {});
  const auto file_result = fo4::scan_duplicate_form_ids_file(empty_file);
  EXPECT_FALSE(file_result.error.has_value());
  EXPECT_TRUE(file_result.duplicates.empty());

  const auto trailing_file = dir.path / "trailing.esp";
  write_bytes(trailing_file, bytes);
  const auto trailing = fo4::scan_duplicate_form_ids_file(trailing_file);
  ASSERT_TRUE(trailing.error.has_value());
  EXPECT_FALSE(trailing.is_io_error);

  const auto huge_file = dir.path / "huge.esp";
  write_bytes(huge_file, plugin_with_tail(raw_header("GRUP", 0xFFFFFFFFu)));
  const auto huge = fo4::scan_duplicate_form_ids_file(huge_file);
  ASSERT_TRUE(huge.error.has_value());
  EXPECT_FALSE(huge.is_io_error);
}

TEST(DuplicateFormIdTests, UnsafeSignatureBytesBecomeQuestionMarks) {
  const char tab_sig[5] = {'K', '\t', 'W', 'D', '\0'};
  const char high_sig[5] = {static_cast<char>(0xE9), '\n', '\r', '\0', '\0'};
  const char comma_sig[5] = {'A', ',', 'B', static_cast<char>(0x7F), '\0'};
  std::vector<std::byte> body;
  append_record(body, tab_sig, 0x00000800u);
  append_record(body, high_sig, 0x00000800u);
  append_record(body, comma_sig, 0x00000800u);
  const auto bytes = build_plugin(body);

  const auto result = fo4::scan_duplicate_form_ids(bytes);
  EXPECT_FALSE(result.error.has_value());
  ASSERT_EQ(result.duplicates.size(), 1u);
  EXPECT_EQ(result.duplicates[0].record_types, (std::vector<std::string>{"K?WD", "????", "A?B?"}));

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_duplicate_formid_scan_bytes(
                reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &rows, &count),
            RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(rows[0], "00000800\t3\tK?WD,????,A?B?");
  radicle_free_string_array(rows, count);
}

TEST(DuplicateFormIdTests, CAbiRejectsNullArgumentsAndReportsIoErrors) {
  char** rows = nullptr;
  size_t count = 0;
  const std::uint8_t byte = 0;
  EXPECT_EQ(radicle_duplicate_formid_scan_bytes(nullptr, 1, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scan_bytes(&byte, 1, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scan_bytes(&byte, 1, &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scan_bytes(nullptr, 0, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  radicle_free_string_array(rows, count);

  EXPECT_EQ(radicle_duplicate_formid_scan_file(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scan_file("x.esp", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scan_file("x.esp", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_duplicate_formid_scanner_create(nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_duplicate_formid_scanner_destroy(nullptr);

  radicle_duplicate_formid_scanner* scanner = nullptr;
  ASSERT_EQ(radicle_duplicate_formid_scanner_create(&scanner), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_duplicate_formid_scanner_scan(scanner, nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scanner_scan(nullptr, "x.esp", &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scanner_scan(scanner, "x.esp", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scanner_scan(scanner, "x.esp", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scanner_invalidate(scanner, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_duplicate_formid_scanner_invalidate(nullptr, "x.esp"), RADICLE_STATUS_INVALID_ARGUMENT);

  TempDir dir("cabierr");
  const auto missing = (dir.path / "missing.esp").string();
  EXPECT_EQ(radicle_duplicate_formid_scan_file(missing.c_str(), &rows, &count), RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_duplicate_formid_scanner_scan(scanner, missing.c_str(), &rows, &count), RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  const auto trailing = dir.path / "trailing.esp";
  auto bytes = build_plugin({});
  bytes.push_back(std::byte{0});
  write_bytes(trailing, bytes);
  EXPECT_EQ(radicle_duplicate_formid_scan_file(trailing.string().c_str(), &rows, &count), RADICLE_STATUS_PARSE_ERROR);
  radicle_duplicate_formid_scanner_destroy(scanner);
}

}  // namespace
