#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
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
#include <string_view>
#include <vector>

namespace {

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool has_extension(const std::string& path, std::string_view extension) {
  return lower(std::filesystem::path(path).extension().string()) == extension;
}

bool is_handled_geometry(std::string_view type) {
  return type == "BSTriShape" || type == "BSSubIndexTriShape" || type == "BSMeshLODTriShape" ||
         type == "BSDynamicTriShape";
}

bool is_geometry_name(std::string_view type) {
  return type.ends_with("TriShape") || type.ends_with("TriStrips");
}

}  // namespace

TEST(NifCorpus, ReadsEveryVanillaNifAndHandlesGeometryTypes) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::string, std::size_t> histogram;
  std::map<std::string, std::string> unhandled;
  std::map<std::uint32_t, std::size_t> bs_versions;
  std::size_t nif_count = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    const auto archive_start = nif_count;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      ++nif_count;
      const auto bytes = reader.extract(i);
      try {
        const auto nif = fo4::nif::read_nif(bytes);
        ++bs_versions[nif.bs_version()];
        for (const auto& block : nif.blocks()) {
          if (!is_geometry_name(block.type)) continue;
          ++histogram[block.type];
          if (!is_handled_geometry(block.type)) unhandled.emplace(block.type, path);
        }
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          const auto vertex_count = shape.verts.size() / 3;
          EXPECT_TRUE(std::all_of(shape.tris.begin(), shape.tris.end(),
                                  [vertex_count](std::uint16_t index) { return index < vertex_count; }))
              << path;
          fo4::nif::shape_to_global(nif, shape.block);
        }
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
    RecordProperty(reader.path().filename().string() + " nif_count", std::to_string(nif_count - archive_start));
  }

  EXPECT_GT(nif_count, 0U);
  for (const auto& [type, example] : unhandled) {
    ADD_FAILURE() << "unhandled geometry block type " << type << " (first seen in " << example << ")";
  }
  for (const auto& [version, count] : bs_versions) {
    RecordProperty("bs_version_" + std::to_string(version) + "_nif_count", std::to_string(count));
  }
  for (const auto& [type, count] : histogram) std::cout << type << ": " << count << '\n';
}
