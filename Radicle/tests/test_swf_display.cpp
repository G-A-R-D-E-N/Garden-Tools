#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfDisplayList;
using fo4::swf::SwfDisplayOptions;
using fo4::swf::SwfDisplayFrame;
using fo4::swf::SwfFrameInstance;
using fo4::swf::SwfTimelineStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

std::vector<std::byte> movie_of(std::uint16_t frames, const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(frames);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> matrix_translate(std::int32_t x, std::int32_t y) {
  SwfBuilder bits;
  bits.push_bits(0, 1);
  bits.push_bits(0, 1);
  bits.push_bits(11, 5);
  bits.push_bits(static_cast<std::uint32_t>(x) & 0x7FFu, 11);
  bits.push_bits(static_cast<std::uint32_t>(y) & 0x7FFu, 11);
  bits.align();
  return bits.body;
}

std::vector<std::byte> matrix_scale_translate(std::int32_t scale, std::int32_t x, std::int32_t y) {
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

std::vector<std::byte> place2(std::uint8_t flags, std::uint16_t depth,
                              std::optional<std::uint16_t> character,
                              const std::vector<std::byte>& matrix, const std::string& name) {
  SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  if (character) {
    body.u16(*character);
  }
  if (!matrix.empty()) {
    body.bytes(matrix);
  }
  if (!name.empty()) {
    body.text(name);
  }
  return body.body;
}

std::vector<std::byte> remove2(std::uint16_t depth) {
  SwfBuilder body;
  body.u16(depth);
  return body.body;
}

std::vector<std::byte> sprite_tag(std::uint16_t id, std::uint16_t frames,
                                  const std::vector<TagPayload>& children) {
  SwfBuilder body;
  body.u16(id);
  body.u16(frames);
  for (const auto& [code, payload] : children) {
    body.tag(code, payload, false);
  }
  body.tag(0);
  return body.body;
}

std::vector<std::byte> shape_definition() { return swf_art_test::define_shape_3_payload(); }

SwfDisplayList display_of(const std::vector<std::byte>& bytes,
                          const SwfDisplayOptions& options = {}) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  return fo4::swf::build_display_list(result.file, options);
}

const SwfFrameInstance* find(const SwfDisplayFrame& frame, std::string_view path) {
  for (const SwfFrameInstance& instance : frame.instances) {
    if (std::string_view(instance.path) == path) {
      return &instance;
    }
  }
  return nullptr;
}

std::string paths_of(const SwfDisplayFrame& frame) {
  std::string out;
  for (const SwfFrameInstance& instance : frame.instances) {
    if (!out.empty()) out += ",";
    out += instance.path;
  }
  return out;
}

}  // namespace

TEST(SwfDisplay, LoopsAChildTimelineWhoseLengthDiffersFromItsParent) {
  const auto sprite = sprite_tag(9, 2, {
      {26, place2(0x22, 1, 5, {}, "Cell")},
      {1, {}},
      {26, place2(0x22, 2, 5, {}, "Marker")},
      {1, {}},
  });
  const auto movie = movie_of(3, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x22, 1, 9, {}, "Art")},
      {1, {}},
      {1, {}},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  ASSERT_EQ(display.timelines.size(), 2u);
  EXPECT_EQ(display.timelines[0].character_id, 0);
  ASSERT_EQ(display.timelines[0].frames.size(), 3u);
  EXPECT_EQ(display.timelines[1].character_id, 9);
  ASSERT_EQ(display.timelines[1].frames.size(), 2u);

  const auto& root = display.timelines[0].frames;
  EXPECT_EQ(paths_of(root[0]), "Art,Art/Cell");
  EXPECT_EQ(paths_of(root[1]), "Art,Art/Cell,Art/Marker");
  EXPECT_EQ(paths_of(root[2]), "Art,Art/Cell");
}

TEST(SwfDisplay, AdvancesTwoInstancesOfOneSpriteIndependently) {
  const auto sprite = sprite_tag(9, 2, {
      {26, place2(0x22, 1, 5, {}, "Cell")},
      {1, {}},
      {1, {}},
  });
  const auto movie = movie_of(4, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x22, 1, 9, {}, "Early")},
      {1, {}},
      {26, place2(0x22, 2, 9, {}, "Late")},
      {1, {}},
      {1, {}},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames;
  ASSERT_EQ(root.size(), 4u);
  EXPECT_EQ(paths_of(root[1]), "Early,Late,Late/Cell");
  EXPECT_EQ(paths_of(root[2]), "Early,Early/Cell,Late");
}

TEST(SwfDisplay, KeepsAnInstanceIdAcrossAMoveAndRenewsItAfterARemove) {
  const auto movie = movie_of(4, {
      {32, shape_definition()},
      {26, place2(0x22, 1, 5, {}, "Box")},
      {1, {}},
      {26, place2(0x05, 1, std::nullopt, matrix_translate(100, 0), "")},
      {1, {}},
      {28, remove2(1)},
      {26, place2(0x22, 1, 5, {}, "Box")},
      {1, {}},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames;
  ASSERT_EQ(root.size(), 4u);
  const auto* placed = find(root[0], "Box");
  const auto* moved = find(root[1], "Box");
  const auto* replaced = find(root[2], "Box");
  ASSERT_NE(placed, nullptr);
  ASSERT_NE(moved, nullptr);
  ASSERT_NE(replaced, nullptr);
  ASSERT_EQ(root[1].instances.size(), 1u);
  ASSERT_EQ(root[2].instances.size(), 1u);
  EXPECT_EQ(moved->instance_id, placed->instance_id);
  EXPECT_NE(replaced->instance_id, placed->instance_id);
  EXPECT_DOUBLE_EQ(moved->world_transform.translate_x, 5.0);
  EXPECT_EQ(replaced->character_id, 5);
}

TEST(SwfDisplay, RestartsAChildWhenAPlacementReplacesTheCharacterAtOneDepth) {
  const auto sprite = sprite_tag(9, 4, {
      {26, place2(0x22, 1, 5, {}, "Cell")},
      {1, {}},
      {26, place2(0x22, 2, 5, {}, "Marker")},
      {1, {}},
      {1, {}},
      {1, {}},
  });
  const auto movie = movie_of(4, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x22, 1, 9, {}, "Art")},
      {1, {}},
      {1, {}},
      {26, place2(0x22, 1, 9, {}, "Art")},
      {1, {}},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames;
  ASSERT_EQ(root.size(), 4u);
  const auto* first = find(root[0], "Art");
  const auto* again = find(root[2], "Art");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(again, nullptr);
  EXPECT_NE(again->instance_id, first->instance_id);
  EXPECT_EQ(paths_of(root[2]), "Art,Art/Cell");
  EXPECT_EQ(paths_of(root[3]), "Art,Art/Cell,Art/Marker");
}

TEST(SwfDisplay, ComposesWorldBoundsThroughNestedTransforms) {
  const auto sprite = sprite_tag(9, 1, {
      {26, place2(0x26, 1, 5, matrix_scale_translate(0x20000, 200, 100), "Cell")},
      {1, {}},
  });
  const auto movie = movie_of(1, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x26, 1, 9, matrix_scale_translate(0x8000, 100, 50), "Art")},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames[0];
  const auto* art = find(root, "Art");
  const auto* cell = find(root, "Art/Cell");
  ASSERT_NE(art, nullptr);
  ASSERT_NE(cell, nullptr);
  EXPECT_TRUE(art->is_sprite);
  EXPECT_FALSE(art->has_bounds);
  EXPECT_TRUE(cell->has_bounds);
  EXPECT_DOUBLE_EQ(cell->transform.scale_x, 2.0);
  EXPECT_DOUBLE_EQ(cell->transform.translate_x, 10.0);
  EXPECT_DOUBLE_EQ(cell->world_transform.scale_x, 1.0);
  EXPECT_DOUBLE_EQ(cell->world_transform.scale_y, 1.0);
  EXPECT_DOUBLE_EQ(cell->world_transform.translate_x, 10.0);
  EXPECT_DOUBLE_EQ(cell->world_transform.translate_y, 5.0);
  EXPECT_DOUBLE_EQ(cell->bounds.x, 10.0);
  EXPECT_DOUBLE_EQ(cell->bounds.y, 5.0);
  EXPECT_DOUBLE_EQ(cell->bounds.width, 100.0);
  EXPECT_DOUBLE_EQ(cell->bounds.height, 50.0);
}

TEST(SwfDisplay, StopsAtASelfReferencingSprite) {
  const auto sprite = sprite_tag(3, 1, {
      {26, place2(0x22, 1, 3, {}, "Loop")},
      {1, {}},
  });
  const auto movie = movie_of(1, {
      {39, sprite},
      {26, place2(0x22, 1, 3, {}, "Loop")},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames[0];
  EXPECT_EQ(paths_of(root), "Loop,Loop/Loop");
}

TEST(SwfDisplay, AddressesANestedSpriteRowWithItsOwnSpriteLocalPlacement) {
  // Invariant: a sprite child's row must address a child of its DefineSprite, never a top-level tag.
  const auto sprite = sprite_tag(9, 1, {
      {26, place2(0x26, 1, 5, matrix_translate(50, 0), "Cell")},
      {1, {}},
  });
  const auto movie = movie_of(1, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x26, 1, 9, matrix_translate(200, 100), "Art")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(movie);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& file = result.file;
  const auto display = fo4::swf::build_display_list(file);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  ASSERT_EQ(display.timelines.size(), 2u);
  const auto* art = find(display.timelines[0].frames[0], "Art");
  const auto* cell = find(display.timelines[0].frames[0], "Art/Cell");
  ASSERT_NE(art, nullptr);
  ASSERT_NE(cell, nullptr);

  // Invariant: a root row's create_tag_index must name the top-level placement that created it.
  ASSERT_LT(art->create_tag_index, file.tags.size());
  EXPECT_EQ(file.tags[art->create_tag_index].code, 26);
  ASSERT_TRUE(file.tags[art->create_tag_index].placement.has_value());
  EXPECT_EQ(file.tags[art->create_tag_index].placement->character_id.value_or(0), 9);
  ASSERT_TRUE(art->transform_tag_index.has_value());
  EXPECT_EQ(*art->transform_tag_index, art->create_tag_index);

  // Invariant: a sprite row's create_tag_index must name a child of the defining DefineSprite.
  const fo4::swf::SwfTag* definition = nullptr;
  for (const auto& tag : file.tags) {
    if (tag.code == 39 && tag.sprite_id && *tag.sprite_id == 9) {
      definition = &tag;
    }
  }
  ASSERT_NE(definition, nullptr);
  ASSERT_LT(cell->create_tag_index, definition->children.size());
  const auto& placement_tag = definition->children[cell->create_tag_index];
  EXPECT_EQ(placement_tag.code, 26);
  ASSERT_TRUE(placement_tag.placement.has_value());
  EXPECT_EQ(placement_tag.placement->depth, 1);
  EXPECT_EQ(placement_tag.placement->character_id.value_or(0), 5);
  ASSERT_TRUE(cell->transform_tag_index.has_value());
  EXPECT_EQ(*cell->transform_tag_index, cell->create_tag_index);
  // Invariant: the address is sprite local, so the top-level stream must hold a different tag at that index.
  ASSERT_LT(cell->create_tag_index, file.tags.size());
  EXPECT_NE(file.tags[cell->create_tag_index].code, 26);
}

TEST(SwfDisplay, LeavesTheTransformIndexEmptyUntilAPlacementAuthorsAMatrix) {
  // Invariant: a row with no authored matrix must have no transform address, so tag zero is never one.
  const auto movie = movie_of(2, {
      {32, shape_definition()},
      {26, place2(0x22, 1, 5, {}, "Box")},
      {1, {}},
      {26, place2(0x05, 1, std::nullopt, matrix_translate(100, 0), "")},
      {1, {}},
  });
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto& root = display.timelines[0].frames;
  ASSERT_EQ(root.size(), 2u);
  const auto* placed = find(root[0], "Box");
  const auto* moved = find(root[1], "Box");
  ASSERT_NE(placed, nullptr);
  ASSERT_NE(moved, nullptr);

  EXPECT_FALSE(placed->transform_tag_index.has_value());
  ASSERT_TRUE(moved->transform_tag_index.has_value());
  EXPECT_EQ(moved->instance_id, placed->instance_id);
  EXPECT_EQ(moved->create_tag_index, placed->create_tag_index);
  EXPECT_NE(*moved->transform_tag_index, moved->create_tag_index);
  EXPECT_DOUBLE_EQ(moved->transform.translate_x, 5.0);
}

TEST(SwfDisplay, RejectsWorkPastTheInstanceBudget) {
  const auto sprite = sprite_tag(9, 1, {
      {26, place2(0x22, 1, 5, {}, "Cell")},
      {1, {}},
  });
  const auto movie = movie_of(1, {
      {32, shape_definition()},
      {39, sprite},
      {26, place2(0x22, 1, 9, {}, "Art")},
      {1, {}},
  });
  SwfDisplayOptions options;
  options.max_instances = 1;
  const auto display = display_of(movie, options);
  EXPECT_EQ(display.status, SwfTimelineStatus::malformed);
  EXPECT_NE(display.message.find("budget"), std::string::npos);
  EXPECT_TRUE(display.timelines.empty());
}
