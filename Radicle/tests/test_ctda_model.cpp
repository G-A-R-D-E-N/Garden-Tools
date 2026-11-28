#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Builds a 32-byte function-condition CTDA payload from its scalar fields.
fo4::test::ByteWriter make_ctda(std::uint8_t flags_byte, std::uint32_t comparison,
                                std::uint16_t function, std::uint16_t unknown2,
                                std::int32_t param1, std::int32_t param2,
                                std::uint32_t run_on, std::uint32_t reference,
                                std::int32_t unknown3) {
  fo4::test::ByteWriter ctda;
  ctda.u8(flags_byte);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u32(comparison);
  ctda.u16(function);
  ctda.u16(unknown2);
  ctda.u32(static_cast<std::uint32_t>(param1));
  ctda.u32(static_cast<std::uint32_t>(param2));
  ctda.u32(run_on);
  ctda.u32(reference);
  ctda.u32(static_cast<std::uint32_t>(unknown3));
  return ctda;
}

// Mutable record lookup: find_record is const-only, so walk via visit_records.
fo4::Record* find_record_mutable(fo4::Plugin& plugin, std::string_view key) {
  fo4::Record* found = nullptr;
  const auto target = fo4::FormKey::parse(key);
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (found == nullptr && record.header.signature != fo4::sig_tes4 &&
        record.form_key == target) {
      found = &record;
    }
  });
  return found;
}

fo4::Plugin make_plugin_with_conditions() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondWeapon\0", 11});

  // CITC count, then two CTDA conditions (second with CIS1/CIS2 strings).
  fo4::test::ByteWriter citc;
  citc.u32(2);
  fo4::test::write_subrecord(out, "CITC", citc.bytes);
  fo4::test::write_subrecord(out, "CTDA", make_ctda(0xE0 | 0x04, 0, 1, 0, 0x02000812, 0, 0, 0, -1).bytes);
  fo4::test::write_subrecord(out, "CTDA", make_ctda(0, 0x3F800000u, 8, 0, 1, 0, 2, 0x00000800, -1).bytes);
  fo4::test::write_subrecord(out, "CIS1", std::string_view{"SomeKeyword\0", 12});
  fo4::test::write_subrecord(out, "CIS2", std::string_view{"TargetName\0", 11});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* find_cond_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

}  // namespace

TEST(CtdaModel, ParseSerializesFunctionConditionByteIdentically) {
  auto bytes = make_ctda(0xE0 | 0x04, 0x3F800000u, 277, 0, 0x02000812, 7, 3, 0x01000800, -1).bytes;
  const auto condition = fo4::parse_ctda_condition(bytes);

  EXPECT_EQ(condition.flags_byte, 0xE4u);
  EXPECT_EQ(condition.flags(), 0x04u);            // UseGlobal
  EXPECT_EQ(condition.compare_operator(), 0x7u);  // high 3 bits
  EXPECT_TRUE(condition.use_global());
  EXPECT_FALSE(condition.is_event_data());
  EXPECT_EQ(condition.comparison_raw, 0x3F800000u);
  EXPECT_FLOAT_EQ(condition.comparison_float(), 1.0f);
  EXPECT_EQ(condition.function, 277);
  EXPECT_EQ(condition.param1, 0x02000812);
  EXPECT_EQ(condition.param2, 7);
  EXPECT_EQ(condition.run_on, 3);
  EXPECT_EQ(condition.reference, 0x01000800);
  EXPECT_EQ(condition.unknown3, -1);

  EXPECT_EQ(fo4::serialize_ctda_condition(condition), bytes);
}

TEST(CtdaModel, GetEventDataLayoutParsesAndSerializes) {
  auto bytes = make_ctda(0, 0x3F800000u, 576, 0x314F, 0x02000812, 0x55, 7, 0x01000800, -1).bytes;
  const auto condition = fo4::parse_ctda_condition(bytes);

  EXPECT_TRUE(condition.is_event_data());
  EXPECT_EQ(condition.function, 576);
  EXPECT_EQ(condition.comparison_raw, 0x3F800000u);
  EXPECT_EQ(condition.unknown2, 0x314F);
  EXPECT_EQ(condition.param1, 0x02000812);
  EXPECT_EQ(condition.param2, 0x55);
  EXPECT_EQ(condition.run_on, 7u);
  EXPECT_EQ(condition.reference, 0x01000800u);
  EXPECT_EQ(condition.unknown3, -1);

  EXPECT_EQ(fo4::serialize_ctda_condition(condition), bytes);
}

TEST(CtdaModel, FunctionFourThousandSixHundredSeventyTwoIsNotGetEventData) {
  auto bytes = make_ctda(0, 0, 4672, 0, 0x314B0000, 0, 0, 0, -1).bytes;
  const auto condition = fo4::parse_ctda_condition(bytes);

  EXPECT_FALSE(condition.is_event_data());
  EXPECT_EQ(condition.function, 4672);
}

TEST(CtdaModel, TwentyEightBytePayloadIsRefused) {
  auto bytes = make_ctda(0, 0, 576, 0x314F, 0x02000812, 0, 7, 0x01000800, -1).bytes;
  bytes.resize(28);
  EXPECT_THROW(fo4::parse_ctda_condition(bytes), std::out_of_range);
}

TEST(CtdaModel, TruncatedPayloadThrows) {
  auto bytes = make_ctda(0, 0, 1, 0, 0, 0, 0, 0, -1).bytes;
  EXPECT_THROW(fo4::parse_ctda_condition(
                   std::span<const std::byte>(bytes.data(), bytes.size() - 1)),
               std::out_of_range);
}

TEST(CtdaModel, OverlongPayloadThrows) {
  auto bytes = make_ctda(0, 0, 1, 0, 0, 0, 0, 0, -1).bytes;
  bytes.push_back(std::byte{0});
  EXPECT_THROW(fo4::parse_ctda_condition(bytes), std::out_of_range);
}

TEST(CtdaModel, PaddingBytesSurviveAConditionEdit) {
  auto bytes = make_ctda(0x40, 0x3F800000u, 8, 0, 1, 0, 2, 0x00000800, -1).bytes;
  bytes[1] = std::byte{0xA1};
  bytes[2] = std::byte{0xB2};
  bytes[3] = std::byte{0xC3};
  auto condition = fo4::parse_ctda_condition(bytes);
  condition.comparison_raw = 0x40000000u;

  auto expected = make_ctda(0x40, 0x40000000u, 8, 0, 1, 0, 2, 0x00000800, -1).bytes;
  expected[1] = std::byte{0xA1};
  expected[2] = std::byte{0xB2};
  expected[3] = std::byte{0xC3};
  EXPECT_EQ(fo4::serialize_ctda_condition(condition), expected);
}

TEST(CtdaModel, ReadAttachesCisStringsToPrecedingCondition) {
  auto plugin = make_plugin_with_conditions();
  const auto* record = find_cond_record(plugin);
  ASSERT_NE(record, nullptr);

  const auto conditions = fo4::read_ctda_conditions(*record);
  ASSERT_EQ(conditions.size(), 2u);

  // First condition: no strings.
  EXPECT_FALSE(conditions[0].parameter1_string.has_value());
  EXPECT_FALSE(conditions[0].parameter2_string.has_value());

  // Second condition carries the trailing CIS1/CIS2.
  ASSERT_TRUE(conditions[1].parameter1_string.has_value());
  EXPECT_EQ(*conditions[1].parameter1_string, "SomeKeyword");
  ASSERT_TRUE(conditions[1].parameter2_string.has_value());
  EXPECT_EQ(*conditions[1].parameter2_string, "TargetName");
}

TEST(CtdaModel, WriteReplacesRunAndUpdatesCitcCount) {
  auto plugin = make_plugin_with_conditions();
  const auto* original = find_cond_record(plugin);
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(fo4::read_ctda_conditions(*original).size(), 2u);

  fo4::CtdaCondition single;
  single.flags_byte = 0x40;  // LessThan, no flags
  single.comparison_raw = 0x40000000u;
  single.function = 8;
  single.param1 = 3;
  single.run_on = 0;
  single.reference = 0x02000800;
  single.unknown3 = -1;

  fo4::Plugin mutable_plugin = plugin;  // copy for mutation
  auto* record = find_record_mutable(mutable_plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  ASSERT_TRUE(fo4::write_ctda_conditions(*record, std::span<const fo4::CtdaCondition>{&single, 1}));

  // CITC stays and reflects the new count.
  const auto& subrecords = record->subrecords;
  ASSERT_GE(subrecords.size(), 2u);
  EXPECT_EQ(subrecords[1].signature.to_string(), "CITC");
  std::uint32_t count = 0;
  std::memcpy(&count, subrecords[1].data.data(), 4);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(subrecords[2].signature.to_string(), "CTDA");

  const auto reread = fo4::read_ctda_conditions(*record);
  ASSERT_EQ(reread.size(), 1u);
  EXPECT_EQ(reread[0].function, 8);
  EXPECT_EQ(reread[0].compare_operator(), 0x2u);
  EXPECT_FLOAT_EQ(reread[0].comparison_float(), 2.0f);
  EXPECT_EQ(reread[0].reference, 0x02000800);

  // No CIS1/CIS2 linger from the replaced run.
  for (const auto& sub : subrecords) {
    EXPECT_NE(sub.signature.to_string(), "CIS1");
    EXPECT_NE(sub.signature.to_string(), "CIS2");
  }
}

TEST(CtdaModel, WriteRefusesRecordWithoutConditions) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NoCond\0", 7});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "NoCond.esp");
  auto* target = find_record_mutable(plugin, "000800:NoCond.esp");
  ASSERT_NE(target, nullptr);
  EXPECT_FALSE(fo4::write_ctda_conditions(*target, {}));
}

TEST(CtdaModel, SetRecordConditionsRoundTripsThroughWriter) {
  auto plugin = make_plugin_with_conditions();
  fo4::CtdaCondition condition;
  condition.flags_byte = 0;
  condition.comparison_raw = 0x3F800000u;
  condition.function = 277;
  condition.param1 = 0x02000812;
  condition.run_on = 1;
  condition.reference = 0x01000800;
  condition.unknown3 = -1;

  EXPECT_EQ(fo4::set_record_conditions(
      plugin, fo4::FormKey::parse("000800:Cond.esp"),
      std::span<const fo4::CtdaCondition>{&condition, 1}), fo4::ConditionMutationStatus::updated);

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Cond.esp");
  const auto* record = find_cond_record(reread);
  ASSERT_NE(record, nullptr);
  const auto conditions = fo4::read_ctda_conditions(*record);
  ASSERT_EQ(conditions.size(), 1u);
  EXPECT_EQ(conditions[0].function, 277);
  EXPECT_EQ(conditions[0].param1, 0x02000812);
  EXPECT_EQ(conditions[0].reference, 0x01000800);
  EXPECT_FLOAT_EQ(conditions[0].comparison_float(), 1.0f);
}

namespace {

fo4::Plugin make_plugin_with_layout(const std::vector<std::string_view>& layout,
                                    std::string_view record_type = "WEAP") {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, record_type, 0);
  const auto record = fo4::test::begin_record(out, record_type, 0, 0x00000800);
  std::uint16_t function = 1;
  for (const auto sig : layout) {
    if (sig == "CTDA") {
      fo4::test::write_subrecord(out, "CTDA",
                                 make_ctda(0, 0x3F800000u, function++, 0, 1, 0, 0, 0, -1).bytes);
    } else if (sig == "CITC") {
      fo4::test::ByteWriter citc;
      citc.u32(2);
      fo4::test::write_subrecord(out, "CITC", citc.bytes);
    } else {
      fo4::test::write_subrecord(out, sig, std::string_view{"Zed\0", 4});
    }
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

fo4::CtdaCondition replacement_condition() {
  fo4::CtdaCondition c;
  c.comparison_raw = 0x3F800000u;
  c.function = 99;
  c.unknown3 = -1;
  return c;
}

std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> sigs;
  for (const auto& sub : record.subrecords) sigs.push_back(sub.signature.to_string());
  return sigs;
}

}

TEST(CtdaModel, SetRecordConditionsDistinguishesMissingRecordAndNoList) {
  auto plugin = make_plugin_with_layout({"EDID", "NAM0"});
  const auto before = fo4::write_plugin(plugin);
  const std::vector<fo4::CtdaCondition> conditions{replacement_condition()};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000999:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::record_missing);
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::no_condition_list);
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), {}),
            fo4::ConditionMutationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(CtdaModel, WriteRefusesRecordWithSeveralConditionLists) {
  auto plugin = make_plugin_with_layout({"EDID", "CTDA", "CTDA", "NAM0", "CTDA"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto before = record->subrecords;
  const auto condition = replacement_condition();
  const std::span<const fo4::CtdaCondition> span{&condition, 1};

  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 0, record->subrecords.size(), span,
                                          std::nullopt),
            fo4::CtdaScopeWrite::multiple_lists);
  EXPECT_FALSE(fo4::write_ctda_conditions(*record, span));
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), span),
            fo4::ConditionMutationStatus::multiple_lists);

  record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(record->subrecords[i].signature, before[i].signature);
    EXPECT_EQ(record->subrecords[i].data, before[i].data);
  }
}

TEST(CtdaModel, ReadRefusesSeveralConditionListsButAllowsBoundedRuns) {
  auto plugin = make_plugin_with_layout({"EDID", "CTDA", "CTDA", "NAM0", "CTDA"});
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_THROW(fo4::read_ctda_conditions(*record), std::runtime_error);
  EXPECT_THROW(fo4::read_ctda_conditions_in(*record, 1, 5), std::runtime_error);
  const auto first = fo4::read_ctda_conditions_in(*record, 1, 3);
  ASSERT_EQ(first.size(), 2u);
  EXPECT_EQ(first[0].function, 1);
  EXPECT_EQ(first[1].function, 2);
  const auto second = fo4::read_ctda_conditions_in(*record, 4, 5);
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].function, 3);
}

TEST(CtdaModel, ReadDoesNotAttachCisAfterTheRunEnds) {
  auto plugin = make_plugin_with_layout({"CTDA", "NAM0", "CIS1", "CIS2"});
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
  ASSERT_NE(record, nullptr);
  const auto conditions = fo4::read_ctda_conditions(*record);
  ASSERT_EQ(conditions.size(), 1u);
  EXPECT_FALSE(conditions[0].parameter1_string.has_value());
  EXPECT_FALSE(conditions[0].parameter2_string.has_value());
}

TEST(CtdaModel, ReadRefusesPayloadOrStringsThatCannotRoundTrip) {
  fo4::CtdaCondition condition;
  condition.function = 8;
  condition.parameter1_string = "first";
  fo4::Record record;
  record.subrecords = fo4::serialize_ctda_run(std::span(&condition, 1));
  record.subrecords[0].data.push_back(std::byte{0x55});
  EXPECT_THROW(fo4::read_ctda_conditions(record), std::runtime_error);
  record.subrecords = fo4::serialize_ctda_run(std::span(&condition, 1));
  record.subrecords[1].data.push_back(std::byte{0x55});
  EXPECT_THROW(fo4::read_ctda_conditions(record), std::runtime_error);
  record.subrecords = fo4::serialize_ctda_run(std::span(&condition, 1));
  record.subrecords[0].data.resize(4);
  EXPECT_THROW(fo4::read_ctda_conditions(record), fo4::CtdaNotLosslessError);
}

TEST(CtdaModel, WriteKeepsSubrecordsAroundSingleRun) {
  auto plugin = make_plugin_with_layout({"EDID", "CTDA", "CTDA", "NAM0", "DESC"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  ASSERT_TRUE(fo4::write_ctda_conditions(*record, std::span<const fo4::CtdaCondition>{&condition, 1}));
  const std::vector<std::string> expected{"EDID", "CTDA", "NAM0", "DESC"};
  EXPECT_EQ(signatures_of(*record), expected);
  ASSERT_EQ(fo4::read_ctda_conditions(*record).size(), 1u);
  EXPECT_EQ(fo4::read_ctda_conditions(*record)[0].function, 99);
}

TEST(CtdaModel, WriteReplacesRunWithCisAfterLeadingCitc) {
  auto plugin = make_plugin_with_layout(
      {"EDID", "CITC", "CTDA", "CIS1", "CIS2", "CTDA", "CIS2", "NAM0"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  ASSERT_TRUE(fo4::write_ctda_conditions(*record, std::span<const fo4::CtdaCondition>{&condition, 1}));
  const std::vector<std::string> expected{"EDID", "CITC", "CTDA", "NAM0"};
  EXPECT_EQ(signatures_of(*record), expected);
  std::uint32_t count = 0;
  std::memcpy(&count, record->subrecords[1].data.data(), 4);
  EXPECT_EQ(count, 1u);
}

TEST(CtdaModel, ScopedWriteLeavesOtherListUntouched) {
  auto plugin = make_plugin_with_layout({"EDID", "CTDA", "CTDA", "NAM0", "CTDA"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 1, 3,
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::nullopt),
            fo4::CtdaScopeWrite::replaced);
  const std::vector<std::string> expected{"EDID", "CTDA", "NAM0", "CTDA"};
  EXPECT_EQ(signatures_of(*record), expected);
  const auto first = fo4::read_ctda_conditions_in(*record, 1, 2);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].function, 99);
  const auto second = fo4::read_ctda_conditions_in(*record, 3, 4);
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].function, 3);
}

TEST(CtdaModel, WriteIgnoresLeadingCisBeforeFirstCtda) {
  auto plugin = make_plugin_with_layout({"EDID", "CIS1", "CTDA"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  ASSERT_TRUE(fo4::write_ctda_conditions(*record, std::span<const fo4::CtdaCondition>{&condition, 1}));
  const std::vector<std::string> expected{"EDID", "CIS1", "CTDA"};
  EXPECT_EQ(signatures_of(*record), expected);
  EXPECT_EQ(fo4::read_ctda_conditions(*record)[0].function, 99);
}

TEST(CtdaModel, InsertSetsCountOnExistingRequiredCitc) {
  auto plugin = make_plugin_with_layout({"EDID", "CITC", "DNAM"}, "SMBN");
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  std::memset(record->subrecords[1].data.data(), 0, 4);
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 0, record->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::size_t{2}),
            fo4::CtdaScopeWrite::inserted);
  const std::vector<std::string> expected{"EDID", "CITC", "CTDA", "DNAM"};
  EXPECT_EQ(signatures_of(*record), expected);
  std::uint32_t count = 0;
  std::memcpy(&count, record->subrecords[1].data.data(), 4);
  EXPECT_EQ(count, 1u);
}

TEST(CtdaModel, InsertAddsCitcToRequiredTypeWithoutOne) {
  auto plugin = make_plugin_with_layout({"EDID", "SNAM", "DNAM"}, "SMQN");
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 0, record->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::size_t{2}),
            fo4::CtdaScopeWrite::inserted);
  const std::vector<std::string> expected{"EDID", "SNAM", "CITC", "CTDA", "DNAM"};
  EXPECT_EQ(signatures_of(*record), expected);
  std::uint32_t count = 0;
  std::memcpy(&count, record->subrecords[2].data.data(), 4);
  EXPECT_EQ(count, 1u);
}

TEST(CtdaModel, InsertAddsCitcToOptionalTypeWithoutOne) {
  auto plugin = make_plugin_with_layout({"EDID", "FNAM", "NVNM"}, "ACTI");
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 0, record->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::size_t{2}),
            fo4::CtdaScopeWrite::inserted);
  const std::vector<std::string> expected{"EDID", "FNAM", "CITC", "CTDA", "NVNM"};
  EXPECT_EQ(signatures_of(*record), expected);
}

TEST(CtdaModel, InsertAddsCitcOnlyToOwnedNestedList) {
  auto branch = make_plugin_with_layout({"EDID", "ANAM", "PRCB"}, "PACK");
  auto* branch_record = find_record_mutable(branch, "000800:Cond.esp");
  ASSERT_NE(branch_record, nullptr);
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*branch_record, 0, branch_record->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::size_t{2}),
            fo4::CtdaScopeWrite::inserted);
  const std::vector<std::string> branch_expected{"EDID", "ANAM", "CITC", "CTDA", "PRCB"};
  EXPECT_EQ(signatures_of(*branch_record), branch_expected);

  auto record_level = make_plugin_with_layout({"EDID", "PSDT", "NAM0"}, "PACK");
  auto* package = find_record_mutable(record_level, "000800:Cond.esp");
  ASSERT_NE(package, nullptr);
  EXPECT_EQ(fo4::write_ctda_conditions_in(*package, 0, package->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::size_t{2}),
            fo4::CtdaScopeWrite::inserted);
  const std::vector<std::string> package_expected{"EDID", "PSDT", "CTDA", "NAM0"};
  EXPECT_EQ(signatures_of(*package), package_expected);
}

TEST(CtdaModel, RemovingAllConditionsKeepsCitcAtZero) {
  auto plugin = make_plugin_with_layout({"EDID", "CITC", "CTDA", "CTDA", "NAM0"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(fo4::write_ctda_conditions(*record, {}));
  const std::vector<std::string> expected{"EDID", "CITC", "NAM0"};
  EXPECT_EQ(signatures_of(*record), expected);
  std::uint32_t count = 1;
  std::memcpy(&count, record->subrecords[1].data.data(), 4);
  EXPECT_EQ(count, 0u);
}

TEST(CtdaModel, WriteRefusesTwoCitcLists) {
  auto plugin = make_plugin_with_layout({"CITC", "CTDA", "CITC", "CTDA"});
  auto* record = find_record_mutable(plugin, "000800:Cond.esp");
  ASSERT_NE(record, nullptr);
  const auto before = record->subrecords;
  const auto condition = replacement_condition();
  EXPECT_EQ(fo4::write_ctda_conditions_in(*record, 0, record->subrecords.size(),
                                          std::span<const fo4::CtdaCondition>{&condition, 1},
                                          std::nullopt),
            fo4::CtdaScopeWrite::multiple_lists);
  ASSERT_EQ(record->subrecords.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(record->subrecords[i].signature, before[i].signature);
    EXPECT_EQ(record->subrecords[i].data, before[i].data);
  }
}
