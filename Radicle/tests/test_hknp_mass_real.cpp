#include "fo4/archive/assets.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_mass.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 37;
constexpr std::size_t k_max_systems = 400;
constexpr double k_relative = 1.0e-4;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) { return std::filesystem::path(lower(path)).extension() == ".nif"; }

bool has_packfile_magic(const std::vector<std::byte>& bytes) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[i]) != k_magic[i]) return false;
  }
  return true;
}

// Requires eight distinct vertices that each sit on a corner of their own bounding box.
bool axis_aligned_box(const fo4::nif::HknpShape& shape, fo4::nif::HknpVec3& low, fo4::nif::HknpVec3& high) {
  if (shape.vertices.size() < 8) return false;
  low = shape.vertices.front();
  high = shape.vertices.front();
  for (const auto& v : shape.vertices) {
    for (std::size_t k = 0; k < 3; ++k) {
      low[k] = std::min(low[k], v[k]);
      high[k] = std::max(high[k], v[k]);
    }
  }
  std::vector<fo4::nif::HknpVec3> distinct;
  for (const auto& v : shape.vertices) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (v[k] != low[k] && v[k] != high[k]) return false;
    }
    if (std::find(distinct.begin(), distinct.end(), v) == distinct.end()) distinct.push_back(v);
  }
  return distinct.size() == 8;
}

float largest_component(const fo4::nif::HknpVec3& v) {
  return std::max({std::fabs(v[0]), std::fabs(v[1]), std::fabs(v[2])});
}

// Constraint: stored box vertices sit one convex radius inside the mass half extents.
std::array<double, 3> solid_half(const fo4::nif::HknpShape& shape, const fo4::nif::HknpVec3& low,
                                 const fo4::nif::HknpVec3& high) {
  std::array<double, 3> half{};
  for (std::size_t k = 0; k < 3; ++k) {
    half[k] = (static_cast<double>(high[k]) - static_cast<double>(low[k])) / 2.0 + shape.convex_radius;
  }
  return half;
}

}  // namespace

// Detects: stored vanilla box mass that drops the convex-radius growth, shifts the centre, or departs from 1.5 times the physical inertia.
TEST(HknpMassRealData, VanillaBoxMassFollowsOneAndAHalfPhysical) {
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
  std::size_t systems = 0;
  std::size_t boxes = 0;
  std::size_t volume_ok = 0;
  std::size_t centre_ok = 0;
  std::size_t inertia_ok = 0;
  double worst_volume = 0.0;
  double worst_inertia = 0.0;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && systems < k_max_systems; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      const auto nif = fo4::nif::read_nif(reader.extract(i));
      for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
        if (!has_packfile_magic(blob.bytes)) continue;
        const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
        ++systems;
        for (const auto& shape : sys.shapes) {
          if (shape.kind != fo4::nif::HknpShapeKind::convex || !shape.has_mass_properties) continue;
          fo4::nif::HknpVec3 low{};
          fo4::nif::HknpVec3 high{};
          if (!axis_aligned_box(shape, low, high)) continue;
          ++boxes;
          const auto& stored = shape.mass_properties;
          const auto half = solid_half(shape, low, high);
          const double volume = 8.0 * half[0] * half[1] * half[2];
          const double volume_error = std::fabs(stored.volume - volume) / volume;
          worst_volume = std::max(worst_volume, volume_error);
          if (volume_error <= k_relative) ++volume_ok;

          bool centre_match = true;
          const double centre_slack = largest_component(stored.center) / 32768.0 + 1.0e-6;
          for (std::size_t k = 0; k < 3; ++k) {
            const double c = (static_cast<double>(low[k]) + static_cast<double>(high[k])) / 2.0;
            if (std::fabs(stored.center[k] - c) > centre_slack) centre_match = false;
          }
          if (centre_match) ++centre_ok;

          const double hx = half[0];
          const double hy = half[1];
          const double hz = half[2];
          std::array<double, 3> expected{volume * (hy * hy + hz * hz) / 2.0, volume * (hx * hx + hz * hz) / 2.0,
                                         volume * (hx * hx + hy * hy) / 2.0};
          std::array<double, 3> got{stored.inertia_raw[0], stored.inertia_raw[1], stored.inertia_raw[2]};
          std::sort(expected.begin(), expected.end());
          std::sort(got.begin(), got.end());
          const double inertia_slack = largest_component(stored.inertia_raw) / 32768.0;
          bool inertia_match = true;
          for (std::size_t k = 0; k < 3; ++k) {
            const double error = std::fabs(got[k] - expected[k]);
            worst_inertia = std::max(worst_inertia, error / expected[k]);
            if (error > k_relative * expected[k] + inertia_slack) inertia_match = false;
          }
          if (inertia_match) ++inertia_ok;
        }
      }
    }
  }

  std::cout << "systems " << systems << ", convex boxes " << boxes << ", volume within 1e-4 " << volume_ok
            << " (worst " << worst_volume << "), centre within packed bound " << centre_ok
            << ", inertia within 1e-4 plus packed bound " << inertia_ok << " (worst relative " << worst_inertia
            << ")\n";
  RecordProperty("box_count", std::to_string(boxes));
  RecordProperty("volume_match", std::to_string(volume_ok));
  RecordProperty("centre_match", std::to_string(centre_ok));
  RecordProperty("inertia_match", std::to_string(inertia_ok));
  EXPECT_GT(boxes, 0U);
  EXPECT_EQ(volume_ok, boxes);
  EXPECT_EQ(centre_ok, boxes);
  EXPECT_EQ(inertia_ok, boxes);
}


// Detects: a dynamic body whose stored density or inverse inertia departs from the relations its box shape measures (density is mass over volume, inverse inertia is the reciprocal of density times the unit inertia).
TEST(HknpMassRealData, VanillaDynamicBoxesRelateDensityMassAndInverseInertia) {
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
  std::size_t systems = 0;
  std::size_t mapped = 0;
  std::size_t identity_orientation = 0;
  std::vector<double> density_ratio;
  std::array<std::vector<double>, 3> inertia_ratio;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && systems < k_max_systems; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      const auto nif = fo4::nif::read_nif(reader.extract(i));
      for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
        if (!has_packfile_magic(blob.bytes)) continue;
        const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
        ++systems;
        for (const auto& body : sys.bodies) {
          if (!body.dynamic || body.shape < 0 || body.motion_index >= sys.inertias.size()) continue;
          const auto& shape = sys.shapes[static_cast<std::size_t>(body.shape)];
          fo4::nif::HknpVec3 low{};
          fo4::nif::HknpVec3 high{};
          if (shape.kind != fo4::nif::HknpShapeKind::convex || !shape.has_mass_properties ||
              !axis_aligned_box(shape, low, high)) {
            continue;
          }
          const auto& inertia = sys.inertias[body.motion_index];
          if (!(inertia.inverse_mass > 0.0f)) continue;
          ++mapped;
          const double mass = 1.0 / static_cast<double>(inertia.inverse_mass);
          const double density = static_cast<double>(inertia.density);
          density_ratio.push_back(density * shape.mass_properties.volume / mass);
          std::array<double, 3> stored{shape.mass_properties.inertia_raw[0], shape.mass_properties.inertia_raw[1],
                                       shape.mass_properties.inertia_raw[2]};
          std::array<double, 3> inverse{inertia.inverse_inertia[0], inertia.inverse_inertia[1],
                                        inertia.inverse_inertia[2]};
          std::sort(stored.begin(), stored.end(), std::greater<double>());
          std::sort(inverse.begin(), inverse.end());
          for (std::size_t k = 0; k < 3; ++k) inertia_ratio[k].push_back(inverse[k] * stored[k] * density);
          const auto& q = inertia.orientation;
          if (std::fabs(std::fabs(q[3]) - 1.0) < 1.0e-3) ++identity_orientation;
        }
      }
    }
  }

  const auto span_of = [](std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? std::array<double, 2>{0.0, 0.0} : std::array<double, 2>{values.front(), values.back()};
  };
  const auto density = span_of(density_ratio);
  std::cout << "systems " << systems << ", mapped dynamic box bodies " << mapped << ", identity orientation "
            << identity_orientation << '\n';
  std::cout << "density * volume / mass " << density[0] << " .. " << density[1] << '\n';
  for (std::size_t k = 0; k < 3; ++k) {
    const auto range = span_of(inertia_ratio[k]);
    std::cout << "inverse_inertia * stored_inertia * density [" << k << "] " << range[0] << " .. " << range[1] << '\n';
  }
  RecordProperty("dynamic_box_count", std::to_string(mapped));
  EXPECT_GT(mapped, 0U);
  EXPECT_NEAR(density[0], 1.0, 1.0e-3);
  EXPECT_NEAR(density[1], 1.0, 1.0e-3);
  for (std::size_t k = 0; k < 3; ++k) {
    const auto range = span_of(inertia_ratio[k]);
    EXPECT_NEAR(range[0], range[1], 1.0e-3 * range[1]);
    EXPECT_NEAR(range[0], 1.0, 1.0e-3);
  }
}
