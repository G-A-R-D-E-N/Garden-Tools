#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"

#include "condition_scope_fixture.hpp"
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

using namespace fo4::test::scope;

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
