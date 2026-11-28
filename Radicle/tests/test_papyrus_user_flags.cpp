#include <fo4/papyrus/user_flags.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using fo4::papyrus::UserFlagTable;
using fo4::pex::PexUserFlag;

TEST(PapyrusUserFlagTable, BuiltInTableMatchesBitsEveryRealPexCarries) {
  const auto table = UserFlagTable::fallout4Default();

  const std::vector<PexUserFlag>& flags = table.flags();
  ASSERT_TRUE(flags.size() == 6);
  EXPECT_EQ(flags[0].name, "hidden");
  EXPECT_EQ(flags[0].index, 0);
  EXPECT_EQ(flags[1].name, "conditional");
  EXPECT_EQ(flags[1].index, 1);
  EXPECT_EQ(flags[2].name, "default");
  EXPECT_EQ(flags[2].index, 2);
  EXPECT_EQ(flags[3].name, "collapsedonref");
  EXPECT_EQ(flags[3].index, 3);
  EXPECT_EQ(flags[4].name, "collapsedonbase");
  EXPECT_EQ(flags[4].index, 4);
  EXPECT_EQ(flags[5].name, "mandatory");
  EXPECT_EQ(flags[5].index, 5);
}

TEST(PapyrusUserFlagTable, CompositeFlagExpandsToChildrenAndOwnsNoBit) {
  const auto table = UserFlagTable::parse(
      "Flag CollapsedOnRef 3 { Group }\nFlag CollapsedOnBase 4 { Group }\n"
      "Flag Collapsed CollapsedOnRef & CollapsedOnBase\n");

  EXPECT_EQ(table.maskFor("Collapsed"), ((1u << 3) | (1u << 4)));
  EXPECT_TRUE(table.flags().size() == 2);
}

TEST(PapyrusUserFlagTable, CommentsAndAllowedKindsListAreIgnored) {
  const auto table = UserFlagTable::parse("/* header\n   comment */\n// Flag NotReal 9\n"
                                          "Flag Mandatory 5\n{\n    Property\n}\n");

  EXPECT_EQ(table.maskFor("Mandatory"), 32u);
  EXPECT_FALSE(table.knows("NotReal"));
}

TEST(PapyrusUserFlagTable, UnknownNameContributesNothingSoKeywordsPassThroughHarmlessly) {
  const auto table = UserFlagTable::fallout4Default();
  const std::vector<std::string> written = {"auto", "const", "hidden"};
  EXPECT_EQ(table.maskFor(written), 1u);
}
