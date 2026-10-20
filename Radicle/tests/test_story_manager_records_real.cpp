#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string_view>

namespace {

constexpr std::string_view k_batch[] = {"SMBN", "SMEN", "SMQN", "SCCO", "TRNS", "CPTH"};
constexpr std::size_t k_link_sample = 64;

std::size_t batch_index(fo4::Signature signature) {
  for (std::size_t i = 0; i < std::size(k_batch); ++i) {
    if (signature == fo4::Signature::from_string(k_batch[i])) return i;
  }
  return std::size(k_batch);
}

class StoryManagerRecordsReal : public testing::TestWithParam<const char*> {};

TEST_P(StoryManagerRecordsReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
  std::array<std::size_t, std::size(k_batch)> sampled{};
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    const auto index = batch_index(signature);
    if (index == std::size(k_batch)) return;
    ++batch_records;
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(signature));
    for (const auto& rule : rules) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record.subrecords.begin(), record.subrecords.end(),
          [&](const auto& subrecord) { return subrecord.signature == rule.signature; }));
      EXPECT_GE(count, static_cast<std::size_t>(rule.minimum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
      EXPECT_LE(count, static_cast<std::size_t>(rule.maximum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
    }
    std::size_t last = 0;
    for (const auto& subrecord : record.subrecords) {
      const auto rule = std::find_if(rules.begin(), rules.end(),
          [&](const auto& candidate) { return candidate.signature == subrecord.signature; });
      if (rule == rules.end()) continue;
      const auto position = static_cast<std::size_t>(std::distance(rules.begin(), rule));
      EXPECT_GE(position, last)
          << record.form_key.to_string() << ' ' << subrecord.signature.to_string();
      last = position;
    }
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (sampled[index]++ < k_link_sample) {
      for (const auto& field : fo4::schema::formlink_fields(signature)) {
        EXPECT_TRUE(fo4::record_field_count(plugin, record.form_key, field).has_value()) << field.path;
      }
    }
    if (signature == fo4::Signature::from_string("SMQN")) {
      // Regression: the list length must equal the derived QNAM counter, or a list that skips an entry desyncs the node's quest count.
      const auto quests = fo4::schema::formlink_list_descriptor(signature, "SMQN\\Quests");
      ASSERT_TRUE(quests.has_value());
      std::uint32_t declared = 0;
      std::size_t counters = 0;
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("QNAM")) continue;
        ++counters;
        if (subrecord.data.size() != 4) continue;
        for (std::size_t byte = 0; byte < 4; ++byte) {
          declared |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(subrecord.data[byte])) << (8 * byte);
        }
      }
      EXPECT_EQ(counters, 1u) << record.form_key.to_string();
      try {
        const auto values = fo4::schema::record_formlink_list_values(plugin, record.form_key, *quests);
        ASSERT_TRUE(values.has_value());
        EXPECT_EQ(values->size(), declared) << record.form_key.to_string();
      } catch (const fo4::FormIdError& error) {
        if (record.form_key != fo4::FormKey::parse("00089E:Fallout4.esm") ||
            std::string_view{error.what()}.find("master index 1") == std::string_view::npos) {
          FAIL() << record.form_key.to_string() << ": " << error.what();
        }
      }
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, StoryManagerRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
