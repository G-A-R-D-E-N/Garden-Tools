#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::byte> sound_movie() {
  return swf_test::movie_of(1, {{14, bytes_of({0x07, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0xAA})},
                                 {15, bytes_of({0x07, 0x00, 0x00})}});
}

std::vector<std::byte> written(const radicle_swf* swf) {
  std::uint8_t* raw = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_write_bytes(swf, &raw, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::byte> bytes;
  if (raw != nullptr) {
    bytes.assign(reinterpret_cast<const std::byte*>(raw),
                 reinterpret_cast<const std::byte*>(raw) + size);
    radicle_free_bytes(raw);
  }
  return bytes;
}

}  // namespace

TEST(CApiSwfSound, ReadsDefineSoundAndStartSoundHeaders) {
  const SwfHandle swf = open(sound_movie());
  ASSERT_NE(swf, nullptr);

  radicle_swf_sound sound{};
  EXPECT_EQ(radicle_swf_sound_at(swf.get(), 0, &sound), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(sound.sound_id, 7);
  EXPECT_EQ(sound.format, 0);
  EXPECT_EQ(sound.rate, 1);
  EXPECT_EQ(sound.sample_count, 1u);
  EXPECT_EQ(sound.data_size, 1u);

  radicle_swf_start_sound start{};
  EXPECT_EQ(radicle_swf_start_sound_at(swf.get(), 1, &start), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(start.sound_id, 7);

  EXPECT_EQ(radicle_swf_sound_at(swf.get(), 1, &sound), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_start_sound_at(swf.get(), 0, &start), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_sound_at(swf.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Invariant: replacing the samples rewrites the DefineSound payload and moves the revision, so a later read sees the new header.
TEST(CApiSwfSound, ReplacesSoundDataAndRebuildsTheView) {
  const SwfHandle swf = open(sound_movie());
  ASSERT_NE(swf, nullptr);
  const auto before = written(swf.get());
  const std::uint64_t revision = radicle_swf_revision(swf.get());

  radicle_swf_sound header{};
  header.sound_id = 7;
  header.format = 2;
  header.rate = 3;
  header.sample_16bit = 1;
  header.stereo = 1;
  header.sample_count = 1024;
  const std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  radicle_swf_plan_result result{};
  EXPECT_EQ(radicle_swf_replace_sound_data(swf.get(), &header, data, sizeof(data), &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);
  EXPECT_GT(result.revision, revision);

  radicle_swf_sound reread{};
  EXPECT_EQ(radicle_swf_sound_at(swf.get(), 0, &reread), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(reread.format, 2);
  EXPECT_EQ(reread.sample_count, 1024u);
  EXPECT_EQ(reread.data_size, 4u);
  EXPECT_NE(written(swf.get()), before);
}

// Invariant: a refusal must leave the bytes and the revision exactly as they were, so a bad retarget is observable only as a status.
TEST(CApiSwfSound, RefusesABadRetargetAndLeavesTheBytesUntouched) {
  const SwfHandle swf = open(sound_movie());
  ASSERT_NE(swf, nullptr);
  const auto before = written(swf.get());
  const std::uint64_t revision = radicle_swf_revision(swf.get());

  radicle_swf_plan_result result{};
  EXPECT_EQ(radicle_swf_retarget_start_sound(swf.get(), 1, 9, &result),
            RADICLE_STATUS_NOT_FOUND)
      << radicle_last_error();
  EXPECT_EQ(radicle_swf_retarget_start_sound(swf.get(), 0, 7, &result),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_retarget_start_sound(swf.get(), 1, 7, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(swf.get()), before);
  EXPECT_EQ(radicle_swf_revision(swf.get()), revision);
}
