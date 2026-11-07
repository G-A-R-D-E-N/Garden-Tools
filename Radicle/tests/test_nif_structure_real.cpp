#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/structure.hpp"
#include "nif_build.hpp"
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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_entry_stride = 37;
constexpr std::size_t k_max_nifs = 200;
constexpr std::size_t k_max_texture_sets = 5;
constexpr std::size_t k_max_texture_scan = 4000;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) { return std::filesystem::path(lower(path)).extension() == ".nif"; }

std::string refusal_key(const std::string& message) {
  const std::string_view head = "block type ";
  const auto start = message.find(head);
  if (start == std::string::npos) return message;
  const auto end = message.find(" has no reference layout", start);
  if (end == std::string::npos) return message;
  return message.substr(start + head.size(), end - start - head.size());
}

std::vector<std::byte> node_payload() {
  return nif_build::named_node_block("NiNode", -1, fo4::nif::Transform{}, {}).data;
}

std::optional<std::vector<std::byte>> add_under_any_node(const fo4::nif::NifFile& nif, std::string& refusal) {
  for (const auto& node : nif.nodes()) {
    try {
      return fo4::nif::add_block(nif, node.block, "NiNode", node_payload());
    } catch (const fo4::parse_error& error) {
      if (refusal.empty()) refusal = error.parse_message();
    }
  }
  return std::nullopt;
}

}  // namespace

// Detects: an add then remove on a vanilla NIF that leaves any byte changed, such as a child list entry, a block size, a type table entry or a block count left behind; a sampled NIF whose add-then-remove is refused by a mapped reference layout is counted, never guessed.
TEST(NifStructureRealData, VanillaNifsRoundTripAddThenRemoveByteForByte) {
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
  std::size_t zero_edit_mismatches = 0;
  std::size_t identical = 0;
  std::size_t mismatches = 0;
  std::size_t refused_add = 0;
  std::size_t refused_remove = 0;
  std::size_t failures = 0;
  std::map<std::string, std::size_t> refusals;
  std::string first_failure;
  std::string first_mismatch;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && nifs < k_max_nifs; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      try {
        const auto original = reader.extract(i);
        const auto nif = fo4::nif::read_nif(original);
        if (fo4::nif::write_nif(nif) != original) {
          ++zero_edit_mismatches;
          if (first_mismatch.empty()) first_mismatch = path + " (zero edit)";
        }
        std::string refusal;
        const auto added = add_under_any_node(nif, refusal);
        if (!added) {
          ++refused_add;
          ++refusals[refusal_key(refusal)];
          continue;
        }
        const auto added_nif = fo4::nif::read_nif(*added);
        const auto last = static_cast<std::uint32_t>(added_nif.blocks().size() - 1);
        try {
          if (fo4::nif::remove_block(added_nif, last) == original) {
            ++identical;
          } else {
            ++mismatches;
            if (first_mismatch.empty()) first_mismatch = path;
          }
        } catch (const fo4::parse_error& error) {
          ++refused_remove;
          ++refusals[refusal_key(error.parse_message())];
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
    if (nifs >= k_max_nifs) break;
  }

  std::cout << "structure round trip: nifs " << nifs << ", zero edit mismatched " << zero_edit_mismatches
            << ", add then remove identical " << identical << ", mismatched " << mismatches
            << ", refused on add " << refused_add << ", refused on remove " << refused_remove << ", failures "
            << failures << '\n';
  for (const auto& [kind, count] : refusals) std::cout << "  refused by " << kind << ": " << count << '\n';
  RecordProperty("structure_nif_count", std::to_string(nifs));
  RecordProperty("structure_identical_count", std::to_string(identical));
  RecordProperty("structure_refused_count", std::to_string(refused_add + refused_remove));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(zero_edit_mismatches, 0U) << first_mismatch;
  EXPECT_EQ(mismatches, 0U) << first_mismatch;
  EXPECT_GE(identical, 100U);
}

// Detects: a texture set written to the wrong slot, or onto a shader it does not belong to, on a vanilla shape whose lighting shader has no texture set; the created NIF must reread with the textures and the shape must keep its block index.
TEST(NifStructureRealData, VanillaShapeWithoutTextureSetGetsOneAndRereads) {
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
  const std::vector<std::string> textures{"textures\\structure_probe_d.dds", "textures\\structure_probe_n.dds"};
  std::size_t scanned = 0;
  std::size_t candidates = 0;
  std::size_t created = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_example;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && created < k_max_texture_sets && scanned < k_max_texture_scan;
         ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++scanned;
      try {
        const auto original = reader.extract(i);
        const auto nif = fo4::nif::read_nif(original);
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          if (shape.shader_ref < 0 || created >= k_max_texture_sets) continue;
          const auto shader = static_cast<std::uint32_t>(shape.shader_ref);
          if (nif.blocks()[shader].type != "BSLightingShaderProperty") continue;
          if (fo4::nif::read_lighting_shader(nif, shader).texture_set_ref >= 0) continue;
          ++candidates;
          const auto bytes = fo4::nif::create_texture_set(nif, shape.block, textures);
          const auto back = fo4::nif::read_nif(bytes);
          const auto reread = fo4::nif::read_shapes(back);
          const auto same = std::find_if(reread.begin(), reread.end(),
                                         [&shape](const fo4::nif::Shape& other) { return other.block == shape.block; });
          if (same == reread.end() || same->shader_ref < 0) {
            ++failures;
            if (first_failure.empty()) first_failure = path + ": shape lost its shader after create";
            continue;
          }
          const auto lit = fo4::nif::read_lighting_shader(back, static_cast<std::uint32_t>(same->shader_ref));
          if (lit.texture_set_ref < 0 || lit.textures != textures) {
            ++failures;
            if (first_failure.empty()) first_failure = path + ": texture set did not reread with its textures";
            continue;
          }
          ++created;
          if (first_example.empty()) first_example = path + " shape block " + std::to_string(shape.block);
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "texture set probe: scanned " << scanned << " NIFs, shapes without a texture set " << candidates
            << ", created and reread " << created << ", failures " << failures << '\n';
  if (!first_example.empty()) std::cout << "first created: " << first_example << '\n';
  RecordProperty("texture_set_created_count", std::to_string(created));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GE(created, 3U);
}
