#include "fo4/archive/ba2.hpp"
#include "fo4/avm1/avm1.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

TEST(SwfRealData, InterfaceGfxAvm1StreamsDecodeAndReencodeByteIdentically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  const std::vector<std::string> named = {"RaceSex_menu.gfx", "TutorialMenu.gfx", "SafeZone.gfx"};
  std::size_t movies = 0;
  std::size_t do_action_tags = 0;
  std::size_t do_init_action_tags = 0;
  std::size_t rejected = 0;
  std::size_t mismatched = 0;
  for (const auto& entry : archive.entries) {
    std::string normalized = entry.path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const auto file_name = std::filesystem::path(normalized).filename().string();
    if (std::find(named.begin(), named.end(), file_name) == named.end()) {
      continue;
    }
    ++movies;
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    const auto read = fo4::swf::read_swf(bytes);
    ASSERT_TRUE(read.ok()) << file_name;
    std::vector<const fo4::swf::SwfTag*> action_tags;
    const auto collect = [&action_tags](const auto& self, const auto& tags) -> void {
      for (const auto& tag : tags) {
        if (tag.code == fo4::swf::tag::kDoAction || tag.code == fo4::swf::tag::kDoInitAction) {
          action_tags.push_back(&tag);
        }
        self(self, tag.children);
      }
    };
    collect(collect, read.file.tags);
    for (const fo4::swf::SwfTag* tag : action_tags) {
      if (tag->code == fo4::swf::tag::kDoAction) {
        ++do_action_tags;
      } else {
        ++do_init_action_tags;
      }
      ASSERT_TRUE(tag->action.has_value());
      const auto stream_bytes =
          std::span<const std::byte>(tag->payload).subspan(tag->action->offset, tag->action->size);
      const auto stream = fo4::avm1::decode_actions(stream_bytes);
      if (!stream.ok()) {
        ++rejected;
        ADD_FAILURE() << file_name << ": " << stream.message;
        continue;
      }
      const auto encoded = fo4::avm1::encode_actions(stream);
      if (encoded.size() != stream_bytes.size() ||
          !std::equal(encoded.begin(), encoded.end(), stream_bytes.begin())) {
        ++mismatched;
      }
    }
  }
  RecordProperty("avm1_movies", std::to_string(movies));
  RecordProperty("do_action_tags", std::to_string(do_action_tags));
  RecordProperty("do_init_action_tags", std::to_string(do_init_action_tags));
  RecordProperty("avm1_rejected", std::to_string(rejected));
  RecordProperty("avm1_reencode_mismatches", std::to_string(mismatched));
  EXPECT_EQ(movies, 3u);
  EXPECT_EQ(rejected, 0u);
  EXPECT_EQ(mismatched, 0u);
  EXPECT_EQ(do_action_tags, 82u);
  EXPECT_EQ(do_init_action_tags, 66u);
}
