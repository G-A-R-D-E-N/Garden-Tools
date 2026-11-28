#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/sidecar_generate.hpp"
#include "fo4/nif/sidecars.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
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
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t k_pinned_outfits_matched = 214;

std::string lower(std::string text) {
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

std::string directory_of(const std::string& path) {
  const auto cut = path.find_last_of("\\/");
  return cut == std::string::npos ? std::string{} : path.substr(0, cut + 1);
}

std::string leaf_of(const std::string& path) {
  const auto cut = path.find_last_of("\\/");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::map<std::string, std::vector<std::uint32_t>> deltas_by_bone(const fo4::nif::SsfShape& shape) {
  std::map<std::string, std::vector<std::uint32_t>> out;
  for (const auto& delta : shape.deltas) {
    auto ids = delta.segment_ids;
    std::sort(ids.begin(), ids.end());
    out[delta.bone_name] = std::move(ids);
  }
  return out;
}

bool same_assignments(const fo4::nif::SsfShape& generated, const fo4::nif::SsfShape& shipped) {
  return generated.base_bone == shipped.base_bone && deltas_by_bone(generated) == deltas_by_bone(shipped);
}

struct OutfitState {
  bool seen{false};
  bool refused{false};
  bool incomplete{false};
  bool all_matched{true};
};

struct ShippedShape {
  std::size_t outfit;
  std::string name;
  fo4::nif::SegmentTable table;
  fo4::nif::SsfShape shipped;
  std::vector<std::string> strings;
};

}  // namespace

// Detects: a generator whose bone names, delta IDs or base bone drift from the shipped vanilla sidecars (bone names come from the NIF strings plus the strings of the vanilla skeleton NIFs); the count of outfits whose segment table regenerates the shipped shape entry exactly is pinned, because the generator reads only sub-segments with cut offsets, never chooses a base bone, and writes no whole-segment IDs, so any change in that count shows up.
TEST(NifSidecarGenerateReal, RegeneratesShippedOutfitSidecarsFromTheirSegmentTables) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::vector<ShippedShape> shapes;
  std::set<std::string> bone_pool;
  std::size_t outfit = 0;
  std::size_t shapes_skipped = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (lower(std::filesystem::path(path).extension().string()) != ".nif") continue;
      try {
        const auto bytes_of_nif = reader.extract(i);
        const auto nif = fo4::nif::read_nif(bytes_of_nif);
        if (lower(path).find("skeleton") != std::string::npos) {
          bone_pool.insert(nif.strings().begin(), nif.strings().end());
        }
        ++outfit;
        for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
          if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
          auto table = fo4::nif::read_segments(nif, block);
          if (table.ssf_file.empty()) continue;
          const auto location = assets.locate_in_archives(directory_of(path) + leaf_of(table.ssf_file));
          if (!location) {
            ++shapes_skipped;
            continue;
          }
          const auto bytes = assets.read(*location);
          const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
          auto name = fo4::nif::read_shape(nif, block).name;
          auto shipped = fo4::nif::read_ssf(text);
          const auto entry = std::find_if(shipped.begin(), shipped.end(),
                                          [&](const fo4::nif::SsfShape& shape) { return shape.name == name; });
          if (entry == shipped.end()) {
            ++shapes_skipped;
            continue;
          }
          shapes.push_back({outfit, std::move(name), std::move(table), *entry, nif.strings()});
        }
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }

  std::map<std::size_t, OutfitState> outfits;
  std::size_t shapes_compared = 0;
  std::size_t shapes_refused = 0;
  std::size_t shapes_listing_segments = 0;
  for (const auto& shape : shapes) {
    auto& state = outfits[shape.outfit];
    auto strings = shape.strings;
    strings.insert(strings.end(), bone_pool.begin(), bone_pool.end());
    fo4::nif::SsfDiagnostics diagnostics;
    bool matched = false;
    try {
      matched = same_assignments(fo4::nif::ssf_shape_from_segments(shape.table, shape.name, strings, &diagnostics), shape.shipped);
    } catch (const fo4::parse_error&) {
      ++shapes_refused;
      state.refused = true;
      continue;
    }
    state.seen = true;
    state.incomplete = state.incomplete || !diagnostics.unresolved_ids.empty() || !diagnostics.ambiguous_ids.empty();
    state.all_matched = state.all_matched && matched;
    ++shapes_compared;
    shapes_listing_segments += fo4::nif::ssf_lists_segments(shape.shipped, shape.table) ? 1 : 0;
  }
  std::size_t outfits_examined = 0;
  std::size_t outfits_matched = 0;
  std::size_t outfits_mismatched = 0;
  std::size_t outfits_refused = 0;
  std::size_t outfits_incomplete = 0;
  for (const auto& [id, state] : outfits) {
    if (state.refused) {
      ++outfits_refused;
    } else if (state.seen) {
      ++outfits_examined;
      if (state.all_matched) {
        ++outfits_matched;
      } else {
        ++outfits_mismatched;
      }
      outfits_incomplete += state.incomplete ? 1 : 0;
    }
  }
  std::cout << "outfits examined " << outfits_examined << ": partial-output matches " << outfits_matched
            << ", mismatched " << outfits_mismatched << ", refused by the generator " << outfits_refused
            << ", with unresolved or ambiguous bone IDs " << outfits_incomplete
            << "; shapes compared " << shapes_compared << ", refused " << shapes_refused
            << ", skipped (no sidecar or entry) " << shapes_skipped
            << "; bone-name pool " << bone_pool.size()
            << "; shipped sidecars list the same segments for " << shapes_listing_segments << " of " << shapes_compared << '\n';
  EXPECT_GT(outfits_examined, 0U);
  EXPECT_EQ(outfits_examined + outfits_refused, 724U);
  EXPECT_EQ(outfits_matched, k_pinned_outfits_matched);
  EXPECT_EQ(outfits_refused, 0U);
}
