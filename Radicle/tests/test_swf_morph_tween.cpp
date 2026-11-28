#include "swf_art_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>

using namespace swf_art_test;

TEST(SwfMorph, InterpolatesTheEdgeStreamsAtARatio) {
  const auto result = read_one(84, morph_payload(true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;

  const auto quarter = fo4::swf::interpolate_morph_commands(morph, 0.25);
  ASSERT_EQ(quarter.size(), 2u);
  EXPECT_EQ(quarter[0].kind, fo4::swf::SwfShapeRecordKind::move);
  EXPECT_EQ(quarter[0].x, 1);
  EXPECT_EQ(quarter[0].y, 1);
  EXPECT_EQ(quarter[1].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(quarter[1].x, 2);
  EXPECT_EQ(quarter[1].y, 3);

  const auto start = fo4::swf::interpolate_morph_commands(morph, -1.0);
  ASSERT_EQ(start.size(), 2u);
  EXPECT_EQ(start[1].x, 4);
  EXPECT_EQ(start[1].y, 6);
  const auto end = fo4::swf::interpolate_morph_commands(morph, 2.0);
  ASSERT_EQ(end.size(), 2u);
  EXPECT_EQ(end[1].x, -4);
  EXPECT_EQ(end[1].y, -6);
}

TEST(SwfMorph, InterpolationPairsGeometryAcrossTheStartStyles) {
  fo4::swf::SwfMorphShape morph;
  fo4::swf::SwfShapeRecord move{fo4::swf::SwfShapeRecordKind::move};
  move.x = 1;
  move.y = 2;
  fo4::swf::SwfShapeRecord style{fo4::swf::SwfShapeRecordKind::style_change};
  style.fill0 = 2;
  style.fill1 = 3;
  style.line = 1;
  fo4::swf::SwfShapeRecord edge{fo4::swf::SwfShapeRecordKind::straight_edge};
  edge.x = 4;
  edge.y = 6;
  fo4::swf::SwfShapeRecord end_move{fo4::swf::SwfShapeRecordKind::move};
  end_move.x = -1;
  end_move.y = -2;
  fo4::swf::SwfShapeRecord end_edge{fo4::swf::SwfShapeRecordKind::straight_edge};
  end_edge.x = -4;
  end_edge.y = -6;
  morph.start_records = {move, style, edge};
  morph.end_records = {end_move, end_edge};

  const auto blended = fo4::swf::interpolate_morph_commands(morph, 0.5);
  ASSERT_EQ(blended.size(), 3u);
  EXPECT_EQ(blended[0].kind, fo4::swf::SwfShapeRecordKind::move);
  EXPECT_EQ(blended[0].x, 0);
  EXPECT_EQ(blended[0].y, 0);
  EXPECT_EQ(blended[1].kind, fo4::swf::SwfShapeRecordKind::style_change);
  EXPECT_EQ(blended[1].fill0, 2);
  EXPECT_EQ(blended[1].fill1, 3);
  EXPECT_EQ(blended[1].line, 1);
  EXPECT_EQ(blended[2].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(blended[2].x, 0);
  EXPECT_EQ(blended[2].y, 0);

  morph.end_records.pop_back();
  EXPECT_EQ(fo4::swf::interpolate_morph_commands(morph, 0.5).size(), 2u);
}

TEST(SwfMorph, InterpolationPromotesAnEdgeThatChangesType) {
  fo4::swf::SwfMorphShape morph;
  fo4::swf::SwfShapeRecord straight{fo4::swf::SwfShapeRecordKind::straight_edge};
  straight.x = 20;
  straight.y = 10;
  fo4::swf::SwfShapeRecord curve{fo4::swf::SwfShapeRecordKind::curved_edge};
  curve.control_x = -20;
  curve.control_y = -10;
  curve.x = 20;
  curve.y = 10;
  morph.start_records = {straight};
  morph.end_records = {curve};

  const auto halfway = fo4::swf::interpolate_morph_commands(morph, 0.5);
  ASSERT_EQ(halfway.size(), 1u);
  EXPECT_EQ(halfway[0].kind, fo4::swf::SwfShapeRecordKind::curved_edge);
  EXPECT_EQ(halfway[0].control_x, -5);
  EXPECT_EQ(halfway[0].control_y, -3);
  EXPECT_EQ(halfway[0].x, 15);
  EXPECT_EQ(halfway[0].y, 8);

  const auto from = fo4::swf::interpolate_morph_commands(morph, 0.0);
  ASSERT_EQ(from.size(), 1u);
  EXPECT_EQ(from[0].control_x, 10);
  EXPECT_EQ(from[0].control_y, 5);
  EXPECT_EQ(from[0].x, 10);
  EXPECT_EQ(from[0].y, 5);
  const auto to = fo4::swf::interpolate_morph_commands(morph, 1.0);
  ASSERT_EQ(to.size(), 1u);
  EXPECT_EQ(to[0].control_x, -20);
  EXPECT_EQ(to[0].control_y, -10);
  EXPECT_EQ(to[0].x, 20);
  EXPECT_EQ(to[0].y, 10);
}
