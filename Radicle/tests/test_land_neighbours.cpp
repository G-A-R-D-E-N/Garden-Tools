#include "fo4/environment/environment.hpp"
#include "fo4/forms/land_edit.hpp"
#include "fo4/land/heightmap.hpp"
#include "fo4/plugin/reader.hpp"
#include "../src/forms/land_normals.hpp"
#include "env_node_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace {

namespace land = fo4::land;
constexpr std::size_t k_side = land::k_heightmap_side;
constexpr float k_base = 80.0f;

using Heights = std::vector<float>;

struct CellSpec {
  std::uint32_t id;
  std::uint32_t x;
  std::uint32_t y;
  Heights heights;
  std::vector<std::byte> vnml;
};

Heights terrain(float (*units)(std::size_t, std::size_t)) {
  Heights heights(land::k_heightmap_vertices);
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      heights[row * k_side + column] = k_base + 8.0f * units(row, column);
    }
  }
  return heights;
}

Heights flat() { return Heights(land::k_heightmap_vertices, k_base); }

std::vector<std::byte> vhgt_of(const Heights& heights) {
  const auto map = land::make_land_heightmap(heights, 10.0f);
  EXPECT_TRUE(map.has_value());
  const auto bytes = land::encode_land_heightmap(*map);
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::vector<std::byte> stale_vnml(std::uint8_t seed) {
  std::vector<std::byte> vnml(land::k_normals_size);
  for (std::size_t index = 0; index < vnml.size(); ++index) {
    vnml[index] = static_cast<std::byte>((index * 7 + seed) % 251);
  }
  return vnml;
}

std::vector<std::byte> vclr_of(std::uint8_t seed) {
  std::vector<std::byte> vclr(land::k_normals_size);
  for (std::size_t index = 0; index < vclr.size(); ++index) {
    vclr[index] = static_cast<std::byte>((index * 11 + seed) % 241);
  }
  return vclr;
}

std::uint32_t land_id(std::uint32_t cell) { return cell + 0x8000; }

struct World {
  fo4::test::TempDataDir dir;
  std::optional<fo4::env::Environment> env;
  explicit World(const std::vector<CellSpec>& cells) {
    using namespace fo4::test::envnode;
    std::vector<Node> block;
    for (const auto& cell : cells) {
      block.push_back(rec("CELL", cell.id, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({cell.x, cell.y, 0})}}));
      block.push_back(grp(cell.id, 6, {grp(cell.id, 9, {rec("LAND", land_id(cell.id),
                                                          {{"DATA", bytes_of({0x0D})},
                                                           {"VNML", cell.vnml},
                                                           {"VHGT", vhgt_of(cell.heights)},
                                                           {"VCLR", vclr_of(static_cast<std::uint8_t>(cell.id))}})})}));
    }
    dir.write("Base.esm",
              make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                            grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(block))})})})}),
              0);
    env.emplace(fo4::env::Environment::load(std::vector<std::filesystem::path>{dir.path() / "Base.esm"}));
  }
};

fo4::Plugin new_patch() {
  return fo4::read_plugin(fo4::test::envnode::make_plugin({}, {}), "Edit.esp");
}

fo4::FormKey cell_key(std::uint32_t id) {
  char text[32];
  std::snprintf(text, sizeof(text), "%06X:Base.esm", id);
  return fo4::FormKey::parse(text);
}

const fo4::Record* land_of(const fo4::Plugin& patch, std::uint32_t cell) {
  return fo4::find_record(patch, cell_key(land_id(cell)));
}

const fo4::Subrecord* sub_of(const fo4::Record& record, std::string_view name) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == name) return &sub;
  }
  return nullptr;
}

std::array<int, 3> stored_normal(const fo4::Record& record, std::size_t row, std::size_t column) {
  const auto& data = sub_of(record, "VNML")->data;
  const auto base = (row * k_side + column) * 3;
  return {static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base])),
          static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base + 1])),
          static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base + 2]))};
}

std::array<int, 3> shipped_normal(const std::vector<std::byte>& data, std::size_t row, std::size_t column) {
  const auto base = (row * k_side + column) * 3;
  return {static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base])),
          static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base + 1])),
          static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[base + 2]))};
}

Heights stored_heights(const fo4::Record& record) {
  const auto heights = land::land_heights(land::decode_land_heightmap(sub_of(record, "VHGT")->data));
  return Heights(heights.begin(), heights.end());
}

using Field = std::function<double(long, long)>;

// Reference CK triangle-list normal over one combined height field, written independently of compute_land_normals (IEEE sqrt instead of the CK's table).
std::array<int, 3> reference_normal(const Field& height, long row, long column) {
  using Corner = std::array<long, 2>;
  static const std::array<std::array<Corner, 3>, 2> even = {{{{{1, 1}, {1, 0}, {0, 0}}}, {{{0, 0}, {0, 1}, {1, 1}}}}};
  static const std::array<std::array<Corner, 3>, 2> odd = {{{{{1, 0}, {0, 0}, {0, 1}}}, {{{0, 1}, {1, 1}, {1, 0}}}}};
  double sum[3] = {0.0, 0.0, 0.0};
  for (long quad_row = row - 1; quad_row <= row; ++quad_row) {
    for (long quad_column = column - 1; quad_column <= column; ++quad_column) {
      for (const auto& triangle : ((quad_row ^ quad_column) & 1) != 0 ? odd : even) {
        bool uses_vertex = false;
        double p[3][3];
        for (std::size_t k = 0; k < 3; ++k) {
          uses_vertex = uses_vertex || (quad_row + triangle[k][0] == row && quad_column + triangle[k][1] == column);
          p[k][0] = 128.0 * static_cast<double>(triangle[k][1]);
          p[k][1] = 128.0 * static_cast<double>(triangle[k][0]);
          p[k][2] = height(quad_row + triangle[k][0], quad_column + triangle[k][1]);
        }
        if (!uses_vertex) continue;
        double d1[3], d2[3];
        for (std::size_t axis = 0; axis < 3; ++axis) {
          d1[axis] = p[1][axis] - p[0][axis];
          d2[axis] = p[2][axis] - p[1][axis];
        }
        const double face[3] = {d1[1] * d2[2] - d1[2] * d2[1], d1[2] * d2[0] - d1[0] * d2[2], d1[0] * d2[1] - d1[1] * d2[0]};
        const double length = std::sqrt(face[0] * face[0] + face[1] * face[1] + face[2] * face[2]);
        for (std::size_t axis = 0; axis < 3; ++axis) sum[axis] += face[axis] / length;
      }
    }
  }
  const double length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
  return {static_cast<int>(std::trunc(sum[0] / length * 127.0)), static_cast<int>(std::trunc(sum[1] / length * 127.0)),
          static_cast<int>(std::trunc(sum[2] / length * 127.0))};
}

// The CK's sqrt table is up to 0.39 % off, which moves a truncated byte by at most one.
bool within_one_byte(const std::array<int, 3>& stored, const std::array<int, 3>& expected) {
  return std::abs(stored[0] - expected[0]) <= 1 && std::abs(stored[1] - expected[1]) <= 1 && std::abs(stored[2] - expected[2]) <= 1;
}

float at(const Heights& heights, long row, long column) {
  return heights[static_cast<std::size_t>(row) * k_side + static_cast<std::size_t>(column)];
}

float rough_units(std::size_t row, std::size_t column) {
  return static_cast<float>((row * 3 + column * 5) % 7);
}

float rougher_units(std::size_t row, std::size_t column) {
  return static_cast<float>((row * 2 + column * 3) % 5);
}

Heights raised(const Heights& heights, float (*extra)(std::size_t, std::size_t)) {
  Heights out = heights;
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) out[row * k_side + column] += 8.0f * extra(row, column);
  }
  return out;
}

float wave_units(std::size_t row, std::size_t column) {
  return static_cast<float>((row + 2 * column) % 3 + 1);
}

std::vector<std::byte> bytes_in(const fo4::Record& record, std::string_view name) {
  return sub_of(record, name)->data;
}

}  // namespace

// Detects: an edit that rewrites only the edited LAND, leaving the east neighbour's shared column and its two nearest normal columns stale.
TEST(LandNeighbours, AnEastNeighbourGainsTheSharedColumnHeightsAndTheNormalsThatReadThem) {
  const auto a_heights = terrain(rough_units);
  auto b_heights = terrain(rougher_units);
  for (std::size_t row = 0; row < k_side; ++row) b_heights[row * k_side] = a_heights[row * k_side + 32];
  const auto b_vnml = stale_vnml(3);
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)}, {0x807, 3, 3, b_heights, b_vnml}});
  auto patch = new_patch();
  const auto edited = raised(a_heights, wave_units);

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), edited);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 1u);
  EXPECT_EQ(result.neighbours_skipped, 7u);

  const auto* a = land_of(patch, 0x806);
  const auto* b = land_of(patch, 0x807);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  const auto b_after = stored_heights(*b);
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      const float expected = column == 0 ? edited[row * k_side + 32] : b_heights[row * k_side + column];
      EXPECT_EQ(b_after[row * k_side + column], expected) << row << "," << column;
    }
  }

  const Field field = [&](long row, long column) -> double {
    return column <= 32 ? at(edited, row, column) : at(b_after, row, column - 32);
  };
  for (std::size_t row = 1; row + 1 < k_side; ++row) {
    for (std::size_t column = 0; column < 2; ++column) {
      EXPECT_TRUE(within_one_byte(stored_normal(*b, row, column), reference_normal(field, static_cast<long>(row), static_cast<long>(32 + column))))
          << row << "," << column;
    }
    EXPECT_EQ(stored_normal(*b, row, 0), stored_normal(*a, row, 32)) << row;
  }
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 2; column < k_side; ++column) {
      EXPECT_EQ(stored_normal(*b, row, column), shipped_normal(b_vnml, row, column)) << row << "," << column;
    }
  }
  EXPECT_NE(stored_normal(*b, 10, 0), shipped_normal(b_vnml, 10, 0));
  EXPECT_EQ(bytes_in(*b, "DATA"), fo4::test::envnode::bytes_of({0x0D}));
  EXPECT_EQ(bytes_in(*b, "VCLR"), vclr_of(0x07));
  ASSERT_EQ(b->subrecords.size(), 4u);
  EXPECT_EQ(b->subrecords[0].signature.to_string(), "DATA");
  EXPECT_EQ(b->subrecords[1].signature.to_string(), "VNML");
  EXPECT_EQ(b->subrecords[2].signature.to_string(), "VHGT");
  EXPECT_EQ(b->subrecords[3].signature.to_string(), "VCLR");
}

// Detects: a diagonal neighbour that is ignored or that gets more than the 2 x 2 vertices around the shared corner.
TEST(LandNeighbours, ADiagonalNeighbourGainsOnlyTheSharedCornerVertex) {
  const auto a_heights = flat();
  const auto ne_heights = flat();
  const auto ne_vnml = stale_vnml(9);
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)}, {0x808, 3, 4, ne_heights, ne_vnml}});
  auto patch = new_patch();
  auto edited = a_heights;
  edited[32 * k_side + 32] += 16.0f;

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), edited);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 1u);
  EXPECT_EQ(result.neighbours_skipped, 2u);

  const auto* ne = land_of(patch, 0x808);
  ASSERT_NE(ne, nullptr);
  const auto heights = stored_heights(*ne);
  for (std::size_t index = 0; index < heights.size(); ++index) {
    EXPECT_EQ(heights[index], index == 0 ? k_base + 16.0f : k_base) << index;
  }
  auto ne_after = ne_heights;
  ne_after[0] = k_base + 16.0f;
  land::HeightGrid edited_grid{};
  std::copy(edited.begin(), edited.end(), edited_grid.begin());
  land::BorderHeights corner_pad;
  corner_pad.south_west = &edited_grid;
  const auto expected = land::compute_land_normals(ne_after, corner_pad);
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      const auto base = (row * k_side + column) * 3;
      const bool near_corner = row < 2 && column < 2;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const int stored = stored_normal(*ne, row, column)[axis];
        EXPECT_EQ(stored, near_corner ? static_cast<int>(expected[base + axis]) : shipped_normal(ne_vnml, row, column)[axis])
            << row << "," << column;
      }
    }
  }
  EXPECT_NE(stored_normal(*ne, 0, 0), shipped_normal(ne_vnml, 0, 0));
}

// Detects: the edited cell's corner vertex padded by extrapolation although the diagonal neighbour's LAND is available.
TEST(LandNeighbours, TheEditedCellsCornerNormalReadsTheDiagonalNeighbourLand) {
  auto ne_heights = flat();
  ne_heights[k_side + 1] += 480.0f;
  World world({{0x806, 2, 3, flat(), stale_vnml(5)}, {0x808, 3, 4, ne_heights, stale_vnml(9)}});
  auto patch = new_patch();
  auto edited = flat();
  edited[31 * k_side + 31] += 16.0f;

  ASSERT_EQ(fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), edited).status, fo4::forms::LandEditStatus::edited);

  land::HeightGrid ne_grid{};
  std::copy(ne_heights.begin(), ne_heights.end(), ne_grid.begin());
  land::BorderHeights corner_pad;
  corner_pad.north_east = &ne_grid;
  const auto with_corner = land::compute_land_normals(edited, corner_pad);
  const auto without_corner = land::compute_land_normals(edited);
  const auto base = (32 * k_side + 32) * 3;
  ASSERT_TRUE(!std::equal(with_corner.begin() + base, with_corner.begin() + base + 3, without_corner.begin() + base));
  const auto stored = stored_normal(*land_of(patch, 0x806), 32, 32);
  for (std::size_t axis = 0; axis < 3; ++axis) EXPECT_EQ(stored[axis], static_cast<int>(with_corner[base + axis])) << axis;
}

// Detects: a neighbour copied although no cell shares the edited cell's edges, or an unrelated cell pulled into the patch.
TEST(LandNeighbours, NoNeighbourMeansNothingExtraIsCopied) {
  World world({{0x806, 2, 3, flat(), stale_vnml(5)}, {0x807, 5, 3, flat(), stale_vnml(3)}});
  auto patch = new_patch();

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), terrain(rough_units));
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 0u);
  EXPECT_NE(land_of(patch, 0x806), nullptr);
  EXPECT_EQ(land_of(patch, 0x807), nullptr);
  EXPECT_EQ(fo4::find_record(patch, cell_key(0x807)), nullptr);
}

// Detects: an edit that leaves the heights as they were still copying neighbours, which must stay untouched.
TEST(LandNeighbours, AnUnchangedEditTouchesNoNeighbour) {
  const auto a_heights = terrain(rough_units);
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)}, {0x807, 3, 3, terrain(rougher_units), stale_vnml(3)}});
  auto patch = new_patch();

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), a_heights);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 0u);
  EXPECT_EQ(result.neighbours_skipped, 0u);
  EXPECT_EQ(land_of(patch, 0x807), nullptr);
}

// Detects: stale neighbour normals kept on every side; west, north and south neighbours must follow the same rule as the east one.
TEST(LandNeighbours, WestNorthAndSouthNeighboursAgreeWithTheCombinedHeightField) {
  const auto a_heights = terrain(rough_units);
  auto west = terrain(rougher_units);
  auto north = terrain(rougher_units);
  auto south = terrain(rougher_units);
  for (std::size_t index = 0; index < k_side; ++index) {
    west[index * k_side + 32] = a_heights[index * k_side];
    north[index] = a_heights[32 * k_side + index];
    south[32 * k_side + index] = a_heights[index];
  }
  std::vector<std::byte> straight_up(land::k_normals_size);
  for (std::size_t index = 2; index < straight_up.size(); index += 3) straight_up[index] = std::byte{127};
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)},
               {0x807, 1, 3, west, straight_up},
               {0x808, 2, 4, north, straight_up},
               {0x809, 2, 2, south, straight_up}});
  auto patch = new_patch();
  const auto edited = raised(a_heights, wave_units);

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), edited);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 3u);

  const auto* a = land_of(patch, 0x806);
  const auto* w = land_of(patch, 0x807);
  const auto* n = land_of(patch, 0x808);
  const auto* s = land_of(patch, 0x809);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(w, nullptr);
  ASSERT_NE(n, nullptr);
  ASSERT_NE(s, nullptr);
  const auto w_h = stored_heights(*w);
  const auto n_h = stored_heights(*n);
  const auto s_h = stored_heights(*s);
  for (std::size_t index = 0; index < k_side; ++index) {
    EXPECT_EQ(w_h[index * k_side + 32], edited[index * k_side]) << index;
    EXPECT_EQ(n_h[index], edited[32 * k_side + index]) << index;
    EXPECT_EQ(s_h[32 * k_side + index], edited[index]) << index;
  }

  const Field horizontal = [&](long row, long column) -> double {
    return column <= 32 ? at(w_h, row, column) : at(edited, row, column - 32);
  };
  const Field up_field = [&](long row, long column) -> double {
    return row <= 32 ? at(edited, row, column) : at(n_h, row - 32, column);
  };
  const Field down_field = [&](long row, long column) -> double {
    return row <= 32 ? at(s_h, row, column) : at(edited, row - 32, column);
  };
  for (std::size_t line = 1; line + 1 < k_side; ++line) {
    for (const std::size_t offset : {std::size_t{31}, std::size_t{32}}) {
      EXPECT_TRUE(within_one_byte(stored_normal(*w, line, offset), reference_normal(horizontal, static_cast<long>(line), static_cast<long>(offset))))
          << "west " << line << "," << offset;
    }
    for (const std::size_t offset : {std::size_t{0}, std::size_t{1}}) {
      EXPECT_TRUE(within_one_byte(stored_normal(*n, offset, line), reference_normal(up_field, static_cast<long>(32 + offset), static_cast<long>(line))))
          << "north " << offset << "," << line;
    }
    for (const std::size_t offset : {std::size_t{31}, std::size_t{32}}) {
      EXPECT_TRUE(within_one_byte(stored_normal(*s, offset, line), reference_normal(down_field, static_cast<long>(offset), static_cast<long>(line))))
          << "south " << offset << "," << line;
    }
  }
}

// Detects: a neighbour that already sits in the patch being copied a second time instead of rewritten in place.
TEST(LandNeighbours, ANeighbourAlreadyInThePatchIsRewrittenInPlace) {
  const auto a_heights = terrain(rough_units);
  auto b_heights = terrain(rougher_units);
  for (std::size_t row = 0; row < k_side; ++row) b_heights[row * k_side] = a_heights[row * k_side + 32];
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)}, {0x807, 3, 3, b_heights, stale_vnml(3)}});
  auto patch = new_patch();
  ASSERT_EQ(fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x807), b_heights).status, fo4::forms::LandEditStatus::edited);
  ASSERT_NE(land_of(patch, 0x807), nullptr);
  const auto before = bytes_in(*land_of(patch, 0x807), "VNML");

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), raised(a_heights, wave_units));
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 1u);
  std::size_t lands = 0;
  for (const auto& group : patch.groups) {
    std::function<void(const fo4::Group&)> walk = [&](const fo4::Group& g) {
      for (const auto& child : g.children) {
        if (const auto* record = std::get_if<fo4::Record>(&child)) {
          if (record->header.signature == fo4::Signature::from_string("LAND") && record->form_key == cell_key(land_id(0x807))) ++lands;
        } else {
          walk(std::get<fo4::Group>(child));
        }
      }
    };
    walk(group);
  }
  EXPECT_EQ(lands, 1u);
  EXPECT_NE(bytes_in(*land_of(patch, 0x807), "VNML"), before);
}

// Detects: a neighbour whose shared heights cannot be encoded aborting the edit or leaving it half written.
TEST(LandNeighbours, ANeighbourThatCannotEncodeTheSharedEdgeIsSkippedAndCounted) {
  const auto a_heights = flat();
  Heights b_heights = flat();
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 1; column < k_side; ++column) b_heights[row * k_side + column] = k_base - 8.0f * 29.0f;
  }
  b_heights[0] = k_base;
  World world({{0x806, 2, 3, a_heights, stale_vnml(5)}, {0x807, 3, 3, b_heights, stale_vnml(3)}});
  auto patch = new_patch();
  auto edited = a_heights;
  for (std::size_t row = 0; row < k_side; ++row) edited[row * k_side + 32] = k_base + 8.0f * 100.0f;

  const auto result = fo4::forms::edit_land_heights(patch, *world.env, cell_key(0x806), edited);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.neighbours_updated, 0u);
  EXPECT_EQ(result.neighbours_skipped, 5u);
  EXPECT_NE(land_of(patch, 0x806), nullptr);
  EXPECT_EQ(land_of(patch, 0x807), nullptr);
}
