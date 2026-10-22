#include "fo4/forms/weapon.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

class WeaponReal : public testing::TestWithParam<const char*> {};

TEST_P(WeaponReal, EveryWeaponDecodesWithoutFailure) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto weap = fo4::Signature::from_string("WEAP");
  const auto fields = fo4::schema::scalar_fields(weap);

  std::vector<fo4::FormKey> weapons;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == weap) weapons.push_back(record.form_key);
  });
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_GT(weapons.size(), 0u);
  }

  for (const auto& key : weapons) {
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr);
    try {
      static_cast<void>(fo4::forms::decode_weapon_damage_types(plugin, key));
    } catch (const std::exception& error) {
      ADD_FAILURE() << GetParam() << ' ' << key.to_string() << " DAMA: " << error.what();
    }
    const auto amount = fo4::schema::scalar_field_descriptor(weap, "WEAP\\DamageAmount");
    ASSERT_TRUE(amount.has_value());
    const auto amount_slots = fo4::schema::resolve_field_slots(
        *record, fo4::schema::scalar_field_shape(amount->scalar), amount->scalar.address);
    for (const auto& slot : amount_slots.slots) {
      EXPECT_EQ(slot.valid, record->header.form_version < amount->scalar.max_form_version)
          << GetParam() << ' ' << key.to_string() << " DamageAmount slot at " << slot.offset;
    }
    for (const auto& field : fields) {
      const fo4::Subrecord* sub = nullptr;
      for (const auto& candidate : record->subrecords) {
        if (candidate.signature == field.scalar.subrecord) {
          sub = &candidate;
          break;
        }
      }
      if (sub == nullptr) continue;
      if (field.scalar.payload_size != 0 && sub->data.size() != field.scalar.payload_size) {
        ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << ": "
                      << field.scalar.subrecord.to_string() << " is " << sub->data.size()
                      << " bytes, expected " << field.scalar.payload_size;
        continue;
      }
      const auto bytes = fo4::schema::scalar_field_bytes(sub->data, field.scalar);
      if (bytes.empty()) {
        if (field.scalar.address.segment.count != 0) {
          EXPECT_TRUE(fo4::schema::header_segment(sub->data, weap, field.scalar.subrecord,
                                                  field.scalar.address)
                          .has_value())
              << GetParam() << ' ' << key.to_string() << ' ' << field.path;
        }
        continue;
      }
      try {
        static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      } catch (const std::exception& error) {
        ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ' ' << field.path << ": "
                      << error.what();
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, WeaponReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
