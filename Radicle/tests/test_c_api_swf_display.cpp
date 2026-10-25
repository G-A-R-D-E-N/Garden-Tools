#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

struct FramesDeleter {
  void operator()(radicle_swf_frames* frames) const { radicle_swf_frames_destroy(frames); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;
using FramesHandle = std::unique_ptr<radicle_swf_frames, FramesDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::byte> matrix_scale_translate(std::int32_t scale, std::int32_t x,
                                              std::int32_t y) {
  SwfBuilder bits;
  bits.push_bits(1, 1);
  bits.push_bits(19, 5);
  bits.push_bits(static_cast<std::uint32_t>(scale), 19);
  bits.push_bits(static_cast<std::uint32_t>(scale), 19);
  bits.push_bits(0, 1);
  bits.push_bits(11, 5);
  bits.push_bits(static_cast<std::uint32_t>(x) & 0x7FFu, 11);
  bits.push_bits(static_cast<std::uint32_t>(y) & 0x7FFu, 11);
  bits.align();
  return bits.body;
}

std::vector<std::byte> place2(std::uint8_t flags, std::uint16_t depth, std::uint16_t character,
                              const std::vector<std::byte>& matrix, const std::string& name) {
  SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  body.u16(character);
  if (!matrix.empty()) {
    body.bytes(matrix);
  }
  if (!name.empty()) {
    body.text(name);
  }
  return body.body;
}

std::vector<std::byte> nested_movie() {
  SwfBuilder sprite;
  sprite.u16(9);
  sprite.u16(1);
  const auto child = place2(0x26, 1, 5, matrix_scale_translate(0x20000, 200, 100), "Cell");
  sprite.tag(26, child, false);
  sprite.tag(1);
  sprite.tag(0);

  const auto art = place2(0x26, 1, 9, matrix_scale_translate(0x8000, 100, 50), "Art");
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(32, swf_art_test::define_shape_3_payload(), false);
  body.tag(39, sprite.body, false);
  body.tag(26, art, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

std::vector<std::byte> unplaceable_movie() {
  SwfBuilder move;
  move.u8(0x05);
  move.u16(3);
  move.push_bits(0, 1);
  move.push_bits(0, 1);
  move.push_bits(11, 5);
  move.push_bits(0, 11);
  move.push_bits(0, 11);
  move.align();
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(26, move.body, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfFrames, ExposesPerFrameInstancesAndWorldBounds) {
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  FramesHandle frames(raw);
  ASSERT_NE(frames, nullptr);
  EXPECT_EQ(radicle_abi_version(), RADICLE_ABI_VERSION);
  ASSERT_EQ(radicle_swf_frames_timeline_count(frames.get()), 2u);

  std::uint16_t root_id = 1;
  std::size_t root_frames = 0;
  ASSERT_EQ(radicle_swf_frames_timeline_at(frames.get(), 0, &root_id, &root_frames),
            RADICLE_STATUS_OK);
  EXPECT_EQ(root_id, 0);
  EXPECT_EQ(root_frames, 1u);
  std::uint16_t sprite_id = 0;
  std::size_t sprite_frames = 0;
  ASSERT_EQ(radicle_swf_frames_timeline_at(frames.get(), 1, &sprite_id, &sprite_frames),
            RADICLE_STATUS_OK);
  EXPECT_EQ(sprite_id, 9);
  EXPECT_EQ(sprite_frames, 1u);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);

  radicle_swf_frames_instance art{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 0, &art), RADICLE_STATUS_OK);
  ASSERT_NE(art.path, nullptr);
  EXPECT_EQ(std::string(art.path), "Art");
  EXPECT_EQ(art.is_sprite, 1);
  EXPECT_EQ(art.has_bounds, 0);
  EXPECT_NE(art.instance_id, 0u);

  radicle_swf_frames_instance cell{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 1, &cell), RADICLE_STATUS_OK);
  ASSERT_NE(cell.path, nullptr);
  ASSERT_NE(cell.name, nullptr);
  EXPECT_EQ(std::string(cell.path), "Art/Cell");
  EXPECT_EQ(std::string(cell.name), "Cell");
  EXPECT_EQ(cell.depth, 1);
  EXPECT_EQ(cell.character_id, 5);
  EXPECT_EQ(cell.timeline_id, 9);
  EXPECT_EQ(cell.timeline_frame, 0u);
  EXPECT_EQ(cell.has_bounds, 1);
  EXPECT_EQ(cell.has_clip_depth, 0);
  EXPECT_DOUBLE_EQ(cell.world_transform.scale_x, 1.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.translate_x, 10.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.translate_y, 5.0);
  EXPECT_DOUBLE_EQ(cell.bounds.x, 10.0);
  EXPECT_DOUBLE_EQ(cell.bounds.y, 5.0);
  EXPECT_DOUBLE_EQ(cell.bounds.width, 100.0);
  EXPECT_DOUBLE_EQ(cell.bounds.height, 50.0);
}

TEST(CApiSwfFrames, OutlivesTheMovieItWasBuiltFrom) {
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK);
  FramesHandle frames(raw);
  swf.reset();

  radicle_swf_frames_instance cell{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 1, &cell), RADICLE_STATUS_OK);
  ASSERT_NE(cell.path, nullptr);
  EXPECT_EQ(std::string(cell.path), "Art/Cell");
  std::uint16_t character_id = 0;
  std::size_t frame_count = 0;
  EXPECT_EQ(radicle_swf_frames_timeline_at(frames.get(), 1, &character_id, &frame_count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(character_id, 9);
}

TEST(CApiSwfFrames, RejectsBadArgumentsAndUnplaceableMovies) {
  radicle_swf_frames* raw = nullptr;
  EXPECT_EQ(radicle_swf_frames_open(nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_frames_open(swf.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK);
  FramesHandle frames(raw);

  EXPECT_EQ(radicle_swf_frames_timeline_count(nullptr), 0u);
  std::uint16_t character_id = 0;
  std::size_t frame_count = 0;
  EXPECT_EQ(radicle_swf_frames_timeline_at(frames.get(), 2, &character_id, &frame_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("range"), std::string::npos);
  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 1, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_swf_frames_instance instance{};
  EXPECT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  auto broken = open(unplaceable_movie());
  ASSERT_NE(broken, nullptr);
  radicle_swf_frames* out = nullptr;
  EXPECT_EQ(radicle_swf_frames_open(broken.get(), &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("depth"), std::string::npos)
      << radicle_last_error();
}
