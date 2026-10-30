#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path official(const char* name, bool& skip) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  skip = dir == nullptr || !std::filesystem::exists(std::filesystem::path{dir} / name);
  return skip ? std::filesystem::path{} : std::filesystem::path{dir} / name;
}

bool tracked(fo4::Signature signature) {
  for (const auto* text : {"PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS", "PLYR", "ADDN"}) {
    if (signature == fo4::Signature::from_string(text)) return true;
  }
  return false;
}

bool variant(fo4::Signature signature) {
  return tracked(signature) && signature != fo4::Signature::from_string("ADDN") &&
         signature != fo4::Signature::from_string("PLYR");
}

void check_authoring(const fo4::Record& record, std::vector<std::string>& violators) {
  const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
  if (!constraint.has_value()) {
    violators.push_back(record.form_key.to_string() + " has no authoring schema");
    return;
  }
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  std::vector<std::size_t> counts(rules.size(), 0);
  std::optional<std::size_t> last;
  for (const auto& sub : record.subrecords) {
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature != sub.signature) continue;
      ++counts[index];
      if (last.has_value() && index < *last) {
        violators.push_back(record.form_key.to_string() + " " + sub.signature.to_string() + " out of order");
      }
      last = index;
      break;
    }
  }
  for (std::size_t index = 0; index < rules.size(); ++index) {
    const auto& rule = rules[index];
    if (counts[index] < static_cast<std::size_t>(rule.minimum) ||
        counts[index] > static_cast<std::size_t>(rule.maximum)) {
      violators.push_back(record.form_key.to_string() + " " + rule.signature.to_string() + " count " +
                          std::to_string(counts[index]));
    }
  }
}

class PlacedVariantsReal : public testing::TestWithParam<const char*> {};

TEST_P(PlacedVariantsReal, EveryTrackedRecordDecodesAndMeetsAuthoringMinimums) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  std::map<std::string, int> totals;
  std::map<std::string, int> preflighted;
  std::vector<std::string> violators;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    if (!tracked(signature)) return;
    ++totals[signature.to_string()];
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& sub : record.subrecords) {
        if (sub.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (variant(signature)) {
      const auto view = fo4::forms::decode_placed_reference(plugin, record);
      EXPECT_TRUE(view.has_value()) << record.form_key.to_string();
      if (view.has_value()) { EXPECT_EQ(view->kind, fo4::forms::PlacedReferenceKind::variant); }
      if (preflighted[signature.to_string()]++ < 64) {
        for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
          if (issue.code == "record_placement_invalid") violators.push_back(record.form_key.to_string() + " placement");
        }
      }
    }
    check_authoring(record, violators);
  });
  EXPECT_TRUE(violators.empty()) << violators.front();
  for (const auto& [type, count] : totals) RecordProperty(type, count);
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(totals["ADDN"], 0);
    EXPECT_GT(totals["PGRE"], 0);
    EXPECT_GT(totals["PHZD"], 0);
  }
}

TEST(PlacedXemiReal, XemiReferencesMatchCensusInDlcRobotAndWorkshop03) {
  for (const auto& [name, expected] : {std::pair{"DLCRobot.esm", 482}, std::pair{"DLCworkshop03.esm", 207}}) {
    bool skip = false;
    const auto path = official(name, skip);
    if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
    record_game_build(detect_game_build(path.parent_path()));
    const std::filesystem::path paths[] = {path};
    const auto env = fo4::env::Environment::load(paths);
    int with_emittance = 0;
    for (const auto* ref : env.plugin_records(0)) {
      if (ref->record->header.signature != fo4::Signature::from_string("REFR")) continue;
      if (fo4::env::decode_placed_reference(env, *ref).emittance) ++with_emittance;
    }
    EXPECT_EQ(with_emittance, expected) << name;
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, PlacedVariantsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
