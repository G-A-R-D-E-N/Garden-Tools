#include "fo4/anim/anim_text_data.hpp"

#include "fo4/archive/ba2.hpp"
#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view k_root = "meshes\\animtextdata\\";

std::string lowered(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  return out;
}

std::string to_text(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

struct Counts {
  std::size_t file_lists{0};
  std::size_t file_index{0};
  std::size_t idle{0};
  std::size_t events{0};
  std::size_t name_indexes{0};
  std::size_t other_directories{0};
  std::size_t mismatches{0};
  std::size_t failures{0};
};

void check_entry(const fo4::archive::Ba2GeneralEntry& entry, Counts& counts) {
  const std::string path = lowered(entry.path);
  if (path.size() <= k_root.size() || path.compare(0, k_root.size(), k_root) != 0) {
    return;
  }
  const std::string_view rest(path.data() + k_root.size(), path.size() - k_root.size());
  const std::size_t slash = rest.find('\\');
  const std::string dir(rest.substr(0, slash));
  if (dir != "animationfiledata" && dir != "dynamicidledata" && dir != "animeventinfo") {
    ++counts.other_directories;
    return;
  }
  const std::vector<std::byte> bytes = fo4::archive::extract_ba2_general_entry(entry);
  const std::string text = to_text(bytes);
  std::string rebuilt;
  try {
    if (dir == "animationfiledata") {
      if (text.rfind("3\n", 0) == 0 || text.rfind("3\r\n", 0) == 0) {
        rebuilt = fo4::anim::write_anim_file_list(fo4::anim::parse_anim_file_list(text));
        ++counts.file_lists;
      } else {
        rebuilt = fo4::anim::write_anim_file_dirlist(fo4::anim::parse_anim_file_dirlist(text));
        ++counts.file_index;
      }
    } else if (text.rfind("Version 1", 0) == 0) {
      rebuilt = fo4::anim::write_anim_idle_data(fo4::anim::parse_anim_idle_data(text));
      ++counts.idle;
    } else if (text.rfind("V2", 0) == 0) {
      rebuilt = fo4::anim::write_anim_event_info(fo4::anim::parse_anim_event_info(text));
      ++counts.events;
    } else {
      rebuilt = fo4::anim::write_anim_file_dirlist(fo4::anim::parse_anim_file_dirlist(text));
      ++counts.name_indexes;
    }
  } catch (const std::exception& error) {
    ++counts.failures;
    ADD_FAILURE() << entry.path << ": " << error.what();
    return;
  }
  if (rebuilt != text) {
    ++counts.mismatches;
    ADD_FAILURE() << entry.path << ": rebuilt " << rebuilt.size() << " of " << text.size()
                  << " bytes";
  }
}

}  // namespace

// Detects: a reader or writer that must reproduce every shipped animation text table byte for byte but drops a line, a count, a header variant or a line ending, which no unit fixture covers for the 4,000-plus real files.
TEST(AnimTextDataRealData, EveryShippedTextTableRebuildsByteForByte) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const std::filesystem::path data_dir(dir);
  if (!std::filesystem::exists(data_dir)) {
    GTEST_SKIP() << "FO4_DATA_DIR does not exist";
  }
  Counts counts;
  std::size_t archives = 0;
  for (const auto& item : std::filesystem::directory_iterator(data_dir)) {
    if (!item.is_regular_file()) {
      continue;
    }
    const std::string name = lowered(item.path().filename().string());
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".ba2") != 0) {
      continue;
    }
    fo4::archive::Ba2GeneralArchive archive;
    try {
      archive = fo4::archive::read_ba2_general_file(item.path());
    } catch (const fo4::archive::Ba2Error&) {
      continue;
    }
    ++archives;
    for (const fo4::archive::Ba2GeneralEntry& entry : archive.entries) {
      check_entry(entry, counts);
    }
  }
  RecordProperty("archives", std::to_string(archives));
  RecordProperty("file_lists", std::to_string(counts.file_lists));
  RecordProperty("file_index", std::to_string(counts.file_index));
  RecordProperty("idle_lists", std::to_string(counts.idle));
  RecordProperty("event_infos", std::to_string(counts.events));
  RecordProperty("name_indexes", std::to_string(counts.name_indexes));
  RecordProperty("other_directories", std::to_string(counts.other_directories));
  EXPECT_EQ(counts.failures, 0U);
  EXPECT_EQ(counts.mismatches, 0U);
  EXPECT_GE(counts.file_lists, 3000U);
  EXPECT_GE(counts.idle, 2000U);
  EXPECT_GE(counts.events, 100U);
  EXPECT_GE(counts.name_indexes, 1U);
}
