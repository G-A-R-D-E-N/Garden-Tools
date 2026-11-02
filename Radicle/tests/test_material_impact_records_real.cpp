#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/behavior_layout.hpp"
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
#include <optional>
#include <string_view>

namespace {

constexpr std::string_view k_batch[] = {"MATO", "MATT", "IPCT", "IPDS", "LENS", "RFCT"};

struct ArrayMember {
  std::string_view record;
  std::string_view subrecord;
};

constexpr ArrayMember k_array_members[] = {{"LENS", "LFSP"}, {"LENS", "FNAM"}, {"LENS", "LFSD"}};

std::optional<std::size_t> batch_index(fo4::Signature signature) {
  for (std::size_t index = 0; index < std::size(k_batch); ++index) {
    if (signature == fo4::Signature::from_string(k_batch[index])) return index;
  }
  return std::nullopt;
}

bool array_member(fo4::Signature signature, fo4::Signature subrecord) {
  return std::any_of(std::begin(k_array_members), std::end(k_array_members), [&](const ArrayMember& member) {
    return signature == fo4::Signature::from_string(member.record) &&
           subrecord == fo4::Signature::from_string(member.subrecord);
  });
}

class MaterialImpactRecordsReal : public testing::TestWithParam<const char*> {};

TEST_P(MaterialImpactRecordsReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
  std::size_t sampled[std::size(k_batch)] = {};
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    const auto index = batch_index(signature);
    if (!index) return;
    ++batch_records;
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(signature));
    for (const auto& subrecord : record.subrecords) {
      const auto listed = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.signature == subrecord.signature;
      });
      EXPECT_TRUE(listed || array_member(signature, subrecord.signature))
          << record.form_key.to_string() << ' ' << subrecord.signature.to_string();
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
      const auto scoped =
          fo4::behavior_field_candidates(record, field.scalar.subrecord, field.scalar.address.repeated);
      for (std::size_t position = 0; position < record.subrecords.size(); ++position) {
        const auto& subrecord = record.subrecords[position];
        if (subrecord.signature != field.scalar.subrecord) continue;
        if (scoped && std::find(scoped->begin(), scoped->end(), position) == scoped->end()) continue;
        const auto& scalar = field.scalar;
        if (scalar.payload_size != 0 &&
            (scalar.minimum_payload_size != 0 ? subrecord.data.size() < scalar.minimum_payload_size
                                              : subrecord.data.size() != scalar.payload_size)) {
          ADD_FAILURE() << record.form_key.to_string() << ' ' << field.path << ": "
                        << subrecord.signature.to_string() << " is " << subrecord.data.size() << " bytes";
        }
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (sampled[*index]++ < 64) {
      for (const auto& field : fo4::schema::formlink_fields(signature)) {
        static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
      }
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, MaterialImpactRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
