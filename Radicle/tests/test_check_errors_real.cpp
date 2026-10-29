#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t fnv1a(std::string_view text, std::uint64_t hash = 1469598103934665603ull) {
  for (const unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string canonical_row(const fo4::CheckFinding& finding) {
  std::string row{fo4::check_code_name(finding.code)};
  row += '|';
  row += fo4::check_severity_name(finding.severity);
  row += '|';
  row += finding.plugin.filename();
  row += '|';
  row += finding.record.to_string();
  row += '|';
  row += finding.subrecord.to_string();
  row += '|';
  row += std::to_string(finding.offset);
  row += '|';
  row += finding.target.to_string();
  row += '|';
  row += finding.message;
  row += '|';
  row += finding.detail;
  return row;
}

std::string hex_u64(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

}  // namespace

TEST(CheckErrorsReal, FullLoadOrderDiagnosticRunIsDeterministic) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const std::filesystem::path data_dir{dir};
  record_game_build(detect_game_build(data_dir));

  const std::vector<std::string> official{"Fallout4.esm",    "DLCRobot.esm",      "DLCworkshop01.esm",
                                          "DLCCoast.esm",    "DLCworkshop02.esm", "DLCworkshop03.esm",
                                          "DLCNukaWorld.esm"};
  const auto installed = installed_master_and_cc_plugins(data_dir);
  std::vector<std::string> names;
  for (const auto& name : official) {
    if (std::find(installed.begin(), installed.end(), name) != installed.end()) names.push_back(name);
  }
  const std::size_t official_count = names.size();
  for (const auto& name : installed) {
    if (std::find(official.begin(), official.end(), name) == official.end()) names.push_back(name);
  }
  // Manifest order must be stable: keep master priority, sort the tail.
  std::sort(names.begin() + static_cast<std::ptrdiff_t>(official_count), names.end());
  if (names.empty()) {
    GTEST_SKIP() << "no official masters in FO4_DATA_DIR";
  }

  std::vector<fo4::Plugin> plugins;
  plugins.reserve(names.size());
  for (const auto& name : names) plugins.push_back(fo4::read_plugin_file(data_dir / name));
  std::vector<const fo4::Plugin*> load_order;
  load_order.reserve(plugins.size());
  for (const auto& plugin : plugins) load_order.push_back(&plugin);

  const auto first = fo4::check_load_order(load_order);
  const auto second = fo4::check_load_order(load_order);

  const auto manifest_of = [](const std::vector<fo4::CheckFinding>& findings) {
    std::vector<std::string> rows;
    rows.reserve(findings.size());
    for (const auto& finding : findings) rows.push_back(canonical_row(finding));
    std::sort(rows.begin(), rows.end());
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& row : rows) {
      hash = fnv1a(row, hash);
      hash = fnv1a(std::string_view{"\n"}, hash);
    }
    return std::make_pair(rows, hash);
  };

  const auto [rows, hash] = manifest_of(first);
  const auto [repeat_rows, repeat_hash] = manifest_of(second);
  EXPECT_EQ(repeat_rows, rows);
  EXPECT_EQ(repeat_hash, hash);

  std::map<std::string, std::size_t> per_code;
  std::size_t errors = 0;
  for (const auto& row : rows) per_code[row.substr(0, row.find('|'))]++;
  for (const auto& finding : first) {
    if (finding.severity == fo4::CheckSeverity::error) ++errors;
  }
  std::size_t counted = 0;
  for (const auto& [code, count] : per_code) counted += count;
  EXPECT_EQ(counted, rows.size());

  std::cout << "CheckErrorsReal plugins=" << names.size() << " findings=" << rows.size()
            << " errors=" << errors << " manifest=" << hex_u64(hash) << "\n";
  for (const auto& [code, count] : per_code) {
    std::cout << "  " << code << "=" << count << "\n";
  }

  testing::Test::RecordProperty("plugins", std::to_string(names.size()));
  testing::Test::RecordProperty("findings", std::to_string(rows.size()));
  testing::Test::RecordProperty("error_findings", std::to_string(errors));
  testing::Test::RecordProperty("manifest", hex_u64(hash));
  for (const auto& [code, count] : per_code) {
    testing::Test::RecordProperty(code, std::to_string(count));
  }
}
