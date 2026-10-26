#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <string_view>

namespace {

constexpr std::string_view k_batch[] = {"CLAS", "AVIF", "RELA", "HDPT", "ECZN",
                                        "EQUP", "MOVT", "COLL", "DUAL", "ANIO"};

constexpr std::size_t k_sample_limit = 64;

bool in_batch(fo4::Signature signature) {
  return std::any_of(std::begin(k_batch), std::end(k_batch), [&](std::string_view name) {
    return signature == fo4::Signature::from_string(name);
  });
}

class ActorSupportReal : public testing::TestWithParam<const char*> {};

TEST_P(ActorSupportReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
  std::map<fo4::Signature, std::size_t> sampled;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    if (!in_batch(signature)) return;
    ++batch_records;
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(signature));
    for (const auto& subrecord : record.subrecords) {
      const auto listed = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.signature == subrecord.signature;
      });
      EXPECT_TRUE(listed) << record.form_key.to_string() << ' ' << subrecord.signature.to_string();
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
    for (const auto& pair : fo4::schema::k_generated_subrecord_pairs) {
      if (pair.record != signature) continue;
      const auto count_of = [&](fo4::Signature name) {
        return static_cast<std::size_t>(std::count_if(
            record.subrecords.begin(), record.subrecords.end(),
            [&](const auto& subrecord) { return subrecord.signature == name; }));
      };
      EXPECT_EQ(count_of(pair.subrecord), count_of(pair.partner))
          << record.form_key.to_string() << ' ' << pair.subrecord.to_string();
    }
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (sampled[signature]++ >= k_sample_limit) return;
    for (const auto& field : fo4::schema::formlink_fields(signature)) {
      static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
    }
    if (signature == fo4::Signature::from_string("HDPT")) {
      static_cast<void>(fo4::schema::record_formlink_list_values(
          plugin, record.form_key, *fo4::schema::formlink_list_descriptor(signature, "HDPT\\ExtraParts")));
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ActorSupportReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
