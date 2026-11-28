#include "fo4/c_api_records.h"
#include "fo4/c_api_ui_records.h"
#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"
#include "official_masters.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view k_signatures[] = {"NOTE", "ENCH", "AMDL", "AORU", "PKIN", "DOBJ", "INNR"};

class ItemModRecordsReal : public testing::TestWithParam<const char*> {};

struct OmodRecordValidation {
  std::vector<std::string> errors;
  bool has_attach_point{false};
  std::size_t include_rows{0};
  std::size_t attach_parent_slot_rows{0};
};

OmodRecordValidation validate_omod_record(const fo4::Plugin& plugin, const fo4::Record& record,
                                          const fo4::PluginFormIdContext& context,
                                          const fo4::schema::ScalarFieldDescriptor& include_count,
                                          const fo4::schema::ScalarFieldDescriptor& property_count,
                                          const fo4::schema::ScalarFieldDescriptor& form_type,
                                          const fo4::schema::ScalarFieldDescriptor& include_mod,
                                          const fo4::schema::ScalarFieldDescriptor& include_minimum_level,
                                          const fo4::schema::ScalarFieldDescriptor& property_value_type,
                                          const fo4::schema::ScalarFieldDescriptor& attach_parent_slot,
                                          const fo4::schema::FormLinkFieldDescriptor& attach_point) {
  OmodRecordValidation result;
  const auto word = [&](const fo4::schema::ScalarFieldDescriptor& field, std::size_t index) {
    const auto text = fo4::record_scalar_text(plugin, record, field, index);
    if (!text) throw std::runtime_error(std::string(field.path) + '[' + std::to_string(index) + "] is absent");
    return static_cast<std::uint32_t>(std::stoull(*text));
  };
  const auto expect_resolves = [&](const fo4::schema::ScalarFieldDescriptor& field, std::size_t index) {
    const fo4::RawFormId raw{word(field, index)};
    if (!raw.is_none() && !raw.is_null() && (raw.is_created() || raw.index_byte() > context.masters.size())) {
      result.errors.push_back(std::string(field.path) + '[' + std::to_string(index) + "] has an invalid FormID");
    }
  };
  const auto collect = [&](auto&& check) {
    try {
      check();
    } catch (const std::exception& error) {
      result.errors.push_back(error.what());
    }
  };
  constexpr std::uint32_t form_types[] = {1330467393u, 1598246990u, 1346454871u, 1162760014u};
  collect([&] {
    const auto type = word(form_type, 0);
    if (std::find(std::begin(form_types), std::end(form_types), type) == std::end(form_types))
      result.errors.push_back("invalid OMOD form type " + std::to_string(type));
  });
  std::optional<std::size_t> includes;
  collect([&] { includes = fo4::record_field_count(record, include_mod); result.include_rows = *includes; });
  if (includes) {
    collect([&] {
      if (*includes != word(include_count, 0)) result.errors.push_back("include count does not match rows");
    });
    collect([&] {
      if (fo4::record_field_count(record, include_minimum_level) != *includes)
        result.errors.push_back("include minimum-level rows do not match");
    });
    for (std::size_t index = 0; index < *includes; ++index)
      collect([&] { expect_resolves(include_mod, index); });
  }
  collect([&] {
    if (fo4::record_field_count(record, property_value_type) != word(property_count, 0))
      result.errors.push_back("property count does not match rows");
  });
  collect([&] {
    result.attach_parent_slot_rows = fo4::record_field_count(record, attach_parent_slot);
  });
  for (std::size_t index = 0; index < result.attach_parent_slot_rows; ++index)
    collect([&] { expect_resolves(attach_parent_slot, index); });
  collect([&] {
    const auto point = fo4::record_formlink_field_raw(record, attach_point, 0);
    result.has_attach_point = point && !point->is_null() && !point->is_none();
  });
  return result;
}

std::size_t count_subrecords(const fo4::Record& record, fo4::Signature signature) {
  return static_cast<std::size_t>(std::count_if(
      record.subrecords.begin(), record.subrecords.end(),
      [&](const fo4::Subrecord& sub) { return sub.signature == signature; }));
}

TEST_P(ItemModRecordsReal, RowsDecodeAndAuthoringLimitsHold) {
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
      for (const auto& sub : record.subrecords) {
        for (const auto& field : scalars) {
          if (sub.signature != field.scalar.subrecord) continue;
          if (field.scalar.localized && plugin.header.tes4_flags.is_localized()) continue;
          if (field.scalar.payload_size != 0 && sub.data.size() != field.scalar.payload_size) {
            ADD_FAILURE() << GetParam() << ' ' << record.form_key.to_string() << ' ' << field.path << ": "
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
            ADD_FAILURE() << GetParam() << ' ' << record.form_key.to_string() << ' ' << field.path << ": "
                          << error.what();
          }
        }
        for (const auto& field : links) {
          if (sub.signature != field.subrecord) continue;
          const auto& sizes = field.payload_sizes;
          if (sizes[0] != 0 && sub.data.size() != sizes[0] && sub.data.size() != sizes[1]) {
            ADD_FAILURE() << GetParam() << ' ' << record.form_key.to_string() << ' ' << field.path << ": "
                          << sub.signature.to_string() << " is " << sub.data.size() << " bytes";
          }
        }
      }
      for (const auto& rule : rules) {
        const auto count = count_subrecords(record, rule.signature);
        EXPECT_GE(count, static_cast<std::size_t>(rule.minimum)) << GetParam() << ' ' << record.form_key.to_string() << ' '
                                       << signature_text << ' ' << rule.signature.to_string();
        EXPECT_LE(count, static_cast<std::size_t>(rule.maximum)) << GetParam() << ' ' << record.form_key.to_string() << ' '
                                       << signature_text << ' ' << rule.signature.to_string();
      }
    });
  }
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_GT(total, 0u);
  }
}

// Constraint: OMOD reads go through the generic OMOD\ rows (FLAT_WRAPPERS_FREEZE.md), so on every official OMOD the DATA run rows must line up with the counts in the DATA header, the form type must be one the schema names, and every include and attach parent slot word must resolve the way radicle_record_resolve_form_id resolves it.
TEST_P(ItemModRecordsReal, EveryObjectModReadsThroughTheGenericRows) {
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto context = fo4::plugin_form_id_context(plugin);

  const auto scalar = [](std::string_view row) {
    auto field = fo4::schema::scalar_field_descriptor(row);
    EXPECT_TRUE(field.has_value()) << row;
    return field;
  };
  const auto include_count = scalar("OMOD\\IncludeCount");
  const auto property_count = scalar("OMOD\\PropertyCount");
  const auto form_type = scalar("OMOD\\FormType");
  const auto include_mod = scalar("OMOD\\IncludeMod");
  const auto include_minimum_level = scalar("OMOD\\IncludeMinimumLevel");
  const auto property_value_type = scalar("OMOD\\PropertyValueType");
  const auto attach_parent_slot = scalar("OMOD\\AttachParentSlot");
  const auto attach_point = fo4::schema::formlink_field_descriptor("OMOD\\AttachPoint");
  ASSERT_TRUE(include_count && property_count && form_type && include_mod && include_minimum_level &&
              property_value_type && attach_parent_slot);
  ASSERT_TRUE(attach_point.has_value());

  const auto signature = fo4::Signature::from_string("OMOD");

  std::size_t object_mods = 0;
  std::size_t with_attach_point = 0;
  std::size_t include_rows = 0;
  std::size_t attach_parent_slot_rows = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != signature) return;
    ++object_mods;
    const auto where = std::string(GetParam()) + ' ' + record.form_key.to_string();
    const auto validation = validate_omod_record(plugin, record, context, *include_count, *property_count, *form_type,
                                                 *include_mod, *include_minimum_level, *property_value_type,
                                                 *attach_parent_slot, *attach_point);
    for (const auto& error : validation.errors) ADD_FAILURE() << where << ": " << error;
    include_rows += validation.include_rows;
    attach_parent_slot_rows += validation.attach_parent_slot_rows;
    if (validation.has_attach_point) ++with_attach_point;
  });
  RecordProperty("object_mods", std::to_string(object_mods));
  RecordProperty("object_mods_with_attach_point", std::to_string(with_attach_point));
  RecordProperty("include_rows", std::to_string(include_rows));
  RecordProperty("attach_parent_slot_rows", std::to_string(attach_parent_slot_rows));
  if (std::string_view{GetParam()} == "Fallout4.esm") {
    EXPECT_GT(object_mods, 0u);
    EXPECT_GT(with_attach_point, 0u);
    EXPECT_GT(include_rows, 0u);
  }
}

TEST(ItemModRecordsDecodeReal, EveryShippedObjectModPropertyRowMatchesTheGenericRowsAndTheResolveExport) {
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto path_text = path.string();
  const char* paths[] = {path_text.c_str()};
  radicle_env* loaded = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &loaded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, void (*)(radicle_env*)> env(loaded, radicle_env_destroy);
  auto plugin = fo4::read_plugin_file(path);

  const auto scalar = [](std::string_view row) {
    auto field = fo4::schema::scalar_field_descriptor(row);
    EXPECT_TRUE(field.has_value()) << row;
    return field;
  };
  const auto property_value_type = scalar("OMOD\\PropertyValueType");
  const auto property_count = scalar("OMOD\\PropertyCount");
  const auto attach_point = fo4::schema::formlink_field_descriptor("OMOD\\AttachPoint");
  ASSERT_TRUE(property_value_type && property_count && attach_point);
  const auto signature = fo4::Signature::from_string("OMOD");

  std::size_t object_mods = 0;
  std::size_t property_rows = 0;
  std::size_t form_rows = 0;
  std::size_t attach_points = 0;
  std::size_t declared_mismatches = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != signature) return;
    ++object_mods;
    const auto where = record.form_key.to_string();
    const radicle_record* wired = nullptr;
    ASSERT_EQ(radicle_env_winner(env.get(),
                                 radicle_form_key{"Fallout4.esm",
                                                  static_cast<std::uint32_t>(record.form_key.local().value())},
                                 &wired),
              RADICLE_STATUS_OK)
        << where;
    radicle_omod_properties_info* info = nullptr;
    EXPECT_EQ(radicle_omod_properties_decode(env.get(), wired, &info), RADICLE_STATUS_OK)
        << where << ": " << radicle_last_error();
    if (info == nullptr) return;
    EXPECT_EQ(info->property_count, fo4::record_field_count(record, *property_value_type)) << where;
    const auto declared = fo4::record_scalar_text(plugin, record, *property_count, 0);
    if (declared && static_cast<std::size_t>(std::stoull(*declared)) != info->property_count) ++declared_mismatches;
    property_rows += info->property_count;
    for (std::size_t index = 0; index < info->property_count; ++index) {
      const auto& row = info->properties[index];
      if (row.value_type != 4 && row.value_type != 6) {
        EXPECT_EQ(row.value1_form.plugin, nullptr) << where << " property " << index;
        continue;
      }
      ++form_rows;
      std::uint32_t raw = 0;
      std::memcpy(&raw, row.value1, sizeof(raw));
      radicle_form_key resolved{nullptr, 0};
      ASSERT_EQ(radicle_record_resolve_form_id(env.get(), wired, raw, &resolved), RADICLE_STATUS_OK)
          << where << " property " << index;
      EXPECT_STREQ(row.value1_form.plugin, resolved.plugin) << where << " property " << index;
      EXPECT_EQ(row.value1_form.local_id, resolved.local_id) << where << " property " << index;
    }
    radicle_omod_properties_free(info);
    const auto point = fo4::record_formlink_field_raw(record, *attach_point, 0);
    if (!point || point->is_null() || point->is_none()) return;
    radicle_form_key resolved_point{nullptr, 0};
    ASSERT_EQ(radicle_record_resolve_form_id(env.get(), wired, point->value(), &resolved_point), RADICLE_STATUS_OK)
        << where;
    ASSERT_NE(resolved_point.plugin, nullptr) << where;
    ++attach_points;
  });
  RecordProperty("object_mods", std::to_string(object_mods));
  RecordProperty("property_rows", std::to_string(property_rows));
  RecordProperty("form_value_rows", std::to_string(form_rows));
  RecordProperty("attach_points", std::to_string(attach_points));
  EXPECT_GT(object_mods, 0u);
  EXPECT_GT(property_rows, 0u);
  EXPECT_GT(form_rows, 0u);
  EXPECT_GT(attach_points, 0u);
  EXPECT_EQ(declared_mismatches, 0u);
}

TEST(ItemModRecordsDecodeReal, SampledFalloutEsmObjectModAttachPointAndKeywordsDecodeThroughTheExport) {
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto path_text = path.string();
  const char* paths[] = {path_text.c_str()};
  radicle_env* loaded = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &loaded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, void (*)(radicle_env*)> env(loaded, radicle_env_destroy);
  auto plugin = fo4::read_plugin_file(path);
  const auto signature = fo4::Signature::from_string("OMOD");
  const auto keywords = fo4::schema::formlink_list_descriptor(signature, "OMOD\\TargetOMODKeywords");
  const auto attach_point = fo4::schema::formlink_field_descriptor("OMOD\\AttachPoint");
  const auto property_value_type = fo4::schema::scalar_field_descriptor("OMOD\\PropertyValueType");
  ASSERT_TRUE(keywords && attach_point && property_value_type);

  bool found = false;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (found || record.header.signature != signature) return;
    const auto point = fo4::record_formlink_field_raw(record, *attach_point, 0);
    if (!point || point->is_null() || point->is_none()) return;
    const auto keyword_count = fo4::schema::record_formlink_list_size(plugin, record.form_key, *keywords);
    ASSERT_TRUE(keyword_count.has_value()) << record.form_key.to_string();
    if (*keyword_count == 0) return;
    found = true;
    const auto where = record.form_key.to_string();
    RecordProperty("sample_form", where);
    RecordProperty("target_keyword_count", std::to_string(*keyword_count));
    const radicle_record* wired = nullptr;
    ASSERT_EQ(radicle_env_winner(env.get(),
                                 radicle_form_key{"Fallout4.esm",
                                                  static_cast<std::uint32_t>(record.form_key.local().value())},
                                 &wired),
              RADICLE_STATUS_OK)
        << where;
    radicle_form_key resolved_point{nullptr, 0};
    ASSERT_EQ(radicle_record_resolve_form_id(env.get(), wired, point->value(), &resolved_point), RADICLE_STATUS_OK)
        << where;
    ASSERT_NE(resolved_point.plugin, nullptr) << where;
    RecordProperty("attach_point_plugin", resolved_point.plugin);
    RecordProperty("attach_point_local_id", std::to_string(resolved_point.local_id));
    radicle_omod_properties_info* info = nullptr;
    ASSERT_EQ(radicle_omod_properties_decode(env.get(), wired, &info), RADICLE_STATUS_OK)
        << where << ": " << radicle_last_error();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->property_count, fo4::record_field_count(record, *property_value_type)) << where;
    RecordProperty("property_row_count", std::to_string(info->property_count));
    radicle_omod_properties_free(info);
  });
  ASSERT_TRUE(found) << "Fallout4.esm has no OMOD with attach point and target keywords";
}

TEST(ItemModRecordsMalformedData, MalformedCountedDataDoesNotHideFixedOffsetAttachPoint) {
  const auto scalar = [](std::string_view row) {
    return *fo4::schema::scalar_field_descriptor(row);
  };
  const auto attach_point = fo4::schema::formlink_field_descriptor("OMOD\\AttachPoint");
  ASSERT_TRUE(attach_point.has_value());
  fo4::Plugin plugin;
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("OMOD");
  std::vector<std::byte> data(29);
  data[10] = std::byte{0x41};
  data[11] = std::byte{0x52};
  data[12] = std::byte{0x4D};
  data[13] = std::byte{0x4F};
  data[16] = std::byte{0x02};
  data[17] = std::byte{0x08};
  data[28] = std::byte{0xFF};
  record.subrecords.push_back({fo4::Signature::from_string("DATA"), std::move(data)});

  const auto validation = validate_omod_record(
      plugin, record, {}, scalar("OMOD\\IncludeCount"), scalar("OMOD\\PropertyCount"),
      scalar("OMOD\\FormType"), scalar("OMOD\\IncludeMod"), scalar("OMOD\\IncludeMinimumLevel"),
      scalar("OMOD\\PropertyValueType"), scalar("OMOD\\AttachParentSlot"), *attach_point);
  EXPECT_TRUE(std::any_of(validation.errors.begin(), validation.errors.end(), [](const auto& error) {
    return error.find("unexpected binary width") != std::string::npos;
  }));
  EXPECT_TRUE(validation.has_attach_point);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ItemModRecordsReal, testing::ValuesIn(fo4::test::kOfficialMasters));

}
