#include "fo4/c_api.h"
#include "fo4/c_api_quest.h"

#include "quest_plugin_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
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
    root_ = std::filesystem::temp_directory_path() / "radicle_c_api_quest_test";
    std::filesystem::remove_all(root_, error);
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

}  // namespace

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
  EXPECT_EQ(count, 9u);

  EXPECT_TRUE(hasRow(rows, count, "quest\t000800:Quests.esp\t1"));
  EXPECT_TRUE(hasRow(rows, count, "script\tQuestScript"));
  EXPECT_TRUE(hasRow(rows, count, "script-property\tQuestScript\tCount\t3"));
  EXPECT_TRUE(hasRow(rows, count, "fragment-script\tQF_TestQuest"));
  EXPECT_TRUE(hasRow(rows, count, "fragment-property\tStage\t3"));
  EXPECT_TRUE(hasRow(rows, count, "fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010"));
  EXPECT_TRUE(hasRow(rows, count, "alias\t5"));
  EXPECT_TRUE(hasRow(rows, count, "alias-script\t5\tAliasScript"));
  EXPECT_TRUE(hasRow(rows, count, "alias-script-property\t5\tAliasScript\tTarget\t1"));

  radicle_free_string_array(rows, count);
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

TEST(CApiQuest, AbiVersionIsSix) {
  EXPECT_EQ(radicle_abi_version(), 6u);
  EXPECT_EQ(RADICLE_ABI_VERSION, 6u);
}
