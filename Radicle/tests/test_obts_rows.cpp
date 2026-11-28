#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

struct Include {
  std::uint32_t mod{0};
  std::uint8_t attach_point{0};
  std::uint8_t optional{0};
  std::uint8_t dont_use_all{0};
};

struct Property {
  std::uint8_t value_type{0};
  std::uint8_t function_type{0};
  std::uint16_t property{0};
  std::uint32_t value1{0};
  std::uint32_t value2{0};
  float step{0.0f};
};

struct Combination {
  std::uint8_t level_min{0};
  std::uint8_t level_max{0};
  std::int16_t parent{-1};
  std::uint8_t is_default{0};
  std::vector<std::uint32_t> keywords{};
  std::uint8_t min_level_for_ranks{0};
  std::uint8_t alt_levels_per_tier{0};
  std::vector<Include> includes{};
  std::vector<Property> properties{};
};

Bytes obts_payload(const Combination& combo) {
  fo4::test::ByteWriter out;
  out.u32(static_cast<std::uint32_t>(combo.includes.size()));
  out.u32(static_cast<std::uint32_t>(combo.properties.size()));
  out.u8(combo.level_min);
  out.u8(0);
  out.u8(combo.level_max);
  out.u8(0);
  out.u16(static_cast<std::uint16_t>(combo.parent));
  out.u8(combo.is_default);
  out.u8(static_cast<std::uint8_t>(combo.keywords.size()));
  for (const auto keyword : combo.keywords) out.u32(keyword);
  out.u8(combo.min_level_for_ranks);
  out.u8(combo.alt_levels_per_tier);
  for (const auto& include : combo.includes) {
    out.u32(include.mod);
    out.u8(include.attach_point);
    out.u8(include.optional);
    out.u8(include.dont_use_all);
  }
  for (const auto& property : combo.properties) {
    out.u8(property.value_type);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    out.u8(property.function_type);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    out.u16(property.property);
    out.u16(0);
    out.u32(property.value1);
    out.u32(property.value2);
    out.f32(property.step);
  }
  return out.bytes;
}

Bytes make_plugin(std::string_view record_signature, const std::vector<Bytes>& payloads) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, record_signature, 0);
  const auto record = fo4::test::begin_record(out, record_signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TemplateHost\0", 13});
  fo4::test::ByteWriter count;
  count.u32(static_cast<std::uint32_t>(payloads.size()));
  fo4::test::write_subrecord(out, "OBTE", count.bytes);
  for (const auto& payload : payloads) {
    fo4::test::write_subrecord(out, "OBTS", payload);
  }
  fo4::test::write_subrecord(out, "STOP", std::span<const std::byte>{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown scalar row " + std::string(path));
  return *field;
}

std::optional<std::string> text(const fo4::Plugin& plugin,
                                const fo4::Record& record,
                                std::string_view path,
                                std::size_t index) {
  return fo4::record_scalar_text(plugin, record, row(path), index);
}

std::size_t field_count(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view path) {
  const auto result = fo4::record_field_count(plugin, key, row(path));
  return result.value_or(~std::size_t{0});
}

std::vector<Bytes> obts_payloads(const fo4::Record& record) {
  std::vector<Bytes> out;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("OBTS")) out.push_back(subrecord.data);
  }
  return out;
}

Combination first_combination() {
  Combination combo;
  combo.level_min = 1;
  combo.level_max = 5;
  combo.parent = -1;
  combo.is_default = 1;
  combo.keywords = {0x00000111u, 0x00000222u};
  combo.min_level_for_ranks = 3;
  combo.alt_levels_per_tier = 4;
  combo.includes = {{0x0000AAAAu, 1, 1, 0}, {0x0000BBBBu, 2, 0, 1}};
  combo.properties = {{1, 2, 0x0102, 0x3F800000u, 0, 0.5f}, {0, 1, 7, 12, 34, -2.0f}};
  return combo;
}

Combination second_combination() {
  Combination combo;
  combo.level_min = 7;
  combo.level_max = 9;
  combo.parent = 3;
  combo.is_default = 0;
  combo.min_level_for_ranks = 5;
  combo.alt_levels_per_tier = 6;
  combo.properties = {{2, 0, 0x0900, 1, 0, 1.0f}};
  return combo;
}

}

TEST(ObtsRows, SchemaCarriesRunRowsForEveryRecordThatHasObjectTemplates) {
  for (const auto* record : {"ARMO", "FURN", "NPC_", "WEAP"}) {
    const std::string prefix = std::string(record) + "\\ObjectTemplate";
    for (const auto* leaf : {"LevelMin", "LevelMax", "ParentCombinationIndex", "Default"}) {
      EXPECT_TRUE(fo4::schema::scalar_field_descriptor(prefix + leaf).has_value())
          << prefix << leaf;
    }
    for (const auto* leaf : {"Keyword", "MinLevelForRanks", "AltLevelsPerTier", "IncludeMod",
                             "IncludeAttachPointIndex", "IncludeOptional", "IncludeDontUseAll",
                             "PropertyValueType", "PropertyFunctionType", "PropertyProperty",
                             "PropertyValue1", "PropertyValue2", "PropertyStep"}) {
      const auto field = fo4::schema::scalar_field_descriptor(prefix + leaf);
      ASSERT_TRUE(field.has_value()) << prefix << leaf;
      EXPECT_NE(field->scalar.address.run.count, 0u) << prefix << leaf;
      EXPECT_TRUE(field->scalar.address.repeated) << prefix << leaf;
    }
  }
}

TEST(ObtsRows, RunSpanSplitsKeywordRankIncludeAndPropertySections) {
  const auto combo = first_combination();
  const auto payload = obts_payload(combo);
  const auto field = row("ARMO\\ObjectTemplatePropertyStep");
  const auto& run = field.scalar.address.run;

  const auto properties = fo4::schema::run_span(payload, run);
  ASSERT_TRUE(properties.has_value());
  EXPECT_EQ(properties->count, 2u);
  EXPECT_EQ(properties->base, 0x10u + 4u * 2u + 2u + 7u * 2u);

  auto includes_run = row("ARMO\\ObjectTemplateIncludeOptional").scalar.address.run;
  const auto includes = fo4::schema::run_span(payload, includes_run);
  ASSERT_TRUE(includes.has_value());
  EXPECT_EQ(includes->count, 2u);
  EXPECT_EQ(includes->base, 0x10u + 4u * 2u + 2u);

  const auto keywords = fo4::schema::run_span(payload, row("ARMO\\ObjectTemplateKeyword").scalar.address.run);
  ASSERT_TRUE(keywords.has_value());
  EXPECT_EQ(keywords->count, 2u);
  EXPECT_EQ(keywords->base, 0x10u);

  auto ranks_run = row("ARMO\\ObjectTemplateMinLevelForRanks").scalar.address.run;
  const auto ranks = fo4::schema::run_span(payload, ranks_run);
  ASSERT_TRUE(ranks.has_value());
  EXPECT_EQ(ranks->count, 1u);
  EXPECT_EQ(ranks->base, 0x10u + 4u * 2u);
}

TEST(ObtsRows, RunSpanFailsClosedWhenTheSectionsDoNotEndAtThePayloadEnd) {
  const auto field = row("ARMO\\ObjectTemplatePropertyStep");
  const auto& run = field.scalar.address.run;

  auto long_payload = obts_payload(first_combination());
  long_payload.push_back(std::byte{0});
  EXPECT_FALSE(fo4::schema::run_span(long_payload, run).has_value());

  auto short_payload = obts_payload(first_combination());
  short_payload.pop_back();
  EXPECT_FALSE(fo4::schema::run_span(short_payload, run).has_value());

  auto huge_count = obts_payload(first_combination());
  huge_count[4] = std::byte{0xFF};
  huge_count[5] = std::byte{0xFF};
  huge_count[6] = std::byte{0xFF};
  huge_count[7] = std::byte{0xFF};
  EXPECT_FALSE(fo4::schema::run_span(huge_count, run).has_value());

  const Bytes empty;
  EXPECT_FALSE(fo4::schema::run_span(empty, run).has_value());
}

TEST(ObtsRows, ArmoRowsReadEveryEntryAcrossTwoCombinations) {
  const auto plugin = fo4::read_plugin(
      make_plugin("ARMO", {obts_payload(first_combination()), obts_payload(second_combination())}),
      "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateIncludeCount"), 2u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeCount", 0), "2");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeCount", 1), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyCount", 0), "2");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyCount", 1), "1");
  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateLevelMin"), 2u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateLevelMin", 0), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateLevelMin", 1), "7");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateLevelMax", 0), "5");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateLevelMax", 1), "9");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateParentCombinationIndex", 0), "-1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateParentCombinationIndex", 1), "3");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateDefault", 0), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateDefault", 1), "0");

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateKeyword"), 2u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateKeyword", 0), "273");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateKeyword", 1), "546");
  EXPECT_FALSE(text(plugin, *record, "ARMO\\ObjectTemplateKeyword", 2).has_value());

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateMinLevelForRanks"), 2u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateMinLevelForRanks", 0), "3");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateMinLevelForRanks", 1), "5");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateAltLevelsPerTier", 0), "4");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateAltLevelsPerTier", 1), "6");

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateIncludeOptional"), 2u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeMod", 0), "43690");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeMod", 1), "48059");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeAttachPointIndex", 0), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeAttachPointIndex", 1), "2");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeOptional", 0), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeOptional", 1), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeDontUseAll", 0), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateIncludeDontUseAll", 1), "1");
  EXPECT_FALSE(text(plugin, *record, "ARMO\\ObjectTemplateIncludeOptional", 2).has_value());

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplatePropertyStep"), 3u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValueType", 0), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValueType", 1), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValueType", 2), "2");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyFunctionType", 0), "2");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyFunctionType", 1), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyFunctionType", 2), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyProperty", 0), "258");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyProperty", 1), "7");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyProperty", 2), "2304");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue1", 0), "1065353216");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue1", 1), "12");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue1", 2), "1");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue2", 0), "0");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue2", 1), "34");
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplatePropertyValue2", 2), "0");
  const auto step0 = text(plugin, *record, "ARMO\\ObjectTemplatePropertyStep", 0);
  const auto step1 = text(plugin, *record, "ARMO\\ObjectTemplatePropertyStep", 1);
  const auto step2 = text(plugin, *record, "ARMO\\ObjectTemplatePropertyStep", 2);
  ASSERT_TRUE(step0.has_value());
  ASSERT_TRUE(step1.has_value());
  ASSERT_TRUE(step2.has_value());
  EXPECT_FLOAT_EQ(std::stof(*step0), 0.5f);
  EXPECT_FLOAT_EQ(std::stof(*step1), -2.0f);
  EXPECT_FLOAT_EQ(std::stof(*step2), 1.0f);
}

TEST(ObtsRows, EveryRecordTypeReadsThroughItsOwnRows) {
  for (const auto* signature : {"FURN", "NPC_", "WEAP"}) {
    const auto plugin = fo4::read_plugin(
        make_plugin(signature, {obts_payload(first_combination())}), "Templates.esp");
    const auto key = fo4::FormKey::parse("000800:Templates.esp");
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr) << signature;
    const std::string prefix = std::string(signature) + "\\ObjectTemplate";
    EXPECT_EQ(text(plugin, *record, prefix + "LevelMax", 0), "5") << signature;
    EXPECT_EQ(text(plugin, *record, prefix + "AltLevelsPerTier", 0), "4") << signature;
    EXPECT_EQ(text(plugin, *record, prefix + "IncludeDontUseAll", 1), "1") << signature;
    EXPECT_EQ(text(plugin, *record, prefix + "PropertyProperty", 1), "7") << signature;
  }
}

TEST(ObtsRows, ZeroIncludesAndZeroPropertiesLeaveOnlyTheRankRow) {
  Combination combo = second_combination();
  combo.properties.clear();
  const auto plugin =
      fo4::read_plugin(make_plugin("ARMO", {obts_payload(combo)}), "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateIncludeOptional"), 0u);
  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplatePropertyStep"), 0u);
  EXPECT_EQ(field_count(plugin, key, "ARMO\\ObjectTemplateMinLevelForRanks"), 1u);
  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateAltLevelsPerTier", 0), "6");
}

TEST(ObtsRows, MalformedPayloadFailsTheRunRowsInsteadOfDroppingSlots) {
  auto payload = obts_payload(first_combination());
  payload.push_back(std::byte{0});
  auto plugin = fo4::read_plugin(make_plugin("ARMO", {payload}), "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  EXPECT_THROW(field_count(plugin, key, "ARMO\\ObjectTemplatePropertyStep"), fo4::schema::ScalarError);
  EXPECT_THROW(field_count(plugin, key, "ARMO\\ObjectTemplateIncludeOptional"), fo4::schema::ScalarError);
  EXPECT_THROW(text(plugin, *record, "ARMO\\ObjectTemplatePropertyStep", 0), fo4::schema::ScalarError);

  const auto before = fo4::test::plugin_state(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, row("ARMO\\ObjectTemplatePropertyStep"), 0, "9"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);
}

TEST(ObtsRows, ABadCombinationDoesNotShiftTheSlotsOfTheNextOne) {
  auto bad = obts_payload(first_combination());
  bad.push_back(std::byte{0});
  auto plugin = fo4::read_plugin(make_plugin("ARMO", {bad, obts_payload(second_combination())}),
                                 "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  const auto step = row("ARMO\\ObjectTemplatePropertyStep");
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(step.scalar), step.scalar.address);
  EXPECT_TRUE(resolved.malformed);

  EXPECT_THROW(text(plugin, *record, "ARMO\\ObjectTemplatePropertyStep", 0), fo4::schema::ScalarError);
  EXPECT_THROW(text(plugin, *record, "ARMO\\ObjectTemplateMinLevelForRanks", 0), fo4::schema::ScalarError);
  const auto before = fo4::test::plugin_state(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, step, 0, "9"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, row("ARMO\\ObjectTemplatePropertyProperty"), 0, "9"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);

  EXPECT_EQ(text(plugin, *record, "ARMO\\ObjectTemplateLevelMin", 1), "7");

  bool flagged = false;
  for (const auto& issue : fo4::schema::preflight_record(*record)) {
    if (issue.code == "invalid_scalar_storage" && issue.path == "ARMO\\ObjectTemplatePropertyStep") {
      flagged = true;
    }
  }
  EXPECT_TRUE(flagged);
}

TEST(ObtsRows, TheIncludeAndPropertyCountRowsRefuseWrites) {
  for (const auto* signature : {"ARMO", "FURN", "NPC_", "WEAP"}) {
    auto plugin = fo4::read_plugin(make_plugin(signature, {obts_payload(first_combination())}),
                                   "Templates.esp");
    const auto key = fo4::FormKey::parse("000800:Templates.esp");
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr) << signature;
    const std::string prefix = std::string(signature) + "\\ObjectTemplate";
    EXPECT_EQ(text(plugin, *record, prefix + "IncludeCount", 0), "2") << signature;
    EXPECT_EQ(text(plugin, *record, prefix + "PropertyCount", 0), "2") << signature;

    const auto before = fo4::test::plugin_state(plugin);
    EXPECT_THROW(fo4::set_record_scalar(plugin, key, row(prefix + "IncludeCount"), 0, "0"),
                 fo4::schema::ScalarError)
        << signature;
    EXPECT_THROW(fo4::set_record_scalar(plugin, key, row(prefix + "PropertyCount"), 0, "5"),
                 fo4::schema::ScalarError)
        << signature;
    EXPECT_EQ(fo4::test::plugin_state(plugin), before) << signature;
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, row(prefix + "LevelMax"), 0, "6"),
              fo4::ScalarMutationStatus::updated)
        << signature;
  }

  for (const auto* signature : {"FURN", "NPC_"}) {
    auto plugin = fo4::read_plugin(make_plugin(signature, {obts_payload(first_combination())}),
                                   "Templates.esp");
    const auto key = fo4::FormKey::parse("000800:Templates.esp");
    const std::string prefix = std::string(signature) + "\\ObjectTemplate";
    const std::array<std::pair<const char*, const char*>, 7> edits{{{"MinLevelForRanks", "0"},
                                                                    {"AltLevelsPerTier", "0"},
                                                                    {"IncludeAttachPointIndex", "0"},
                                                                    {"IncludeOptional", "0"},
                                                                    {"IncludeDontUseAll", "1"},
                                                                    {"PropertyValueType", "0"},
                                                                    {"PropertyFunctionType", "0"}}};
    for (const auto& [leaf, value] : edits) {
      EXPECT_EQ(fo4::set_record_scalar(plugin, key, row(prefix + leaf), 0, value),
                fo4::ScalarMutationStatus::updated)
          << signature << leaf;
    }
  }
}

TEST(ObtsRows, WritesLandOnTheRightBytesAndNothingElseMoves) {
  auto plugin = fo4::read_plugin(
      make_plugin("FURN", {obts_payload(first_combination()), obts_payload(second_combination())}),
      "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplateIncludeOptional"), 1, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplateMinLevelForRanks"), 1, "11"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplatePropertyProperty"), 2, "4660"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplatePropertyStep"), 1, "3.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplateLevelMax"), 1, "20"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplateKeyword"), 1, "4660"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplateIncludeMod"), 1, "255"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplatePropertyValue1"), 2, "77"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("FURN\\ObjectTemplatePropertyValue2"), 0, "5"),
            fo4::ScalarMutationStatus::updated);

  auto first = first_combination();
  first.includes[1].optional = 1;
  first.properties[1].step = 3.5f;
  first.keywords[1] = 4660;
  first.includes[1].mod = 255;
  first.properties[0].value2 = 5;
  auto second = second_combination();
  second.min_level_for_ranks = 11;
  second.properties[0].property = 4660;
  second.level_max = 20;
  second.properties[0].value1 = 77;

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Templates.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(obts_payloads(*record), (std::vector<Bytes>{obts_payload(first), obts_payload(second)}));
}

TEST(ObtsRows, OutOfRangeIndexAndBadValuesDoNotTouchTheRecord) {
  auto plugin = fo4::read_plugin(make_plugin("WEAP", {obts_payload(first_combination())}),
                                 "Templates.esp");
  const auto key = fo4::FormKey::parse("000800:Templates.esp");
  const auto before = fo4::test::plugin_state(plugin);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("WEAP\\ObjectTemplateIncludeOptional"), 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, row("WEAP\\ObjectTemplatePropertyStep"), 2, "1.0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, row("WEAP\\ObjectTemplateIncludeOptional"), 0, "2"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);
}
