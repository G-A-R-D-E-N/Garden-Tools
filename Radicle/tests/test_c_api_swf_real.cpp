#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ArchiveDeleter {
  void operator()(radicle_ba2* ba2) const { radicle_ba2_close(ba2); }
};

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using ArchiveHandle = std::unique_ptr<radicle_ba2, ArchiveDeleter>;
using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

std::uint64_t fnv1a(std::uint64_t hash, std::string_view text) {
  for (const auto ch : text) {
    hash ^= static_cast<std::uint8_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string number(std::size_t value) { return std::to_string(value); }

/* Canonical text of one movie; two readers of the same bytes must produce the same text, so
   the dump must double as the determinism check. */
std::string dump(const radicle_swf* swf) {
  radicle_swf_info info{};
  EXPECT_EQ(radicle_swf_get_info(swf, &info), RADICLE_STATUS_OK);
  std::string text;
  auto add = [&text](std::string_view value) {
    text.append(value);
    text.push_back('|');
  };
  add(number(info.version));
  add(number(info.frame_count));
  add(number(info.tag_count));
  add(number(info.abc_count));
  add(number(info.symbol_count));
  add(number(info.export_count));
  add(number(info.frame_label_count));

  for (std::size_t index = 0; index < radicle_swf_tag_count(swf); ++index) {
    radicle_swf_tag tag{};
    EXPECT_EQ(radicle_swf_tag_at(swf, index, &tag), RADICLE_STATUS_OK);
    add(tag.name);
    add(number(tag.length));
    add(number(tag.parent));
    add(number(tag.sprite_id));
  }
  for (std::size_t index = 0; index < info.symbol_count; ++index) {
    radicle_swf_symbol symbol{};
    EXPECT_EQ(radicle_swf_symbol_at(swf, index, &symbol), RADICLE_STATUS_OK);
    add(number(symbol.tag_id));
    add(symbol.name);
  }
  for (std::size_t abc = 0; abc < info.abc_count; ++abc) {
    radicle_swf_abc_info block{};
    EXPECT_EQ(radicle_swf_abc_info_at(swf, abc, &block), RADICLE_STATUS_OK);
    add(block.name);
    add(number(block.flags));
    add(number(block.payload_size));
    add(number(static_cast<std::size_t>(block.parsed)));
    if (!block.parsed) continue;
    const auto classes = radicle_swf_abc_class_count(swf, abc);
    add(number(classes));
    for (std::size_t index = 0; index < classes; ++index) {
      radicle_swf_class klass{};
      EXPECT_EQ(radicle_swf_abc_class_at(swf, abc, index, &klass), RADICLE_STATUS_OK);
      add(klass.name);
      add(klass.super_name);
      add(number(klass.method_count));
      for (std::size_t method_index = 0; method_index < klass.method_count; ++method_index) {
        radicle_swf_method method{};
        EXPECT_EQ(radicle_swf_abc_class_method(swf, abc, index, method_index, &method), RADICLE_STATUS_OK);
        add(method.name);
        add(method.return_type);
        add(number(method.call_count));
        for (std::size_t call_index = 0; call_index < method.call_count; ++call_index) {
          radicle_swf_call call{};
          EXPECT_EQ(radicle_swf_abc_call_at(swf, abc, RADICLE_SWF_OWNER_CLASS, index, method_index, call_index,
                                            &call),
                    RADICLE_STATUS_OK);
          add(call.target);
          add(number(call.is_external_interface));
          add(number(call.is_event_registration));
        }
      }
    }
    add(number(radicle_swf_abc_method_count(swf, abc)));
  }
  return text;
}

std::size_t tree_size(const fo4::swf::SwfTag& tag) {
  std::size_t total = 1;
  for (const auto& child : tag.children) total += tree_size(child);
  return total;
}

}  // namespace

// Regression: the C ABI must keep every tag and sprite parent, and must report the same tree as read_swf.
TEST(CApiSwfReal, ReadsEveryInterfaceMovieThroughTheArchiveAndTheBytesPathAlike) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }

  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);

  radicle_ba2_info archive_info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &archive_info), RADICLE_STATUS_OK);

  std::size_t movies = 0;
  std::size_t abc_classes = 0;
  std::size_t parsed_blocks = 0;
  std::size_t unparsed_blocks = 0;
  std::string unparsed_paths;
  std::uint64_t aggregate = 1469598103934665603ull;  // FNV-1a offset basis

  for (std::size_t index = 0; index < archive_info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    ASSERT_EQ(radicle_ba2_get_entry(archive.get(), index, &entry), RADICLE_STATUS_OK);
    const std::string path = entry.path == nullptr ? std::string() : std::string(entry.path);
    if (!is_movie(path)) continue;

    radicle_swf* raw = nullptr;
    const auto opened = radicle_swf_open_ba2(archive.get(), path.c_str(), &raw);
    if (opened == RADICLE_STATUS_PARSE_ERROR && std::string(radicle_last_error()).find("LZMA") != std::string::npos) {
      continue;  // phase 7 reads ZWS movies; the vanilla Interface archive has none in this census
    }
    ASSERT_EQ(opened, RADICLE_STATUS_OK) << path << ": " << radicle_last_error();
    const SwfHandle swf(raw);
    ++movies;

    radicle_swf_info info{};
    ASSERT_EQ(radicle_swf_get_info(swf.get(), &info), RADICLE_STATUS_OK);

    // The archive path and the bytes path must agree, and the tree must hold every read_swf tag.
    uint8_t* bytes = nullptr;
    size_t size = 0;
    ASSERT_EQ(radicle_ba2_extract(archive.get(), index, &bytes, &size), RADICLE_STATUS_OK);
    radicle_swf* raw_bytes = nullptr;
    ASSERT_EQ(radicle_swf_open_bytes(bytes, size, &raw_bytes), RADICLE_STATUS_OK) << path;
    const SwfHandle from_bytes(raw_bytes);
    const auto archive_dump = dump(swf.get());
    ASSERT_EQ(dump(from_bytes.get()), archive_dump) << path;

    const auto parsed = fo4::swf::read_swf(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    radicle_free_bytes(bytes);
    ASSERT_TRUE(parsed.ok()) << path;
    std::size_t expected_tags = 0;
    for (const auto& tag : parsed.file.tags) expected_tags += tree_size(tag);
    EXPECT_EQ(info.tag_count, expected_tags) << path;

    std::size_t movie_unparsed = 0;
    for (std::size_t abc = 0; abc < info.abc_count; ++abc) {
      radicle_swf_abc_info block{};
      ASSERT_EQ(radicle_swf_abc_info_at(swf.get(), abc, &block), RADICLE_STATUS_OK);
      if (block.parsed == 0) {
        ++movie_unparsed;
        continue;
      }
      ++parsed_blocks;
      abc_classes += radicle_swf_abc_class_count(swf.get(), abc);
    }
    unparsed_blocks += movie_unparsed;
    if (movie_unparsed != 0) unparsed_paths += (unparsed_paths.empty() ? "" : ";") + path;

    aggregate = fnv1a(aggregate, path);
    aggregate = fnv1a(aggregate, archive_dump);
  }

  EXPECT_GT(movies, 0u);
  EXPECT_GT(parsed_blocks, 0u);
  EXPECT_GT(abc_classes, 0u);
  RecordProperty("movies", std::to_string(movies));
  RecordProperty("parsed_abc_blocks", std::to_string(parsed_blocks));
  RecordProperty("abc_classes", std::to_string(abc_classes));
  RecordProperty("unparsed_abc_blocks", std::to_string(unparsed_blocks));
  if (unparsed_blocks != 0) RecordProperty("unparsed_abc_movies", unparsed_paths);
  RecordProperty("dump_hash", std::to_string(aggregate));
}

namespace {

/* An edit must be visible without depending on a particular movie's string table. */
std::string edit_first_string(std::string text, std::string_view marker) {
  const std::size_t line = text.find("  string ");
  if (line == std::string::npos) return {};
  const std::size_t quote = text.find('"', line);
  const std::size_t close = quote == std::string::npos ? std::string::npos : text.find('"', quote + 1);
  if (close == std::string::npos) return {};
  text.insert(close, std::string{marker});
  return text;
}

}  // namespace

TEST(CApiSwfAbcWriteReal, DumpsEditsAssemblesAndWritesARealInterfaceBlock) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);

  for (std::size_t index = 0; index < info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK || entry.path == nullptr) {
      continue;
    }
    const std::string_view path{entry.path};
    if (path.size() < 4 || path.substr(path.size() - 4) != ".swf") continue;
    radicle_swf* raw = nullptr;
    if (radicle_swf_open_ba2(archive.get(), entry.path, &raw) != RADICLE_STATUS_OK) continue;
    SwfHandle swf(raw);
    const std::size_t abc_count = radicle_swf_abc_count(swf.get());
    if (abc_count == 0) continue;

    std::uint8_t* text_raw = nullptr;
    std::size_t text_size = 0;
    ASSERT_EQ(radicle_swf_abc_text(swf.get(), 0, &text_raw, &text_size), RADICLE_STATUS_OK)
        << radicle_last_error();
    const std::string text(reinterpret_cast<const char*>(text_raw), text_size);
    radicle_free_bytes(text_raw);
    ASSERT_NE(text.find("abc "), std::string::npos);

    const std::string edited = edit_first_string(text, "RadicleRealEdit");
    ASSERT_FALSE(edited.empty());
    ASSERT_EQ(radicle_swf_abc_apply_text(swf.get(), 0,
                                         reinterpret_cast<const std::uint8_t*>(edited.data()),
                                         edited.size()),
              RADICLE_STATUS_OK)
        << radicle_last_error();

    std::uint8_t* bytes_raw = nullptr;
    std::size_t bytes_size = 0;
    ASSERT_EQ(radicle_swf_write_bytes(swf.get(), &bytes_raw, &bytes_size), RADICLE_STATUS_OK)
        << radicle_last_error();
    const std::vector<std::uint8_t> bytes(bytes_raw, bytes_raw + bytes_size);
    radicle_free_bytes(bytes_raw);

    radicle_swf* raw_reopened = nullptr;
    ASSERT_EQ(radicle_swf_open_bytes(bytes.data(), bytes.size(), &raw_reopened), RADICLE_STATUS_OK)
        << radicle_last_error();
    const SwfHandle reopened(raw_reopened);
    std::uint8_t* after_raw = nullptr;
    std::size_t after_size = 0;
    ASSERT_EQ(radicle_swf_abc_text(reopened.get(), 0, &after_raw, &after_size), RADICLE_STATUS_OK)
        << radicle_last_error();
    const std::string after(reinterpret_cast<const char*>(after_raw), after_size);
    radicle_free_bytes(after_raw);
    EXPECT_NE(after.find("RadicleRealEdit"), std::string::npos);
    return;
  }
  GTEST_SKIP() << "no Interface movie with an ABC block";
}
