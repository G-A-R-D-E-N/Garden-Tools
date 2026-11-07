#include "fo4/lod/lod.hpp"

#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using fo4::lod::LodSettings;
using fo4::lod::LodTile;
using fo4::lod::LodTileKind;
using fo4::lod::LodTilePlacement;
using fo4::lod::LodTileStructure;
using fo4::nif::Transform;
using namespace nif_build;

namespace {

std::array<std::byte, 16> measured_settings() {
  const auto settings = LodSettings{-96, -96, 256, 4, 32};
  return fo4::lod::serialize_lod_settings(settings);
}

Block multi_bound_node_block(std::int32_t name, const std::vector<std::int32_t>& children,
                             std::int32_t bound) {
  Writer writer;
  writer.i32(name);
  writer.u32(0);
  writer.i32(-1);
  writer.u32(0);
  writer.transform(Transform{});
  writer.i32(-1);
  writer.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) writer.i32(child);
  writer.i32(bound);
  return Block{"BSMultiBoundNode", writer.bytes()};
}

Block multi_bound_block(std::int32_t data) {
  Writer writer;
  writer.i32(data);
  return Block{"BSMultiBound", writer.bytes()};
}

Block multi_bound_aabb_block() {
  Writer writer;
  for (int index = 0; index < 8; ++index) writer.f32(0.0f);
  return Block{"BSMultiBoundAABB", writer.bytes()};
}

Block tile_shape(std::string_view shape_type) {
  return shape_block(std::string{shape_type}, 0, Transform{}, k_full, 16, triangle_vertices(),
                     {0, 1, 2});
}

std::vector<std::byte> tile_nif(std::string_view root_type, std::string_view shape_type) {
  if (root_type == "BSMultiBoundNode") {
    return build_nif({"root"},
                     {multi_bound_node_block(0, {1}, 2), tile_shape(shape_type),
                      multi_bound_block(3), multi_bound_aabb_block()},
                     {0});
  }
  return build_nif({"root"},
                   {node_block("NiNode", Transform{}, {1, 2}), tile_shape(shape_type),
                    multi_bound_node_block(0, {}, 3), multi_bound_block(4),
                    multi_bound_aabb_block()},
                   {0});
}

LodTileStructure classify(std::vector<std::byte> bytes, LodTileKind kind) {
  return fo4::lod::check_lod_tile_structure(
      fo4::nif::read_nif(std::span<const std::byte>(bytes)), kind);
}

}  // namespace

// Detects: a lodsettings reader that must read the measured 16-byte field order.
TEST(Lod, ReadsTheSixteenByteSettingsRecord) {
  const auto bytes = measured_settings();
  const auto settings = fo4::lod::parse_lod_settings(bytes);
  EXPECT_EQ(settings, (LodSettings{-96, -96, 256, 4, 32}));
  EXPECT_EQ(fo4::lod::serialize_lod_settings(settings), bytes);
}

// Detects: a reader that must reject a lodsettings file of any other width.
TEST(Lod, RejectsSettingsOfAnyOtherWidth) {
  for (const std::size_t size : {std::size_t{0}, std::size_t{15}, std::size_t{17}, std::size_t{24}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(fo4::lod::parse_lod_settings(bytes), fo4::lod::LodError) << size;
  }
}

// Detects: a record that must refuse zero stride or an inverted level range.
TEST(Lod, RejectsNonEncodableSettings) {
  EXPECT_THROW(fo4::lod::serialize_lod_settings(LodSettings{0, 0, 0, 4, 32}),
               fo4::lod::LodError);
  EXPECT_THROW(fo4::lod::serialize_lod_settings(LodSettings{0, 0, 64, 0, 32}),
               fo4::lod::LodError);
  EXPECT_THROW(fo4::lod::serialize_lod_settings(LodSettings{0, 0, 64, 32, 4}),
               fo4::lod::LodError);
  auto bytes = measured_settings();
  bytes[4] = std::byte{0};
  bytes[5] = std::byte{0};
  bytes[6] = std::byte{0};
  bytes[7] = std::byte{0};
  EXPECT_THROW(fo4::lod::parse_lod_settings(bytes), fo4::lod::LodError);
}

// Detects: the lodsettings path helper must keep the measured lodsettings\<world>.lod convention.
TEST(Lod, LodSettingsPathAndWorldspaceRoundTrip) {
  const auto path = fo4::lod::lod_settings_path("Commonwealth");
  EXPECT_EQ(path, "lodsettings\\Commonwealth.lod");
  const auto world = fo4::lod::lod_settings_worldspace(path);
  ASSERT_TRUE(world.has_value());
  EXPECT_EQ(*world, "Commonwealth");
  EXPECT_EQ(fo4::lod::lod_settings_worldspace("lodsettings\\Commonwealth.LOD").value(),
            "Commonwealth");
  EXPECT_FALSE(fo4::lod::lod_settings_worldspace("lodsettings\\Commonwealth.dlodsettings")
                   .has_value());
  EXPECT_FALSE(fo4::lod::lod_settings_worldspace("lodsettings\\Commonwealth.lod.bak")
                   .has_value());
}

// Detects: a tile path parser that must read the measured four-part tile file name and kind.
TEST(Lod, ParsesVanillaStyleTilePaths) {
  const auto terrain = fo4::lod::parse_lod_tile_path(
      "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-25.-11.BTR");
  ASSERT_TRUE(terrain.has_value());
  EXPECT_EQ(*terrain, (LodTile{"Commonwealth", 16, -25, -11, LodTileKind::terrain}));

  const auto object = fo4::lod::parse_lod_tile_path(
      "meshes/terrain/DLC03FarHarbor/objects/DLC03FarHarbor.4.-9.5.bto");
  ASSERT_TRUE(object.has_value());
  EXPECT_EQ(*object, (LodTile{"DLC03FarHarbor", 4, -9, 5, LodTileKind::object}));

  for (const std::string_view bad : {"Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-25.BTR",
                                     "Meshes\\Terrain\\Commonwealth\\Commonwealth.0.1.1.BTR",
                                     "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.a.1.BTR",
                                     "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.1.1.BTT",
                                     "Commonwealth.16.1.1.dds"}) {
    EXPECT_FALSE(fo4::lod::parse_lod_tile_path(bad).has_value()) << bad;
  }
}

// Detects: a formatter or convention check that must produce the measured Meshes\Terrain layout.
TEST(Lod, FormatsTilePathsAndChecksTheMeasuredConvention) {
  const LodTile terrain{"Commonwealth", 16, -25, -11, LodTileKind::terrain};
  EXPECT_EQ(fo4::lod::format_lod_tile_path(terrain),
            "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-25.-11.BTR");
  const LodTile object{"DLC03FarHarbor", 4, -9, 5, LodTileKind::object};
  EXPECT_EQ(fo4::lod::format_lod_tile_path(object),
            "Meshes\\Terrain\\DLC03FarHarbor\\Objects\\DLC03FarHarbor.4.-9.5.BTO");
  EXPECT_TRUE(fo4::lod::lod_tile_path_follows_convention(
      "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-25.-11.BTR", terrain));
  EXPECT_TRUE(fo4::lod::lod_tile_path_follows_convention(
      "meshes/terrain/DLC03FarHarbor/Objects/DLC03FarHarbor.4.-9.5.BTO", object));
  EXPECT_FALSE(fo4::lod::lod_tile_path_follows_convention(
      "Meshes\\Terrain\\DLC03FarHarbor\\DLC03FarHarbor.4.-9.5.BTO", object));
  EXPECT_FALSE(fo4::lod::lod_tile_path_follows_convention(
      "Meshes\\Terrain\\DLC03FarHarbor\\Objects\\Other.4.-9.5.BTO", object));
}

// Detects: a placement check that must align tiles to the southwest cell block grid.
TEST(Lod, ChecksTilePlacementAgainstTheSouthwestOrigin) {
  const auto bytes = measured_settings();
  const auto settings = fo4::lod::parse_lod_settings(bytes);
  EXPECT_EQ(fo4::lod::check_lod_tile_placement(
                settings, LodTile{"Commonwealth", 16, -80, -80, LodTileKind::terrain}),
            LodTilePlacement::ok);
  EXPECT_EQ(fo4::lod::check_lod_tile_placement(
                settings, LodTile{"Commonwealth", 16, -79, -80, LodTileKind::terrain}),
            LodTilePlacement::not_block_aligned);
  EXPECT_EQ(fo4::lod::check_lod_tile_placement(
                settings, LodTile{"Commonwealth", 16, -80, -81, LodTileKind::terrain}),
            LodTilePlacement::not_block_aligned);
  EXPECT_EQ(fo4::lod::check_lod_tile_placement(
                settings, LodTile{"Commonwealth", 2, -96, -96, LodTileKind::terrain}),
            LodTilePlacement::level_out_of_range);
  const auto permissive = LodSettings{0, 0, 64, 1, 32};
  EXPECT_EQ(fo4::lod::check_lod_tile_placement(
                permissive, LodTile{"Commonwealth", 3, 0, 0, LodTileKind::terrain}),
            LodTilePlacement::level_not_power_of_two);
}

// Detects: a tile structure check that must accept the measured vanilla root and shape blocks.
TEST(Lod, AcceptsVanillaStyleTileStructures) {
  EXPECT_EQ(classify(tile_nif("NiNode", "BSSubIndexTriShape"), LodTileKind::object),
            LodTileStructure::ok);
  EXPECT_EQ(classify(tile_nif("BSMultiBoundNode", "BSTriShape"), LodTileKind::terrain),
            LodTileStructure::ok);
  EXPECT_EQ(classify(tile_nif("BSMultiBoundNode", "BSSubIndexTriShape"), LodTileKind::terrain),
            LodTileStructure::ok);
}

// Detects: a structure check that must refuse the wrong root, older geometry and an empty tile.
TEST(Lod, RejectsTileStructuresWithTheWrongRootOrShape) {
  EXPECT_EQ(classify(tile_nif("NiNode", "BSSubIndexTriShape"), LodTileKind::terrain),
            LodTileStructure::root_mismatch);
  EXPECT_EQ(classify(tile_nif("BSMultiBoundNode", "BSTriShape"), LodTileKind::object),
            LodTileStructure::root_mismatch);
  EXPECT_EQ(classify(tile_nif("NiNode", "BSTriShape"), LodTileKind::object),
            LodTileStructure::shape_type_mismatch);
  EXPECT_EQ(classify(tile_nif("NiNode", "NiTriShape"), LodTileKind::object),
            LodTileStructure::shape_type_mismatch);
  EXPECT_EQ(classify(build_nif({"root"}, {node_block("NiNode", Transform{}, {})}, {0}),
                     LodTileKind::object),
            LodTileStructure::no_shapes);
  EXPECT_EQ(classify(build_nif({"root"}, {node_block("NiNode", Transform{}, {})}, {}),
                     LodTileKind::object),
            LodTileStructure::root_count);
  EXPECT_EQ(classify(build_nif({"a", "b"},
                               {node_block("NiNode", Transform{}, {}),
                                node_block("NiNode", Transform{}, {})},
                               {0, 1}),
                     LodTileKind::object),
            LodTileStructure::root_count);
}

// Detects: a tile that must carry the measured multi-bound chain, because every shipped tile does.
TEST(Lod, RejectsATileWithoutTheMultiBoundChain) {
  const auto bare = build_nif({"root"}, {node_block("NiNode", Transform{}, {1}), tile_shape("BSSubIndexTriShape")}, {0});
  EXPECT_EQ(classify(bare, LodTileKind::object), LodTileStructure::missing_bound_chain);
}

// Detects: a multi-bound node whose reference must name a BSMultiBound block instead of any other block.
TEST(Lod, RejectsABoundReferenceThatIsNotABound) {
  const auto wrong = build_nif({"root"},
                               {node_block("NiNode", Transform{}, {1, 2}), tile_shape("BSSubIndexTriShape"),
                                multi_bound_node_block(0, {}, 1), multi_bound_block(4),
                                multi_bound_aabb_block()},
                               {0});
  EXPECT_EQ(classify(wrong, LodTileKind::object), LodTileStructure::bound_reference_mismatch);
}

// Detects: a BSMultiBound whose data must name a BSMultiBoundAABB block instead of any other block.
TEST(Lod, RejectsABoundWhoseDataIsNotAnAabb) {
  const auto wrong = build_nif({"root"},
                               {node_block("NiNode", Transform{}, {1, 2}), tile_shape("BSSubIndexTriShape"),
                                multi_bound_node_block(0, {}, 3), multi_bound_block(1),
                                multi_bound_aabb_block()},
                               {0});
  EXPECT_EQ(classify(wrong, LodTileKind::object), LodTileStructure::bound_reference_mismatch);
}
