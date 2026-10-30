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
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view k_signatures[] = {"MGEF", "SPEL", "LCTN"};

class MagicLocationReal : public testing::TestWithParam<const char*> {};

std::size_t count_subrecords(const fo4::Record& record, fo4::Signature signature) {
  return static_cast<std::size_t>(std::count_if(
      record.subrecords.begin(), record.subrecords.end(),
      [&](const fo4::Subrecord& sub) { return sub.signature == signature; }));
}

TEST_P(MagicLocationReal, RowsDecodeAndAuthoringLimitsHold) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);

  std::size_t total = 0;
  for (const auto signature_text : k_signatures) {
    const auto signature = fo4::Signature::from_string(signature_text);
    const auto constraint = fo4::schema::record_authoring_constraint(signature);
    ASSERT_TRUE(constraint.has_value()) << signature_text;
    const auto rules = fo4::schema::authoring_subrecords(*constraint);
    const auto scalars = fo4::schema::scalar_fields(signature);
    const auto links = fo4::schema::formlink_fields(signature);

    std::vector<const fo4::Record*> records;
    fo4::visit_records(plugin, [&](const fo4::Record& record) {
      if (record.header.signature == signature) records.push_back(&record);
    });
    total += records.size();

    for (const auto* record : records) {
      const auto& key = record->form_key;
      for (const auto& sub : record->subrecords) {
        for (const auto& field : scalars) {
          if (sub.signature != field.scalar.subrecord) continue;
          if (field.scalar.payload_size != 0 && sub.data.size() != field.scalar.payload_size) {
            ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << ": "
                          << sub.signature.to_string() << " is " << sub.data.size()
                          << " bytes, expected " << field.scalar.payload_size;
            continue;
          }
          try {
            const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
            if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
          } catch (const std::exception& error) {
            ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << ": "
                          << error.what();
          }
        }
        for (const auto& field : links) {
          if (sub.signature != field.subrecord) continue;
          const auto& sizes = field.payload_sizes;
          if (sizes[0] != 0 && sub.data.size() != sizes[0] && sub.data.size() != sizes[1]) {
            ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << ": "
                          << sub.signature.to_string() << " is " << sub.data.size() << " bytes";
          }
        }
      }
      std::size_t last = 0;
      for (const auto& sub : record->subrecords) {
        const auto it = std::find_if(rules.begin(), rules.end(),
            [&](const auto& rule) { return rule.signature == sub.signature; });
        if (it == rules.end()) continue;
        const auto index = static_cast<std::size_t>(it - rules.begin());
        EXPECT_GE(index, last) << GetParam() << ' ' << key.to_string() << ' ' << sub.signature.to_string();
        last = index;
      }
      for (const auto& rule : rules) {
        const auto count = count_subrecords(*record, rule.signature);
        EXPECT_GE(count, static_cast<std::size_t>(rule.minimum)) << GetParam() << ' ' << key.to_string() << ' '
                                       << signature_text << ' ' << rule.signature.to_string();
        EXPECT_LE(count, static_cast<std::size_t>(rule.maximum)) << GetParam() << ' ' << key.to_string() << ' '
                                       << signature_text << ' ' << rule.signature.to_string();
      }
    }
  }
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_GT(total, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, MagicLocationReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
