#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
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
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_max_blobs = 200;
constexpr std::size_t k_max_packfiles = 200;
constexpr std::size_t k_entry_stride = 37;

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
