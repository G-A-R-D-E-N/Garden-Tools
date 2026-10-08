#include "fo4/c_api.h"
#include "fo4/c_api_quest.h"

#include "quest_plugin_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TempPlugin {
public:
  explicit TempPlugin(const std::vector<std::byte>& bytes) {
    std::error_code error;
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_c_api_quest_" + std::to_string(id));
    std::filesystem::create_directories(root_, error);
    path_ = root_ / "Quests.esp";
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

  ~TempPlugin() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path root_;
  std::filesystem::path path_;
};

bool hasRow(char** rows, size_t count, const std::string& expected) {
  for (size_t index = 0; index < count; ++index) {
    if (rows[index] != nullptr && expected == rows[index]) return true;
  }
  return false;
}

const radicle_quest_property* findProperty(const radicle_quest_property* properties, size_t count,
                                           const std::string& name) {
  for (size_t index = 0; index < count; ++index) {
    if (properties[index].name != nullptr && name == properties[index].name) {
      return &properties[index];
    }
  }
  return nullptr;
}

std::vector<std::string> questRows(std::int32_t count) {
  const TempPlugin file(fo4::test::make_quest_plugin(1, count));

  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  char** rows = nullptr;
  size_t row_count = 0;
  EXPECT_EQ(radicle_quest_runtime_rows(plugin, "000800:Quests.esp", &rows, &row_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  std::vector<std::string> result;
  for (size_t index = 0; index < row_count; ++index) {
    result.emplace_back(rows[index]);
  }
  radicle_free_string_array(rows, row_count);
  radicle_plugin_destroy(plugin);
  return result;
}

}

TEST(CApiQuest, ReportsQuestRuntimeRows) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_rows(plugin, "000800:Quests.esp", &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(count, 6u);

  EXPECT_TRUE(hasRow(rows, count, "quest\t000800:Quests.esp\t1"));
  EXPECT_TRUE(hasRow(rows, count, "script\tQuestScript"));
  EXPECT_TRUE(hasRow(rows, count, "fragment-script\tQF_TestQuest"));
  EXPECT_TRUE(hasRow(rows, count, "fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010"));
  EXPECT_TRUE(hasRow(rows, count, "alias\t5"));
  EXPECT_TRUE(hasRow(rows, count, "alias-script\t5\tAliasScript"));

  for (size_t index = 0; index < count; ++index) {
    EXPECT_EQ(std::string(rows[index]).find("property"), std::string::npos);
  }

  radicle_free_string_array(rows, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, RowsCarryNoPropertyFacts) {
  const auto seven = questRows(7);
  const auto ninety_nine = questRows(99);

  EXPECT_EQ(seven, ninety_nine);
}

TEST(CApiQuest, ReportsEveryPropertyValue) {
  const TempPlugin file(fo4::test::make_quest_plugin(1, 7));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(properties, nullptr);
  ASSERT_EQ(count, 9u);

  const auto* integer = findProperty(properties, count, "Count");
  ASSERT_NE(integer, nullptr);
  EXPECT_EQ(integer->owner, RADICLE_QUEST_PROPERTY_SCRIPT);
  EXPECT_EQ(integer->alias, -1);
  ASSERT_NE(integer->script, nullptr);
  EXPECT_STREQ(integer->script, "QuestScript");
  EXPECT_EQ(integer->type, 3);
  EXPECT_EQ(integer->has_integer, 1);
  EXPECT_EQ(integer->integer, 7);

  const auto* real = findProperty(properties, count, "Ratio");
  ASSERT_NE(real, nullptr);
  EXPECT_EQ(real->flags, 1);
  EXPECT_EQ(real->has_real, 1);
  EXPECT_FLOAT_EQ(real->real, 1.5f);

  const auto* boolean = findProperty(properties, count, "Enabled");
  ASSERT_NE(boolean, nullptr);
  EXPECT_EQ(boolean->has_boolean, 1);
  EXPECT_EQ(boolean->boolean, 1);

  const auto* text = findProperty(properties, count, "Label");
  ASSERT_NE(text, nullptr);
  ASSERT_NE(text->text, nullptr);
  EXPECT_EQ(text->text_size, 12u);
  EXPECT_EQ(std::string(text->text), std::string("Garden\tPatch"));

  const auto* elements = findProperty(properties, count, "Stages");
  ASSERT_NE(elements, nullptr);
  EXPECT_EQ(elements->has_element_count, 1);
  EXPECT_EQ(elements->element_count, 3u);

  const auto* fragment = findProperty(properties, count, "Stage");
  ASSERT_NE(fragment, nullptr);
  EXPECT_EQ(fragment->owner, RADICLE_QUEST_PROPERTY_FRAGMENT);
  EXPECT_EQ(fragment->alias, -1);
  ASSERT_NE(fragment->script, nullptr);
  EXPECT_STREQ(fragment->script, "");
  EXPECT_EQ(fragment->has_integer, 1);
  EXPECT_EQ(fragment->integer, 10);

  const auto* object = findProperty(properties, count, "Target");
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->owner, RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT);
  EXPECT_EQ(object->alias, 5);
  ASSERT_NE(object->script, nullptr);
  EXPECT_STREQ(object->script, "AliasScript");
  EXPECT_EQ(object->has_object, 1);
  EXPECT_EQ(object->object_form_id, 0x00012345u);
  EXPECT_EQ(object->has_property_alias, 1);
  EXPECT_EQ(object->property_alias, 5);

  radicle_free_quest_properties(properties, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, KeepsEmbeddedNulBytesInAPropertyString) {
  const TempPlugin file(fo4::test::make_quest_plugin(1, 7));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  const std::string expected("Garden\0Patch", 12);
  ASSERT_EQ(expected.size(), 12u);

  const auto* embedded = findProperty(properties, count, "Embedded");
  ASSERT_NE(embedded, nullptr);
  ASSERT_NE(embedded->text, nullptr);
  EXPECT_EQ(embedded->text_size, 12u);
  EXPECT_EQ(std::memcmp(embedded->text, expected.data(), expected.size()), 0);
  EXPECT_EQ(embedded->text[embedded->text_size], '\0');

  radicle_free_quest_properties(properties, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, EmptyStringPropertyDiffersFromNoString) {
  const TempPlugin file(fo4::test::make_quest_plugin(1, 7));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto* empty = findProperty(properties, count, "Empty");
  ASSERT_NE(empty, nullptr);
  ASSERT_NE(empty->text, nullptr);
  EXPECT_EQ(empty->text_size, 0u);
  EXPECT_EQ(std::string(empty->text), std::string());
  EXPECT_EQ(std::strlen(empty->text), 0u);

  const auto* integer = findProperty(properties, count, "Count");
  ASSERT_NE(integer, nullptr);
  EXPECT_EQ(integer->text, nullptr);
  EXPECT_EQ(integer->text_size, 0u);

  radicle_free_quest_properties(properties, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, FragmentPropertyCarriesNoScriptNameOfItsOwn) {
  const TempPlugin file(fo4::test::make_quest_plugin(1, 7));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto* fragment = findProperty(properties, count, "Stage");
  ASSERT_NE(fragment, nullptr);
  EXPECT_EQ(fragment->owner, RADICLE_QUEST_PROPERTY_FRAGMENT);
  EXPECT_EQ(fragment->alias, -1);
  ASSERT_NE(fragment->script, nullptr);
  EXPECT_STREQ(fragment->script, "");

  radicle_free_quest_properties(properties, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, IntegerPropertyValuesStayDistinct) {
  for (const std::int32_t expected : {7, 99}) {
    const TempPlugin file(fo4::test::make_quest_plugin(1, expected));

    radicle_plugin* plugin = nullptr;
    ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

    radicle_quest_property* properties = nullptr;
    size_t count = 0;
    ASSERT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, &count),
              RADICLE_STATUS_OK)
        << radicle_last_error();

    const auto* integer = findProperty(properties, count, "Count");
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->has_integer, 1);
    EXPECT_EQ(integer->integer, expected);

    radicle_free_quest_properties(properties, count);
    radicle_plugin_destroy(plugin);
  }
}

TEST(CApiQuest, MissingQuestHasNoProperties) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_quest_runtime_properties(plugin, "000900:Quests.esp", &properties, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(properties, nullptr);
  EXPECT_EQ(count, 0u);

  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, PropertyArgumentsAreRequired) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  radicle_quest_property* properties = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_quest_runtime_properties(nullptr, "000800:Quests.esp", &properties, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_properties(plugin, nullptr, &properties, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_properties(plugin, "000800:Quests.esp", &properties, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_properties(plugin, "not-a-form-key", &properties, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(properties, nullptr);
  EXPECT_EQ(count, 0u);

  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, StartGameFlagIsReportedPerQuest) {
  const TempPlugin file(fo4::test::make_quest_plugin(0));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_rows(plugin, "000800:Quests.esp", &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(hasRow(rows, count, "quest\t000800:Quests.esp\t0"));

  radicle_free_string_array(rows, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, MissingQuestIsAnUnsupportedRowNotAnError) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_quest_runtime_rows(plugin, "000900:Quests.esp", &rows, &count), RADICLE_STATUS_OK);

  bool unsupported = false;
  for (size_t index = 0; index < count; ++index) {
    if (std::strncmp(rows[index], "unsupported\t", 12) == 0) unsupported = true;
  }
  EXPECT_TRUE(unsupported);

  radicle_free_string_array(rows, count);
  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, NullArgumentsReturnInvalidArgument) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  char** rows = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_quest_runtime_rows(nullptr, "000800:Quests.esp", &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_rows(plugin, nullptr, &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_rows(plugin, "000800:Quests.esp", nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_quest_runtime_rows(plugin, "000800:Quests.esp", &rows, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, MalformedFormKeyReturnsInvalidArgument) {
  const TempPlugin file(fo4::test::make_quest_plugin(1));

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.path().string().c_str(), &plugin), RADICLE_STATUS_OK);

  char** rows = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_quest_runtime_rows(plugin, "not-a-form-key", &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  radicle_plugin_destroy(plugin);
}

TEST(CApiQuest, AbiVersionIsTwelve) {
  EXPECT_EQ(radicle_abi_version(), 12u);
  EXPECT_EQ(RADICLE_ABI_VERSION, 12u);
}
