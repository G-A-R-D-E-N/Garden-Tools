#include "fo4/archive/assets.hpp"
#include "fo4/environment/strings.hpp"
#include "fo4/localization/strings.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace {

struct RealData {
  std::filesystem::path dir;
  std::vector<std::filesystem::path> archives;
};

std::optional<RealData> real_data() {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) return std::nullopt;
  record_game_build(detect_game_build(dir));
  RealData data{dir, {}};
  const std::vector<std::string> plugins{"Fallout4.esm"};
  data.archives = fo4::archive::archive_load_order(data.dir, {}, plugins);
  if (data.archives.empty()) return std::nullopt;
  return data;
}

bool starts_with(const std::vector<std::byte>& bytes, std::string_view prefix) {
  if (bytes.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::to_integer<char>(bytes[i]) != prefix[i]) return false;
  }
  return true;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string normalized(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return ch == '\\' ? '/' : static_cast<char>(std::tolower(ch));
  });
  return text;
}

std::optional<fo4::localization::StringTableKind> string_table_kind(const std::string& path) {
  const auto text = normalized(path);
  if (!text.starts_with("strings/")) return std::nullopt;
  if (ends_with(text, ".strings")) return fo4::localization::StringTableKind::strings;
  if (ends_with(text, ".dlstrings")) return fo4::localization::StringTableKind::dlstrings;
  if (ends_with(text, ".ilstrings")) return fo4::localization::StringTableKind::ilstrings;
  return std::nullopt;
}

std::string language_of(const std::string& path) {
  const auto stem = std::filesystem::path(path).stem().string();
  const auto underscore = stem.rfind('_');
  return underscore == std::string::npos ? "none" : stem.substr(underscore + 1);
}

}  // namespace

TEST(Ba2RealData, IndexesVanillaArchivesAndExtractsMeshesAndTextures) {
  const auto data = real_data();
  if (!data) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, data->archives);
  bool saw_mesh = false;
  bool saw_texture = false;
  for (const auto& reader : assets.archives()) {
    EXPECT_TRUE(reader.has_name_table()) << reader.path();
    EXPECT_FALSE(reader.entries().empty()) << reader.path();
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!saw_mesh && reader.kind() == fo4::archive::Ba2Kind::general && ends_with(path, ".nif")) {
        EXPECT_TRUE(starts_with(reader.extract(i), "Gamebryo File Format")) << path;
        saw_mesh = true;
      }
      if (!saw_texture && reader.kind() == fo4::archive::Ba2Kind::texture) {
        EXPECT_TRUE(starts_with(reader.extract(i), "DDS ")) << path;
        saw_texture = true;
      }
    }
  }
  EXPECT_TRUE(saw_mesh);
  EXPECT_TRUE(saw_texture);
}

TEST(Ba2RealData, ResolvesVanillaWorldspaceNameFromArchivedStrings) {
  const auto data = real_data();
  if (!data) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const std::vector<std::filesystem::path> plugins{data->dir / "Fallout4.esm"};
  if (!std::filesystem::exists(plugins[0])) GTEST_SKIP() << "Fallout4.esm is not present";
  const auto assets = fo4::archive::AssetResolver::open({}, data->archives);
  const auto location = assets.locate_in_archives("Strings/Fallout4_en.STRINGS");
  ASSERT_TRUE(location.has_value());

  const auto env = fo4::env::Environment::load(plugins);
  const auto tables = fo4::env::StringTables::load(env, data->dir / "Strings", "en", &assets);
  const auto* commonwealth = env.winner({fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{0x3C}});
  ASSERT_NE(commonwealth, nullptr);
  EXPECT_EQ(tables.resolve_name(env, *commonwealth), std::optional<std::string_view>{"Commonwealth"});
}

TEST(Ba2RealData, ParsesEveryVanillaStringTableInMastersAndCcArchives) {
  const auto data = real_data();
  if (!data) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto archives = fo4::archive::archive_load_order(data->dir, {}, installed_master_and_cc_plugins(data->dir));
  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::string, std::size_t> by_extension;
  std::map<std::string, std::size_t> by_language;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      const auto kind = string_table_kind(path);
      if (!kind) continue;
      try {
        fo4::localization::read_string_table(reader.extract(i), *kind);
        ++by_extension[normalized(std::filesystem::path(path).extension().string())];
        ++by_language[language_of(path)];
      } catch (const fo4::localization::StringTableError& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  EXPECT_TRUE(by_extension.contains(".strings"));
  for (const auto& [extension, count] : by_extension) RecordProperty(extension + "_file_count", std::to_string(count));
  for (const auto& [language, count] : by_language) {
    RecordProperty("language_" + language + "_file_count", std::to_string(count));
  }
}

TEST(Ba2RealData, ExtractsEveryVanillaEntry) {
  const auto data = real_data();
  if (!data || std::getenv("FO4_BA2_EXTRACT_ALL") == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR and FO4_BA2_EXTRACT_ALL=1 to extract every vanilla BA2 entry";
  }
  const auto assets = fo4::archive::AssetResolver::open({}, data->archives);
  std::size_t extracted = 0;
  for (const auto& reader : assets.archives()) {
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto bytes = reader.extract(i);
      if (reader.kind() == fo4::archive::Ba2Kind::general) {
        EXPECT_EQ(bytes.size(), reader.entries()[i].unpacked_size()) << reader.entries()[i].path;
      }
      ++extracted;
    }
  }
  EXPECT_GT(extracted, 0u);
}
