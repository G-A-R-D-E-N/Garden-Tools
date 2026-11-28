#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

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

// Constraint: a compressed mesh child is counted by triangles, because a re-encoded mesh stores a different vertex count.
std::size_t child_primitive_count(const fo4::nif::HknpShape& shape) {
  if (shape.kind == fo4::nif::HknpShapeKind::compressed_mesh) return shape.mesh_triangles.size();
  return shape.vertices.size();
}

}  // namespace

// Invariant: every supported vanilla compound re-authors to the same children, so a lost child, a moved placement, a changed child kind, or a changed child vertex count (triangle count for a compressed mesh) fails this test.
TEST(NifCollisionCompoundRealData, VanillaCompoundsReauthorToTheSameChildren) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t compounds = 0;
  std::size_t rebuilt = 0;
  std::size_t skipped_unsupported = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_mismatch;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (!has_packfile_magic(blob.bytes, 0)) continue;
          const auto system =
              fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          for (const auto& shape : system.shapes) {
            if (shape.kind != fo4::nif::HknpShapeKind::compound) continue;
            if (shape.instances.empty()) continue;
            bool supported = true;
            for (const auto& instance : shape.instances) {
              if (instance.shape < 0 ||
                  static_cast<std::size_t>(instance.shape) >= system.shapes.size()) {
                supported = false;
                break;
              }
            }
            if (!supported) {
              ++skipped_unsupported;
              continue;
            }
            ++compounds;
            std::vector<fo4::nif::HknpCompoundChild> children;
            children.reserve(shape.instances.size());
            for (const auto& instance : shape.instances) {
              fo4::nif::HknpCompoundChild child;
              child.shape = system.shapes[static_cast<std::size_t>(instance.shape)];
              child.placement = instance;
              children.push_back(std::move(child));
            }
            try {
              const auto authored = fo4::nif::author_hknp_compound_body(children, {});
              const auto again = fo4::nif::decode_hknp_system(
                  fo4::nif::read_hknp_packfile(fo4::nif::write_hknp_packfile(authored)));
              const fo4::nif::HknpShape* again_compound = nullptr;
              for (const auto& candidate : again.shapes) {
                if (candidate.kind == fo4::nif::HknpShapeKind::compound) {
                  again_compound = &candidate;
                  break;
                }
              }
              if (again_compound == nullptr ||
                  again_compound->instances.size() != shape.instances.size()) {
                if (first_mismatch.empty()) first_mismatch = path + " block " + std::to_string(blob.block);
                continue;
              }
              bool same = true;
              for (std::size_t k = 0; k < shape.instances.size() && same; ++k) {
                const auto& want = system.shapes[static_cast<std::size_t>(shape.instances[k].shape)];
                const auto& got =
                    again.shapes[static_cast<std::size_t>(again_compound->instances[k].shape)];
                same = got.kind == want.kind &&
                       child_primitive_count(got) == child_primitive_count(want);
                for (std::size_t axis = 0; axis < 3 && same; ++axis) {
                  same = std::fabs(again_compound->instances[k].translation[axis] -
                                   shape.instances[k].translation[axis]) <= 1.0e-4f;
                }
              }
              if (same) {
                ++rebuilt;
              } else if (first_mismatch.empty()) {
                first_mismatch = path + " block " + std::to_string(blob.block);
              }
            } catch (const std::invalid_argument& error) {
              ++failures;
              if (first_failure.empty()) first_failure = path + ": " + error.what();
            } catch (const fo4::parse_error& error) {
              ++failures;
              if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "compounds " << compounds << ", rebuilt to the same children " << rebuilt
            << ", skipped unsupported " << skipped_unsupported << ", failures " << failures << '\n';
  if (!first_mismatch.empty()) std::cout << "first mismatch: " << first_mismatch << '\n';
  if (!first_failure.empty()) std::cout << "first failure: " << first_failure << '\n';
  RecordProperty("compound_count", std::to_string(compounds));
  RecordProperty("compound_rebuilt_count", std::to_string(rebuilt));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GT(compounds, 0U);
  EXPECT_EQ(rebuilt, compounds) << first_mismatch;
}
