#pragma once

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <vector>

namespace radicle_bench {

struct EnvBenchResult {
  std::size_t plugin_count{0};
  std::size_t record_count{0};
  std::uintmax_t plugin_bytes{0};
  std::size_t archive_count{0};
  std::uint64_t rss_before_load_kb{0};
  std::uint64_t rss_after_load_kb{0};
  std::uint64_t peak_rss_after_load_kb{0};
  double load_ms{0};
  double archives_ms{0};
  double strings_ms{0};
  double walk_ms{0};
  std::uintmax_t retained_file_bytes{0};
  std::uintmax_t payload_bytes{0};
  std::uintmax_t subrecord_bytes{0};
  std::uintmax_t subrecord_count{0};
  std::uintmax_t group_count{0};
  std::uintmax_t node_bytes{0};
};

std::vector<std::filesystem::path> default_masters(const std::filesystem::path& data_dir);

EnvBenchResult run_env_bench(const std::filesystem::path& data_dir,
                             const std::vector<std::filesystem::path>& plugins);

void print(std::ostream& out, const EnvBenchResult& result);

}  // namespace radicle_bench
