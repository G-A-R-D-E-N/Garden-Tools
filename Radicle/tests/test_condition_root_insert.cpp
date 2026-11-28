#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

fo4::Plugin make_plugin(std::string_view signature,
                        const std::vector<std::string_view>& subrecords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondRecord\0", 12});
  for (const auto sig : subrecords) fo4::test::write_subrecord(out, sig, std::string_view{"Data"});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> sigs;
  for (const auto& sub : record.subrecords) sigs.push_back(sub.signature.to_string());
  return sigs;
}

fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

}  // namespace

TEST(ConditionRootInsert, InfoWithoutSuccessorAppendsListAtEnd) {
  auto plugin = make_plugin("INFO", {"TRDT", "NAM1"});
  const std::vector<fo4::CtdaCondition> conditions{function_condition(7)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse("000800:Cond.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures_of(*record), (std::vector<std::string>{"EDID", "TRDT", "NAM1", "CTDA"}));
  const auto read = fo4::schema::read_root_conditions(*record);
  ASSERT_TRUE(read.ok);
  ASSERT_EQ(read.conditions.size(), 1u);
  EXPECT_EQ(read.conditions[0].function, 7);
}

TEST(ConditionRootInsert, PerkWithoutSuccessorAppendsListAtEnd) {
  auto plugin = make_plugin("PERK", {"ICON"});
  const std::vector<fo4::CtdaCondition> conditions{function_condition(7)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse("000800:Cond.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures_of(*record), (std::vector<std::string>{"EDID", "ICON", "CTDA"}));
  const auto read = fo4::schema::read_root_conditions(*record);
  ASSERT_TRUE(read.ok);
  ASSERT_EQ(read.conditions.size(), 1u);
  EXPECT_EQ(read.conditions[0].function, 7);
}
