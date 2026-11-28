#include "fo4/forms/object_template.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "authoring_order.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"
#include "official_masters.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view k_signatures[] = {"ACTI", "CONT", "DOOR", "FURN", "MSTT", "TACT", "TERM"};

class ObjectBaseRecordsReal : public testing::TestWithParam<const char*> {};

std::size_t count_subrecords(const fo4::Record& record, fo4::Signature signature) {
  return static_cast<std::size_t>(std::count_if(
      record.subrecords.begin(), record.subrecords.end(),
      [&](const fo4::Subrecord& sub) { return sub.signature == signature; }));
}

TEST_P(ObjectBaseRecordsReal, RowsDecodeAndAuthoringLimitsHold) {
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
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

    fo4::visit_records(plugin, [&](const fo4::Record& record) {
      if (record.header.signature != signature) return;
      ++total;
      const auto key = record.form_key.to_string();
      for (const auto& sub : record.subrecords) {
        for (const auto& field : scalars) {
          if (sub.signature != field.scalar.subrecord) continue;
          if (field.scalar.payload_size != 0 && sub.data.size() != field.scalar.payload_size) {
            ADD_FAILURE() << GetParam() << ' ' << key << ' ' << field.path << ": "
                          << sub.signature.to_string() << " is " << sub.data.size()
                          << " bytes, expected " << field.scalar.payload_size;
            continue;
          }
          try {
            const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
            if (bytes.empty()) continue;
            if (field.scalar.storage == fo4::schema::ScalarStorage::zstring &&
                !field.scalar.embedded &&
                (field.scalar.localized ||
                 fo4::schema::is_localized_field(signature, sub.signature))) {
              static_cast<void>(
                  fo4::schema::read_localized_string(plugin, signature, sub.signature, sub));
              continue;
            }
            static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
          } catch (const std::exception& error) {
            ADD_FAILURE() << GetParam() << ' ' << key << ' ' << field.path << ": "
                          << error.what();
          }
        }
        for (const auto& field : links) {
          if (sub.signature != field.subrecord) continue;
          const auto& sizes = field.payload_sizes;
          if (sizes[0] != 0 && sub.data.size() != sizes[0] && sub.data.size() != sizes[1]) {
            ADD_FAILURE() << GetParam() << ' ' << key << ' ' << field.path << ": "
                          << sub.signature.to_string() << " is " << sub.data.size() << " bytes";
          }
        }
      }
      const auto violation = fo4::test::authoring_order_violation(record, rules);
      if (violation.has_value()) {
        ADD_FAILURE() << GetParam() << ' ' << key << ' ' << signature_text << ' '
                      << violation->subrecord.to_string() << " is out of authoring order: index "
                      << violation->index << " follows " << violation->last_index;
      }
      for (const auto& rule : rules) {
        const auto count = count_subrecords(record, rule.signature);
        EXPECT_GE(count, static_cast<std::size_t>(rule.minimum))
            << GetParam() << ' ' << key << ' ' << signature_text << ' ' << rule.signature.to_string();
        EXPECT_LE(count, static_cast<std::size_t>(rule.maximum))
            << GetParam() << ' ' << key << ' ' << signature_text << ' ' << rule.signature.to_string();
      }
    });
  }
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_GT(total, 0u);
  }
}

TEST_P(ObjectBaseRecordsReal, ObjectTemplateCountsMatchTheirCombinations) {
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);

  const auto obte = fo4::Signature::from_string("OBTE");
  const auto obtf = fo4::Signature::from_string("OBTF");
  const auto obts = fo4::Signature::from_string("OBTS");
  const auto stop = fo4::Signature::from_string("STOP");
  const auto full = fo4::Signature::from_string("FULL");
  std::size_t templates = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != fo4::Signature::from_string("NPC_") &&
        record.header.signature != fo4::Signature::from_string("FURN")) {
      return;
    }
    const auto key = record.form_key.to_string();
    std::size_t obte_index = record.subrecords.size();
    std::uint32_t expected = 0;
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      if (record.subrecords[i].signature == obte) {
        obte_index = i;
        const auto& data = record.subrecords[i].data;
        if (data.size() != 4) {
          ADD_FAILURE() << GetParam() << ' ' << key << " OBTE is " << data.size() << " bytes";
          return;
        }
        expected = static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[0])) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1])) << 8) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3])) << 24);
        break;
      }
    }
    if (obte_index == record.subrecords.size()) return;
    ++templates;
    std::size_t combinations = 0;
    bool saw_stop = false;
    for (std::size_t i = obte_index + 1; i < record.subrecords.size(); ++i) {
      const auto& sub = record.subrecords[i];
      if (sub.signature == obts) ++combinations;
      if (sub.signature == stop) {
        saw_stop = true;
        EXPECT_TRUE(sub.data.empty()) << GetParam() << ' ' << key << " STOP is not empty";
        break;
      }
      if (sub.signature == obtf) {
        EXPECT_TRUE(sub.data.empty()) << GetParam() << ' ' << key << " OBTF is not empty";
      }
      if (sub.signature == full) {
        // invariant: xEdit SetUnordered lets the name lead without OBTF (DLCRobot has two).
        const bool follows_marker =
            i >= obte_index + 1 && (record.subrecords[i - 1].signature == obtf ||
                                    record.subrecords[i - 1].signature == obte);
        EXPECT_TRUE(follows_marker)
            << GetParam() << ' ' << key << " template FULL does not follow OBTF or OBTE";
      }
    }
    EXPECT_TRUE(saw_stop) << GetParam() << ' ' << key << " template has no STOP";
    EXPECT_EQ(combinations, static_cast<std::size_t>(expected))
        << GetParam() << ' ' << key << " OBTE does not match the OBTS runs";
    // Detects: a marker between combinations made every multi-combination template unreadable, so it must decode.
    for (std::size_t combination = 0; combination < combinations; ++combination) {
      fo4::forms::ObjectTemplateRows rows;
      EXPECT_EQ(fo4::forms::read_object_template_rows(plugin, record.form_key, combination, rows),
                fo4::forms::ObjectTemplateWrite::updated)
          << GetParam() << ' ' << key << " combination " << combination;
    }
  });
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_EQ(templates, 267u) << GetParam();
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ObjectBaseRecordsReal, testing::ValuesIn(fo4::test::kOfficialMasters));

}
