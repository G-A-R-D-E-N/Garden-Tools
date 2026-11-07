#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode_constraints.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 7;

struct ClassTally {
  std::size_t total{0};
  std::size_t exact{0};
  std::map<std::size_t, std::size_t> differing_offsets;
};

std::vector<std::size_t> packfile_starts(const std::vector<std::byte>& bytes) {
  static constexpr std::array<std::byte, 4> k_magic = {std::byte{0x57}, std::byte{0xE0}, std::byte{0xE0},
                                                       std::byte{0x57}};
  std::vector<std::size_t> starts;
  if (bytes.size() < 8) return starts;
  auto hit = std::search(bytes.begin() + 4, bytes.end(), k_magic.begin(), k_magic.end());
  while (hit != bytes.end()) {
    starts.push_back(static_cast<std::size_t>(hit - bytes.begin()));
    hit = std::search(hit + 4, bytes.end(), k_magic.begin(), k_magic.end());
  }
  return starts;
}

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  }
  return value;
}

float f32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  const std::uint32_t bits = u32_at(bytes, at);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

bool mentions_constraints(const std::vector<std::byte>& bytes) {
  static constexpr std::array<std::byte, 10> k_text = {std::byte{'C'}, std::byte{'o'}, std::byte{'n'}, std::byte{'s'},
                                                       std::byte{'t'}, std::byte{'r'}, std::byte{'a'}, std::byte{'i'},
                                                       std::byte{'n'}, std::byte{'t'}};
  return std::search(bytes.begin(), bytes.end(), k_text.begin(), k_text.end()) != bytes.end();
}

void tally(std::map<std::string, ClassTally>& tallies, const std::string& kind, const std::vector<std::byte>& encoded,
           const std::vector<std::byte>& stored) {
  ClassTally& entry = tallies[kind];
  ++entry.total;
  if (encoded == stored) {
    ++entry.exact;
    return;
  }
  for (std::size_t i = 0; i < std::min(encoded.size(), stored.size()); ++i) {
    if (encoded[i] != stored[i]) ++entry.differing_offsets[i];
  }
}

}  // namespace

// Detects: a vanilla constraint whose atom chain stops on a type the table lacks, or a writer whose bytes differ from the stored object; every constraint must walk to its terminator and each writer must rebuild the stored bytes.
TEST(HknpConstraintRealData, VanillaConstraintsWalkFullyAndWriteBackToTheStoredBytes) {
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
  std::map<std::string, ClassTally> tallies;
  std::size_t packfiles = 0;
  std::size_t rebuild_mismatches = 0;
  std::string first_rebuild;
  std::size_t constraints = 0;
  std::size_t unread = 0;
  std::size_t failures = 0;
  std::string first_unread;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (path.size() < 4 || path.compare(path.size() - 4, 4, ".nif") != 0) continue;
      const auto bytes = reader.extract(i);
      if (!mentions_constraints(bytes)) continue;
      for (const std::size_t m : packfile_starts(bytes)) {
        const std::size_t length = u32_at(bytes, m - 4);
        if (length < 0x50 || length > bytes.size() - m) continue;
        const std::span<const std::byte> packfile(bytes.data() + m, length);
        try {
          auto pack = fo4::nif::read_hknp_packfile(packfile);
          if (pack.objects.empty()) continue;
          const auto& root = pack.objects.front().class_name;
          if (root != "hknpPhysicsSystemData" && root != "hknpRagdollData") continue;
          ++packfiles;
          const auto rebuilt = fo4::nif::write_hknp_packfile(pack);
          if (!std::equal(rebuilt.begin(), rebuilt.end(), packfile.begin(), packfile.end())) {
            ++rebuild_mismatches;
            if (first_rebuild.empty()) first_rebuild = path + " at " + std::to_string(m);
          }
          const auto sys = fo4::nif::decode_hknp_system(std::move(pack));
          for (const auto& constraint : sys.constraints) {
            ++constraints;
            if (constraint.unread_atom != 0) {
              ++unread;
              if (first_unread.empty()) first_unread = path + " atom " + std::to_string(constraint.unread_atom);
            }
            if (constraint.object >= sys.packfile.objects.size()) continue;
            const auto& data = sys.packfile.objects[constraint.object].bytes;
            if (constraint.kind == "hkpRagdollConstraintData") {
              tally(tallies, constraint.kind, fo4::nif::encode_ragdoll_constraint(constraint), data);
            } else if (constraint.kind == "hkpLimitedHingeConstraintData") {
              tally(tallies, constraint.kind, fo4::nif::encode_limited_hinge_constraint(constraint), data);
            } else if (constraint.kind == "hkpBallAndSocketConstraintData") {
              tally(tallies, constraint.kind, fo4::nif::encode_ball_and_socket_constraint(constraint), data);
            } else if (constraint.kind == "hkpPrismaticConstraintData") {
              tally(tallies, constraint.kind, fo4::nif::encode_prismatic_constraint(constraint), data);
            }
          }
          for (const auto& object : sys.packfile.objects) {
            if (object.class_name == "hknpBreakableConstraintData") {
              fo4::nif::HknpConstraint wrapper;
              wrapper.break_threshold = f32_at(object.bytes, 0x20);
              tally(tallies, object.class_name, fo4::nif::encode_breakable_constraint(wrapper), object.bytes);
            } else if (object.class_name == "hkpPositionConstraintMotor") {
              fo4::nif::HknpPositionMotor motor;
              motor.tau = f32_at(object.bytes, 32);
              motor.damping = f32_at(object.bytes, 36);
              motor.proportional_recovery_velocity = f32_at(object.bytes, 40);
              motor.constant_recovery_velocity = f32_at(object.bytes, 44);
              tally(tallies, object.class_name, fo4::nif::encode_position_motor(motor), object.bytes);
            }
          }
        } catch (const fo4::parse_error& error) {
          ++failures;
          if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
        }
      }
    }
  }

  std::cout << "packfiles with constraint roots " << packfiles << ", rebuild mismatches " << rebuild_mismatches
            << ", constraints " << constraints
            << ", unread atoms " << unread << ", failures " << failures << '\n';
  for (const auto& [kind, entry] : tallies) {
    std::cout << kind << ": " << entry.exact << " of " << entry.total << " write back exactly";
    std::size_t shown = 0;
    for (const auto& [offset, count] : entry.differing_offsets) {
      if (shown++ == 6) break;
      std::cout << (shown == 1 ? ", differing at 0x" : ", 0x") << std::hex << offset << std::dec << " (" << count << ")";
    }
    std::cout << '\n';
    RecordProperty(kind + "_count", std::to_string(entry.total));
    RecordProperty(kind + "_exact_count", std::to_string(entry.exact));
  }
  if (!first_unread.empty()) std::cout << "first unread atom: " << first_unread << '\n';

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(rebuild_mismatches, 0U) << first_rebuild;
  EXPECT_EQ(unread, 0U) << first_unread;
  EXPECT_GT(constraints, 0U);
  for (const auto& [kind, entry] : tallies) {
    EXPECT_EQ(entry.exact, entry.total) << kind;
  }
}
