#include "env_bench/env_bench.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

TEST(EnvBenchRealData, LoadsOfficialMasters) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data_dir = dir;
  record_game_build(detect_game_build(data_dir));
  const auto plugins = radicle_bench::default_masters(data_dir);
  if (plugins.empty()) GTEST_SKIP() << "no official masters found under " << data_dir.string();

  const auto result = radicle_bench::run_env_bench(data_dir, plugins);
  radicle_bench::print(std::cout, result);

  RecordProperty("plugins", std::to_string(result.plugin_count));
  RecordProperty("plugin_bytes", std::to_string(result.plugin_bytes));
  RecordProperty("records", std::to_string(result.record_count));
  RecordProperty("archives", std::to_string(result.archive_count));
  RecordProperty("rss_before_load_kb", std::to_string(result.rss_before_load_kb));
  RecordProperty("rss_after_load_kb", std::to_string(result.rss_after_load_kb));
  RecordProperty("peak_rss_kb", std::to_string(result.peak_rss_after_load_kb));
  RecordProperty("load_ms", std::to_string(result.load_ms));
  RecordProperty("archives_ms", std::to_string(result.archives_ms));
  RecordProperty("strings_ms", std::to_string(result.strings_ms));
  RecordProperty("walk_ms", std::to_string(result.walk_ms));
  RecordProperty("retained_file_bytes", std::to_string(result.retained_file_bytes));
  RecordProperty("payload_bytes", std::to_string(result.payload_bytes));
  RecordProperty("subrecord_bytes", std::to_string(result.subrecord_bytes));
  RecordProperty("subrecords", std::to_string(result.subrecord_count));
  RecordProperty("node_bytes", std::to_string(result.node_bytes));

  EXPECT_EQ(result.plugin_count, plugins.size());
  EXPECT_GT(result.plugin_bytes, 0u);
  EXPECT_GT(result.record_count, 0u);
  EXPECT_EQ(result.retained_file_bytes, result.plugin_bytes);
  EXPECT_GT(result.subrecord_count, result.record_count);
  EXPECT_GT(result.subrecord_bytes, 0u);
  EXPECT_EQ(result.payload_bytes, 0u);
}
