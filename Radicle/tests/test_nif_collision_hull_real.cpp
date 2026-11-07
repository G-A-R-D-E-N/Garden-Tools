#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_build.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_max_nifs = 400;

bool has_extension(const std::string& path, std::string_view extension) {
  return std::filesystem::path(path).extension() == extension;
}

struct Bounds {
  std::array<double, 3> low{{0.0, 0.0, 0.0}};
  std::array<double, 3> high{{0.0, 0.0, 0.0}};
  bool valid{false};
};

Bounds bounds_of(const std::vector<fo4::nif::HknpVec3>& points) {
  Bounds out;
  if (points.empty()) return out;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    out.low[axis] = points.front()[axis];
    out.high[axis] = points.front()[axis];
  }
  out.valid = true;
  for (const auto& point : points) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      out.low[axis] = std::min(out.low[axis], static_cast<double>(point[axis]));
      out.high[axis] = std::max(out.high[axis], static_cast<double>(point[axis]));
    }
  }
  return out;
}

Bounds bounds_of_mesh(const std::vector<float>& verts, const std::vector<std::uint16_t>& tris, double units_per_metre) {
  std::vector<bool> referenced(verts.size() / 3, false);
  for (const auto index : tris) {
    if (index < referenced.size()) referenced[index] = true;
  }
  std::vector<fo4::nif::HknpVec3> points;
  for (std::size_t v = 0; v < referenced.size(); ++v) {
    if (!referenced[v]) continue;
    points.push_back(fo4::nif::HknpVec3{static_cast<float>(verts[3 * v] / units_per_metre),
                                        static_cast<float>(verts[3 * v + 1] / units_per_metre),
                                        static_cast<float>(verts[3 * v + 2] / units_per_metre)});
  }
  return bounds_of(points);
}

double diagonal_of(const Bounds& bounds) {
  double sum = 0.0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double span = bounds.high[axis] - bounds.low[axis];
    sum += span * span;
  }
  return std::sqrt(sum);
}

}  // namespace

TEST(NifCollisionReal, HullBuiltFromMeshCapturesTheMeshExtentsAndEncodesBack) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout 4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout 4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  const double units_per_metre = static_cast<double>(fo4::nif::k_hknp_game_units_per_metre);

  std::size_t paired = 0;
  std::size_t measured = 0;
  std::size_t nifs = 0;
  double worst_round_trip = 0.0;
  double worst_vanilla = 0.0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && nifs < k_max_nifs; ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        const auto blobs = fo4::nif::read_collision_blobs(nif);
        if (blobs.empty()) continue;
        Bounds vanilla;
        for (const auto& blob : blobs) {
          try {
            const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
            for (const auto& shape : system.shapes) {
              if (shape.kind != fo4::nif::HknpShapeKind::convex || shape.vertices.empty()) continue;
              vanilla = bounds_of(shape.vertices);
              break;
            }
          } catch (const fo4::parse_error&) {
            continue;
          }
          if (vanilla.valid) break;
        }
        if (!vanilla.valid) continue;
        const auto collision_block = blobs.front().block;
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          if (shape.verts.size() < 12 || shape.tris.size() < 12) continue;
          ++paired;
          const auto mesh = bounds_of_mesh(shape.verts, shape.tris, units_per_metre);
          const double mesh_diagonal = diagonal_of(mesh);
          if (!(mesh_diagonal > 0.0)) continue;
          fo4::nif::MeshHull hull;
          try {
            hull = fo4::nif::build_convex_hull_from_mesh(shape.verts, shape.tris, 0,
                                                         static_cast<float>(1e-3 * mesh_diagonal * units_per_metre));
          } catch (const std::invalid_argument&) {
            continue;
          }
          const auto hull_bounds = bounds_of(hull.shape.vertices);
          ASSERT_TRUE(hull_bounds.valid) << path;
          EXPECT_EQ(hull.shape.convex_radius, 0.0f) << path;
          EXPECT_GT(hull.nearest_face_distance_metres, 0.0f) << path << ": the hull has no vertex off its face planes";
          for (std::size_t axis = 0; axis < 3; ++axis) {
            const double slack = 1e-3 * mesh_diagonal;
            EXPECT_NEAR(hull_bounds.low[axis], mesh.low[axis], slack) << path << ": hull lost the mesh low extreme";
            EXPECT_NEAR(hull_bounds.high[axis], mesh.high[axis], slack) << path << ": hull lost the mesh high extreme";
          }
          const auto again = fo4::nif::build_convex_hull_from_mesh(shape.verts, shape.tris, 0,
                                                                    static_cast<float>(1e-3 * mesh_diagonal * units_per_metre));
          EXPECT_EQ(again.shape.vertices, hull.shape.vertices) << path << ": hull build is not deterministic";
          EXPECT_EQ(again.face_indices, hull.face_indices) << path << ": hull faces are not deterministic";
          fo4::nif::HknpStaticBody body;
          const auto encoded = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(hull.shape, body));
          ASSERT_FALSE(encoded.empty()) << path;
          const auto pack = fo4::nif::read_hknp_packfile(encoded);
          EXPECT_EQ(fo4::nif::write_hknp_packfile(pack), encoded) << path << ": our encoder is not byte stable";
          const auto decoded = fo4::nif::decode_hknp_system(pack);
          bool found = false;
          for (const auto& built : decoded.shapes) {
            if (built.kind != fo4::nif::HknpShapeKind::convex) continue;
            EXPECT_EQ(built.vertices.size(), hull.shape.vertices.size()) << path;
            EXPECT_EQ(built.convex_radius, 0.0f) << path << ": the decoder read back a non-zero radius";
            const auto decoded_bounds = bounds_of(built.vertices);
            const double diagonal = diagonal_of(hull_bounds);
            if (diagonal > 0.0) {
              for (std::size_t axis = 0; axis < 3; ++axis) {
                const double deviation = std::max(std::fabs(decoded_bounds.low[axis] - hull_bounds.low[axis]),
                                                  std::fabs(decoded_bounds.high[axis] - hull_bounds.high[axis]));
                worst_round_trip = std::max(worst_round_trip, deviation / diagonal);
              }
            }
            found = true;
            break;
          }
          EXPECT_TRUE(found) << path << ": the authored packfile decoded no convex shape";
          const auto find_blob = [](const std::vector<fo4::nif::CollisionBlob>& list, std::uint32_t block) {
            return std::find_if(list.begin(), list.end(),
                                [block](const fo4::nif::CollisionBlob& item) { return item.block == block; });
          };
          const auto original_blob = find_blob(blobs, collision_block);
          ASSERT_NE(original_blob, blobs.end()) << path;
          const auto replaced = fo4::nif::replace_collision_blob(nif, collision_block, encoded);
          const auto replaced_nif = fo4::nif::read_nif(replaced);
          const auto replaced_blobs = fo4::nif::read_collision_blobs(replaced_nif);
          ASSERT_EQ(replaced_blobs.size(), blobs.size()) << path;
          const auto replaced_blob = find_blob(replaced_blobs, collision_block);
          ASSERT_NE(replaced_blob, replaced_blobs.end()) << path;
          EXPECT_EQ(replaced_blob->bytes, encoded) << path << ": the stored blob is not our payload";
          EXPECT_EQ(replaced_blob->owner, original_blob->owner) << path << ": the owner changed";
          EXPECT_EQ(replaced_blob->body_id, original_blob->body_id) << path << ": the body id changed";
          const auto replaced_system =
              fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(replaced_blob->bytes));
          ASSERT_EQ(replaced_system.shapes.size(), 1U) << path;
          EXPECT_EQ(replaced_system.shapes[0].vertices.size(), hull.shape.vertices.size()) << path;
          for (std::size_t b = 0; b < nif.blocks().size(); ++b) {
            if (b == collision_block) continue;
            const auto& before_block = nif.blocks()[b];
            const auto& after_block = replaced_nif.blocks()[b];
            ASSERT_EQ(before_block.size, after_block.size) << path << ": block " << b << " changed size";
            const auto before = std::span<const std::byte>(nif.bytes()).subspan(before_block.offset, before_block.size);
            const auto after =
                std::span<const std::byte>(replaced_nif.bytes()).subspan(after_block.offset, after_block.size);
            EXPECT_TRUE(std::equal(before.begin(), before.end(), after.begin()))
                << path << ": block " << b << " changed";
          }
          const double vanilla_diagonal = diagonal_of(vanilla);
          const double hull_diagonal = diagonal_of(hull_bounds);
          if (vanilla_diagonal > 0.0 && hull_diagonal > 0.0 && hull_diagonal <= 2.0 * vanilla_diagonal &&
              vanilla_diagonal <= 2.0 * hull_diagonal) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
              const double deviation = std::max(std::fabs(hull_bounds.low[axis] - vanilla.low[axis]),
                                                std::fabs(hull_bounds.high[axis] - vanilla.high[axis]));
              worst_vanilla = std::max(worst_vanilla, deviation / vanilla_diagonal);
            }
          }
          ++measured;
          break;
        }
      } catch (const fo4::parse_error&) {
        continue;
      }
    }
    if (nifs >= k_max_nifs) break;
  }
  RecordProperty("nifs_scanned", std::to_string(nifs));
  RecordProperty("paired_meshes", std::to_string(paired));
  if (measured == 0) {
    GTEST_SKIP() << "the sample corpus pairs no convex collision block with a hull-buildable mesh (nifs=" << nifs
                 << ", paired=" << paired << ")";
  }
  RecordProperty("measured_pairs", std::to_string(measured));
  RecordProperty("worst_round_trip_ratio", std::to_string(worst_round_trip));
  RecordProperty("worst_vanilla_ratio", std::to_string(worst_vanilla));
}
