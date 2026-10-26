#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string_view>

namespace {

constexpr std::string_view k_batch[] = {"LSCR", "OVIS", "NOCM", "DEBR", "RFGP", "MESG"};
constexpr std::size_t k_link_sample_limit = 64;

std::size_t batch_index(fo4::Signature signature) {
  for (std::size_t index = 0; index < std::size(k_batch); ++index) {
    if (signature == fo4::Signature::from_string(k_batch[index])) return index;
  }
  return std::size(k_batch);
}

class WorldMiscRecordsReal : public testing::TestWithParam<const char*> {};

TEST_P(WorldMiscRecordsReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
  std::size_t linked[std::size(k_batch)] = {};
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    const auto index = batch_index(signature);
    if (index == std::size(k_batch)) return;
    ++batch_records;
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(signature));
    std::size_t last_rule = 0;
    for (const auto& subrecord : record.subrecords) {
      const auto found = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.signature == subrecord.signature;
      });
      if (found == rules.end()) continue;
      const auto rule_index = static_cast<std::size_t>(found - rules.begin());
      EXPECT_GE(rule_index, last_rule)
          << record.form_key.to_string() << ' ' << subrecord.signature.to_string();
      last_rule = rule_index;
    }
    for (const auto& rule : rules) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record.subrecords.begin(), record.subrecords.end(),
          [&](const auto& subrecord) { return subrecord.signature == rule.signature; }));
      EXPECT_GE(count, static_cast<std::size_t>(rule.minimum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
      EXPECT_LE(count, static_cast<std::size_t>(rule.maximum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
    }
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (linked[index]++ < k_link_sample_limit) {
      for (const auto& field : fo4::schema::formlink_fields(signature)) {
        static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
      }
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, WorldMiscRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

TEST(NavmeshObstacleReal, NocmRecordKeepsEveryUncountedRunUnderOneModel) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  auto plugin = fo4::read_plugin_file(std::filesystem::path(directory) / "Fallout4.esm");
  const auto group = fo4::schema::subrecord_group_descriptor(
      fo4::Signature::from_string("NOCM"), R"(NOCM\Obstacle)");
  ASSERT_TRUE(group.has_value());
  const auto nocm = fo4::Signature::from_string("NOCM");
  const auto indx = fo4::Signature::from_string("INDX");
  const auto intv = fo4::Signature::from_string("INTV");
  const auto data = fo4::Signature::from_string("DATA");
  const auto nam1 = fo4::Signature::from_string("NAM1");
  std::size_t records = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != nocm) return;
    ++records;
    const auto scoped = fo4::subrecord_group_spans(plugin, record.form_key, *group);
    ASSERT_TRUE(scoped.has_value());
    std::size_t runs = 0;
    std::size_t datas = 0;
    std::size_t models = 0;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == data) ++datas;
      if (subrecord.signature == nam1) ++models;
    }
    for (const auto& span : *scoped) {
      runs += span.runs.size();
      ASSERT_LT(span.first, span.end);
      ASSERT_LE(span.end, record.subrecords.size());
      EXPECT_EQ(record.subrecords[span.first].signature, indx);
      EXPECT_EQ(record.subrecords[span.end - 1].signature, nam1);
      for (const auto& run : span.runs) {
        ASSERT_GE(run.first, span.first);
        ASSERT_LE(run.end, span.end);
        EXPECT_EQ(record.subrecords[run.first].signature, indx);
        EXPECT_EQ(record.subrecords[run.end - 1].signature, intv);
      }
    }
    EXPECT_EQ(scoped->size(), 87u);
    EXPECT_EQ(runs, 197u);
    EXPECT_EQ(datas, 1589u);
    EXPECT_EQ(models, 87u);
    const auto index = fo4::schema::scalar_field_descriptor("NOCM\\Index");
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(fo4::record_field_count(plugin, record.form_key, *index), 197u);
  });
  EXPECT_EQ(records, 1u);
}

}  // namespace
