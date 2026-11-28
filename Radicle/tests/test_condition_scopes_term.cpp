#include "condition_scope_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::scope;

std::vector<std::byte> u8_bytes(std::uint8_t value) {
  fo4::test::ByteWriter out;
  out.u8(value);
  return out.bytes;
}

std::vector<std::byte> half(std::uint16_t value) {
  fo4::test::ByteWriter out;
  out.u16(value);
  return out.bytes;
}

std::vector<std::byte> word(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Body term_body() {
  return {
      {"COCT", word(1)},
      {"CNTO", u8_bytes(0)},
      {"BSIZ", word(2)},
      {"BTXT", zstring("Welcome")},
      {"CTDA", ctda_bytes(1)},
      {"BTXT", zstring("Goodbye")},
      {"ISIZ", word(2)},
      {"ITXT", zstring("Open")},   {"RNAM", zstring("Opened")}, {"ANAM", u8_bytes(8)},
      {"ITID", half(1)},           {"UNAM", zstring("Display")}, {"VNAM", zstring("img.dds")},
      {"CTDA", ctda_bytes(5)},     {"CIS1", zstring("Param")},
      {"ITXT", zstring("Back")},   {"ANAM", u8_bytes(5)},
  };
}

}

TEST(ConditionScopesTerm, ResolvesBodyTextAndMenuItemPathsOnlyOnTerm) {
  for (const auto path : {"Body Text[0].Conditions", "body text[1].conditions",
                          " Body Text[0] . Conditions "}) {
    const auto descriptor = descriptor_for("TERM", path);
    ASSERT_TRUE(descriptor.has_value()) << path;
    EXPECT_EQ(descriptor->kind, fo4::schema::ConditionScopeKind::terminal_body_text) << path;
  }
  for (const auto path : {"Menu Items[0].Conditions", "menu items[1].conditions"}) {
    const auto descriptor = descriptor_for("TERM", path);
    ASSERT_TRUE(descriptor.has_value()) << path;
    EXPECT_EQ(descriptor->kind, fo4::schema::ConditionScopeKind::terminal_menu_item) << path;
  }

  EXPECT_FALSE(descriptor_for("MESG", "Body Text[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("MESG", "Menu Items[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("TERM", "Buttons[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("TERM", "Body Text[0].Keywords").has_value());
  EXPECT_FALSE(descriptor_for("TERM", "Menu Items[x].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("TERM", "Item Text[0].Conditions").has_value());
}

TEST(ConditionScopesTerm, ReadsOneListPerBodyTextItemAndPerMenuItem) {
  auto plugin = make_plugin("TERM", term_body());
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto body = descriptor_for("TERM", "Body Text[0].Conditions");
  const auto menu = descriptor_for("TERM", "Menu Items[0].Conditions");
  ASSERT_TRUE(body.has_value());
  ASSERT_TRUE(menu.has_value());

  EXPECT_EQ(functions_in(*record, *body, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *body, 1), Functions{});
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, *body, 2).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
  EXPECT_EQ(functions_in(*record, *menu, 0), Functions{5});
  EXPECT_EQ(functions_in(*record, *menu, 1), Functions{});
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, *menu, 2).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);

  const auto read = fo4::schema::read_scoped_conditions(*record, *menu, 0);
  ASSERT_TRUE(read.ok);
  ASSERT_EQ(read.conditions.size(), 1u);
  ASSERT_TRUE(read.conditions[0].parameter1_string.has_value());
  EXPECT_EQ(*read.conditions[0].parameter1_string, "Param");
  EXPECT_FALSE(read.conditions[0].parameter2_string.has_value());
}

TEST(ConditionScopesTerm, BodyTextItemWithoutConditionsReadsNoneBesideTheMenuItems) {
  auto plugin = make_plugin("TERM", term_body());
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto body = descriptor_for("TERM", "Body Text[0].Conditions");
  ASSERT_TRUE(body.has_value());

  const auto read = fo4::schema::read_scoped_conditions(*record, *body, 1);
  ASSERT_TRUE(read.ok) << static_cast<int>(read.error);
  EXPECT_TRUE(read.conditions.empty());
  EXPECT_EQ(index_of_nth(*record, "BTXT", 1) + 1, index_of_nth(*record, "ISIZ", 0));
}

TEST(ConditionScopesTerm, InsertLandsAfterItsBodyTextAndAtTheEndOfItsMenuItem) {
  auto plugin = make_plugin("TERM", term_body());
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto before = record->subrecords;
  const auto body = descriptor_for("TERM", "Body Text[0].Conditions");
  const auto menu = descriptor_for("TERM", "Menu Items[0].Conditions");
  ASSERT_TRUE(body.has_value());
  ASSERT_TRUE(menu.has_value());
  const auto body_item = index_of_nth(*record, "BTXT", 1);
  const std::vector<fo4::CtdaCondition> conditions{function_condition(7)};

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *body, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *menu, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* edited = cond_record(reread);
  ASSERT_NE(edited, nullptr);
  expect_prefix_unchanged(before, *edited, body_item + 1);
  EXPECT_EQ(index_of_nth(*edited, "CTDA", 1), body_item + 1);
  EXPECT_EQ(signatures_of(*edited),
            (std::vector<std::string>{"EDID", "COCT", "CNTO", "BSIZ", "BTXT", "CTDA", "BTXT", "CTDA",
                                      "ISIZ", "ITXT", "RNAM", "ANAM", "ITID", "UNAM", "VNAM", "CTDA",
                                      "CIS1", "ITXT", "ANAM", "CTDA"}));
  EXPECT_EQ(index_of_nth(*edited, "CTDA", 3), edited->subrecords.size() - 1);
  EXPECT_EQ(edited->subrecords.size(), before.size() + 2);
  EXPECT_EQ(functions_in(*edited, *body, 0), Functions{1});
  EXPECT_EQ(functions_in(*edited, *body, 1), Functions{7});
  EXPECT_EQ(functions_in(*edited, *menu, 0), Functions{5});
  EXPECT_EQ(functions_in(*edited, *menu, 1), Functions{7});

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *menu, 2, conditions),
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *body, 2, conditions),
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
}

TEST(ConditionScopesTerm, ReplacingAMenuItemListDropsItsCisAndKeepsTheOtherItems) {
  auto plugin = make_plugin("TERM", term_body());
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto menu = descriptor_for("TERM", "Menu Items[0].Conditions");
  const auto body = descriptor_for("TERM", "Body Text[0].Conditions");
  ASSERT_TRUE(menu.has_value());
  ASSERT_TRUE(body.has_value());
  const auto cis1 = index_of_nth(*record, "CIS1", 0);
  ASSERT_LT(cis1, record->subrecords.size());
  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *menu, 0, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* edited = cond_record(reread);
  ASSERT_NE(edited, nullptr);
  EXPECT_EQ(index_of_nth(*edited, "CIS1", 0), edited->subrecords.size());
  EXPECT_EQ(functions_in(*edited, *body, 0), Functions{1});
  EXPECT_EQ(functions_in(*edited, *body, 1), Functions{});
  EXPECT_EQ(functions_in(*edited, *menu, 0), Functions{9});
  EXPECT_EQ(functions_in(*edited, *menu, 1), Functions{});
}

TEST(ConditionScopesTerm, BareRootWriteIsRefusedBesideItemLists) {
  auto plugin = make_plugin("TERM", term_body());
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
