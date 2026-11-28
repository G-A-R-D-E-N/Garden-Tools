#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_plain.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 11;
constexpr std::size_t k_max_models = 1500;
constexpr float k_zero_vertex = 1.0e-4f;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) { return std::filesystem::path(lower(path)).extension() == ".nif"; }

bool finite(const std::array<float, 3>& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool shape_is_sound(const fo4::nif::PlainShape& shape) {
  using fo4::nif::PlainShapeKind;
  if (!finite(shape.origin)) return false;
  for (const float value : shape.basis) {
    if (!std::isfinite(value)) return false;
  }
  switch (shape.kind) {
    case PlainShapeKind::box:
      return finite(shape.half_extents) && shape.half_extents[0] > 0.0f && shape.half_extents[1] > 0.0f &&
             shape.half_extents[2] > 0.0f;
    case PlainShapeKind::sphere:
      return shape.radius > 0.0f && std::isfinite(shape.radius);
    case PlainShapeKind::capsule:
      return shape.radius > 0.0f && std::isfinite(shape.radius) && finite(shape.point_a) && finite(shape.point_b);
    case PlainShapeKind::convex_hull:
      return shape.points.size() >= 12 && shape.points.size() % 3 == 0 &&
             std::all_of(shape.points.begin(), shape.points.end(), [](float v) { return std::isfinite(v); });
    case PlainShapeKind::triangle_mesh: {
      const std::size_t vertices = shape.points.size() / 3;
      if (shape.indices.empty() || shape.indices.size() % 3 != 0) return false;
      if (shape.triangle_materials.size() != shape.indices.size() / 3) return false;
      return std::all_of(shape.indices.begin(), shape.indices.end(),
                         [&](std::uint32_t index) { return index < vertices; });
    }
  }
  return false;
}

}  // namespace

TEST(CollisionPlainReal, VanillaSystemsBuildSoundShapesInEngineAxes) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t models = 0;
  std::size_t systems = 0;
  std::size_t bodies = 0;
  std::size_t shapes = 0;
  std::size_t skipped = 0;
  std::size_t undecodable = 0;
  std::size_t unsound = 0;
  std::size_t read_failures = 0;
  std::string first_failure;
  std::size_t dynamic_with_mass = 0;
  std::size_t dynamic_without_mass = 0;
  std::size_t spheres_at_origin_with_body_offset = 0;
  std::size_t spheres_off_origin = 0;
  std::array<std::size_t, 6> by_kind{};

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && models < k_max_models; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      std::vector<fo4::nif::CollisionBlob> blobs;
      try {
        blobs = fo4::nif::read_collision_blobs(fo4::nif::read_nif(reader.extract(i)));
      } catch (const fo4::parse_error& error) {
        ++read_failures;
        if (first_failure.empty()) first_failure = path + ": " + error.what();
        continue;
      }
      if (blobs.empty()) continue;
      ++models;
      systems += blobs.size();
      const auto plain = fo4::nif::build_plain_collision(blobs);
      bodies += plain.bodies.size();
      shapes += plain.shapes.size();
      skipped += plain.skipped_shapes;
      undecodable += plain.undecodable_systems;
      for (const auto& shape : plain.shapes) {
        ++by_kind[static_cast<std::size_t>(shape.kind)];
        if (!shape_is_sound(shape)) {
          ++unsound;
          ADD_FAILURE() << path << " collision " << shape.system << " body " << shape.body;
        }
      }
      for (const auto& body : plain.bodies) {
        if (!body.dynamic) continue;
        ++(body.has_mass ? dynamic_with_mass : dynamic_without_mass);
      }
      for (const auto& blob : blobs) {
        try {
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          for (const auto& body : sys.bodies) {
            if (body.shape < 0) continue;
            const auto& shape = sys.shapes[static_cast<std::size_t>(body.shape)];
            if (shape.kind != fo4::nif::HknpShapeKind::sphere) continue;
            const bool zero_vertex = std::fabs(shape.point_a[0]) < k_zero_vertex &&
                                     std::fabs(shape.point_a[1]) < k_zero_vertex &&
                                     std::fabs(shape.point_a[2]) < k_zero_vertex;
            const bool body_offset = std::fabs(body.position[0]) > k_zero_vertex ||
                                     std::fabs(body.position[1]) > k_zero_vertex ||
                                     std::fabs(body.position[2]) > k_zero_vertex;
            if (zero_vertex && body_offset) ++spheres_at_origin_with_body_offset;
            if (!zero_vertex) ++spheres_off_origin;
          }
        } catch (const fo4::parse_error&) {
        }
      }
    }
  }

  std::cout << "models " << models << ", systems " << systems << ", bodies " << bodies << ", shapes " << shapes
            << ", skipped " << skipped << ", undecodable systems " << undecodable << '\n';
  std::cout << "box " << by_kind[1] << ", sphere " << by_kind[2] << ", capsule " << by_kind[3] << ", hull "
            << by_kind[4] << ", mesh " << by_kind[5] << '\n';
  std::cout << "dynamic bodies with mass " << dynamic_with_mass << ", without " << dynamic_without_mass << '\n';
  std::cout << "top-level spheres with a zero vertex and an offset body " << spheres_at_origin_with_body_offset
            << ", with a non-zero vertex " << spheres_off_origin << '\n';
  RecordProperty("shape_count", std::to_string(shapes));
  RecordProperty("skipped_shapes", std::to_string(skipped));
  RecordProperty("undecodable_systems", std::to_string(undecodable));
  EXPECT_GT(shapes, 0U);
  EXPECT_EQ(unsound, 0U);
  EXPECT_EQ(read_failures, 0U) << first_failure;
}
