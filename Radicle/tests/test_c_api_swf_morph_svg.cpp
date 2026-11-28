#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using swf_art_test::rgba;
using swf_art_test::signed_bits;
using swf_test::SwfBuilder;
using swf_test::TagPayload;

constexpr std::uint16_t kMorphId = 3;
constexpr std::uint16_t kSpriteId = 4;
constexpr std::uint16_t kGradientMorphId = 12;

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
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

void triangle(SwfBuilder& body, std::int32_t size) {
  for (const auto& [dx, dy] : {std::pair{size, 0}, std::pair{0, size}, std::pair{-size, -size}}) {
    body.push_bits(1, 1);
    body.push_bits(1, 1);
    body.push_bits(2, 4);
    body.push_bits(1, 1);
    signed_bits(body, dx, 4);
    signed_bits(body, dy, 4);
  }
  body.push_bits(0, 6);
  body.align();
}

// Constraint: one solid fill pair and no line styles, so interpolate_morph_shape accepts the morph; the start edges set fill 1, the end edges only move.
std::vector<std::byte> solid_morph_payload() {
  SwfBuilder start;
  start.u8(1);
  start.u8(0x00);
  rgba(start, 255, 0, 0, 255);
  rgba(start, 0, 0, 255, 255);
  start.u8(0);
  start.u8(0x10);
  start.push_bits(0, 1);
  start.push_bits(0x05, 5);
  start.push_bits(8, 5);
  signed_bits(start, 1, 8);
  signed_bits(start, 2, 8);
  start.push_bits(1, 1);
  triangle(start, 6);

  SwfBuilder end;
  end.u8(0);
  end.push_bits(0, 1);
  end.push_bits(0x01, 5);
  end.push_bits(8, 5);
  signed_bits(end, 5, 8);
  signed_bits(end, 5, 8);
  triangle(end, 3);

  SwfBuilder body;
  body.u16(kMorphId);
  body.rect(0, 100, 0, 80, 9);
  body.rect(0, 160, 0, 120, 9);
  body.u32(static_cast<std::uint32_t>(start.body.size()));
  body.bytes(start.body);
  body.bytes(end.body);
  return body.body;
}

// Constraint: PlaceObject2 puts the ratio after the matrix and colour transform; 0x10 is HasRatio, 0x02 HasCharacter, 0x01 Move.
std::vector<std::byte> place_object2(std::uint16_t depth, std::optional<std::uint16_t> character,
                                     std::optional<std::uint16_t> ratio, bool move) {
  SwfBuilder body;
  body.u8((character ? 0x02 : 0) | (ratio ? 0x10 : 0) | (move ? 0x01 : 0));
  body.u16(depth);
  if (character) body.u16(*character);
  if (ratio) body.u16(*ratio);
  return body.body;
}

std::vector<std::byte> morph_sprite_movie() {
  const std::vector<std::byte> none;
  const auto sprite = swf_test::sprite_tag(
      kSpriteId, 3,
      {{26, place_object2(1, kMorphId, std::uint16_t{0}, false)},
       {1, none},
       {26, place_object2(1, std::nullopt, std::uint16_t{32768}, true)},
       {1, none},
       {26, place_object2(1, std::nullopt, std::uint16_t{65535}, true)},
       {1, none}});
  const std::vector<TagPayload> tags{
      {46, solid_morph_payload()},
      {46, swf_art_test::morph_payload(false)},
      {39, sprite},
      {26, place_object2(1, kSpriteId, std::nullopt, false)},
      {1, none},
  };
  return swf_test::movie_of(1, tags);
}

std::string svg_at(const radicle_swf* swf, std::uint16_t id, std::uint16_t ratio) {
  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf, id, ratio, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  if (text == nullptr) return {};
  EXPECT_EQ(text[size], 0);
  std::string svg(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  return svg;
}

}  // namespace

TEST(CApiSwfMorphSvg, ExportsTheStartMiddleAndEndShapeAsDistinctSvg) {
  const auto swf = open(morph_sprite_movie());
  ASSERT_NE(swf, nullptr);

  const auto start = svg_at(swf.get(), kMorphId, 0);
  const auto middle = svg_at(swf.get(), kMorphId, 32768);
  const auto end = svg_at(swf.get(), kMorphId, 65535);
  EXPECT_TRUE(start.starts_with("<svg")) << start;
  EXPECT_NE(start.find("<path"), std::string::npos) << start;
  EXPECT_NE(start.find("width=\"5\""), std::string::npos) << start;
  EXPECT_NE(end.find("width=\"8\""), std::string::npos) << end;
  EXPECT_NE(start.find("fill=\"#ff0000\""), std::string::npos) << start;
  EXPECT_NE(end.find("fill=\"#0000ff\""), std::string::npos) << end;
  EXPECT_NE(middle.find("fill=\"#7f0080\""), std::string::npos) << middle;
  EXPECT_NE(middle.find("d=\"M0.35 0.2L0.15 0.2"), std::string::npos) << middle;
  EXPECT_NE(start, middle);
  EXPECT_NE(middle, end);
  EXPECT_NE(start, end);
  EXPECT_EQ(svg_at(swf.get(), kMorphId, 0), start);
}

TEST(CApiSwfMorphSvg, RefusesNonMorphsAndNullArgumentsWithOutputsCleared) {
  const auto swf = open(morph_sprite_movie());
  ASSERT_NE(swf, nullptr);

  std::uint8_t sentinel = 0;
  std::uint8_t* text = &sentinel;
  std::size_t size = 7;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf.get(), kSpriteId, 0, &text, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_NE(std::string(radicle_last_error()).find("not a decoded morph shape"), std::string::npos)
      << radicle_last_error();

  text = &sentinel;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf.get(), 99, 0, &text, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);

  text = &sentinel;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf.get(), kGradientMorphId, 0, &text, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("non-solid"), std::string::npos) << radicle_last_error();

  size = 7;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf.get(), kMorphId, 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);
  text = &sentinel;
  size = 7;
  EXPECT_EQ(radicle_swf_morph_shape_svg(nullptr, kMorphId, 0, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);
  text = &sentinel;
  EXPECT_EQ(radicle_swf_morph_shape_svg(swf.get(), kMorphId, 0, &text, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
}

TEST(CApiSwfMorphSvg, ReportsThePlaceObjectRatioOnEachFrameInstance) {
  const auto swf = open(morph_sprite_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const FramesHandle frames(raw);

  std::size_t sprite_timeline = radicle_swf_frames_timeline_count(frames.get());
  for (std::size_t index = 0; index < radicle_swf_frames_timeline_count(frames.get()); ++index) {
    std::uint16_t id = 0;
    std::size_t frame_count = 0;
    ASSERT_EQ(radicle_swf_frames_timeline_at(frames.get(), index, &id, &frame_count), RADICLE_STATUS_OK);
    if (id == kSpriteId) {
      sprite_timeline = index;
      EXPECT_EQ(frame_count, 3u);
    }
  }
  ASSERT_LT(sprite_timeline, radicle_swf_frames_timeline_count(frames.get()));

  const std::uint16_t expected[] = {0, 32768, 65535};
  for (std::size_t frame = 0; frame < 3; ++frame) {
    radicle_swf_frames_instance instance{};
    ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), sprite_timeline, frame, 0, &instance),
              RADICLE_STATUS_OK)
        << radicle_last_error();
    EXPECT_EQ(instance.character_id, kMorphId);
    EXPECT_EQ(instance.has_ratio, 1) << frame;
    EXPECT_EQ(instance.ratio, expected[frame]) << frame;
  }

  radicle_swf_frames_instance root{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 0, &root), RADICLE_STATUS_OK);
  EXPECT_EQ(root.character_id, kSpriteId);
  EXPECT_EQ(root.has_ratio, 0);
  EXPECT_EQ(root.ratio, 0);
}
