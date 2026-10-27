#include <fo4/c_api.h>
#include <fo4/c_api_papyrus_source.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

radicle_status summarize_source(const std::string& source, std::vector<std::string>& rows) {
  char** raw = nullptr;
  size_t count = 0;
  const radicle_status status = radicle_papyrus_source_summary(
      reinterpret_cast<const uint8_t*>(source.data()), source.size(), &raw, &count);
  rows.clear();
  for (size_t index = 0; index < count; ++index) rows.emplace_back(raw[index]);
  radicle_free_string_array(raw, count);
  return status;
}

}  // namespace

TEST(CApiPapyrusSourceTest, ReportsTheFieldsAnEditorNeeds) {
  const std::string source =
      "Scriptname Summary:Probe extends ObjectReference\n"
      "Import Utility\n"
      "Int Property Count Auto\n"
      "Int Property Total AutoReadOnly\n"
      "Event Actor.OnPlayerLoadGame(Actor akSender)\n"
      "EndEvent\n"
      "Function Helper()\n"
      "EndFunction\n"
      "State Busy\n"
      "  Function Tick()\n"
      "  EndFunction\n"
      "EndState\n";
  std::vector<std::string> rows;
  ASSERT_EQ(summarize_source(source, rows), RADICLE_STATUS_OK);
  const std::vector<std::string> expected = {
      "script\tSummary:Probe\tObjectReference\t",
      "import\tUtility",
      "property\tCount\t1",
      "property\tTotal\t1",
      "state\tBusy",
      "function\tActor.OnPlayerLoadGame\t\t0\t1",
      "function\tHelper\t\t0\t0",
      "function\tTick\tBusy\t0\t0",
  };
  EXPECT_EQ(rows, expected);
}

TEST(CApiPapyrusSourceTest, ReportsAParseFailureAsARow) {
  std::vector<std::string> rows;
  ASSERT_EQ(summarize_source("Scriptname B\nInt a = 1 # 2\n", rows), RADICLE_STATUS_OK);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], "error\t2\t11\tunexpected character '#'");

  /* Must keep a message that quotes a decoded tab on one row with three separators. */
  std::vector<std::string> quoted;
  ASSERT_EQ(summarize_source("Scriptname B\nState \"a\tb\"\nEndState\n", quoted), RADICLE_STATUS_OK);
  ASSERT_EQ(quoted.size(), 1u);
  EXPECT_EQ(std::count(quoted[0].begin(), quoted[0].end(), '\t'), 3);
  EXPECT_EQ(quoted[0].find('\n'), std::string::npos);
  EXPECT_EQ(quoted[0].find('\r'), std::string::npos);
  EXPECT_NE(quoted[0].find("expected identifier but found \"a b\""), std::string::npos);
}

TEST(CApiPapyrusSourceTest, RefusesMissingEmptyAndOversizedSources) {
  char** raw = reinterpret_cast<char**>(1);
  size_t count = 9;
  EXPECT_EQ(radicle_papyrus_source_summary(nullptr, 4, &raw, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source is required");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  const std::string empty;
  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(empty.data()), 0, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source must not be empty");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  const std::string one = "Scriptname B\n";
  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(one.data()),
                                           RADICLE_PAPYRUS_SOURCE_MAX_BYTES + 1, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source size 4194305 is above the 4194304-byte cap");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(one.data()), one.size(), nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "out_rows and out_count are required");
}
