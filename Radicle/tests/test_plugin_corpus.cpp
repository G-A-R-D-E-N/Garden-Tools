// Sweeps every decoder over a directory of third-party plugins.
//
// The shipped masters are one author using one toolchain. A mod corpus is
// many authors using the CK, xEdit and various libraries, so it exercises
// shapes Bethesda never emits -- which is exactly where the remaining decoder
// assumptions break.
//
// Point FO4_PLUGIN_CORPUS at a directory; it recurses. Skips without it.

#include "fo4/forms/armor.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/quest.hpp"
#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "hedr_count.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

// Collapse the variable parts of a message so distinct causes group together.
std::string normalize(std::string message) {
  std::string out;
  bool in_number = false;
  for (const char ch : message) {
    if (std::isdigit(static_cast<unsigned char>(ch)) ||
        (in_number && (ch == 'x' || std::isxdigit(static_cast<unsigned char>(ch))))) {
      if (!in_number) {
        out += '#';
        in_number = true;
      }
      continue;
    }
    in_number = false;
    out += ch;
  }
  return out;
}

struct Tally {
  std::size_t plugins{0};
  std::size_t records{0};
  std::map<std::string, std::size_t> failures;
  std::map<std::string, std::string> first_example;

  void fail(const std::string& message, const std::string& where) {
    const auto key = normalize(message);
    if (!failures.count(key)) first_example[key] = where + ": " + message;
    failures[key]++;
  }
};

template <class Fn>
void try_decode(Tally& tally, const std::string& where, Fn&& fn) {
  ++tally.records;
  try {
    fn();
  } catch (const std::exception& error) {
    tally.fail(error.what(), where);
  }
}

}  // namespace

TEST(PluginCorpus, EveryDecoderSurvivesThirdPartyPlugins) {
  const char* dir = std::getenv("FO4_PLUGIN_CORPUS");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_PLUGIN_CORPUS to a directory of plugins to run this";
  }

  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{
           dir, std::filesystem::directory_options::skip_permission_denied}) {
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == ".esp" || ext == ".esl" || ext == ".esm") paths.push_back(entry.path());
  }
  if (paths.empty()) GTEST_SKIP() << "no plugins under " << dir;
  std::sort(paths.begin(), paths.end());

  Tally read_tally;
  Tally decode_tally;

  for (const auto& path : paths) {
    const auto name = path.filename().string();
    fo4::Plugin plugin;
    try {
      plugin = fo4::read_plugin_file(path);
    } catch (const std::exception& error) {
      read_tally.fail(error.what(), name);
      continue;
    }
    ++read_tally.plugins;

    for (const auto* record : fo4::collect_records(plugin)) {
      const auto signature = record->header.signature.to_string();
      const auto where = name + " " + record->form_key.to_string();
      if (signature == "QUST") {
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_quest_aliases(plugin, record->form_key); });
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_quest_stages(plugin, record->form_key); });
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_quest_objectives(plugin, record->form_key); });
      } else if (signature == "RACE") {
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_race(plugin, record->form_key); });
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_race_body_data(plugin, record->form_key); });
      } else if (signature == "ARMO") {
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_armor(plugin, record->form_key); });
      } else if (signature == "ARMA") {
        try_decode(decode_tally, where,
                   [&] { fo4::forms::decode_armor_addon(plugin, record->form_key); });
      }
    }
  }

  std::cout << "corpus: " << paths.size() << " files, " << read_tally.plugins
            << " parsed, " << decode_tally.records << " decode attempts\n";
  if (!read_tally.failures.empty()) {
    std::cout << "--- parse failures ---\n";
    for (const auto& [message, count] : read_tally.failures) {
      std::cout << "  " << count << "x  " << read_tally.first_example[message] << "\n";
    }
  }
  if (!decode_tally.failures.empty()) {
    std::cout << "--- decode failures ---\n";
    for (const auto& [message, count] : decode_tally.failures) {
      std::cout << "  " << count << "x  " << decode_tally.first_example[message] << "\n";
    }
  }

  EXPECT_TRUE(read_tally.failures.empty()) << "plugins failed to parse";
  EXPECT_TRUE(decode_tally.failures.empty()) << "records failed to decode";
  EXPECT_GT(read_tally.plugins, 100u);
}

TEST(PluginCorpus, EveryUnmodifiedPluginWritesByteIdentically) {
  const char* dir = std::getenv("FO4_PLUGIN_CORPUS");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_PLUGIN_CORPUS to a directory of plugins to run this";
  }

  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{
           dir, std::filesystem::directory_options::skip_permission_denied}) {
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == ".esp" || ext == ".esl" || ext == ".esm") paths.push_back(entry.path());
  }
  if (paths.empty()) GTEST_SKIP() << "no plugins under " << dir;
  std::sort(paths.begin(), paths.end());

  std::vector<std::string> failures;
  std::size_t processed = 0;
  for (const auto& path : paths) {
    try {
      auto plugin = fo4::read_plugin_file(path);
      const auto original = fo4::plugin_original_bytes(plugin);
      plugin.original_range.clear();
      const auto written = fo4::write_plugin(plugin);
      if (written.size() != original.size() ||
          !std::equal(written.begin(), written.end(), original.begin())) {
        if (!fo4::test::hedr_count_only_difference(written, original, plugin)) {
          failures.push_back(path.string());
        }
      }
      ++processed;
    } catch (const std::exception& error) {
      failures.push_back(path.string() + ": " + error.what());
    }
  }

  for (std::size_t i = 0; i < failures.size() && i < 25; ++i) {
    ADD_FAILURE() << failures[i];
  }
  EXPECT_TRUE(failures.empty()) << failures.size() << " plugin(s) were not byte-identical";
  EXPECT_GT(processed, 100u);
}
