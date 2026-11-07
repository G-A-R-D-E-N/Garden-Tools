#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/quest/runtime.hpp"
#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"

#include "quest_plugin_fixture.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

TEST(QuestVmadExtras, DecodesFragmentScriptsAndAliasScripts) {
  const auto extras = fo4::read_quest_vmad_extras(fo4::test::vmad_extras(), 2);

  EXPECT_FALSE(extras.unsupported) << extras.unsupported_reason;
  EXPECT_TRUE(extras.fully_consumed);
  EXPECT_EQ(extras.script_name, "QF_TestQuest");
  ASSERT_EQ(extras.properties.size(), 1u);
  EXPECT_EQ(extras.properties[0].name, "Stage");
  ASSERT_TRUE(extras.properties[0].integer.has_value());
  EXPECT_EQ(*extras.properties[0].integer, 10);

  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].stage, 10u);
  EXPECT_EQ(extras.fragments[0].script, "QF_TestQuest");
  EXPECT_EQ(extras.fragments[0].function, "Fragment_Stage_0010");

  ASSERT_EQ(extras.aliases.size(), 1u);
  EXPECT_EQ(extras.aliases[0].alias_id, 5);
  ASSERT_EQ(extras.aliases[0].scripts.size(), 1u);
  EXPECT_EQ(extras.aliases[0].scripts[0].name, "AliasScript");
  ASSERT_EQ(extras.aliases[0].scripts[0].properties.size(), 1u);
  const auto& property = extras.aliases[0].scripts[0].properties[0];
  EXPECT_EQ(property.name, "Target");
  ASSERT_TRUE(property.alias.has_value());
  EXPECT_EQ(*property.alias, 5);
  ASSERT_TRUE(property.object.has_value());
  EXPECT_EQ(property.object->value(), 0x00012345u);
}

TEST(QuestVmadExtras, TruncatedExtrasReportUnsupportedInsteadOfThrowing) {
  auto truncated = fo4::test::vmad_extras();
  truncated.resize(truncated.size() / 2);

  const auto extras = fo4::read_quest_vmad_extras(truncated, 2);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.unsupported_reason.empty());
  EXPECT_FALSE(extras.fully_consumed);
}

TEST(QuestRuntime, AggregatesStartGameFlagScriptsFragmentsAndAliases) {
  auto plugin = fo4::read_plugin(fo4::test::make_quest_plugin(1), "Quests.esp");
  const auto data = fo4::quest::quest_runtime_data(plugin, fo4::test::quest_key());

  EXPECT_TRUE(data.unsupported.empty()) << data.unsupported.front();
  EXPECT_TRUE(data.start_game_enabled);
  ASSERT_EQ(data.scripts.size(), 1u);
  EXPECT_EQ(data.scripts[0].name, "QuestScript");
  ASSERT_EQ(data.scripts[0].properties.size(), 7u);
  EXPECT_EQ(data.scripts[0].properties[0].name, "Count");
  EXPECT_EQ(data.fragment_script, "QF_TestQuest");
  ASSERT_EQ(data.fragment_properties.size(), 1u);
  ASSERT_EQ(data.fragments.size(), 1u);
  EXPECT_EQ(data.fragments[0].stage, 10u);
  EXPECT_EQ(data.fragments[0].function, "Fragment_Stage_0010");
  ASSERT_EQ(data.alias_scripts.size(), 1u);
  EXPECT_EQ(data.alias_scripts[0].alias_id, 5);
  ASSERT_EQ(data.alias_scripts[0].scripts.size(), 1u);
}

TEST(QuestRuntime, StartGameFlagComesFromDnamBitZero) {
  auto plugin = fo4::read_plugin(fo4::test::make_quest_plugin(0), "Quests.esp");
  const auto data = fo4::quest::quest_runtime_data(plugin, fo4::test::quest_key());

  EXPECT_FALSE(data.start_game_enabled);
  EXPECT_TRUE(data.unsupported.empty()) << data.unsupported.front();
  EXPECT_EQ(data.fragments.size(), 1u);
}

TEST(QuestRuntime, MissingQuestIsReportedNotGuessed) {
  auto plugin = fo4::read_plugin(fo4::test::make_quest_plugin(1), "Quests.esp");
  const auto data = fo4::quest::quest_runtime_data(plugin, fo4::FormKey::parse("000900:Quests.esp"));

  ASSERT_EQ(data.unsupported.size(), 1u);
  EXPECT_FALSE(data.unsupported[0].empty());
  EXPECT_TRUE(data.scripts.empty());
  EXPECT_TRUE(data.fragments.empty());
  EXPECT_TRUE(data.alias_scripts.empty());
  EXPECT_FALSE(data.start_game_enabled);
}

TEST(QuestRuntime, KeepsThePropertyValuesTheCAbiHasToCarry) {
  const auto seven = fo4::quest::quest_runtime_data(
      fo4::read_plugin(fo4::test::make_quest_plugin(1, 7), "Quests.esp"), fo4::test::quest_key());
  const auto ninety_nine = fo4::quest::quest_runtime_data(
      fo4::read_plugin(fo4::test::make_quest_plugin(1, 99), "Quests.esp"), fo4::test::quest_key());

  ASSERT_EQ(seven.scripts.size(), 1u);
  ASSERT_EQ(seven.scripts[0].properties.size(), 7u);
  const auto& count = seven.scripts[0].properties[0];
  ASSERT_TRUE(count.integer.has_value());
  EXPECT_EQ(*count.integer, 7);
  EXPECT_FALSE(count.text.has_value());
  const auto& ratio = seven.scripts[0].properties[1];
  EXPECT_EQ(ratio.flags, 1);
  ASSERT_TRUE(ratio.real.has_value());
  EXPECT_FLOAT_EQ(*ratio.real, 1.5f);
  const auto& enabled = seven.scripts[0].properties[2];
  ASSERT_TRUE(enabled.boolean.has_value());
  EXPECT_TRUE(*enabled.boolean);
  const auto& label = seven.scripts[0].properties[3];
  ASSERT_TRUE(label.text.has_value());
  EXPECT_EQ(*label.text, std::string("Garden\tPatch"));
  const auto& stages = seven.scripts[0].properties[4];
  ASSERT_TRUE(stages.element_count.has_value());
  EXPECT_EQ(*stages.element_count, 3u);
  const auto& embedded = seven.scripts[0].properties[5];
  ASSERT_TRUE(embedded.text.has_value());
  EXPECT_EQ(embedded.text->size(), 12u);
  EXPECT_EQ(std::memcmp(embedded.text->data(), "Garden\0Patch", 12), 0);
  const auto& empty = seven.scripts[0].properties[6];
  ASSERT_TRUE(empty.text.has_value());
  EXPECT_TRUE(empty.text->empty());

  ASSERT_EQ(ninety_nine.scripts.size(), 1u);
  ASSERT_EQ(ninety_nine.scripts[0].properties.size(), 7u);
  ASSERT_TRUE(ninety_nine.scripts[0].properties[0].integer.has_value());
  EXPECT_EQ(*ninety_nine.scripts[0].properties[0].integer, 99);
}

TEST(QuestRuntimeReal, EveryShippedQuestDecodesOrReportsUnsupported) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "Fallout4.esm not found under " << dir;

  auto plugin = fo4::read_plugin_file(path);
  const fo4::RecordIndex index{plugin};
  std::size_t quests = 0;
  std::size_t clean = 0;
  std::size_t reported = 0;
  std::size_t scripts = 0;
  std::size_t fragments = 0;
  std::size_t alias_scripts = 0;
  std::map<std::string, std::size_t> causes;

  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature != fo4::sig_qust) continue;
    ++quests;
    const auto data = fo4::quest::quest_runtime_data(index, record->form_key);
    if (data.unsupported.empty()) {
      ++clean;
    } else {
      ++reported;
      causes[data.unsupported.front()]++;
    }
    scripts += data.scripts.size();
    fragments += data.fragments.size();
    alias_scripts += data.alias_scripts.size();
  }

  std::cout << "quest runtime: quests=" << quests << " clean=" << clean << " unsupported=" << reported
            << " scripts=" << scripts << " fragments=" << fragments
            << " alias_scripts=" << alias_scripts << std::endl;
  for (const auto& cause : causes) {
    std::cout << "  class(" << cause.second << "): " << cause.first << std::endl;
  }

  EXPECT_GT(quests, 0u);
  EXPECT_EQ(clean + reported, quests) << "every quest must decode or report unsupported";
}
