#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/structure.hpp"
#include "nif_build.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "../src/nif/write_refs.hpp"
#include "real_data.hpp"

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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
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
          if (fo4::nif::remove_block(added_nif, last).bytes == original) {
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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
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

TEST(NifStructureRealData, VanillaShapeRemovalCountsItsOwnedBlocks) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t scanned = 0;
  std::size_t removed_shapes = 0;
  std::size_t owned_blocks = 0;
  std::size_t refused = 0;
  std::size_t failures = 0;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0;
         i < reader.entries().size() && removed_shapes < k_max_texture_sets && scanned < k_max_texture_scan; ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++scanned;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        const auto shapes = fo4::nif::read_shapes(nif);
        if (shapes.empty()) continue;
        fo4::nif::BlockRemoval removal;
        try {
          removal = fo4::nif::remove_block(nif, shapes.front().block);
        } catch (const fo4::parse_error&) {
          ++refused;
          continue;
        }
        const auto after = fo4::nif::read_nif(removal.bytes);
        if (nif.blocks().size() != after.blocks().size() + 1 + removal.removed.size()) {
          ++failures;
          if (first_failure.empty()) first_failure = path + ": block count after removal disagrees with the removed list";
          continue;
        }
        ++removed_shapes;
        owned_blocks += removal.removed.size();
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  std::cout << "shape removal probe: scanned " << scanned << " NIFs, shapes removed " << removed_shapes
            << ", dependent blocks removed " << owned_blocks << ", refused " << refused << ", failures " << failures
            << '\n';
  RecordProperty("shape_removal_count", std::to_string(removed_shapes));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GE(removed_shapes, 3U);
  EXPECT_GE(owned_blocks, 3U);
}

// Detects: a removal in a vanilla controller-manager NIF that leaves a manager, sequence, controller or palette reference pointing at the wrong block; the palette or a controller holds every block of the usual manager NIFs (FXEmptyExplosion01 among them), so this fixed NIF is used, where only a shape and its alpha property can go.
TEST(NifStructureRealData, VanillaControllerManagerNifRemovesABlockAndKeepsItsGraph) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  const std::string wanted = "meshes\\effects\\laserltblueprojectile.nif";
  std::optional<std::vector<std::byte>> original;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && !original; ++i) {
      if (lower(reader.entries()[i].path) == wanted) original = reader.extract(i);
    }
  }
  ASSERT_TRUE(original.has_value()) << wanted << " is not in the general archives";

  const auto nif = fo4::nif::read_nif(*original);
  std::optional<std::uint32_t> removed;
  fo4::nif::BlockRemoval edited;
  std::string refusal;
  for (std::uint32_t block = 1; block < nif.blocks().size() && !removed; ++block) {
    try {
      edited = fo4::nif::remove_block(nif, block);
      removed = block;
    } catch (const fo4::parse_error& error) {
      if (refusal.empty()) refusal = error.parse_message();
    }
  }
  ASSERT_TRUE(removed.has_value()) << refusal;

  std::vector<std::uint32_t> gone = edited.removed;
  gone.push_back(*removed);
  const auto is_gone = [&](std::int64_t index) {
    return index >= 0 && std::find(gone.begin(), gone.end(), static_cast<std::uint32_t>(index)) != gone.end();
  };
  const auto reread = fo4::nif::read_nif(edited.bytes);
  ASSERT_EQ(reread.blocks().size(), nif.blocks().size() - gone.size());
  const auto before = fo4::nif::write_detail::load_model(nif);
  const auto after = fo4::nif::write_detail::load_model(reread);
  std::size_t checked = 0;
  std::map<std::string, std::size_t> walked;
  for (std::uint32_t block = 0; block < before.blocks.size(); ++block) {
    if (is_gone(block)) continue;
    ++walked[std::string(fo4::nif::write_detail::block_type(before, block))];
    const auto mapped = block - static_cast<std::uint32_t>(
        std::count_if(gone.begin(), gone.end(), [&](std::uint32_t g) { return g < block; }));
    std::vector<std::int64_t> old_targets;
    for (const auto offset : fo4::nif::write_detail::ref_offsets(before, block)) {
      const auto target = fo4::nif::write_detail::int_at(before.blocks[block], offset);
      if (is_gone(target)) continue;
      old_targets.push_back(target < 0 ? target
                                       : target - std::count_if(gone.begin(), gone.end(), [&](std::uint32_t g) {
                                           return static_cast<std::int64_t>(g) < target;
                                         }));
    }
    std::vector<std::int64_t> new_targets;
    for (const auto offset : fo4::nif::write_detail::ref_offsets(after, mapped)) {
      new_targets.push_back(fo4::nif::write_detail::int_at(after.blocks[mapped], offset));
    }
    EXPECT_EQ(new_targets, old_targets) << "block " << block;
    checked += new_targets.size();
  }
  for (const auto& [type, count] : walked) std::cout << "walked " << type << " " << count << "\n";
  EXPECT_GT(checked, 0U);
  EXPECT_GT(walked["NiControllerManager"] + walked["NiControllerSequence"] + walked["NiDefaultAVObjectPalette"], 0U);
}

TEST(NifStructureRealData, VanillaParticleEffectAndSceneLayoutsWalkEveryBlock) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  const std::vector<std::string> wanted_nifs{
      "meshes\\effects\\muzpistolsmall01.nif",
      "meshes\\effects\\mpsmuzsmokepuffsmall02.nif",
      "meshes\\water\\water1024.nif",
      "meshes\\sky\\stars.nif",
      "meshes\\vehicles\\automotive\\fusionflea01.nif",
      "meshes\\effects\\explosiongrenade01.nif",
      "meshes\\effects\\defaulttracerbeam.nif",
      "meshes\\effects\\impactnuke01.nif",
      "meshes\\actors\\character\\_1stperson\\characterassets\\skeleton.nif",
  };
  const std::vector<std::string> expected{
      "NiParticleSystem",
      "NiPSysData",
      "NiPSysAgeDeathModifier",
      "NiPSysEmitterCtlr",
      "NiPSysBoxEmitter",
      "BSMasterParticleSystem",
      "BSPSysMultiTargetEmitterCtlr",
      "BSMultiBound",
      "BSMultiBoundOBB",
      "BSWaterShaderProperty",
      "BSSkyShaderProperty",
      "BSBlastNode",
      "BSDamageStage",
      "NiPointLight",
      "BSProceduralLightningController",
      "NiCamera",
  };

  std::map<std::string, std::size_t> walked;
  for (const auto& wanted : wanted_nifs) {
    std::optional<std::vector<std::byte>> original;
    for (const auto& reader : assets.archives()) {
      if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
      for (std::size_t i = 0; i < reader.entries().size() && !original; ++i) {
        if (lower(reader.entries()[i].path) == wanted) original = reader.extract(i);
      }
    }
    ASSERT_TRUE(original.has_value()) << wanted << " is not in the general archives";

    const auto model = fo4::nif::write_detail::load_model(fo4::nif::read_nif(*original));
    for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
      const auto type = std::string(fo4::nif::write_detail::block_type(model, block));
      if (std::find(expected.begin(), expected.end(), type) == expected.end()) continue;
      ++walked[type];
      for (const auto offset : fo4::nif::write_detail::ref_offsets(model, block)) {
        const auto target = fo4::nif::write_detail::int_at(model.blocks[block], offset);
        EXPECT_TRUE(target == -1 || (target >= 0 && static_cast<std::size_t>(target) < model.blocks.size()))
            << wanted << " block " << block << " " << type;
      }
    }
  }
  for (const auto& type : expected) {
    std::cout << "walked " << type << " " << walked[type] << "\n";
    EXPECT_GT(walked[type], 0U) << type;
  }
}
