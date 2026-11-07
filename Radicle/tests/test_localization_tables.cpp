#include "fo4/localization/strings.hpp"

#include "fo4/schema/localized_fields.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

using fo4::localization::StringTableKind;

StringTableKind kind_of(std::string_view record, std::string_view subrecord) {
  return fo4::localization::string_table_kind(fo4::Signature::from_string(record),
                                              fo4::Signature::from_string(subrecord));
}

}  // namespace

TEST(LocalizationTables, DescIsADlStringEverywhereExceptLscr) {
  EXPECT_EQ(kind_of("ARMO", "DESC"), StringTableKind::dlstrings);
  EXPECT_EQ(kind_of("MESG", "DESC"), StringTableKind::dlstrings);
  EXPECT_EQ(kind_of("WEAP", "DESC"), StringTableKind::dlstrings);
  EXPECT_EQ(kind_of("LSCR", "DESC"), StringTableKind::strings);
}

TEST(LocalizationTables, QuestAndBookCnamAreDlStrings) {
  EXPECT_EQ(kind_of("QUST", "CNAM"), StringTableKind::dlstrings);
  EXPECT_EQ(kind_of("BOOK", "CNAM"), StringTableKind::dlstrings);
  EXPECT_EQ(kind_of("QUST", "FULL"), StringTableKind::strings);
  EXPECT_EQ(kind_of("QUST", "NNAM"), StringTableKind::strings);
  EXPECT_EQ(kind_of("BOOK", "FULL"), StringTableKind::strings);
  EXPECT_EQ(kind_of("BOOK", "DESC"), StringTableKind::dlstrings);
}

TEST(LocalizationTables, InfoRnamIsAStringAndEveryOtherInfoSubrecordIsAnIlString) {
  EXPECT_EQ(kind_of("INFO", "RNAM"), StringTableKind::strings);
  EXPECT_EQ(kind_of("INFO", "NAM1"), StringTableKind::ilstrings);
  EXPECT_EQ(kind_of("DIAL", "FULL"), StringTableKind::strings);
}

TEST(LocalizationTables, EverythingElseIsAString) {
  EXPECT_EQ(kind_of("ARMO", "FULL"), StringTableKind::strings);
  EXPECT_EQ(kind_of("NPC_", "SHRT"), StringTableKind::strings);
  EXPECT_EQ(kind_of("REFR", "XATO"), StringTableKind::strings);
}

TEST(LocalizationTables, EveryDeclaredFieldGetsTheKindXEditWouldPick) {
  const auto book = fo4::Signature::from_string("BOOK");
  const auto cnam = fo4::Signature::from_string("CNAM");
  const auto desc = fo4::Signature::from_string("DESC");
  const auto lscr = fo4::Signature::from_string("LSCR");
  const auto rnam = fo4::Signature::from_string("RNAM");
  std::size_t dlstrings = 0;
  std::size_t ilstrings = 0;
  for (const auto& entry : fo4::schema::k_generated_localized_fields) {
    auto expected = StringTableKind::strings;
    if ((entry.subrecord == desc && entry.record != lscr) ||
        (entry.subrecord == cnam && (entry.record == fo4::sig_qust || entry.record == book))) {
      expected = StringTableKind::dlstrings;
      ++dlstrings;
    } else if (entry.record == fo4::sig_info && entry.subrecord != rnam) {
      expected = StringTableKind::ilstrings;
      ++ilstrings;
    }
    EXPECT_EQ(fo4::localization::string_table_kind(entry.record, entry.subrecord), expected)
        << entry.record.to_string() << ' ' << entry.subrecord.to_string();
  }
  EXPECT_GT(dlstrings, 0u);
  EXPECT_GT(ilstrings, 0u);
}

TEST(LocalizationTables, ExtensionsAreTheUpperCaseXEditOnes) {
  EXPECT_EQ(fo4::localization::string_table_extension(StringTableKind::strings), ".STRINGS");
  EXPECT_EQ(fo4::localization::string_table_extension(StringTableKind::dlstrings), ".DLSTRINGS");
  EXPECT_EQ(fo4::localization::string_table_extension(StringTableKind::ilstrings), ".ILSTRINGS");
}

TEST(LocalizationTables, PathIsDataRelativeStringsFolderWithLanguageAndKind) {
  const auto base = std::filesystem::path("Strings");
  EXPECT_EQ(fo4::localization::string_table_path("Patch.esp", StringTableKind::strings, "English"),
            base / "Patch_English.STRINGS");
  EXPECT_EQ(fo4::localization::string_table_path("Patch.ESL", StringTableKind::ilstrings, "German"),
            base / "Patch_German.ILSTRINGS");
  EXPECT_EQ(
      fo4::localization::string_table_path("Patch.v2.esp", StringTableKind::dlstrings, "English"),
      base / "Patch.v2_English.DLSTRINGS");
  EXPECT_EQ(fo4::localization::string_table_path("NoExtension", StringTableKind::strings, "French"),
            base / "NoExtension_French.STRINGS");
}
