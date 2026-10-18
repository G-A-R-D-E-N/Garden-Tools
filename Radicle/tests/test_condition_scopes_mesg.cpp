#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Body = std::vector<std::pair<std::string, std::vector<std::byte>>>;
using Functions = std::vector<std::uint16_t>;

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> ctda_bytes(std::uint16_t function) {
  fo4::test::ByteWriter ctda;
  ctda.u32(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.i32(0);
  ctda.i32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.i32(-1);
  return ctda.bytes;
}

fo4::Plugin make_plugin(std::string_view signature, const Body& body) {
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
  for (const auto& [sig, data] : body) {
    fo4::test::write_subrecord(out, sig, std::span<const std::byte>{data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* cond_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

std::optional<fo4::schema::ConditionScopeDescriptor> descriptor_for(std::string_view record,
                                                                    std::string_view path) {
  const auto scoped =
      fo4::schema::scoped_condition_path(fo4::Signature::from_string(record), path);
  if (!scoped.has_value()) return std::nullopt;
  return scoped->descriptor;
}

Functions functions_in(const fo4::Record& record,
                       const fo4::schema::ConditionScopeDescriptor& descriptor,
                       std::size_t index) {
  const auto read = fo4::schema::read_scoped_conditions(record, descriptor, index);
  EXPECT_TRUE(read.ok) << "scope " << index << " status " << static_cast<int>(read.error);
  Functions out;
  for (const auto& condition : read.conditions) out.push_back(condition.function);
  return out;
}

std::size_t index_of_nth(const fo4::Record& record, std::string_view signature, std::size_t n) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != sig) continue;
    if (seen++ == n) return i;
  }
  return record.subrecords.size();
}

void expect_prefix_unchanged(const std::vector<fo4::Subrecord>& before, const fo4::Record& after,
                             std::size_t count) {
  ASSERT_GE(after.subrecords.size(), count);
  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << "subrecord " << i;
  }
}

fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> out;
  for (const auto& sub : record.subrecords) out.push_back(sub.signature.to_string());
  return out;
}

Body mesg_buttons_body() {
  return {
      {"DESC", zstring("Text")},
      {"ITXT", zstring("Yes")},
      {"CTDA", ctda_bytes(1)},
      {"ITXT", zstring("No")},
      {"ITXT", zstring("Maybe")},
      {"CTDA", ctda_bytes(3)},
      {"CIS1", zstring("Param")},
  };
}

}  // namespace

TEST(ConditionScopesMesg, ResolvesMenuButtonPathOnlyOnMesg) {
  for (const auto path : {"Menu Buttons[0].Conditions", "menu buttons[2].conditions",
                          " Menu Buttons[1] . Conditions "}) {
    const auto descriptor = descriptor_for("MESG", path);
    ASSERT_TRUE(descriptor.has_value()) << path;
    EXPECT_EQ(descriptor->kind, fo4::schema::ConditionScopeKind::message_button) << path;
  }
  const auto scoped = fo4::schema::scoped_condition_path(fo4::Signature::from_string("MESG"),
                                                         "Menu Buttons[1].Conditions");
  ASSERT_TRUE(scoped.has_value());
  EXPECT_EQ(scoped->container_index, 1u);

  EXPECT_FALSE(descriptor_for("MESG", "Buttons[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("MESG", "Conditions").has_value());
  EXPECT_FALSE(descriptor_for("MESG", "Menu Buttons[0].Keywords").has_value());
  EXPECT_FALSE(descriptor_for("SPEL", "Menu Buttons[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("MESG", "Menu Buttons[x].Conditions").has_value());
}

TEST(ConditionScopesMesg, ReadsOneListPerButtonAndRefusesOutOfRange) {
  auto plugin = make_plugin("MESG", mesg_buttons_body());
  const auto descriptor = descriptor_for("MESG", "Menu Buttons[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);

  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{3});
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, *descriptor, 3).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);

  const auto read = fo4::schema::read_scoped_conditions(*record, *descriptor, 2);
  ASSERT_TRUE(read.ok);
  ASSERT_EQ(read.conditions.size(), 1u);
  ASSERT_TRUE(read.conditions[0].parameter1_string.has_value());
  EXPECT_EQ(*read.conditions[0].parameter1_string, "Param");
  EXPECT_FALSE(read.conditions[0].parameter2_string.has_value());
}

TEST(ConditionScopesMesg, InsertLandsAfterItsButtonTextAndKeepsSiblings) {
  auto plugin = make_plugin("MESG", mesg_buttons_body());
  const auto descriptor = descriptor_for("MESG", "Menu Buttons[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto middle_itxt = index_of_nth(*cond_record(plugin), "ITXT", 1);

  const std::vector<fo4::CtdaCondition> conditions{function_condition(7)};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, middle_itxt + 1);
  EXPECT_EQ(signatures_of(*record),
            (std::vector<std::string>{"EDID", "DESC", "ITXT", "CTDA", "ITXT", "CTDA", "ITXT",
                                      "CTDA", "CIS1"}));
  EXPECT_EQ(index_of_nth(*record, "CTDA", 1), middle_itxt + 1);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{7});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{3});
  EXPECT_EQ(index_of_nth(*record, "CITC", 0), record->subrecords.size());
}

TEST(ConditionScopesMesg, ReplaceDropsCisWithItsConditionAndKeepsOtherButtons) {
  auto plugin = make_plugin("MESG", mesg_buttons_body());
  const auto descriptor = descriptor_for("MESG", "Menu Buttons[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto cis1 = index_of_nth(*cond_record(plugin), "CIS1", 0);
  ASSERT_LT(cis1, before.size());

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 2, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, cis1 - 1);
  EXPECT_EQ(signatures_of(*record),
            (std::vector<std::string>{"EDID", "DESC", "ITXT", "CTDA", "ITXT", "ITXT", "CTDA"}));
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{9});

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 3, conditions),
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
}

TEST(ConditionScopesMesg, BareRootWriteIsRefusedBesideButtonLists) {
  auto plugin = make_plugin("MESG", mesg_buttons_body());
  const auto before = fo4::write_plugin(plugin);
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto read = fo4::schema::read_root_conditions(*record);
  EXPECT_TRUE(read.ok);
  EXPECT_TRUE(read.conditions.empty());

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::no_condition_list);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
