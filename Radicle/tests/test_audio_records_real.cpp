#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {

class AudioRecordsReal : public testing::TestWithParam<const char*> {};

constexpr std::array<std::string_view, 8> kBatch{"AECH", "ASPC", "REVB", "SNCT", "SOPM", "SCSN", "STAG", "MUST"};

}  // namespace

TEST_P(AudioRecordsReal, EveryRowDecodesAndAuthoringMinimumsHold) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  if (!std::filesystem::is_regular_file(path)) {
    GTEST_SKIP() << "missing official master: " << path;
  }
  const auto plugin = fo4::read_plugin_file(path);
  const bool localized = plugin.header.tes4_flags.is_localized();
  std::map<std::string, std::size_t> counts;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto name = record.header.signature.to_string();
    if (std::find(kBatch.begin(), kBatch.end(), std::string_view{name}) == kBatch.end()) return;
    ++counts[name];
    SCOPED_TRACE(record.form_key.to_string());
    for (const auto& issue : fo4::schema::preflight_record(record, localized)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ' ' << issue.message;
    }
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
    EXPECT_TRUE(constraint.has_value());
    if (!constraint) return;
    for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
      const auto present = static_cast<std::size_t>(std::count_if(
          record.subrecords.begin(), record.subrecords.end(),
          [&](const fo4::Subrecord& sub) { return sub.signature == rule.signature; }));
      EXPECT_GE(present, static_cast<std::size_t>(rule.minimum)) << rule.signature.to_string();
      EXPECT_LE(present, static_cast<std::size_t>(rule.maximum)) << rule.signature.to_string();
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(counts.size(), 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, AudioRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));
