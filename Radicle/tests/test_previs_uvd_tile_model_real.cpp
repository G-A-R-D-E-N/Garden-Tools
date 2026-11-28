#include "fo4/archive/ba2_reader.hpp"
#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_build.hpp"
#include "fo4/previs/uvd_tile_check.hpp"
#include "fo4/previs/uvd_tile_header.hpp"
#include "fo4/previs/uvd_tile_model.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace fo4::previs;

struct Sweep {
  std::size_t tomes{0};
  std::size_t parsed{0};
  std::size_t stage1{0};
  std::size_t stage2{0};
  std::size_t stage3{0};
  std::size_t lod_tiles{0};
  std::size_t lod_exact{0};
  UvdTileClaims claims;
  std::vector<std::string> failures;

  void note(std::string text) {
    if (failures.size() < 8) {
      failures.push_back(std::move(text));
    }
  }
};

std::string lowered(std::string text) {
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

bool is_general_archive(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::array<char, 12> header{};
  in.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (!in) {
    return false;
  }
  return header[0] == 'B' && header[1] == 'T' && header[2] == 'D' && header[3] == 'X' && header[8] == 'G' && header[9] == 'N' &&
         header[10] == 'R' && header[11] == 'L';
}

std::vector<std::byte> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    throw std::runtime_error("cannot open " + path.string());
  }
  std::vector<std::byte> out(static_cast<std::size_t>(in.tellg()));
  in.seekg(0);
  if (!out.empty()) {
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  }
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return out;
}

void check_tome(const std::string& label, const std::vector<std::byte>& bytes, Sweep& sweep) {
  ++sweep.tomes;
  UvdTileModel model;
  try {
    model = parse_uvd_tiles(bytes);
  } catch (const UvdTileLayoutError& e) {
    sweep.note(label + ": parse refused: " + e.reason());
    return;
  }
  ++sweep.parsed;
  const auto stages = uvd_tile_round_trips(bytes, model);
  if (stages.stage1) {
    ++sweep.stage1;
  } else {
    const auto written = write_uvd_tiles(model);
    const auto diff = std::mismatch(written.begin(), written.end(), bytes.begin(), bytes.end());
    sweep.note(label + ": stage 1 differs at byte " + std::to_string(diff.first - written.begin()) + " (written " +
               std::to_string(written.size()) + ", original " + std::to_string(bytes.size()) + ")");
  }
  if (stages.stage2) {
    ++sweep.stage2;
  } else {
    sweep.note(label + ": stage 2 (tile interior decode and encode) differs");
  }
  if (stages.stage3) {
    ++sweep.stage3;
  } else {
    sweep.note(label + ": stage 3 (header sections decode and encode) differs");
  }
  validate_uvd_tiles(model, sweep.claims);
  try {
    const auto header = decode_uvd_header(model);
    const auto bodies = decode_uvd_bodies(model);
    for (std::size_t slot = 0; slot < bodies.slots.size(); ++slot) {
      if (!bodies.slots[slot]) {
        continue;
      }
      ++sweep.lod_tiles;
      if (uvd_tile_lod(bodies.slots[slot]->box, model.lod_base) == header.lod[slot]) {
        ++sweep.lod_exact;
      }
    }
  } catch (const UvdTileLayoutError& e) {
    sweep.note(label + ": decode for the LOD formula refused: " + e.reason());
  }
}

void require_clean(const Sweep& sweep, const char* corpus) {
  for (const auto& line : sweep.claims.lines()) std::cout << "  " << line << "\n";
  std::cout << corpus << ": " << sweep.tomes << " tomes, parsed " << sweep.parsed << ", stage 1 " << sweep.stage1 << ", stage 2 "
            << sweep.stage2 << ", stage 3 " << sweep.stage3 << "\n";
  std::cout << corpus << ": tile LOD == cbrt(volume) / lod base on " << sweep.lod_exact << "/" << sweep.lod_tiles << " tiles\n";
  for (const auto& failure : sweep.failures) ADD_FAILURE() << failure;
  EXPECT_EQ(sweep.lod_exact, sweep.lod_tiles);
  EXPECT_EQ(sweep.parsed, sweep.tomes);
  EXPECT_EQ(sweep.stage1, sweep.tomes);
  EXPECT_EQ(sweep.stage2, sweep.tomes);
  EXPECT_EQ(sweep.stage3, sweep.tomes);
  for (const auto& violation : sweep.claims.violations()) {
    ADD_FAILURE() << "violated invariant " << violation.claim << ": " << violation.failed << "/" << violation.of;
  }
  for (std::size_t i = 0; i < k_uvd_claim_count; ++i) {
    const auto claim = static_cast<UvdClaim>(i);
    if (uvd_claim_info(claim).enforced) {
      EXPECT_GT(sweep.claims.count(claim).total, 0u) << "enforced claim never exercised: " << uvd_claim_info(claim).name;
    }
  }
}

struct Decoded {
  UvdTileModel model;
  UvdBodies bodies;
  UvdHeader header;
};

Decoded decode_all(const std::vector<std::byte>& bytes) {
  Decoded d;
  d.model = parse_uvd_tiles(bytes);
  d.bodies = decode_uvd_bodies(d.model);
  d.header = decode_uvd_header(d.model);
  return d;
}

constexpr std::array<std::pair<const char*, std::uint32_t>, 17> k_region_fields{{{"topKd", 0x30},
                                                                                {"topSplits", 0x3c},
                                                                                {"slotPaths", 0x98},
                                                                                {"lod", 0x9c},
                                                                                {"cellStarts", 0x88},
                                                                                {"runA", 0x58},
                                                                                {"runB", 0x60},
                                                                                {"clusterNodes", 0x80},
                                                                                {"clusterPortals", 0x84},
                                                                                {"gateRecords", 0x70},
                                                                                {"gateLists", 0x78},
                                                                                {"objectBoxes", 0x44},
                                                                                {"objectDistances", 0x48},
                                                                                {"objectIds", 0x50},
                                                                                {"tileTable", 0xa0},
                                                                                {"leafRows", 0xa4},
                                                                                {"children", 0xa8}}};

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, bytes.data() + at, 4);
  return v;
}

std::vector<std::string> section_order(const std::vector<std::byte>& bytes, const std::vector<std::string>* only) {
  std::vector<std::pair<std::uint32_t, std::string>> present;
  for (const auto& [name, field] : k_region_fields) {
    const std::uint32_t at = u32_at(bytes, field);
    if (at != 0 && (only == nullptr || std::find(only->begin(), only->end(), name) != only->end())) {
      present.emplace_back(at, name);
    }
  }
  std::sort(present.begin(), present.end());
  std::vector<std::string> names;
  for (const auto& entry : present) names.push_back(entry.second);
  return names;
}

bool all_aligned(const std::vector<std::byte>& bytes) {
  return bytes.size() % 16 == 0 && std::all_of(k_region_fields.begin(), k_region_fields.end(), [&](const auto& f) { return u32_at(bytes, f.second) % 16 == 0; });
}

const std::map<std::string, std::string>& difference_reasons() {
  static const std::map<std::string, std::string> reasons{
      {"world box", "retail objects can stick out of the retail world box; the builder joins the cell box with every object box"},
      {"tile box", "follows the world box"},
      {"cells", "retail splits an occluded tile into several cells; the builder keeps one open cell"},
      {"cell fields", "retail cell list, cluster and box encode occluders (terrain raises the cell floor); the builder lists every object in a full-box cell"},
      {"portals", "retail has a closed floor face or several cells; the builder has six open outside faces"},
      {"planes and bsp", "retail splits a cell with planes where occluders cut it; the builder has none"},
      {"kd", "retail kd tree maps several cells; the builder has one leaf"},
      {"cluster", "retail cluster box and count follow the occluders"},
      {"run lists", "retail runs index several cell object sets; the builder has one run of every object"},
      {"object distances", "the builder omits the optional distance table"},
      {"gates", "gates join retail cells; the builder has none"},
      {"cluster portals", "cluster portals join retail clusters; the builder has none"},
      {"child trees", "retail face trees carry the occluder footprint; the builder has trivial open trees"},
      {"leaf rows", "retail leaf-match words describe the face trees"},
  };
  return reasons;
}

std::vector<std::string> structural_differences(const Decoded& retail, const Decoded& built) {
  std::vector<std::string> fields;
  const UvdTileBody& rt = *retail.bodies.slots[0];
  const UvdTileBody& bt = *built.bodies.slots[0];
  const auto note = [&fields](bool different, const char* field) {
    if (different) {
      fields.push_back(field);
    }
  };
  note(retail.header.world_min != built.header.world_min || retail.header.world_max != built.header.world_max, "world box");
  note(rt.box != bt.box, "tile box");
  note(rt.cells.size() != bt.cells.size(), "cells");
  note(rt.cells.size() == bt.cells.size() &&
           std::any_of(rt.cells.begin(), rt.cells.end(), [&](const UvdCell& c) {
             const UvdCell& o = bt.cells[static_cast<std::size_t>(&c - rt.cells.data())];
             return c.list_start != o.list_start || c.list_count != o.list_count || c.cluster != o.cluster || c.tag != o.tag || c.x != o.x || c.y != o.y || c.z != o.z;
           }),
       "cell fields");
  note(rt.portals.size() != bt.portals.size() || std::any_of(rt.portals.begin(), rt.portals.end(), [](const UvdPortal& p) { return p.outside == 0; }), "portals");
  note(!rt.planes.empty() || !rt.bsp.empty(), "planes and bsp");
  note(!rt.kd || !bt.kd || rt.kd->node_count != bt.kd->node_count || rt.kd->map_width != bt.kd->map_width || rt.kd->entries != bt.kd->entries, "kd");
  note(rt.cluster_count != bt.cluster_count || retail.header.clusters.size() != built.header.clusters.size() ||
           (!retail.header.clusters.empty() && (retail.header.clusters[0].x != built.header.clusters[0].x || retail.header.clusters[0].y != built.header.clusters[0].y ||
                                                retail.header.clusters[0].z != built.header.clusters[0].z)),
       "cluster");
  note(retail.header.run_a != built.header.run_a || !retail.header.run_b.empty() || retail.header.widths != built.header.widths, "run lists");
  note(retail.header.objects.distances.has_value(), "object distances");
  note(retail.header.gates.count != 0 || !retail.header.gates.records.empty(), "gates");
  note(!retail.header.cluster_portals.empty(), "cluster portals");
  note(retail.header.child_trees.size() != built.header.child_trees.size() ||
           std::any_of(retail.header.child_trees.begin(), retail.header.child_trees.end(), [](const auto& t) { return !t || t->node_count != 1 || t->entries != std::vector<std::uint32_t>{0}; }),
       "child trees");
  note(retail.header.child_rows.size() != built.header.child_rows.size() || retail.header.child_rows[0].words != built.header.child_rows[0].words ||
           !retail.header.child_rows[0].block.empty(),
       "leaf rows");
  return fields;
}

}  // namespace

// Regression: a wrong region order, size rule, derived field, kd rank table or tile decode makes parse, write or an enforced invariant fail on a shipped tome while synthetic tomes still pass.
TEST(PrevisReal, UvdTileModelRoundTripsEveryShippedTome) {
  const char* env = std::getenv("FO4_DATA_DIR");
  if (env == nullptr || *env == '\0') {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const fs::path dir{env};
  const auto build = detect_game_build(dir);
  record_game_build(build);
  std::vector<fs::path> archives;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) == ".ba2" && is_general_archive(entry.path())) {
      archives.push_back(entry.path());
    }
  }
  std::sort(archives.begin(), archives.end());
  Sweep sweep;
  for (const auto& path : archives) {
    auto reader = fo4::archive::Ba2Reader::open(path);
    for (std::size_t index = 0; index < reader.entries().size(); ++index) {
      const auto& stored = reader.entries()[index].path;
      const auto name = lowered(stored);
      if (name.size() < 13 || name.substr(name.size() - 4) != ".uvd") {
        continue;
      }
      if (name.compare(0, 4, "vis\\") != 0 && name.compare(0, 4, "vis/") != 0) {
        continue;
      }
      check_tome(path.filename().string() + ":" + stored, reader.extract(index), sweep);
    }
  }
  RecordProperty("tomes", static_cast<int>(sweep.tomes));
  require_clean(sweep, "Data archives");
  if (!exact_counts_known(build)) {
    GTEST_SKIP() << "exact tome counts are not measured for this build";
  }
  EXPECT_EQ(sweep.tomes, 1422u);
}

// Regression: the same round trip must hold on a second retail corpus (set RADICLE_PRP_TOME_DIR to a folder of extracted .uvd files), so a layout rule fitted to one banner version fails here.
TEST(PrevisReal, UvdTileModelRoundTripsAnExtractedTomeFolder) {
  const char* env = std::getenv("RADICLE_PRP_TOME_DIR");
  if (env == nullptr || *env == '\0') {
    GTEST_SKIP() << "set RADICLE_PRP_TOME_DIR to a folder of extracted .uvd tomes";
  }
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(fs::path{env})) {
    if (entry.is_regular_file() && lowered(entry.path().extension().string()) == ".uvd") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  ASSERT_FALSE(files.empty()) << "no .uvd files under " << env;
  Sweep sweep;
  for (const auto& file : files) check_tome(file.string(), read_file(file), sweep);
  RecordProperty("tomes", static_cast<int>(sweep.tomes));
  require_clean(sweep, "Extracted folder");
}

// Regression: a builder whose header words, section order, alignment, LOD or open-cell layout drift from the shipped single-tile tomes writes a tome the tile model accepts but retail would not; the open shipped tomes must be reproduced byte for byte and every other difference must have a stated reason.
TEST(PrevisReal, UvdConservativeBuilderMatchesTheSmallestShippedTomes) {
  const char* env = std::getenv("FO4_DATA_DIR");
  if (env == nullptr || *env == '\0') {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  std::vector<fs::path> archives;
  for (const auto& entry : fs::directory_iterator(fs::path{env})) {
    if (lowered(entry.path().extension().string()) == ".ba2" && is_general_archive(entry.path())) {
      archives.push_back(entry.path());
    }
  }
  std::sort(archives.begin(), archives.end());
  std::size_t examined = 0;
  std::size_t identical = 0;
  std::map<std::string, std::size_t> differing;
  for (const auto& path : archives) {
    auto reader = fo4::archive::Ba2Reader::open(path);
    for (std::size_t index = 0; index < reader.entries().size(); ++index) {
      const auto name = lowered(reader.entries()[index].path);
      const bool vis = name.compare(0, 4, "vis\\") == 0 || name.compare(0, 4, "vis/") == 0;
      if (name.size() < 13 || name.substr(name.size() - 4) != ".uvd" || !vis || reader.entries()[index].unpacked_size() > 4096) {
        continue;
      }
      const auto bytes = reader.extract(index);
      const auto retail = decode_all(bytes);
      if (retail.model.slots.size() != 1 || !retail.bodies.slots[0]) {
        continue;
      }
      ++examined;
      const UvdTileBody& tile = *retail.bodies.slots[0];
      UvdBuildInput in;
      in.cell_box = std::array<float, 6>{retail.header.world_min[0], retail.header.world_min[1], retail.header.world_min[2],
                                         retail.header.world_max[0], retail.header.world_max[1], retail.header.world_max[2]};
      for (std::size_t i = 0; i < retail.header.objects.ids.size(); ++i) {
        UvdBuildObject object;
        object.box = retail.header.objects.boxes[i];
        object.id = retail.header.objects.ids[i];
        in.objects.push_back(object);
      }
      in.expand = tile.expand;
      const auto built_bytes = write_uvd_tome_tiled(in);
      const auto built = decode_all(built_bytes);
      const std::string label = name + " (" + std::to_string(bytes.size()) + " bytes, " + std::to_string(in.objects.size()) + " objects)";
      EXPECT_TRUE(all_aligned(built_bytes)) << label;
      EXPECT_TRUE(all_aligned(bytes)) << label;
      EXPECT_EQ(built.model.banner.substr(0, 8), retail.model.banner.substr(0, 8)) << label;
      EXPECT_EQ(built.model.lod_base, retail.model.lod_base) << label;
      EXPECT_EQ(built.model.reserved10, retail.model.reserved10) << label;
      EXPECT_EQ(built.model.path_bits, retail.model.path_bits) << label;
      EXPECT_EQ(built.model.slot_paths, retail.model.slot_paths) << label;
      EXPECT_EQ(built.model.cell_starts.size(), retail.model.cell_starts.size()) << label;
      EXPECT_EQ(u32_at(built_bytes, 0x8c), u32_at(bytes, 0x8c)) << label;
      EXPECT_EQ(u32_at(built_bytes, 0x40), u32_at(bytes, 0x40)) << label;
      const auto built_order = section_order(built_bytes, nullptr);
      EXPECT_EQ(built_order, section_order(bytes, &built_order)) << label;
      if (built_bytes == bytes) {
        ++identical;
        continue;
      }
      const auto fields = structural_differences(retail, built);
      EXPECT_FALSE(fields.empty()) << label << ": bytes differ but no structural difference was found";
      for (const auto& field : fields) {
        ++differing[field];
        EXPECT_TRUE(difference_reasons().contains(field)) << label << ": unexplained difference in " << field;
      }
    }
  }
  std::cout << "single-tile tomes of at most 4096 bytes: " << examined << ", reproduced byte for byte: " << identical << "\n";
  for (const auto& [field, count] : differing) {
    std::cout << "  differs in " << field << " on " << count << " tomes: " << difference_reasons().at(field) << "\n";
  }
  RecordProperty("single_tile_tomes", static_cast<int>(examined));
  RecordProperty("reproduced", static_cast<int>(identical));
  ASSERT_GT(examined, 0u) << "no single-tile tome of at most 4096 bytes found";
  EXPECT_GE(identical, 1u) << "no shipped open single-tile tome was reproduced";
}
