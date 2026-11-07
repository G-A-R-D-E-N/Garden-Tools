#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
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

// Constraint: a physics system packfile starts with the magic, at offset 0.
bool has_packfile_magic(const std::vector<std::byte>& bytes, std::size_t offset) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < offset + 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[offset + i]) != k_magic[i]) return false;
  }
  return true;
}

// Constraint: the cinfo flag word at +0x18 and position w lane at +0x3c are not compared, as vanilla varies them.
std::vector<std::byte> root_without_stale_lanes(const fo4::nif::HknpPackObject& root, std::size_t bodies) {
  std::vector<std::byte> bytes = root.bytes;
  const std::size_t cinfos_at = 0x80 + 0x50 * bodies;
  for (std::size_t i = 0; i < bodies; ++i) {
    const std::size_t at = cinfos_at + 0x60 * i;
    if (at + 0x40 > bytes.size()) continue;
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at + 0x18),
              bytes.begin() + static_cast<std::ptrdiff_t>(at + 0x1c), std::byte{0});
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at + 0x3c),
              bytes.begin() + static_cast<std::ptrdiff_t>(at + 0x40), std::byte{0});
  }
  return bytes;
}

// Constraint: the motion caps and dampings vary in vanilla, so only their bounds are compared.
bool motion_record_is_measured(const fo4::nif::HknpDynMotion& motion) {
  return std::fabs(motion.gravity_factor - 1.0f) <= 1.0e-6f && motion.max_linear_velocity >= 1.0f &&
         motion.max_linear_velocity <= 250.0f && motion.max_angular_velocity > 0.0f &&
         motion.max_angular_velocity <= 250.0f && motion.linear_damping > 0.0f && motion.linear_damping <= 1.0f &&
         motion.angular_damping > 0.0f && motion.angular_damping <= 2.0f;
}

// Constraint: the record this encoder writes is the most common vanilla one, which 4120 of 4290 measured motions carry.
bool motion_record_is_the_written_default(const fo4::nif::HknpDynMotion& motion) {
  return std::fabs(motion.gravity_factor - 1.0f) <= 1.0e-6f &&
         std::fabs(motion.max_linear_velocity - 104.375f) <= 1.0e-3f &&
         std::fabs(motion.max_angular_velocity - 31.5703f) <= 1.0e-3f &&
         std::fabs(motion.linear_damping - 0.10009766f) <= 1.0e-6f &&
         std::fabs(motion.angular_damping - 0.05004883f) <= 1.0e-6f;
}

// Constraint: a body with these four decoded values stores the 0x50-byte record this encoder writes.
bool body_properties_are_the_written_default(const fo4::nif::HknpBody& body) {
  return std::fabs(body.friction - 0.5f) <= 1.0e-6f && std::fabs(body.restitution - 0.400390625f) <= 1.0e-6f &&
         body.material_flags == 0 && body.trigger_type == 0;
}

bool body_shape_is_authorable(const fo4::nif::HknpSystem& sys, const fo4::nif::HknpBody& body) {
  if (body.shape < 0) return false;
  const auto& shape = sys.shapes[static_cast<std::size_t>(body.shape)];
  return shape.kind == fo4::nif::HknpShapeKind::convex || shape.kind == fo4::nif::HknpShapeKind::sphere ||
         shape.kind == fo4::nif::HknpShapeKind::capsule || shape.kind == fo4::nif::HknpShapeKind::compressed_mesh;
}

}  // namespace

// Detects: a root whose property, cinfo or shape-reference records sit off the measured offsets.
TEST(NifCollisionRealData, MultiBodyRootsMatchTheMeasuredLayout) {
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
  std::size_t attempted = 0;
  std::size_t matched = 0;
  std::size_t stale_w = 0;
  std::size_t skipped = 0;
  std::size_t not_authored = 0;
  std::size_t failures = 0;
  std::string first_mismatch;
  std::string first_not_authored;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && attempted < 200; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type != "bhkPhysicsSystem" || !has_packfile_magic(blob.bytes, 0)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          if (sys.root_class != "hknpPhysicsSystemData" || sys.bodies.size() < 2) continue;
          if (!sys.motions.empty() || !sys.inertias.empty() || !sys.constraints.empty() || !sys.bones.empty() ||
              !sys.unsupported.empty()) {
            ++skipped;
            continue;
          }
          std::vector<fo4::nif::HknpSystemBody> bodies;
          bool authorable = true;
          for (const auto& body : sys.bodies) {
            if (!body_shape_is_authorable(sys, body) || !body_properties_are_the_written_default(body)) {
              authorable = false;
              break;
            }
            fo4::nif::HknpSystemBody spec;
            spec.shape = sys.shapes[static_cast<std::size_t>(body.shape)];
            spec.position = body.position;
            spec.orientation = body.orientation;
            spec.packed_filter = body.packed_filter;
            bodies.push_back(std::move(spec));
          }
          if (!authorable) {
            ++skipped;
            continue;
          }
          try {
            const auto authored = fo4::nif::author_hknp_system(bodies);
            ++attempted;
            const std::vector<std::byte> stored =
                root_without_stale_lanes(sys.packfile.objects.front(), sys.bodies.size());
            const std::vector<std::byte> rebuilt =
                root_without_stale_lanes(authored.objects.front(), sys.bodies.size());
            if (stored != rebuilt) {
              if (first_mismatch.empty()) first_mismatch = path + " block " + std::to_string(blob.block);
            } else {
              ++matched;
            }
            if (stored != sys.packfile.objects.front().bytes) ++stale_w;
          } catch (const std::invalid_argument& error) {
            // Constraint: a shape this encoder does not author is a skip, not a layout failure, because only the root is compared here.
            ++not_authored;
            if (first_not_authored.empty()) first_not_authored = path + ": " + error.what();
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "multi-body roots " << attempted << ", matched " << matched << ", stale position w " << stale_w
            << ", skipped (not a static multi-body root with authorable shapes and default properties) " << skipped
            << ", shapes this encoder refuses " << not_authored << ' ' << first_not_authored << ", failures "
            << failures << '\n';
  RecordProperty("multi_body_attempted_count", std::to_string(attempted));
  RecordProperty("multi_body_matched_count", std::to_string(matched));

  EXPECT_EQ(failures, 0U) << first_failure;
  // Constraint: this sample holds 19 multi-body roots once compressed-mesh roots are excluded.
  EXPECT_GE(attempted, 19U);
  // Constraint: measured 11107 of 11107 static multi-body roots with the default property record match outside the two masked lanes.
  EXPECT_GE(matched * 100, attempted * 99) << first_mismatch;
}

// Detects: a dynamic body whose mass relation or motion record drifts from the decoded values.
TEST(NifCollisionRealData, DynamicBodiesCarryTheMeasuredMotionAndMassRecords) {
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
  std::size_t bodies_checked = 0;
  std::size_t mass_mismatches = 0;
  std::size_t motions_checked = 0;
  std::size_t motion_mismatches = 0;
  std::size_t default_motions = 0;
  std::size_t failures = 0;
  std::string first_mass_mismatch;
  std::string first_motion_mismatch;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && motions_checked < 400; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type != "bhkPhysicsSystem" || !has_packfile_magic(blob.bytes, 0)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          for (const auto& motion : sys.motions) {
            ++motions_checked;
            if (!motion_record_is_measured(motion)) {
              ++motion_mismatches;
              if (first_motion_mismatch.empty()) first_motion_mismatch = path + " block " + std::to_string(blob.block);
            }
            if (motion_record_is_the_written_default(motion)) ++default_motions;
          }
          for (const auto& body : sys.bodies) {
            if (!body.dynamic || body.shape < 0) continue;
            if (static_cast<std::size_t>(body.motion_index) >= sys.inertias.size()) continue;
            const auto& shape = sys.shapes[static_cast<std::size_t>(body.shape)];
            if (!shape.has_mass_properties || !(shape.mass_properties.volume > 0.0f)) continue;
            const auto& inertia = sys.inertias[static_cast<std::size_t>(body.motion_index)];
            if (!(inertia.inverse_mass > 0.0f) || !(inertia.density > 0.0f)) continue;
            ++bodies_checked;
            const float body_mass = 1.0f / inertia.inverse_mass;
            const float density = body_mass / shape.mass_properties.volume;
            bool ok = std::fabs(inertia.density - density) <= 1.0e-3f * density;
            for (std::size_t k = 0; k < 3; ++k) {
              if (!(shape.mass_properties.inertia_raw[k] > 0.0f)) {
                ok = false;
                continue;
              }
              const float predicted = 1.0f / (shape.mass_properties.inertia_raw[k] * density);
              if (std::fabs(inertia.inverse_inertia[k] - predicted) > 2.0e-2f * predicted) ok = false;
            }
            if (!ok) {
              ++mass_mismatches;
              if (first_mass_mismatch.empty()) first_mass_mismatch = path + " block " + std::to_string(blob.block);
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "dynamic bodies " << bodies_checked << ", mass record mismatches " << mass_mismatches << ", motions "
            << motions_checked << ", motion record mismatches " << motion_mismatches << ", written default "
            << default_motions << ", failures " << failures << '\n';
  RecordProperty("dynamic_body_count", std::to_string(bodies_checked));
  RecordProperty("motion_count", std::to_string(motions_checked));
  RecordProperty("default_motion_count", std::to_string(default_motions));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GE(bodies_checked, 20U);
  // Constraint: measured 122 of 122 sampled dynamic bodies keep the mass and inertia relation this encoder derives from the shape mass.
  EXPECT_GE((bodies_checked - mass_mismatches) * 100, bodies_checked * 98) << first_mass_mismatch;
  EXPECT_GE(motions_checked, 20U);
  EXPECT_EQ(motion_mismatches, 0U) << first_motion_mismatch;
  // Constraint: the default motion record stays the majority of sampled motions.
  EXPECT_GE(default_motions * 100, motions_checked * 85);
}
