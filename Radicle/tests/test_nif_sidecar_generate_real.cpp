#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/sidecar_generate.hpp"
#include "fo4/nif/sidecars.hpp"
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
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t k_pinned_outfits_matched = 24;

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

}  // namespace

// Detects: a generator whose bone names, delta IDs or base bone drift from the shipped vanilla sidecars; each vanilla outfit's segment table must regenerate its shipped shape entry exactly, and the count of exact outfits is pinned so any change shows up.
TEST(NifSidecarGenerateReal, RegeneratesShippedOutfitSidecarsFromTheirSegmentTables) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t outfits = 0;
  std::size_t outfits_matched = 0;
  std::size_t shapes = 0;
  std::size_t shapes_matched = 0;
  std::size_t shapes_listing_segments = 0;
  std::size_t refused = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (lower(std::filesystem::path(path).extension().string()) != ".nif") continue;
      try {
        const auto bytes_of_nif = reader.extract(i);
        const auto nif = fo4::nif::read_nif(bytes_of_nif);
        bool seen = false;
        bool all_matched = true;
        for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
          if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
          const auto table = fo4::nif::read_segments(nif, block);
          if (table.ssf_file.empty()) continue;
          const auto location = assets.locate_in_archives(directory_of(path) + leaf_of(table.ssf_file));
          if (!location) continue;
          const auto bytes = assets.read(*location);
          const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
          const auto name = fo4::nif::read_shape(nif, block).name;
          const auto shipped = fo4::nif::read_ssf(text);
          const auto entry = std::find_if(shipped.begin(), shipped.end(),
                                          [&](const fo4::nif::SsfShape& shape) { return shape.name == name; });
          if (entry == shipped.end()) continue;
          bool matched = false;
          try {
            matched = same_assignments(fo4::nif::ssf_shape_from_segments(table, name, nif.strings()), *entry);
          } catch (const fo4::parse_error&) {
            ++refused;
          }
          seen = true;
          all_matched = all_matched && matched;
          ++shapes;
          shapes_matched += matched ? 1 : 0;
          shapes_listing_segments += fo4::nif::ssf_lists_segments(*entry, table) ? 1 : 0;
        }
        if (seen) {
          ++outfits;
          outfits_matched += all_matched ? 1 : 0;
        }
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  std::cout << "outfits matched " << outfits_matched << " of " << outfits
            << "; shapes matched " << shapes_matched << " of " << shapes
            << " (" << refused << " refused by the generator)"
            << "; shipped sidecars list the same segments for " << shapes_listing_segments << " of " << shapes << '\n';
  EXPECT_GT(outfits, 0U);
  EXPECT_EQ(outfits_matched, k_pinned_outfits_matched);
}
