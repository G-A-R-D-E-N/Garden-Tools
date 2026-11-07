#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "hknp_mesh_walk.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 37;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  return std::filesystem::path(lower(path)).extension() == ".nif";
}

bool has_packfile_magic(const std::vector<std::byte>& bytes) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[i]) != k_magic[i]) return false;
  }
  return true;
}

float extent_of(const std::vector<fo4::nif::HknpVec3>& vertices) {
  if (vertices.empty()) return 0.0f;
  fo4::nif::HknpVec3 lo = vertices.front();
  fo4::nif::HknpVec3 hi = vertices.front();
  for (const auto& v : vertices) {
    for (std::size_t k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], v[k]);
      hi[k] = std::max(hi[k], v[k]);
    }
  }
  return std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
}

}  // namespace

// Detects: a vanilla compressed mesh whose re-encoded triangles drift beyond quantisation, lose a triangle or a material, come back with a section over 128 primitives, or whose section or mesh trees miss a primitive.
TEST(HknpMeshEncodeRealData, VanillaMeshesRoundTripThroughTheEncoder) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t meshes = 0;
  std::size_t triangles = 0;
  std::size_t matched = 0;
  std::size_t unmatched = 0;
  std::size_t sections = 0;
  std::size_t walk_failures = 0;
  std::size_t encode_failures = 0;
  float worst_absolute = 0.0f;
  float worst_relative = 0.0f;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (!has_packfile_magic(blob.bytes)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          for (const auto& shape : sys.shapes) {
            if (shape.kind != fo4::nif::HknpShapeKind::compressed_mesh || shape.mesh_triangles.empty()) continue;
            ++meshes;
            const std::string where = path + " block " + std::to_string(blob.block);
            const float extent = extent_of(shape.mesh_vertices);
            const float tolerance = std::max(extent * 2.0e-3f, 1.0e-5f);
            triangles += shape.mesh_triangles.size();
            try {
              const auto pack = fo4::nif::author_hknp_static_body(shape, {});
              const auto back = fo4::nif::decode_hknp_system(
                  fo4::nif::read_hknp_packfile(fo4::nif::write_hknp_packfile(pack)));
              if (back.shapes.size() != 1U || back.shapes[0].kind != fo4::nif::HknpShapeKind::compressed_mesh) {
                ++encode_failures;
                if (first_failure.empty()) first_failure = where + ": decoded back as something else";
                continue;
              }
              const auto result = hknp_mesh_walk::match_triangles(hknp_mesh_walk::triangles_of(shape),
                                                                  hknp_mesh_walk::triangles_of(back.shapes[0]),
                                                                  tolerance);
              matched += result.matched;
              unmatched += result.unmatched;
              if (result.unmatched != 0U && first_failure.empty()) first_failure = where + ": triangle not matched, " + result.first_unmatched;
              worst_absolute = std::max(worst_absolute, result.worst);
              if (extent > 0.0f) worst_relative = std::max(worst_relative, result.worst / extent);

              const auto& data = [&pack]() -> const fo4::nif::HknpPackObject& {
                for (const auto& object : pack.objects) {
                  if (object.class_name == "hknpCompressedMeshShapeData") return object;
                }
                throw fo4::parse_error(0, "no compressed mesh data object");
              }();
              const std::size_t count = hknp_mesh_walk::u32_at(data, 0x58);
              sections += count;
              const auto section_walk = hknp_mesh_walk::section_tree_leaves(
                  data, hknp_mesh_walk::local_target(data, 0x10), hknp_mesh_walk::u32_at(data, 0x18));
              std::vector<bool> seen_section(count, false);
              bool ok = section_walk.in_range && section_walk.leaves.size() == count;
              for (const std::size_t section : section_walk.leaves) {
                if (section >= count || seen_section[section]) {
                  ok = false;
                  break;
                }
                seen_section[section] = true;
              }
              const std::size_t base = hknp_mesh_walk::local_target(data, 0x50);
              for (std::size_t s = 0; ok && s < count; ++s) {
                const std::size_t record = base + 0x60 * s;
                const std::size_t prims = hknp_mesh_walk::u32_at(data, record + 0x50) & 0xFFU;
                const auto walk = hknp_mesh_walk::mesh_tree_leaves(
                    data, hknp_mesh_walk::local_target(data, record), hknp_mesh_walk::u32_at(data, record + 0x08));
                std::vector<bool> seen(prims, false);
                ok = walk.in_range && walk.leaves.size() == prims && prims <= 128;
                for (const std::size_t prim : walk.leaves) {
                  if (prim >= prims || seen[prim]) {
                    ok = false;
                    break;
                  }
                  seen[prim] = true;
                }
              }
              if (!ok) {
                ++walk_failures;
                if (first_failure.empty()) first_failure = where + ": a tree misses or repeats a primitive or section";
              }
            } catch (const std::invalid_argument& error) {
              ++encode_failures;
              if (first_failure.empty()) first_failure = where + ": " + error.what();
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++encode_failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "compressed meshes " << meshes << ", triangles " << triangles << ", matched " << matched
            << ", unmatched " << unmatched << ", sections " << sections << ", worst error " << worst_absolute
            << " (relative " << worst_relative << "), walk failures " << walk_failures << ", encode failures "
            << encode_failures << '\n';
  RecordProperty("mesh_round_trip_mesh_count", std::to_string(meshes));
  RecordProperty("mesh_round_trip_triangle_count", std::to_string(triangles));
  RecordProperty("mesh_round_trip_unmatched", std::to_string(unmatched));
  RecordProperty("mesh_round_trip_worst_relative", std::to_string(worst_relative));

  EXPECT_GT(meshes, 0U);
  EXPECT_EQ(encode_failures, 0U) << first_failure;
  EXPECT_EQ(unmatched, 0U) << first_failure;
  EXPECT_EQ(walk_failures, 0U) << first_failure;
}
