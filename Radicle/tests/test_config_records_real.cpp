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
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view k_batch[] = {"GLOB", "GMST", "KYWD", "KSSM", "CLFM", "LCRT", "VTYP",
                                        "DMGT", "AACT", "DFOB", "OTFT", "CMPO", "DLBR", "DLVW"};

bool in_batch(fo4::Signature signature) {
  return std::any_of(std::begin(k_batch), std::end(k_batch), [&](std::string_view name) {
    return signature == fo4::Signature::from_string(name);
  });
}

class ConfigRecordsReal : public testing::TestWithParam<const char*> {};

TEST_P(ConfigRecordsReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
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
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      const auto slots = fo4::schema::resolve_field_slots(
          record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      EXPECT_FALSE(slots.malformed) << record.form_key.to_string() << ' ' << field.path;
      EXPECT_FALSE(slots.misaligned) << record.form_key.to_string() << ' ' << field.path;
      for (const auto& slot : slots.slots) {
        if (!slot.valid || (field.scalar.localized && plugin.header.tes4_flags.is_localized())) continue;
        static_cast<void>(fo4::schema::decode_scalar(
            fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
            field.scalar));
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(signature)) {
      static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
    }
    if (signature == fo4::Signature::from_string("GMST")) {
      const auto edid = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& subrecord) {
        return subrecord.signature == fo4::Signature::from_string("EDID");
      });
      const char initial = edid == record.subrecords.end() || edid->data.empty() ? '\0' : static_cast<char>(edid->data.front());
      const std::pair<std::string_view, bool> gates[] = {
          {"GMST\\FloatValue", initial == 'f'},
          {"GMST\\IntegerValue", initial != 'b' && initial != 'f' && initial != 's'},
          {"GMST\\BooleanValue", initial == 'b'},
          {"GMST\\StringValue", initial == 's'}};
      for (const auto& [path, expected] : gates) {
        const auto field = *fo4::schema::scalar_field_descriptor(path);
        const auto slots = fo4::schema::resolve_field_slots(
            record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
        EXPECT_EQ(fo4::record_field_count(plugin, record.form_key, path), 1u)
            << record.form_key.to_string() << ' ' << path;
        ASSERT_EQ(slots.slots.size(), 1u) << record.form_key.to_string() << ' ' << path;
        EXPECT_EQ(slots.slots[0].valid, expected) << record.form_key.to_string() << ' ' << path;
      }
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ConfigRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
