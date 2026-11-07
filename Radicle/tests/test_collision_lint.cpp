#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision_lint.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fo4::nif::CollisionIssue;
using nif_build::Block;
using nif_build::Writer;

constexpr std::uint32_t k_box_crc = 0x064003D4U;
constexpr std::uint16_t k_owner_flags = 0x80;
constexpr std::size_t k_constraint_root_array = 0x50;
constexpr std::size_t k_constraint_root_count = 0x58;
constexpr std::size_t k_constraint_binding = 0x70;
constexpr std::size_t k_compound_array = 0x60;
constexpr std::size_t k_compound_instance = 0x70;
constexpr std::size_t k_compound_child = 0x50;
constexpr std::size_t k_compound_size = 0xF0;

fo4::nif::HknpPackfile box_struct(std::uint32_t packed_filter = 1) {
  fo4::nif::HknpStaticBody body;
  body.packed_filter = packed_filter;
  return fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, k_box_crc),
                                           body);
}

std::vector<std::byte> box_packfile(std::uint32_t packed_filter = 1) {
  return fo4::nif::write_hknp_packfile(box_struct(packed_filter));
}

std::size_t box_shape_object(const fo4::nif::HknpPackfile& pack) {
  const auto& root = pack.objects.front();
  std::uint32_t cinfo = 0;
  for (const auto& fixup : root.local) {
    if (fixup.source == 0x40) cinfo = fixup.target;
  }
  for (const auto& fixup : root.global) {
    if (fixup.source == cinfo) return fixup.object;
  }
  throw std::runtime_error("box packfile has no body shape");
}

fo4::nif::HknpPackfile compound_struct(const std::string& cls) {
  auto pack = box_struct();
  const auto shape = box_shape_object(pack);
  std::uint32_t cinfo = 0;
  for (const auto& fixup : pack.objects.front().local) {
    if (fixup.source == 0x40) cinfo = fixup.target;
  }
  fo4::nif::HknpPackObject compound;
  compound.class_name = cls;
  compound.bytes.assign(k_compound_size, std::byte{0});
  compound.bytes[k_compound_array + 8] = std::byte{1};
  compound.local.push_back({static_cast<std::uint32_t>(k_compound_array), static_cast<std::uint32_t>(k_compound_instance)});
  compound.global.push_back({static_cast<std::uint32_t>(k_compound_instance + k_compound_child), static_cast<std::uint32_t>(shape)});
  pack.classes.push_back({0x5EEDF00DU, cls});
  pack.objects.push_back(compound);
  const auto compound_index = static_cast<std::uint32_t>(pack.objects.size() - 1);
  for (auto& fixup : pack.objects.front().global) {
    if (fixup.source == cinfo) fixup.object = compound_index;
  }
  return pack;
}

fo4::nif::HknpPackfile constraint_struct() {
  fo4::nif::HknpPackfile pack;
  pack.classes = {{0x00C0FFEEU, "hknpPhysicsSystemData"}, {0x0BADF00DU, "hkpBallAndSocketConstraintData"}};
  fo4::nif::HknpPackObject root;
  root.class_name = "hknpPhysicsSystemData";
  root.bytes.assign(k_constraint_binding + 0x18, std::byte{0});
  root.bytes[k_constraint_root_count] = std::byte{1};
  root.local.push_back({static_cast<std::uint32_t>(k_constraint_root_array), static_cast<std::uint32_t>(k_constraint_binding)});
  root.global.push_back({static_cast<std::uint32_t>(k_constraint_binding), 1U});
  fo4::nif::HknpPackObject constraint;
  constraint.class_name = "hkpBallAndSocketConstraintData";
  constraint.bytes.assign(0x30, std::byte{0});
  constraint.bytes[0x20] = std::byte{0x1d};
  pack.objects = {root, constraint};
  return pack;
}

fo4::nif::HknpPackfile renamed_shape_struct() {
  auto pack = box_struct();
  const auto shape = box_shape_object(pack);
  const auto old_name = pack.objects[shape].class_name;
  for (auto& cls : pack.classes) {
    if (cls.name == old_name) cls.name = "hknpMoppBvTreeShape";
  }
  for (auto& object : pack.objects) {
    if (object.class_name == old_name) object.class_name = "hknpMoppBvTreeShape";
  }
  return pack;
}

Block node_block(std::int32_t collision, const std::vector<std::int32_t>& extras,
                 const std::vector<std::int32_t>& children) {
  Writer w;
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) w.i32(extra);
  w.i32(-1);
  w.u32(0);
  w.transform(fo4::nif::Transform{});
  w.i32(collision);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return Block{"NiNode", w.bytes()};
}

Block object_block(std::int32_t target, std::int32_t data, std::uint32_t body) {
  Writer w;
  w.i32(target);
  w.u16(k_owner_flags);
  w.i32(data);
  w.u32(body);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

Block system_block(const std::vector<std::byte>& packfile) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(packfile.size()));
  w.append(packfile);
  return Block{"bhkPhysicsSystem", w.bytes()};
}

Block bsx_block(std::uint32_t flags) {
  Writer w;
  w.i32(-1);
  w.u32(flags);
  return Block{"BSXFlags", w.bytes()};
}

std::vector<std::byte> collision_nif(const std::vector<std::byte>& packfile, std::int32_t target, std::int32_t data,
                                     std::uint32_t body, std::uint32_t bsx_flags) {
  return nif_build::build_nif({}, {node_block(-1, {4}, {1}), node_block(2, {}, {}), object_block(target, data, body),
                                   system_block(packfile), bsx_block(bsx_flags)},
                              {0});
}

std::vector<fo4::nif::CollisionFinding> findings(const std::vector<std::byte>& bytes) {
  return fo4::nif::lint_collision(fo4::nif::read_nif(bytes));
}

void expect_exactly(const std::vector<std::byte>& bytes, CollisionIssue issue, std::uint32_t block) {
  const auto found = findings(bytes);
  ASSERT_EQ(found.size(), 1U);
  EXPECT_TRUE(found[0].issue == issue);
  EXPECT_EQ(found[0].block, block);
}

bool has_finding(const std::vector<std::byte>& bytes, CollisionIssue issue, std::uint32_t block) {
  const auto found = findings(bytes);
  return std::any_of(found.begin(), found.end(), [issue, block](const fo4::nif::CollisionFinding& finding) {
    return finding.issue == issue && finding.block == block;
  });
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

}  // namespace

// Detects: a clean physics system with its object, system and BSX Havok flag reporting a finding; the lint is only useful if a sound file is silent.
TEST(CollisionLint, AWellFormedBoxCollisionHasNoFindings) {
  EXPECT_TRUE(findings(collision_nif(box_packfile(), 1, 3, 0, 2)).empty());
}

// Detects: a collision object whose target is no node (the node was dropped) going unreported as dangling.
TEST(CollisionLint, ReportsAnObjectWhoseTargetIsNotANode) {
  expect_exactly(collision_nif(box_packfile(), -1, 3, 0, 2), CollisionIssue::dangling_object, 2);
}

// Detects: a collision object whose system reference is missing going unreported as dangling.
TEST(CollisionLint, ReportsAnObjectWhoseSystemIsMissing) {
  EXPECT_TRUE(has_finding(collision_nif(box_packfile(), 1, -1, 0, 2), CollisionIssue::dangling_object, 2));
}

// Detects: a physics system left behind after its object is removed, which a lint must report as orphaned.
TEST(CollisionLint, ReportsASystemThatNoObjectNames) {
  const auto nif = nif_build::build_nif({}, {node_block(-1, {}, {}), system_block(box_packfile())}, {0});
  expect_exactly(nif, CollisionIssue::system_without_object, 1);
}

// Detects: a body id past the end of its system going unnoticed, which crashes or mis-places the body at load.
TEST(CollisionLint, ReportsABodyIdPastTheEndOfItsSystem) {
  expect_exactly(collision_nif(box_packfile(), 1, 3, 1, 2), CollisionIssue::body_out_of_range, 2);
}

// Detects: a body on layer 0 (unidentified) going unreported, which the engine's collision filter then ignores.
TEST(CollisionLint, ReportsABodyOnLayerZero) {
  expect_exactly(collision_nif(box_packfile(0), 1, 3, 0, 2), CollisionIssue::layer_zero, 3);
}

// Detects: a collision object with no BSX Havok flag in the file, which the engine treats as no physics.
TEST(CollisionLint, ReportsCollisionWithoutTheBsxHavokFlag) {
  expect_exactly(collision_nif(box_packfile(), 1, 3, 0, 0), CollisionIssue::bsx_mismatch, 2);
}

// Detects: the lint flagging BSX Havok without collision, which vanilla has in animation and skeleton files.
TEST(CollisionLint, BsxHavokWithoutCollisionIsNotAFinding) {
  const auto nif = nif_build::build_nif({}, {node_block(-1, {1}, {}), bsx_block(2)}, {0});
  EXPECT_TRUE(findings(nif).empty());
}

// Detects: a one-child dynamic compound going unreported, which the Havok compiler rejects.
TEST(CollisionLint, ReportsACompoundWithOneChild) {
  expect_exactly(collision_nif(fo4::nif::write_hknp_packfile(compound_struct("hknpDynamicCompoundShape")), 1, 3, 0, 2),
                 CollisionIssue::one_child_compound, 3);
}

// Detects: a static one-child compound, which vanilla ships at scale, being reported as a fault.
TEST(CollisionLint, OneChildStaticCompoundIsNotAFinding) {
  EXPECT_TRUE(findings(collision_nif(fo4::nif::write_hknp_packfile(compound_struct("hknpStaticCompoundShape")), 1, 3, 0, 2)).empty());
}

// Detects: a shape class the decoder does not handle going unreported, so the body would silently lose its collision.
TEST(CollisionLint, ReportsAShapeClassTheDecoderDoesNotHandle) {
  expect_exactly(collision_nif(fo4::nif::write_hknp_packfile(renamed_shape_struct()), 1, 3, 0, 2),
                 CollisionIssue::unsupported_shape, 3);
}

// Detects: a constraint atom type with no known length going unreported, so the constraint's remaining atoms are never checked.
TEST(CollisionLint, ReportsAConstraintAtomTheWalkCannotRead) {
  EXPECT_TRUE(has_finding(collision_nif(fo4::nif::write_hknp_packfile(constraint_struct()), 1, 3, 0, 2),
                          CollisionIssue::unread_atom, 3));
}

// Detects: a system payload that is not a packfile crashing the lint instead of being reported.
TEST(CollisionLint, ReportsAPayloadThatIsNotAPackfile) {
  expect_exactly(collision_nif(std::vector<std::byte>(16, std::byte{0x41}), 1, 3, 0, 2),
                 CollisionIssue::undecodable_system, 3);
}

// Detects: a vanilla collision system that the lint flags, so a rule contradicting Fallout 4 data would fire on every real file.
TEST(CollisionLintRealData, VanillaSampleHasNoStructuralFindings) {
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
  constexpr std::size_t k_stride = 37;
  std::size_t nifs = 0;
  std::size_t with_collision = 0;
  std::size_t failures = 0;
  std::map<CollisionIssue, std::size_t> counts;
  std::string first_failure;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_stride) {
      const auto& path = reader.entries()[i].path;
      if (lower(path).size() < 4 || lower(path).substr(lower(path).size() - 4) != ".nif") continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        const auto found = fo4::nif::lint_collision(nif);
        if (!found.empty()) ++with_collision;
        for (const auto& finding : found) {
          ++counts[finding.issue];
          if (first_failure.empty()) first_failure = path + " block " + std::to_string(finding.block);
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "nifs " << nifs << ", nifs with findings " << with_collision << ", parse failures " << failures << '\n';
  for (const auto& [issue, count] : counts) {
    std::cout << "issue " << static_cast<int>(issue) << " count " << count << '\n';
  }
  if (!first_failure.empty()) std::cout << "first finding: " << first_failure << '\n';
  EXPECT_EQ(counts[CollisionIssue::dangling_object], 0U);
  EXPECT_EQ(counts[CollisionIssue::system_without_object], 0U);
  EXPECT_EQ(counts[CollisionIssue::body_out_of_range], 0U);
  EXPECT_EQ(counts[CollisionIssue::bsx_mismatch], 0U);
  EXPECT_EQ(counts[CollisionIssue::undecodable_system], 0U);
  EXPECT_EQ(counts[CollisionIssue::layer_zero], 0U);
  EXPECT_EQ(counts[CollisionIssue::one_child_compound], 0U);
  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GT(nifs, 0U);
}
