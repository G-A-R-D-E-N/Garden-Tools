#include "fo4/loadorder/load_order_files.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using fo4::loadorder::AssembledLoadOrder;
using fo4::loadorder::LoadOrderEntry;
using fo4::loadorder::OfficialTable;
using fo4::loadorder::PluginList;
using fo4::test::make_tes4_plugin;
using fo4::test::TempDataDir;

constexpr std::uint32_t k_master = 0x1;
constexpr std::uint32_t k_light = 0x200;
constexpr std::size_t k_record_header_size = 24;

std::vector<std::string> names_of(const AssembledLoadOrder& order) {
  std::vector<std::string> names;
  for (const auto& entry : order.entries) names.push_back(entry.name);
  return names;
}

const LoadOrderEntry* find_entry(const AssembledLoadOrder& order, const std::string& name) {
  const auto found = std::find_if(order.entries.begin(), order.entries.end(),
                                  [&](const LoadOrderEntry& entry) { return entry.name == name; });
  return found == order.entries.end() ? nullptr : &*found;
}

bool has_finding(const AssembledLoadOrder& order, const std::string& code, const std::string& plugin) {
  return std::any_of(order.findings.begin(), order.findings.end(), [&](const auto& finding) {
    return finding.code == code && finding.plugin == plugin;
  });
}

bool has_finding_detail(const AssembledLoadOrder& order, const std::string& code, const std::string& text) {
  return std::any_of(order.findings.begin(), order.findings.end(), [&](const auto& finding) {
    return finding.code == code && finding.detail.find(text) != std::string::npos;
  });
}

std::vector<std::byte> with_tes4_data_size(std::uint32_t size) {
  auto bytes = make_tes4_plugin(0, 1.0f, {});
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[4 + i] = std::byte{static_cast<std::uint8_t>(size >> (8 * i))};
  }
  return bytes;
}

}  // namespace

TEST(LoadOrderFiles, ParsesPluginListRulesInFileOrder) {
  const auto list = fo4::loadorder::parse_plugin_list(
      "# comment\r\n"
      "*Fallout4.esm\r\n"
      "x\r\n"
      "Mod.esp\n"
      "*Mod.esp\n"
      "\n"
      "*Other.esp\r\n");
  ASSERT_EQ(list.entries.size(), 4u);
  EXPECT_EQ(list.entries[0].name, "Fallout4.esm");
  EXPECT_TRUE(list.entries[0].active);
  EXPECT_EQ(list.entries[0].line, 2u);
  EXPECT_EQ(list.entries[1].name, "x");
  EXPECT_FALSE(list.entries[1].active);
  EXPECT_EQ(list.entries[1].line, 3u);
  EXPECT_EQ(list.entries[2].name, "Mod.esp");
  EXPECT_TRUE(list.entries[2].active);
  EXPECT_EQ(list.entries[2].line, 4u);
  EXPECT_EQ(list.entries[3].name, "Other.esp");
  EXPECT_TRUE(list.entries[3].active);
  EXPECT_EQ(list.entries[3].line, 7u);
  ASSERT_EQ(list.duplicates.size(), 1u);
  EXPECT_EQ(list.duplicates[0].name, "Mod.esp");
  EXPECT_TRUE(list.duplicates[0].active);
  EXPECT_EQ(list.duplicates[0].line, 5u);
}

TEST(LoadOrderFiles, StarredDuplicateActivatesFirstEntryAndIsRecorded) {
  const auto list = fo4::loadorder::parse_plugin_list("Mod.esp\n*MOD.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "Mod.esp");
  EXPECT_TRUE(list.entries[0].active);
  EXPECT_EQ(list.entries[0].line, 1u);
  ASSERT_EQ(list.duplicates.size(), 1u);
  EXPECT_EQ(list.duplicates[0].name, "MOD.esp");
  EXPECT_TRUE(list.duplicates[0].active);
  EXPECT_EQ(list.duplicates[0].line, 2u);
}

TEST(LoadOrderFiles, MixedCaseDuplicateKeepsActiveFirstEntry) {
  const auto list = fo4::loadorder::parse_plugin_list("*MOD.esp\nMod.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "MOD.esp");
  EXPECT_TRUE(list.entries[0].active);
  ASSERT_EQ(list.duplicates.size(), 1u);
  EXPECT_EQ(list.duplicates[0].name, "Mod.esp");
  EXPECT_EQ(list.duplicates[0].line, 2u);
}

TEST(LoadOrderFiles, NamesWithControlCharactersAreSkippedAndReported) {
  TempDataDir data;
  data.write("Good.esp", make_tes4_plugin(0, 1.0f, {}), 1);

  const auto list = fo4::loadorder::parse_plugin_list("*Good.esp\n*Bad\tName.esp\nBad\x01.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "Good.esp");
  EXPECT_EQ(list.invalid_lines, (std::vector<std::size_t>{2, 3}));

  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Good.esp"}));
  EXPECT_TRUE(has_finding_detail(order, "invalid_name", "line 2"));
  EXPECT_TRUE(has_finding_detail(order, "invalid_name", "line 3"));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, ParsesPluginListWithoutBom) {
  const auto list = fo4::loadorder::parse_plugin_list("A.esm\n*B.esp");
  ASSERT_EQ(list.entries.size(), 2u);
  EXPECT_EQ(list.entries[0].name, "A.esm");
  EXPECT_EQ(list.entries[1].name, "B.esp");
  EXPECT_EQ(list.entries[1].line, 2u);
  EXPECT_TRUE(list.duplicates.empty());
}

TEST(LoadOrderFiles, Utf8BomStaysOnLineOneAndBlocksStarAndComment) {
  const auto starred = fo4::loadorder::parse_plugin_list("\xEF\xBB\xBF" "*Fallout4.esm\n");
  ASSERT_EQ(starred.entries.size(), 1u);
  EXPECT_EQ(starred.entries[0].name, "\xC3\xAF\xC2\xBB\xC2\xBF" "*Fallout4.esm");
  EXPECT_FALSE(starred.entries[0].active);

  const auto comment = fo4::loadorder::parse_plugin_list("\xEF\xBB\xBF" "# x\n");
  ASSERT_EQ(comment.entries.size(), 1u);
  EXPECT_EQ(comment.entries[0].name, "\xC3\xAF\xC2\xBB\xC2\xBF" "# x");
}

TEST(LoadOrderFiles, TrailingSpaceIsKeptAndTabMakesAnInvalidLine) {
  const auto list = fo4::loadorder::parse_plugin_list("Mod.esp \n*Other.esp\t\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "Mod.esp ");
  EXPECT_EQ(list.invalid_lines, (std::vector<std::size_t>{2}));
}

TEST(LoadOrderFiles, HashAndStarCountOnlyAtByteZero) {
  const auto list = fo4::loadorder::parse_plugin_list("A.esp # note\n*#x\n");
  ASSERT_EQ(list.entries.size(), 2u);
  EXPECT_EQ(list.entries[0].name, "A.esp # note");
  EXPECT_FALSE(list.entries[0].active);
  EXPECT_EQ(list.entries[1].name, "#x");
  EXPECT_TRUE(list.entries[1].active);
}

TEST(LoadOrderFiles, OneByteChunksAreSkippedUnlessTheyEndInLf) {
  const auto single = fo4::loadorder::parse_plugin_list("A\n");
  ASSERT_EQ(single.entries.size(), 1u);
  EXPECT_EQ(single.entries[0].name, "A");

  const auto unterminated = fo4::loadorder::parse_plugin_list("A\nB");
  ASSERT_EQ(unterminated.entries.size(), 1u);
  EXPECT_EQ(unterminated.entries[0].name, "A");

  const auto star_only = fo4::loadorder::parse_plugin_list("*\n");
  ASSERT_EQ(star_only.entries.size(), 1u);
  EXPECT_TRUE(star_only.entries[0].name.empty());
  EXPECT_TRUE(star_only.entries[0].active);
}

TEST(LoadOrderFiles, LongLineIsReadInFgetsChunks) {
  const auto list = fo4::loadorder::parse_plugin_list(std::string(600, 'a') + "\n");
  ASSERT_EQ(list.entries.size(), 2u);
  EXPECT_EQ(list.entries[0].name.size(), 511u);
  EXPECT_EQ(list.entries[1].name.size(), 89u);
  EXPECT_EQ(list.entries[1].line, 1u);
}

TEST(LoadOrderFiles, FullChunkWithLfGivesOneName) {
  const auto list = fo4::loadorder::parse_plugin_list(std::string(511, 'a') + "\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, std::string(511, 'a'));
}

TEST(LoadOrderFiles, OverlongChunkThenOneByteNameWithLf) {
  const auto list = fo4::loadorder::parse_plugin_list(std::string(512, 'a') + "\n");
  ASSERT_EQ(list.entries.size(), 2u);
  EXPECT_EQ(list.entries[0].name.size(), 511u);
  EXPECT_EQ(list.entries[1].name, "a");
  EXPECT_EQ(list.entries[1].line, 1u);
}

TEST(LoadOrderFiles, Windows1252HighByteDecodesToUtf8) {
  const auto list = fo4::loadorder::parse_plugin_list("Caf\xE9.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "Caf\xC3\xA9.esp");
}

TEST(LoadOrderFiles, UndefinedWindows1252ByteDecodesToC1Code) {
  const auto list = fo4::loadorder::parse_plugin_list("Bad\x81" ".esp\nGood.esp\n");
  ASSERT_EQ(list.entries.size(), 2u);
  EXPECT_EQ(list.entries[0].name, "Bad\xC2\x81" ".esp");
  EXPECT_EQ(list.entries[1].name, "Good.esp");
  EXPECT_EQ(list.entries[1].line, 2u);
  EXPECT_TRUE(list.invalid_lines.empty());
}

TEST(LoadOrderFiles, LoneCrInsideALineMakesAnInvalidLine) {
  const auto list = fo4::loadorder::parse_plugin_list("A\rB.esp\nGood.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "Good.esp");
  EXPECT_EQ(list.invalid_lines, (std::vector<std::size_t>{1}));
}

TEST(LoadOrderFiles, CtrlZEndsThePluginListText) {
  const auto list = fo4::loadorder::parse_plugin_list("A.esp\n\x1A" "B.esp\n");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "A.esp");
  EXPECT_TRUE(list.invalid_lines.empty());
}

TEST(LoadOrderFiles, BomOnlyFileIsOneUnstarredEntry) {
  const auto list = fo4::loadorder::parse_plugin_list("\xEF\xBB\xBF");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries[0].name, "\xC3\xAF\xC2\xBB\xC2\xBF");
  EXPECT_FALSE(list.entries[0].active);
  EXPECT_EQ(list.entries[0].line, 1u);
}

TEST(LoadOrderFiles, CccSplitsOnCarriageReturn) {
  const auto names = fo4::loadorder::parse_ccc("a\rb");
  EXPECT_EQ(names, (std::vector<std::string>{"a", "b"}));
}

TEST(LoadOrderFiles, CccKeepsSpacesAndSkipsEmptyTokens) {
  const auto names = fo4::loadorder::parse_ccc("  cc_b.esl \r\n\r\n\tcc_a.esl\r\n   \ncc_c.esl\t \n");
  EXPECT_EQ(names, (std::vector<std::string>{"  cc_b.esl ", "   "}));
}

TEST(LoadOrderFiles, CccDropsTokensWithAControlCharacter) {
  const auto names = fo4::loadorder::parse_ccc("cc_a.esl\tx\ncc_b.esl\n");
  EXPECT_EQ(names, (std::vector<std::string>{"cc_b.esl"}));
}

TEST(LoadOrderFiles, CccBomStaysOnFirstNameAndUndefinedByteDecodesToC1) {
  const auto names = fo4::loadorder::parse_ccc("\xEF\xBB\xBF" "cc_a.esl\nBad\x81\ncc_b.esl\n");
  EXPECT_EQ(names, (std::vector<std::string>{"\xC3\xAF\xC2\xBB\xC2\xBF" "cc_a.esl", "Bad\xC2\x81", "cc_b.esl"}));
}

TEST(LoadOrderFiles, OfficialNamesFollowTheSelectedTable) {
  TempDataDir data;
  data.write("Fallout4.esm", make_tes4_plugin(k_master, 1.0f, {}), 2);
  data.write("Mod.esm", make_tes4_plugin(k_master, 1.0f, {}), 1);
  data.write("DLCDummy10thAnniversary.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 3);
  data.write("DLCRobot.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 10);
  data.write("DLCworkshop03.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 11);
  data.write("DLCUltraHighResolution.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 12);

  const auto og = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(og),
            (std::vector<std::string>{"Fallout4.esm", "DLCRobot.esm", "DLCworkshop03.esm",
                                      "DLCUltraHighResolution.esm", "Mod.esm", "DLCDummy10thAnniversary.esm"}));

  const auto ae = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::ae);
  EXPECT_EQ(names_of(ae),
            (std::vector<std::string>{"Fallout4.esm", "DLCRobot.esm", "DLCworkshop03.esm",
                                      "DLCUltraHighResolution.esm", "DLCDummy10thAnniversary.esm", "Mod.esm"}));
  EXPECT_FALSE(ae.failed);
}

TEST(LoadOrderFiles, CccNamesFollowOfficialNames) {
  TempDataDir data;
  data.write("Fallout4.esm", make_tes4_plugin(k_master, 1.0f, {}), 1);
  data.write("Early.esm", make_tes4_plugin(k_master, 1.0f, {}), 0);
  data.write("DLCRobot.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 2);
  data.write("cc_a.esl", make_tes4_plugin(0, 1.0f, {}), 3);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, std::vector<std::string>{"cc_a.esl"},
                                                         OfficialTable::og_ng);
  EXPECT_EQ(names_of(order),
            (std::vector<std::string>{"Fallout4.esm", "DLCRobot.esm", "cc_a.esl", "Early.esm"}));
  ASSERT_NE(find_entry(order, "cc_a.esl"), nullptr);
  EXPECT_TRUE(find_entry(order, "cc_a.esl")->checked);
  EXPECT_TRUE(find_entry(order, "cc_a.esl")->light);
}

TEST(LoadOrderFiles, PluginListMasterFlagFilesPrecedeNonMastersInListOrder) {
  TempDataDir data;
  data.write("Beta.esp", make_tes4_plugin(0, 1.0f, {}), 0);
  data.write("Zeta.esm", make_tes4_plugin(k_master, 1.0f, {}), 5);
  data.write("Alpha.esm", make_tes4_plugin(k_master, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Beta.esp\nZeta.esm\nAlpha.esm\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Zeta.esm", "Alpha.esm", "Beta.esp"}));
}

TEST(LoadOrderFiles, PluginListNonMastersPrecedeRemainingNonMastersInListOrder) {
  TempDataDir data;
  data.write("Extra.esp", make_tes4_plugin(0, 1.0f, {}), 0);
  data.write("NonA.esp", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("NonB.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("NonB.esp\nNonA.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"NonB.esp", "NonA.esp", "Extra.esp"}));
}

TEST(LoadOrderFiles, PluginListEntryWithMissingMasterIsNotOrderedAsListed) {
  TempDataDir data;
  data.write("Ok.esp", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("Bad.esp", make_tes4_plugin(0, 1.0f, {"Missing.esm"}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Bad.esp\nOk.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Ok.esp", "Bad.esp"}));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, EsmDependentListedBeforeItsMasterIsHoisted) {
  TempDataDir data;
  data.write("Dep.esm", make_tes4_plugin(k_master, 1.0f, {"Base.esm"}), 1);
  data.write("Base.esm", make_tes4_plugin(k_master, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Dep.esm\nBase.esm\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Base.esm", "Dep.esm"}));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, EsmHoistsEspMasterBeforeItsDependent) {
  TempDataDir data;
  data.write("Dep.esm", make_tes4_plugin(k_master, 1.0f, {"Base.esp"}), 1);
  data.write("Base.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Dep.esm\nBase.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Base.esp", "Dep.esm"}));
}

TEST(LoadOrderFiles, EspDependentListedBeforeItsMasterIsNotReordered) {
  TempDataDir data;
  data.write("Dep.esp", make_tes4_plugin(0, 1.0f, {"Base.esp"}), 1);
  data.write("Base.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Dep.esp\nBase.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Dep.esp", "Base.esp"}));
}

TEST(LoadOrderFiles, EspWithLightFlagOrdersWithEspFiles) {
  TempDataDir data;
  data.write("Mod.esm", make_tes4_plugin(k_master, 1.0f, {}), 3);
  data.write("Lite.esp", make_tes4_plugin(k_light, 1.0f, {}), 1);
  data.write("Full.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esm", "Lite.esp", "Full.esp"}));
  const auto* lite_entry = find_entry(order, "Lite.esp");
  ASSERT_NE(lite_entry, nullptr);
  EXPECT_TRUE(lite_entry->light);
  EXPECT_FALSE(lite_entry->master_flag);
}

TEST(LoadOrderFiles, EslWithoutLightFlagOrdersWithMasters) {
  TempDataDir data;
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("Small.esl", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Small.esl", "Mod.esp"}));
  const auto* esl_entry = find_entry(order, "Small.esl");
  ASSERT_NE(esl_entry, nullptr);
  EXPECT_TRUE(esl_entry->master_flag);
  EXPECT_TRUE(esl_entry->light);
}

TEST(LoadOrderFiles, TransitiveMasterChainIsHoistedInDependencyOrder) {
  TempDataDir data;
  data.write("A.esm", make_tes4_plugin(k_master, 1.0f, {"B.esm"}), 1);
  data.write("B.esm", make_tes4_plugin(k_master, 1.0f, {"C.esm"}), 2);
  data.write("C.esm", make_tes4_plugin(k_master, 1.0f, {}), 3);

  const auto list = fo4::loadorder::parse_plugin_list("A.esm\nB.esm\nC.esm\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"C.esm", "B.esm", "A.esm"}));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, TwoMastersOfOneDependentEndInReverseMastOrder) {
  TempDataDir data;
  data.write("Dep.esm", make_tes4_plugin(k_master, 1.0f, {"M1.esm", "M2.esm"}), 1);
  data.write("M1.esm", make_tes4_plugin(k_master, 1.0f, {}), 2);
  data.write("M2.esm", make_tes4_plugin(k_master, 1.0f, {}), 3);

  const auto list = fo4::loadorder::parse_plugin_list("Dep.esm\nM1.esm\nM2.esm\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"M2.esm", "M1.esm", "Dep.esm"}));
}

TEST(LoadOrderFiles, EqualTimestampsBreakTiesByCaseInsensitiveName) {
  TempDataDir data;
  data.write("beta.esp", make_tes4_plugin(0, 1.0f, {}), 5);
  data.write("Alpha.esp", make_tes4_plugin(0, 1.0f, {}), 5);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Alpha.esp", "beta.esp"}));
}

TEST(LoadOrderFiles, TruncatedTes4HeaderIsReportedAndDropped) {
  TempDataDir data;
  data.write("Short.esp", std::vector<std::byte>(10, std::byte{0x54}), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "unreadable_header", "Short.esp"));
}

TEST(LoadOrderFiles, ActivePluginWithMissingMasterIsNeverChecked) {
  TempDataDir data;
  data.write("Orphan.esp", make_tes4_plugin(0, 1.0f, {"Missing.esm"}), 1);

  const auto list = fo4::loadorder::parse_plugin_list("*Orphan.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  ASSERT_NE(find_entry(order, "Orphan.esp"), nullptr);
  EXPECT_FALSE(find_entry(order, "Orphan.esp")->checked);
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, InactiveMasterOfActivePluginBecomesChecked) {
  TempDataDir data;
  data.write("Base.esp", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("Child.esp", make_tes4_plugin(0, 1.0f, {"Base.esp"}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Base.esp\n*Child.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  ASSERT_NE(find_entry(order, "Base.esp"), nullptr);
  EXPECT_TRUE(find_entry(order, "Base.esp")->checked);
  EXPECT_TRUE(find_entry(order, "Child.esp")->checked);
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, GrandmasterOfActivePluginBecomesCheckedTransitively) {
  TempDataDir data;
  data.write("Child.esp", make_tes4_plugin(0, 1.0f, {"Parent.esp"}), 1);
  data.write("Parent.esp", make_tes4_plugin(0, 1.0f, {"Grand.esp"}), 2);
  data.write("Grand.esp", make_tes4_plugin(0, 1.0f, {}), 3);

  const auto list = fo4::loadorder::parse_plugin_list("*Child.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  ASSERT_NE(find_entry(order, "Grand.esp"), nullptr);
  EXPECT_TRUE(find_entry(order, "Parent.esp")->checked);
  EXPECT_TRUE(find_entry(order, "Grand.esp")->checked);
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, VersionOneIsCheckedAndLargerVersionIsSilentlyUnchecked) {
  TempDataDir data;
  data.write("Ok.esp", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("Bad.esp", make_tes4_plugin(0, 1.5f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("*Ok.esp\n*Bad.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_TRUE(find_entry(order, "Ok.esp")->checked);
  EXPECT_FALSE(find_entry(order, "Bad.esp")->checked);
  EXPECT_TRUE(has_finding(order, "version_unchecked", "Bad.esp"));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, CheckedMasterAboveVersionOneFailsAssembly) {
  TempDataDir data;
  data.write("Old.esp", make_tes4_plugin(0, 1.5f, {}), 1);
  data.write("Child.esp", make_tes4_plugin(0, 1.0f, {"Old.esp"}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("*Child.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_TRUE(order.failed);
  EXPECT_TRUE(has_finding(order, "master_too_new", "Child.esp"));
}

TEST(LoadOrderFiles, CheckedPluginWithMissingMasterFailsAssembly) {
  TempDataDir data;
  data.write("DLCRobot.esm", make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"}), 1);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_TRUE(order.failed);
  EXPECT_TRUE(has_finding(order, "missing_master", "DLCRobot.esm"));
}

TEST(LoadOrderFiles, ZeroByteFilesAreIgnoredAndListedAsMissing) {
  TempDataDir data;
  data.write("Empty.esp", {}, 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto list = fo4::loadorder::parse_plugin_list("Empty.esp\nMod.esp\nMod.esp\n");
  const auto order = fo4::loadorder::assemble_load_order(data.path(), list, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "missing_file", "Empty.esp"));
  EXPECT_TRUE(has_finding(order, "duplicate_entry", "Mod.esp"));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, UnreadableHeaderIsReportedAndDropped) {
  TempDataDir data;
  data.write("Junk.esp", std::vector<std::byte>(40, std::byte{0x41}), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "unreadable_header", "Junk.esp"));
}

TEST(LoadOrderFiles, Tes4DataSizeAboveSixteenMiBIsReportedAndDropped) {
  TempDataDir data;
  data.write("Huge.esp", with_tes4_data_size(16u * 1024u * 1024u + 1), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "unreadable_header", "Huge.esp"));
  EXPECT_TRUE(has_finding_detail(order, "unreadable_header", "exceeds 16 MiB"));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, Tes4DataSizePastEndOfFileIsReportedAndDropped) {
  TempDataDir data;
  const auto remaining = static_cast<std::uint32_t>(make_tes4_plugin(0, 1.0f, {}).size() - k_record_header_size);
  data.write("Short.esp", with_tes4_data_size(remaining + 1), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "unreadable_header", "Short.esp"));
  EXPECT_TRUE(has_finding_detail(order, "unreadable_header", "overruns the file"));
  EXPECT_FALSE(order.failed);
}

TEST(LoadOrderFiles, Tes4DataSizeExactlySixteenMiBIsNotTreatedAsTooLarge) {
  TempDataDir data;
  data.write("Edge.esp", with_tes4_data_size(16u * 1024u * 1024u), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(has_finding(order, "unreadable_header", "Edge.esp"));
  EXPECT_TRUE(has_finding_detail(order, "unreadable_header", "overruns the file"));
  EXPECT_FALSE(has_finding_detail(order, "unreadable_header", "exceeds"));
}

TEST(LoadOrderFiles, MutualMasterCycleIsReportedInsteadOfLooping) {
  TempDataDir data;
  data.write("A.esm", make_tes4_plugin(k_master, 1.0f, {"B.esm"}), 1);
  data.write("B.esm", make_tes4_plugin(k_master, 1.0f, {"A.esm"}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_TRUE(order.failed);
  EXPECT_TRUE(has_finding_detail(order, "hoist_cycle", "A.esm"));
  EXPECT_TRUE(has_finding_detail(order, "hoist_cycle", "B.esm"));
}

TEST(LoadOrderFiles, SelfMasterEsmIsReportedAsCycle) {
  TempDataDir data;
  data.write("Self.esm", make_tes4_plugin(k_master, 1.0f, {"Self.esm"}), 1);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_TRUE(order.failed);
  EXPECT_TRUE(has_finding(order, "hoist_cycle", "Self.esm"));
}

TEST(LoadOrderFiles, Fallout4EsmIsCheckedWhenPresent) {
  TempDataDir data;
  data.write("Fallout4.esm", make_tes4_plugin(k_master, 1.0f, {}), 1);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 2);

  const auto order = fo4::loadorder::assemble_load_order(data.path(), PluginList{}, {}, OfficialTable::og_ng);
  ASSERT_NE(find_entry(order, "Fallout4.esm"), nullptr);
  EXPECT_TRUE(find_entry(order, "Fallout4.esm")->checked);
  EXPECT_FALSE(find_entry(order, "Mod.esp")->checked);
}

TEST(LoadOrderFiles, ResolvedFilesFromSeveralFoldersAssembleLikeOneDataFolder) {
  TempDataDir whole;
  TempDataDir first;
  TempDataDir second;
  const auto master = make_tes4_plugin(k_master, 1.0f, {});
  const auto dependent = make_tes4_plugin(k_master, 1.0f, {"Fallout4.esm"});
  auto plugin = make_tes4_plugin(0, 1.0f, {});
  whole.write("Fallout4.esm", master, 1);
  whole.write("DLCRobot.esm", dependent, 2);
  whole.write("Late.esp", plugin, 3);
  whole.write("Early.esp", plugin, 0);
  first.write("Fallout4.esm", master, 1);
  first.write("Late.esp", plugin, 3);
  second.write("DLCRobot.esm", dependent, 2);
  second.write("Early.esp", plugin, 0);

  const auto list = fo4::loadorder::parse_plugin_list("Late.esp\nEarly.esp\n");
  const std::vector<std::string> expected{"Fallout4.esm", "DLCRobot.esm", "Late.esp", "Early.esp"};
  EXPECT_EQ(names_of(fo4::loadorder::assemble_load_order(whole.path(), list, {}, OfficialTable::og_ng)), expected);

  const std::vector<std::filesystem::path> files{first.path() / "Fallout4.esm", first.path() / "Late.esp",
                                                 second.path() / "DLCRobot.esm", second.path() / "Early.esp"};
  EXPECT_EQ(names_of(fo4::loadorder::assemble_load_order(files, list, {}, OfficialTable::og_ng)), expected);
}

TEST(LoadOrderFiles, ResolvedFilesKeepTheFirstPathForADuplicateName) {
  TempDataDir first;
  TempDataDir second;
  first.write("Mod.esp", make_tes4_plugin(k_master, 1.0f, {}), 1);
  second.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 1);

  const std::vector<std::filesystem::path> files{first.path() / "Mod.esp", second.path() / "Mod.esp"};
  const auto order = fo4::loadorder::assemble_load_order(files, PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  ASSERT_NE(find_entry(order, "Mod.esp"), nullptr);
  EXPECT_TRUE(find_entry(order, "Mod.esp")->master_flag);

  const std::vector<std::filesystem::path> reversed{second.path() / "Mod.esp", first.path() / "Mod.esp"};
  const auto reversed_order = fo4::loadorder::assemble_load_order(reversed, PluginList{}, {}, OfficialTable::og_ng);
  ASSERT_NE(find_entry(reversed_order, "Mod.esp"), nullptr);
  EXPECT_FALSE(find_entry(reversed_order, "Mod.esp")->master_flag);
}

TEST(LoadOrderFiles, ResolvedFilesSkipNonPluginAndZeroByteFiles) {
  TempDataDir data;
  data.write("Readme.txt", make_tes4_plugin(0, 1.0f, {}), 1);
  data.write("Empty.esp", {}, 2);
  data.write("Mod.esp", make_tes4_plugin(0, 1.0f, {}), 3);
  std::filesystem::create_directories(data.path() / "Folder.esp");

  const std::vector<std::filesystem::path> files{data.path() / "Readme.txt", data.path() / "Empty.esp",
                                                 data.path() / "Folder.esp", data.path() / "Mod.esp"};
  const auto order = fo4::loadorder::assemble_load_order(files, PluginList{}, {}, OfficialTable::og_ng);
  EXPECT_EQ(names_of(order), (std::vector<std::string>{"Mod.esp"}));
  EXPECT_TRUE(order.findings.empty());
}
