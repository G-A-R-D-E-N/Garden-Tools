#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_edit.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfFile;
using fo4::swf::SwfFrameInstance;
using fo4::swf::SwfInstanceEditTarget;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

// Raw 16.16 / twips fields: a fixture must author the encoding the decoder sees.
struct Matrix {
  std::int32_t scale_x{65536};
  std::int32_t scale_y{65536};
  std::int32_t skew0{0};
  std::int32_t skew1{0};
  std::int32_t translate_x{0};
  std::int32_t translate_y{0};
};

void push_matrix(SwfBuilder& builder, const Matrix& matrix) {
  const bool has_scale = matrix.scale_x != 65536 || matrix.scale_y != 65536;
  builder.push_bits(has_scale ? 1 : 0, 1);
  if (has_scale) {
    builder.push_bits(19, 5);
    builder.push_bits(static_cast<std::uint32_t>(matrix.scale_x), 19);
    builder.push_bits(static_cast<std::uint32_t>(matrix.scale_y), 19);
  }
  const bool has_rotate = matrix.skew0 != 0 || matrix.skew1 != 0;
  builder.push_bits(has_rotate ? 1 : 0, 1);
  if (has_rotate) {
    builder.push_bits(19, 5);
    builder.push_bits(static_cast<std::uint32_t>(matrix.skew0), 19);
    builder.push_bits(static_cast<std::uint32_t>(matrix.skew1), 19);
  }
  builder.push_bits(16, 5);
  builder.push_bits(static_cast<std::uint32_t>(matrix.translate_x), 16);
  builder.push_bits(static_cast<std::uint32_t>(matrix.translate_y), 16);
  builder.align();
}

// HasCharacter|HasMatrix|HasName; a clip depth requires HasClipDepth.
std::vector<std::byte> place2(std::uint16_t depth, std::uint16_t character, std::string_view name,
                              const Matrix& matrix, std::optional<std::uint16_t> clip_depth) {
  SwfBuilder builder;
  builder.u8(clip_depth ? 0x66 : 0x26);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  if (clip_depth) {
    builder.u16(*clip_depth);
  }
  return builder.body;
}

// Constraint: a v3 body has two flag bytes and its blend mode follows the name.
std::vector<std::byte> place3(std::uint16_t depth, std::uint16_t character, std::string_view name,
                              const Matrix& matrix, std::uint8_t blend) {
  SwfBuilder builder;
  builder.u8(0x26);
  builder.u8(0x02);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  builder.u8(blend);
  return builder.body;
}

// Move|HasMatrix without a character: it must re-author the MATRIX of a live depth.
std::vector<std::byte> move2_matrix(std::uint16_t depth, const Matrix& matrix) {
  SwfBuilder builder;
  builder.u8(0x05);
  builder.u16(depth);
  push_matrix(builder, matrix);
  return builder.body;
}

// Constraint: a replacement must be able to restate or omit the MATRIX and NAME it inherits.
std::vector<std::byte> move2_character(std::uint16_t depth, std::uint16_t character,
                                      std::optional<Matrix> matrix,
                                      std::optional<std::string_view> name) {
  SwfBuilder builder;
  builder.u8(static_cast<std::uint8_t>(0x03 | (matrix ? 0x04 : 0) | (name ? 0x20 : 0)));
  builder.u16(depth);
  builder.u16(character);
  if (matrix) {
    push_matrix(builder, *matrix);
  }
  if (name) {
    builder.text(*name);
  }
  return builder.body;
}

// Constraint: the carry rule must refuse a lifetime that carries clip actions.
std::vector<std::byte> place2_clip_actions(std::uint16_t depth, std::uint16_t character,
                                          std::string_view name) {
  SwfBuilder builder;
  builder.u8(0xA6);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, Matrix{});
  builder.text(name);
  builder.u16(0);
  return builder.body;
}

// Constraint: a v1 body has no flag byte, so its MATRIX offset is four and a CXFORM may follow.
std::vector<std::byte> place1(std::uint16_t character, std::uint16_t depth, const Matrix& matrix) {
  SwfBuilder builder;
  builder.u16(character);
  builder.u16(depth);
  push_matrix(builder, matrix);
  builder.push_bits(0, 1);
  builder.push_bits(1, 1);
  builder.push_bits(8, 4);
  builder.push_bits(64, 8);
  builder.push_bits(64, 8);
  builder.push_bits(64, 8);
  builder.align();
  return builder.body;
}

// HasCharacter only: no MATRIX, so a lifetime that authors one must be refused.
std::vector<std::byte> place2_without_matrix(std::uint16_t depth, std::uint16_t character) {
  SwfBuilder builder;
  builder.u8(0x02);
  builder.u16(depth);
  builder.u16(character);
  return builder.body;
}

std::vector<std::byte> remove2(std::uint16_t depth) {
  SwfBuilder builder;
  builder.u16(depth);
  return builder.body;
}

std::vector<std::byte> remove_object(std::uint16_t character, std::uint16_t depth) {
  SwfBuilder builder;
  builder.u16(character);
  builder.u16(depth);
  return builder.body;
}

std::vector<std::byte> movie_of(const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

SwfFile open(const std::vector<std::byte>& bytes) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, fo4::swf::SwfStatus::ok) << result.message;
  return result.file;
}

const SwfInstanceEditTarget* find_target(const std::vector<SwfInstanceEditTarget>& targets,
                                        std::string_view path) {
  const auto found = std::find_if(targets.begin(), targets.end(),
                                  [&](const SwfInstanceEditTarget& target) {
                                    return target.path == path;
                                  });
  return found == targets.end() ? nullptr : &*found;
}

std::vector<std::vector<SwfFrameInstance>> root_frames(const SwfFile& file) {
  std::vector<std::vector<SwfFrameInstance>> out;
  const auto display = fo4::swf::build_display_list(file);
  if (display.timelines.empty()) {
    return out;
  }
  for (const auto& frame : display.timelines.front().frames) {
    out.push_back(frame.instances);
  }
  return out;
}

// Constraint: compare depth, name, character and transform as text, with one depth relabelled.
std::vector<std::string> frame_fingerprint(const std::vector<SwfFrameInstance>& frame,
                                          std::string_view moved_name, std::uint16_t source_depth,
                                          std::uint16_t destination_depth) {
  std::vector<std::string> out;
  for (const auto& instance : frame) {
    const auto depth = instance.depth == source_depth && instance.name == moved_name
                           ? destination_depth
                           : instance.depth;
    const auto& transform = instance.transform;
    std::ostringstream text;
    text << std::setprecision(17) << depth << '|' << instance.name << '|' << instance.character_id
         << '|' << transform.scale_x << ',' << transform.scale_y << ',' << transform.rotate_skew0
         << ',' << transform.rotate_skew1 << ',' << transform.translate_x << ','
         << transform.translate_y;
    out.push_back(text.str());
  }
  std::sort(out.begin(), out.end());
  return out;
}

struct SweepRng {
  std::uint64_t state{1};

  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }

  std::uint64_t below(std::uint64_t bound) { return next() % bound; }

  bool chance(std::uint64_t percent) { return below(100) < percent; }
};

// Constraint: each named placement needs a unique name, because the frame oracle identifies the moved one by it.
std::vector<TagPayload> sweep_movie(SweepRng& rng) {
  const std::uint16_t depths[] = {1, 2, 3, 5, 6, 7};
  const std::uint16_t characters[] = {9, 11, 12};
  std::vector<TagPayload> tags{{26, place2(3, 9, "n0", Matrix{}, std::nullopt)}, {1, {}}};
  std::set<std::uint16_t> live{3};
  int names = 1;
  for (int step = 0; step < 7; ++step) {
    const auto action = rng.below(100);
    const auto depth = depths[rng.below(6)];
    const auto character = characters[rng.below(3)];
    if (action < 35) {
      const auto name = "n" + std::to_string(names++);
      if (rng.chance(10) && depth + 3 <= 9) {
        tags.push_back(
            {26, place2(depth, character, name, Matrix{}, static_cast<std::uint16_t>(depth + 3))});
      } else if (rng.chance(60)) {
        tags.push_back({26, place2(depth, character, name, Matrix{}, std::nullopt)});
      } else {
        tags.push_back({26, place2_without_matrix(depth, character)});
      }
      live.insert(depth);
    } else if (action < 60) {
      // Constraint: a move must target a live depth, so no seed is produced that fails to display.
      if (!live.empty()) {
        std::vector<std::uint16_t> placed(live.begin(), live.end());
        const auto target = placed[rng.below(placed.size())];
        if (rng.chance(50)) {
          tags.push_back({26, move2_character(target, character,
                                              rng.chance(60) ? std::optional<Matrix>(Matrix{})
                                                             : std::nullopt,
                                              std::nullopt)});
        } else {
          tags.push_back({26, move2_matrix(target, Matrix{65536, 65536, 0, 0, 40, 0})});
        }
      }
    } else if (action < 85) {
      tags.push_back({28, remove2(depth)});
      live.erase(depth);
    } else {
      tags.push_back({5, remove_object(character, depth)});
      // Constraint: the read side erases on any removal, so the generator must drop the depth too.
      live.erase(depth);
    }
    if (rng.chance(80)) {
      tags.push_back({1, {}});
    }
  }
  tags.push_back({1, {}});
  return tags;
}

}  // namespace

TEST(SwfDepthReassign, RenamesTheDepthAcrossTheLifetimeAndEveryFrame) {
  const auto source = movie_of({{26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, move2_matrix(5, Matrix{65536, 65536, 0, 0, 100, 0})},
                                {1, {}},
                                {28, remove2(5)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);
  ASSERT_EQ(hero->create_tag_index, 0u);

  fo4::swf::SwfDepthReassignResult result;
  ASSERT_TRUE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 9, result))
      << result.message;
  EXPECT_EQ(result.source_depth, 5);
  EXPECT_EQ(result.destination_depth, 9);
  EXPECT_TRUE(result.inserted_tag_indices.empty());
  // Constraint: the creation, the matrix-only move and the removal are the whole lifetime.
  EXPECT_EQ(result.replaced_tag_indices, (std::vector<std::size_t>{0, 2, 4}));
  EXPECT_NE(result.new_instance_id, 0u);

  const auto frames = root_frames(file);
  ASSERT_EQ(frames.size(), 3u);
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& frame = frames[index];
    ASSERT_EQ(frame.size(), 1u);
    EXPECT_EQ(frame[0].depth, 9);
    EXPECT_EQ(frame[0].character_id, 9);
    EXPECT_EQ(frame[0].name, "hero");
  }
  EXPECT_TRUE(frames[2].empty());

  // Constraint: the matrix the lifetime authored must survive the move untouched.
  EXPECT_EQ(frames[1][0].transform.translate_x, 5.0);
  EXPECT_EQ(file.tags.size(), 7u);  // Six authored tags plus End, so the rewrite added no tag.
}

TEST(SwfDepthReassign, SynthesizesOneRemovalAtTheDestination) {
  const auto source = movie_of({{26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, place2(5, 12, "other", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);

  fo4::swf::SwfDepthReassignResult result;
  ASSERT_TRUE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 9, result))
      << result.message;
  ASSERT_EQ(result.inserted_tag_indices.size(), 1u);
  ASSERT_EQ(file.tags.size(), 6u);  // Four authored tags plus End plus the synthesized removal.

  const auto frames = root_frames(file);
  ASSERT_GE(frames.size(), 2u);
  ASSERT_EQ(frames[0].size(), 1u);
  EXPECT_EQ(frames[0][0].depth, 9);
  EXPECT_EQ(frames[0][0].name, "hero");
  // Constraint: the replacement must own the source depth and the destination must be empty.
  bool other_found = false;
  for (const auto& instance : frames[1]) {
    EXPECT_NE(instance.depth, 9);
    if (instance.name == "other") {
      other_found = true;
      EXPECT_EQ(instance.depth, 5);
    }
  }
  EXPECT_TRUE(other_found);
}

TEST(SwfDepthReassign, RejectsAMoveWithCharacterThatOmitsAnAuthoredMatrix) {
  const auto source = movie_of({{26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, move2_character(5, 12, std::nullopt, std::string_view{"hero"})},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);

  fo4::swf::SwfDepthReassignResult result;
  EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 9, result));
  EXPECT_NE(result.message.find("inherits the authored matrix"), std::string::npos)
      << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfDepthReassign, RejectsAMoveWithCharacterThatOmitsAnAuthoredName) {
  const auto source = movie_of({{26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, move2_character(5, 12, Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);

  fo4::swf::SwfDepthReassignResult result;
  EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 9, result));
  EXPECT_NE(result.message.find("inherits the authored instance name"), std::string::npos)
      << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfDepthReassign, RejectsALifetimeOrTerminatorTheCarryRuleCannotModel) {
  // Constraint: a v1 or v3 lifetime tag and clip actions leave state the carry rule cannot restate.
  const std::vector<std::vector<TagPayload>> movies{
      {{4, place1(9, 5, Matrix{})}, {1, {}}, {26, move2_character(5, 12, Matrix{}, std::nullopt)},
       {1, {}}},
      {{70, place3(5, 9, "", Matrix{}, 7)},
       {1, {}},
       {26, move2_character(5, 12, Matrix{}, std::nullopt)},
       {1, {}}},
      {{26, place2_clip_actions(5, 9, "hero")},
       {1, {}},
       {26, move2_character(5, 12, Matrix{}, std::string_view{"hero"})},
       {1, {}}},
  };
  const std::vector<std::string> expected{"not fully modeled", "not fully modeled", "clip actions"};
  for (std::size_t index = 0; index < movies.size(); ++index) {
    const auto source = movie_of(movies[index]);
    auto file = open(source);
    const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
    ASSERT_FALSE(targets.empty());
    fo4::swf::SwfDepthReassignResult result;
    EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, targets.front().instance_id, 9, result))
        << "movie " << index;
    EXPECT_NE(result.message.find(expected[index]), std::string::npos)
        << "movie " << index << ": " << result.message;
    EXPECT_EQ(fo4::swf::write_swf(file), source);
  }
}

TEST(SwfDepthReassign, RejectsASourceOrDestinationInsideAMaskRange) {
  // Constraint: a mask at depth 3 covers 4 to 6, so neither end may sit inside that range.
  const auto make = [](std::uint16_t source_depth) {
    return movie_of({{26, place2(3, 9, "mask", Matrix{}, static_cast<std::uint16_t>(6))},
                     {1, {}},
                     {26, place2(source_depth, 11, "hero", Matrix{}, std::nullopt)},
                     {1, {}}});
  };
  {
    auto file = open(make(5));
    const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
    const auto* hero = find_target(targets, "hero");
    ASSERT_NE(hero, nullptr);
    fo4::swf::SwfDepthReassignResult result;
    EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 9, result));
    EXPECT_NE(result.message.find("mask"), std::string::npos) << result.message;
  }
  {
    auto file = open(make(9));
    const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
    const auto* hero = find_target(targets, "hero");
    ASSERT_NE(hero, nullptr);
    fo4::swf::SwfDepthReassignResult result;
    EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 5, result));
    EXPECT_NE(result.message.find("mask"), std::string::npos) << result.message;
  }
}

TEST(SwfDepthReassign, RejectsAControlTagThatTargetsTheDestination) {
  const auto source = movie_of({{26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {28, remove2(7)},
                                {1, {}},
                                {26, place2(5, 12, "other", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);

  fo4::swf::SwfDepthReassignResult result;
  EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 7, result));
  EXPECT_NE(result.message.find("control tag"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfDepthReassign, RejectsADestinationOccupiedWhileTheObjectIsLive) {
  // Constraint: the occupier sits before the lifetime starts, so only the occupancy rule catches it.
  const auto source = movie_of({{26, place2(7, 11, "wall", Matrix{}, std::nullopt)},
                                {26, place2(5, 9, "hero", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, place2(5, 12, "other", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hero = find_target(targets, "hero");
  ASSERT_NE(hero, nullptr);

  fo4::swf::SwfDepthReassignResult result;
  EXPECT_FALSE(fo4::swf::reassign_instance_depth(file, 0, hero->instance_id, 7, result));
  EXPECT_NE(result.message.find("occupied"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfDepthReassign, SweepsEveryNamedRootInstanceOverEveryDepthUnderTheFrameOracle) {
  // Constraint: this must be deterministic, so the generator is seeded and every run sweeps the same movies.
  std::size_t successes = 0;
  std::size_t rejections = 0;
  std::size_t synthesized = 0;

  for (int seed = 0; seed < 24; ++seed) {
    SweepRng rng;
    rng.state = 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(seed) * 0x100000001b3ull);
    const auto source = movie_of(sweep_movie(rng));
    const auto pristine = open(source);
    const auto pristine_frames = root_frames(pristine);
    if (pristine_frames.empty()) {
      continue;
    }

    // Constraint: one candidate per created object, resolved at the first frame it appears on.
    std::vector<std::pair<std::uint32_t, SwfInstanceEditTarget>> candidates;
    std::set<std::size_t> seen;
    for (std::uint32_t frame = 0; frame < pristine_frames.size(); ++frame) {
      for (const auto& target : fo4::swf::build_instance_edit_targets(pristine, frame)) {
        if (target.inside_sprite || !seen.insert(target.create_tag_index).second) {
          continue;
        }
        candidates.emplace_back(frame, target);
      }
    }

    for (const auto& candidate : candidates) {
      const auto frame = candidate.first;
      const auto created = pristine.tags[candidate.second.create_tag_index].placement;
      if (!created.has_value() || !created->name.has_value()) {
        continue;
      }
      const auto depth = created->depth;
      const auto moved_name = *created->name;
      for (std::uint16_t destination = 1; destination <= 13; ++destination) {
        if (destination == depth) {
          continue;
        }
        auto post = open(source);
        fo4::swf::SwfDepthReassignResult result;
        if (fo4::swf::reassign_instance_depth(post, frame, candidate.second.instance_id,
                                              destination, result)) {
          ++successes;
          EXPECT_EQ(result.source_depth, depth);
          EXPECT_EQ(result.destination_depth, destination);
          EXPECT_FALSE(result.replaced_tag_indices.empty());
          EXPECT_GE(post.tags.size(), pristine.tags.size());
          EXPECT_LE(post.tags.size(), pristine.tags.size() + 1);
          if (post.tags.size() == pristine.tags.size() + 1) {
            ++synthesized;
          }
          const auto post_frames = root_frames(post);
          ASSERT_EQ(post_frames.size(), pristine_frames.size());
          // The whole movie must survive: every frame equals the original with one depth relabelled.
          for (std::size_t index = 0; index < pristine_frames.size(); ++index) {
            const auto before =
                frame_fingerprint(pristine_frames[index], moved_name, depth, destination);
            const auto after = frame_fingerprint(post_frames[index], moved_name, depth, destination);
            EXPECT_EQ(before, after) << "seed " << seed << " depth " << depth << " to "
                                     << destination << " frame " << index;
          }
        } else {
          ++rejections;
          EXPECT_FALSE(result.message.empty());
          EXPECT_TRUE(result.replaced_tag_indices.empty());
          EXPECT_EQ(fo4::swf::write_swf(post), source);
        }
      }
    }
  }

  EXPECT_GT(successes, 0u);
  EXPECT_GT(rejections, 0u);
  EXPECT_GT(synthesized, 0u);
}
