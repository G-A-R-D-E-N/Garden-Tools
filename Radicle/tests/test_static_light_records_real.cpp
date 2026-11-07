#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::string_view, 7> k_batch{"STAT", "LIGH", "BNDS", "LAYR", "LGTM", "MSWP", "TXST"};
constexpr std::size_t k_authoring_sample = 64;

class StaticLightReal : public testing::TestWithParam<const char*> {};

bool in_batch(fo4::Signature signature) {
  return std::any_of(k_batch.begin(), k_batch.end(),
                     [&](std::string_view name) { return signature == fo4::Signature::from_string(name); });
}

}  // namespace

TEST_P(StaticLightReal, BatchRowsDecodeAndAuthoringMinimumsHold) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::vector<fo4::FormKey> keys;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (in_batch(record.header.signature)) keys.push_back(record.form_key);
  });

  for (const auto& key : keys) {
    try {
      const auto* record = fo4::find_record(plugin, key);
      ASSERT_NE(record, nullptr);
      const auto fields = fo4::schema::scalar_fields(record->header.signature);
      for (const auto& field : fields) {
        for (const auto& subrecord : record->subrecords) {
          if (subrecord.signature != field.scalar.subrecord) continue;
          const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
          if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
        }
      }
      for (const auto& field : fo4::schema::formlink_fields(record->header.signature)) {
        const fo4::schema::FieldShape shape{field.subrecord, field.offset, 4, 0, false};
        const auto slots = fo4::schema::resolve_field_slots(*record, shape, field.address);
        for (const auto& slot : slots.slots) {
          if (!slot.valid) { ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << " is not decodable"; }
        }
      }
    } catch (const std::exception& error) {
      ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ": " << error.what();
    }
  }

  const auto step = std::max<std::size_t>(1, keys.size() / k_authoring_sample);
  for (std::size_t index = 0; index < keys.size(); index += step) {
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, keys[index])) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) {
        ADD_FAILURE() << GetParam() << ' ' << keys[index].to_string() << ' ' << issue.path << ": " << issue.message;
      }
    }
  }
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(keys.size(), 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, StaticLightReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));
