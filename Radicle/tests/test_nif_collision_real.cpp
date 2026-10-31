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
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_max_blobs = 200;
constexpr std::size_t k_max_packfiles = 200;
constexpr std::size_t k_entry_stride = 37;
constexpr std::size_t k_max_convex_vertices = 255;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  return std::filesystem::path(lower(path)).extension() == ".nif";
}

// Constraint: both candidate layouts start with the packfile magic, at offset 0 or offset 4.
bool has_packfile_magic(const std::vector<std::byte>& bytes, std::size_t offset) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < offset + 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[offset + i]) != k_magic[i]) return false;
  }
  return true;
}

std::string hex_prefix(const std::vector<std::byte>& bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (std::size_t i = 0; i < bytes.size() && i < 8; ++i) {
    const auto value = std::to_integer<std::uint8_t>(bytes[i]);
    if (!out.empty()) out.push_back(' ');
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
  }
  return out;
}

}  // namespace

TEST(NifCollisionRealData, VanillaCollisionSystemsCarryPackfileBytes) {
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
  std::size_t nifs = 0;
  std::size_t physics = 0;
  std::size_t ragdoll = 0;
  std::size_t magic_at_zero = 0;
  std::size_t magic_after_length = 0;
  std::size_t empty_payloads = 0;
  std::size_t odd_payloads = 0;
  std::size_t attributed = 0;
  std::size_t attributed_with_node = 0;
  std::size_t flagged = 0;
  std::size_t unattributed = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_odd_payload;
  std::string first_unattributed;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && physics + ragdoll < k_max_blobs; ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type == "bhkPhysicsSystem") {
            ++physics;
            if (blob.bytes.empty()) {
              ++empty_payloads;
            } else if (has_packfile_magic(blob.bytes, 0)) {
              ++magic_at_zero;
            } else if (has_packfile_magic(blob.bytes, 4)) {
              ++magic_after_length;
            } else {
              // Invariant: prefix layout is unresolved, so this case is counted and never asserted.
              ++odd_payloads;
              if (first_odd_payload.empty()) first_odd_payload = path + " (" + hex_prefix(blob.bytes) + ")";
            }
          } else if (blob.type == "bhkRagdollSystem") {
            ++ragdoll;
          } else {
            ADD_FAILURE() << path << ": unexpected collision block type " << blob.type;
          }
          if (blob.owner >= 0) {
            ++attributed;
            if (blob.target_node >= 0) ++attributed_with_node;
            if (blob.flags != 0) ++flagged;
          } else {
            ++unattributed;
            if (first_unattributed.empty()) {
              first_unattributed = path + " block " + std::to_string(blob.block);
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
    if (physics + ragdoll >= k_max_blobs) break;
  }

  std::cout << "nifs " << nifs << ", bhkPhysicsSystem " << physics << ", bhkRagdollSystem " << ragdoll
            << ", packfile at 0: " << magic_at_zero << ", packfile after a 4-byte prefix: " << magic_after_length
            << ", empty payloads " << empty_payloads << ", payloads with neither magic: " << odd_payloads
            << ", attributed systems " << attributed << ", with a target node " << attributed_with_node
            << ", with object flags " << flagged
            << ", unattributed systems " << unattributed << ", failures " << failures << '\n';
  if (!first_odd_payload.empty()) {
    std::cout << "first payload without a packfile magic: " << first_odd_payload << '\n';
  }
  if (!first_unattributed.empty()) {
    std::cout << "first system without a collision object: " << first_unattributed << '\n';
  }
  RecordProperty("nif_count", std::to_string(nifs));
  RecordProperty("physics_system_count", std::to_string(physics));
  RecordProperty("ragdoll_system_count", std::to_string(ragdoll));
  RecordProperty("packfile_at_zero_count", std::to_string(magic_at_zero));
  RecordProperty("packfile_after_prefix_count", std::to_string(magic_after_length));
  RecordProperty("payload_without_known_magic_count", std::to_string(odd_payloads));
  RecordProperty("attributed_system_count", std::to_string(attributed));
  RecordProperty("target_node_count", std::to_string(attributed_with_node));
  RecordProperty("unattributed_system_count", std::to_string(unattributed));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GT(nifs, 0U);
  EXPECT_GT(physics, 0U);
}

// Detects: a vanilla collision packfile the reader rejects, or one the rebuilder writes back with different bytes; every sampled packfile must round-trip exactly.
TEST(NifCollisionRealData, VanillaCollisionPackfilesRebuildByteForByte) {
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
  std::size_t nifs = 0;
  std::size_t packfiles = 0;
  std::size_t identical = 0;
  std::size_t mismatches = 0;
  std::size_t unframed = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_mismatch;
  std::string first_unframed;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && identical + mismatches < k_max_packfiles;
         i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (!has_packfile_magic(blob.bytes, 0)) {
            ++unframed;
            if (first_unframed.empty()) first_unframed = path + " (" + hex_prefix(blob.bytes) + ")";
            continue;
          }
          ++packfiles;
          if (fo4::nif::write_hknp_packfile(fo4::nif::read_hknp_packfile(blob.bytes)) == blob.bytes) {
            ++identical;
          } else {
            ++mismatches;
            if (first_mismatch.empty()) first_mismatch = path + " block " + std::to_string(blob.block);
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
    if (identical + mismatches >= k_max_packfiles) break;
  }

  std::cout << "nifs " << nifs << ", packfiles " << packfiles << ", rebuilt identical " << identical
            << ", mismatched " << mismatches
            << ", without a packfile magic " << unframed << ", failures " << failures << '\n';
  if (!first_unframed.empty()) {
    std::cout << "first payload without a packfile magic: " << first_unframed << '\n';
  }
  RecordProperty("hknp_packfile_count", std::to_string(packfiles));
  RecordProperty("hknp_identical_count", std::to_string(identical));
  RecordProperty("hknp_mismatch_count", std::to_string(mismatches));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(mismatches, 0U) << first_mismatch;
  EXPECT_GT(identical, 0U);
}

namespace {

const char* shape_kind_name(fo4::nif::HknpShapeKind kind) {
  switch (kind) {
    case fo4::nif::HknpShapeKind::convex: return "convex";
    case fo4::nif::HknpShapeKind::sphere: return "sphere";
    case fo4::nif::HknpShapeKind::capsule: return "capsule";
    case fo4::nif::HknpShapeKind::scaled_convex: return "scaled_convex";
    case fo4::nif::HknpShapeKind::compound: return "compound";
  }
  return "unknown";
}

bool all_finite(const fo4::nif::HknpVec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool geometry_is_sane(const fo4::nif::HknpShape& shape) {
  if (shape.kind == fo4::nif::HknpShapeKind::sphere || shape.kind == fo4::nif::HknpShapeKind::capsule) {
    if (!std::isfinite(shape.radius) || shape.radius < 0.0f) return false;
  }
  for (const auto& vertex : shape.vertices) {
    if (!all_finite(vertex)) return false;
  }
  return all_finite(shape.point_a) && all_finite(shape.point_b);
}

bool ends_with_skeleton_nif(const std::string& path) {
  const std::string name = lower(path);
  return std::string_view{name}.ends_with("skeleton.nif");
}

std::uint32_t read_u32_at(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(object.bytes.at(offset + i))) << (8 * i);
  }
  return value;
}

bool skeleton_pose_count_matches_parents(const fo4::nif::HknpSystem& sys) {
  for (const auto& object : sys.packfile.objects) {
    if (object.class_name != "hkaSkeleton") continue;
    return object.bytes.size() >= 0x44 && read_u32_at(object, 0x40) == read_u32_at(object, 0x20);
  }
  return false;
}

bool single_authorable_body(const fo4::nif::HknpSystem& sys) {
  if (sys.bodies.size() != 1 || sys.bodies[0].dynamic || !sys.constraints.empty() || !sys.bones.empty() ||
      !sys.unsupported.empty() || !sys.motions.empty() || !sys.inertias.empty()) {
    return false;
  }
  const auto& objects = sys.packfile.objects;
  if (objects.size() != 4 || objects[0].class_name != "hknpPhysicsSystemData" ||
      objects[1].class_name != "hknpConvexPolytopeShape" || objects[2].class_name != "hkRefCountedProperties" ||
      objects[3].class_name != "hknpShapeMassProperties") {
    return false;
  }
  const int index = sys.bodies[0].shape;
  if (index < 0 || static_cast<std::size_t>(index) >= sys.shapes.size()) return false;
  const auto kind = sys.shapes[static_cast<std::size_t>(index)].kind;
  return kind == fo4::nif::HknpShapeKind::sphere || kind == fo4::nif::HknpShapeKind::capsule ||
         kind == fo4::nif::HknpShapeKind::convex;
}

bool position_w_is_set(const fo4::nif::HknpSystem& sys) {
  for (const auto& object : sys.packfile.objects) {
    if (object.class_name == "hknpPhysicsSystemData") {
      return object.bytes.size() >= 0x110 && read_u32_at(object, 0x10c) != 0;
    }
  }
  return false;
}

void blank_mass_centre(fo4::nif::HknpPackfile& pack) {
  for (auto& object : pack.objects) {
    if (object.class_name != "hknpShapeMassProperties" || object.bytes.size() < 0x18) continue;
    std::fill(object.bytes.begin() + 0x10, object.bytes.begin() + 0x18, std::byte{0});
  }
}

bool same_objects(const fo4::nif::HknpPackfile& a, const fo4::nif::HknpPackfile& b) {
  if (a.objects.size() != b.objects.size()) return false;
  for (std::size_t i = 0; i < a.objects.size(); ++i) {
    if (a.objects[i].bytes != b.objects[i].bytes) return false;
  }
  return true;
}

float largest_half_extent(const fo4::nif::HknpShape& shape) {
  float largest = 0.0f;
  if (shape.vertices.empty()) return largest;
  for (std::size_t k = 0; k < 3; ++k) {
    float low = shape.vertices.front()[k];
    float high = low;
    for (const auto& v : shape.vertices) {
      low = std::min(low, v[k]);
      high = std::max(high, v[k]);
    }
    largest = std::max(largest, (high - low) / 2.0f);
  }
  return largest;
}

bool centers_agree(const fo4::nif::HknpShape& stored, const fo4::nif::HknpSystem& authored) {
  if (authored.bodies.size() != 1 || authored.bodies[0].shape < 0) return false;
  const auto& rebuilt = authored.shapes[static_cast<std::size_t>(authored.bodies[0].shape)];
  if (stored.has_mass_properties != rebuilt.has_mass_properties) return false;
  const float tolerance = 1e-3f * std::max(1e-3f, largest_half_extent(stored));
  for (std::size_t k = 0; k < 3; ++k) {
    if (std::fabs(stored.mass_properties.center[k] - rebuilt.mass_properties.center[k]) > tolerance) return false;
  }
  return true;
}

bool first_stage_encodable(const fo4::nif::HknpSystem& sys, const fo4::nif::HknpShape& shape) {
  if (shape.kind != fo4::nif::HknpShapeKind::convex) return true;
  if (shape.class_name == "hknpConvexShape") return shape.vertices.size() <= k_max_convex_vertices;
  if (shape.class_name != "hknpConvexPolytopeShape" || shape.object >= sys.packfile.objects.size()) return false;
  const auto& bytes = sys.packfile.objects[shape.object].bytes;
  if (bytes.size() < 0x46) return false;
  const std::size_t stored = std::to_integer<std::size_t>(bytes[0x44]) |
                             (std::to_integer<std::size_t>(bytes[0x45]) << 8);
  return stored == shape.faces.size();
}

bool same_shape(const fo4::nif::HknpShape& a, const fo4::nif::HknpShape& b) {
  return a.kind == b.kind && std::abs(a.radius - b.radius) <= 1e-5f &&
         std::abs(a.convex_radius - b.convex_radius) <= 1e-5f && a.vertices == b.vertices && a.planes == b.planes &&
         a.point_a == b.point_a && a.point_b == b.point_b;
}

}  // namespace

// Detects: a vanilla physics or ragdoll packfile the typed decode rejects, a ragdoll whose constraints, bones or reference-pose count are misread, or a decoded sphere, capsule or convex vertex that is not finite or has a negative radius; every sampled system and every skeleton-backed ragdoll must decode with finite geometry and matching pose counts.
TEST(NifCollisionRealData, VanillaPhysicsSystemsDecodeWithoutParseFailure) {
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
  std::size_t physics_systems = 0;
  std::size_t bodies = 0;
  std::size_t constraints = 0;
  std::size_t bones = 0;
  std::size_t unsupported = 0;
  std::size_t failures = 0;
  std::size_t bad_geometry = 0;
  std::map<std::string, std::size_t> shape_kinds;
  std::map<std::string, std::size_t> unsupported_classes;
  std::string first_failure;
  std::string first_bad_geometry;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && systems < k_max_packfiles; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (!has_packfile_magic(blob.bytes, 0)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          ++systems;
          if (blob.type == "bhkPhysicsSystem") ++physics_systems;
          bodies += sys.bodies.size();
          constraints += sys.constraints.size();
          bones += sys.bones.size();
          unsupported += sys.unsupported.size();
          for (const auto& shape : sys.shapes) {
            ++shape_kinds[shape_kind_name(shape.kind)];
            if (!geometry_is_sane(shape)) {
              ++bad_geometry;
              if (first_bad_geometry.empty()) first_bad_geometry = path + " block " + std::to_string(blob.block);
            }
          }
          for (const auto& entry : sys.unsupported) ++unsupported_classes[entry.class_name];
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
    if (systems >= k_max_packfiles) break;
  }

  std::size_t ragdolls = 0;
  std::size_t ragdoll_constraints = 0;
  std::size_t ragdoll_bones = 0;
  std::size_t ragdoll_failures = 0;
  std::size_t pose_mismatches = 0;
  std::string first_ragdoll_failure;
  std::string first_pose_mismatch;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!ends_with_skeleton_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type != "bhkRagdollSystem") continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          ++ragdolls;
          ragdoll_constraints += sys.constraints.size();
          ragdoll_bones += sys.bones.size();
          if (!skeleton_pose_count_matches_parents(sys)) {
            ++pose_mismatches;
            if (first_pose_mismatch.empty()) first_pose_mismatch = path + " block " + std::to_string(blob.block);
          }
        }
      } catch (const fo4::parse_error& error) {
        ++ragdoll_failures;
        if (first_ragdoll_failure.empty()) first_ragdoll_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "decoded ragdolls " << ragdolls << ", constraints " << ragdoll_constraints << ", bones " << ragdoll_bones
            << ", pose count mismatches " << pose_mismatches << ", failures " << ragdoll_failures << '\n';
  std::cout << "decoded systems " << systems << " (bhkPhysicsSystem " << physics_systems << "), bodies " << bodies
            << ", constraints " << constraints << ", bones " << bones << ", unsupported shapes " << unsupported
            << ", bad geometry " << bad_geometry << ", failures " << failures << '\n';
  for (const auto& [kind, count] : shape_kinds) std::cout << "  shape " << kind << ": " << count << '\n';
  for (const auto& [name, count] : unsupported_classes) std::cout << "  unsupported " << name << ": " << count << '\n';
  RecordProperty("hknp_decoded_system_count", std::to_string(systems));
  RecordProperty("hknp_physics_system_count", std::to_string(physics_systems));
  RecordProperty("hknp_body_count", std::to_string(bodies));
  RecordProperty("hknp_constraint_count", std::to_string(constraints));
  RecordProperty("hknp_unsupported_shape_count", std::to_string(unsupported));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(bad_geometry, 0U) << first_bad_geometry;
  EXPECT_GT(physics_systems, 0U);
  EXPECT_EQ(ragdoll_failures, 0U) << first_ragdoll_failure;
  EXPECT_GT(ragdolls, 0U);
  EXPECT_GT(ragdoll_constraints, 0U);
  EXPECT_GT(ragdoll_bones, 0U);
  EXPECT_EQ(pose_mismatches, 0U) << first_pose_mismatch;
}

TEST(NifCollisionRealData, AuthoredSingleBodySystemsMatchVanillaBytes) {
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
  std::size_t skipped = 0;
  std::size_t skipped_position_w = 0;
  std::size_t identical = 0;
  std::size_t mismatches = 0;
  std::size_t center_mismatches = 0;
  std::size_t rejected = 0;
  std::size_t failures = 0;
  std::string first_mismatch;
  std::string first_center_mismatch;
  std::string first_rejected;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && attempted < k_max_packfiles; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type != "bhkPhysicsSystem" || !has_packfile_magic(blob.bytes, 0)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          if (!single_authorable_body(sys)) continue;
          const auto& body = sys.bodies[0];
          const fo4::nif::HknpStaticBody static_body{body.position, body.orientation, body.packed_filter};
          const auto& shape = sys.shapes[static_cast<std::size_t>(body.shape)];
          if (!first_stage_encodable(sys, shape)) {
            ++skipped;
            continue;
          }
          if (position_w_is_set(sys)) {
            ++skipped_position_w;
            continue;
          }
          ++attempted;
          try {
            const auto authored_bytes =
                fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, static_body));
            const auto authored = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(authored_bytes));
            auto stored_blanked = sys.packfile;
            auto authored_blanked = authored.packfile;
            blank_mass_centre(stored_blanked);
            blank_mass_centre(authored_blanked);
            if (same_objects(stored_blanked, authored_blanked) &&
                fo4::nif::write_hknp_packfile(stored_blanked) == fo4::nif::write_hknp_packfile(authored_blanked)) {
              ++identical;
            } else {
              ++mismatches;
              if (first_mismatch.empty()) first_mismatch = path + " block " + std::to_string(blob.block);
            }
            if (!centers_agree(shape, authored)) {
              ++center_mismatches;
              if (first_center_mismatch.empty()) first_center_mismatch = path + " block " + std::to_string(blob.block);
            }
          } catch (const std::invalid_argument& error) {
            ++rejected;
            if (first_rejected.empty()) first_rejected = path + ": " + error.what();
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "single-body systems " << attempted << ", skipped (not first-stage) " << skipped
            << ", skipped (position w) " << skipped_position_w << ", rebuilt identical " << identical
            << " (mass centre blanked), mismatched " << mismatches << ", centre mismatches " << center_mismatches
            << ", rejected " << rejected << ", failures " << failures << '\n';
  RecordProperty("single_body_attempted_count", std::to_string(attempted));
  RecordProperty("single_body_identical_count", std::to_string(identical));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(rejected, 0U) << first_rejected;
  EXPECT_GE(attempted, 100U);
  EXPECT_GE(identical * 100, attempted * 99) << first_mismatch;
  EXPECT_EQ(center_mismatches, 0U) << first_center_mismatch;
}

TEST(NifCollisionRealData, ReencodedSpheresConvexAndCapsulesMatchVanilla) {
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
  std::size_t checked = 0;
  std::size_t skipped = 0;
  std::size_t mismatches = 0;
  std::size_t rejected = 0;
  std::size_t failures = 0;
  std::string first_mismatch;
  std::string first_rejected;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && checked < k_max_packfiles; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type != "bhkPhysicsSystem" || !has_packfile_magic(blob.bytes, 0)) continue;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          for (const auto& shape : sys.shapes) {
            if (shape.kind != fo4::nif::HknpShapeKind::sphere && shape.kind != fo4::nif::HknpShapeKind::capsule &&
                shape.kind != fo4::nif::HknpShapeKind::convex) {
              continue;
            }
            if (!first_stage_encodable(sys, shape)) {
              ++skipped;
              continue;
            }
            ++checked;
            try {
              const auto back = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(
                  fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}))));
              if (back.bodies.size() != 1 || back.bodies[0].shape < 0 ||
                  !same_shape(back.shapes[static_cast<std::size_t>(back.bodies[0].shape)], shape)) {
                ++mismatches;
                if (first_mismatch.empty()) first_mismatch = path + " block " + std::to_string(blob.block);
              }
            } catch (const std::invalid_argument& error) {
              ++rejected;
              if (first_rejected.empty()) first_rejected = path + ": " + error.what();
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "re-encoded shapes " << checked << ", skipped (not first-stage) " << skipped << ", mismatched "
            << mismatches << ", rejected " << rejected << ", failures " << failures << '\n';
  RecordProperty("reencoded_shape_count", std::to_string(checked));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(rejected, 0U) << first_rejected;
  EXPECT_EQ(mismatches, 0U) << first_mismatch;
  EXPECT_GT(checked, 0U);
}
