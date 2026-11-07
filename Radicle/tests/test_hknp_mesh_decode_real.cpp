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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 37;
constexpr float k_domain_tolerance = 1.0e-6f;

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

std::uint8_t u8_of(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  return std::to_integer<std::uint8_t>(object.bytes.at(offset));
}

std::uint32_t u32_of(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(object.bytes.at(offset + i))) << (8 * i);
  }
  return value;
}

float f32_of(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  const std::uint32_t bits = u32_of(object, offset);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

fo4::nif::HknpVec3 vec3_of(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  return {f32_of(object, offset), f32_of(object, offset + 4), f32_of(object, offset + 8)};
}

std::optional<std::size_t> local_of(const fo4::nif::HknpPackObject& object, std::uint32_t source) {
  for (const auto& fixup : object.local) {
    if (fixup.source == source) return fixup.target;
  }
  return std::nullopt;
}

std::optional<std::size_t> global_of(const fo4::nif::HknpPackObject& object, std::uint32_t source) {
  for (const auto& fixup : object.global) {
    if (fixup.source == source) return fixup.object;
  }
  return std::nullopt;
}

bool outside_domain(float value, float lo, float hi) {
  const float slack = k_domain_tolerance * std::max(hi - lo, 1.0e-6f);
  return value < lo - slack || value > hi + slack;
}

}  // namespace

// Detects: a vanilla compressed mesh whose vertices leave the domain beyond float rounding, whose triangle index points past the vertex list, whose section or triangle counts disagree with the stored primitive array, whose material runs do not cover their section, or whose decode throws on a sampled system; every sampled compressed mesh must hold all five.
TEST(HknpMeshDecodeRealData, VanillaCompressedMeshesDecodeWithinDomainAndCounts) {
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
  std::size_t meshes = 0;
  std::size_t unresolved = 0;
  std::size_t triangles = 0;
  std::size_t quads = 0;
  std::size_t vertices = 0;
  std::size_t sections = 0;
  std::size_t failures = 0;
  std::size_t out_of_domain = 0;
  std::size_t bad_index = 0;
  std::size_t count_mismatch = 0;
  std::size_t run_mismatch = 0;
  std::set<std::uint32_t> material_crcs;
  std::string first_failure;
  std::string first_out_of_domain;
  std::string first_bad_index;
  std::string first_count_mismatch;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (!has_packfile_magic(blob.bytes)) continue;
          ++systems;
          const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
          const auto& pack = sys.packfile;
          for (const auto& entry : sys.unsupported) {
            if (entry.class_name == "hknpCompressedMeshShape") ++unresolved;
          }
          for (const auto& shape : sys.shapes) {
            if (shape.kind != fo4::nif::HknpShapeKind::compressed_mesh) continue;
            ++meshes;
            const std::string where = path + " block " + std::to_string(blob.block);
            const auto& object = pack.objects[shape.object];
            const auto data_index = global_of(object, 0x60);
            const auto& data = pack.objects[data_index.value()];
            const auto lo = vec3_of(data, 0x20);
            const auto hi = vec3_of(data, 0x30);
            for (const auto& v : shape.mesh_vertices) {
              for (std::size_t k = 0; k < 3; ++k) {
                if (outside_domain(v[k], lo[k], hi[k])) {
                  ++out_of_domain;
                  if (first_out_of_domain.empty()) first_out_of_domain = where;
                  break;
                }
              }
            }
            vertices += shape.mesh_vertices.size();
            for (const auto& tri : shape.mesh_triangles) {
              ++triangles;
              if (tri.quad) ++quads;
              const std::size_t used = tri.quad ? 4 : 3;
              for (std::size_t k = 0; k < used; ++k) {
                if (tri.indices[k] >= shape.mesh_vertices.size()) {
                  ++bad_index;
                  if (first_bad_index.empty()) first_bad_index = where;
                }
              }
              material_crcs.insert(tri.material_crc);
            }

            const std::size_t section_base = local_of(data, 0x50).value_or(0);
            const std::uint32_t section_count = u32_of(data, 0x58);
            const std::size_t run_base = local_of(data, 0xA0).value_or(0);
            const std::uint32_t prim_count = u32_of(data, 0x68);
            std::uint64_t section_prims = 0;
            for (std::uint32_t s = 0; s < section_count; ++s) {
              const std::size_t at = section_base + 0x60 * std::size_t{s};
              const std::uint32_t num_prim = u32_of(data, at + 0x50) & 0xFFU;
              const std::uint32_t runs_word = u32_of(data, at + 0x54);
              std::uint32_t run_total = 0;
              for (std::uint32_t k = 0; k < (runs_word & 0xFFU); ++k) {
                const std::size_t run_at = run_base + 4 * std::size_t{(runs_word >> 8) + k};
                run_total += u8_of(data, run_at + 3);
              }
              ++sections;
              section_prims += num_prim;
              if (run_total != num_prim) {
                ++run_mismatch;
                if (first_count_mismatch.empty()) first_count_mismatch = where + " section " + std::to_string(s);
              }
            }
            if (section_prims != prim_count || shape.mesh_triangles.size() != prim_count ||
                shape.mesh_section_counts.size() != section_count) {
              ++count_mismatch;
              if (first_count_mismatch.empty()) first_count_mismatch = where;
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "decoded systems " << systems << ", compressed meshes " << meshes << " (unresolved shared "
            << unresolved << "), sections " << sections << ", triangles " << triangles << " (quads " << quads
            << "), vertices " << vertices << ", distinct material CRCs " << material_crcs.size() << '\n';
  std::cout << "failures " << failures << ", out of domain " << out_of_domain << ", bad index " << bad_index
            << ", count mismatch " << count_mismatch << ", run mismatch " << run_mismatch << '\n';
  RecordProperty("hknp_mesh_system_count", std::to_string(systems));
  RecordProperty("hknp_compressed_mesh_count", std::to_string(meshes));
  RecordProperty("hknp_compressed_mesh_unresolved_count", std::to_string(unresolved));
  RecordProperty("hknp_mesh_triangle_count", std::to_string(triangles));
  RecordProperty("hknp_mesh_quad_count", std::to_string(quads));
  RecordProperty("hknp_mesh_vertex_count", std::to_string(vertices));
  RecordProperty("hknp_mesh_material_crc_count", std::to_string(material_crcs.size()));

  EXPECT_GT(meshes, 0U);
  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(out_of_domain, 0U) << first_out_of_domain;
  EXPECT_EQ(bad_index, 0U) << first_bad_index;
  EXPECT_EQ(count_mismatch, 0U) << first_count_mismatch;
  EXPECT_EQ(run_mismatch, 0U) << first_count_mismatch;
}

// Detects: a body whose stored material slot stops matching its own body index, which would mean the slot is a table index after all and the resolved CRC must change; every sampled body must hold its own index.
TEST(HknpMeshDecodeRealData, VanillaBodySlotIsItsOwnBodyIndex) {
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
  std::size_t bodies = 0;
  std::size_t slot_mismatch = 0;
  std::size_t failures = 0;
  std::string first_mismatch;
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
          for (std::size_t b = 0; b < sys.bodies.size(); ++b) {
            ++bodies;
            if (sys.bodies[b].material_slot != b) {
              ++slot_mismatch;
              if (first_mismatch.empty()) first_mismatch = path + " body " + std::to_string(b);
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "bodies " << bodies << ", material slot not equal to body index " << slot_mismatch << ", failures "
            << failures << '\n';
  RecordProperty("hknp_body_count", std::to_string(bodies));
  RecordProperty("hknp_body_slot_mismatch_count", std::to_string(slot_mismatch));
  EXPECT_GT(bodies, 0U);
  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(slot_mismatch, 0U) << first_mismatch;
}
