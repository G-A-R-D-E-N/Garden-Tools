#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/connect_points.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::ConnectChildren;
using fo4::nif::ConnectParents;
using fo4::nif::NifFile;

constexpr std::size_t k_max_blocks = 400;
const std::string k_workshop_parent = "WorkshopConnectPoints";

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  return std::filesystem::path(lower(path)).extension() == ".nif";
}

std::span<const std::byte> block_span(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return std::span<const std::byte>(nif.bytes()).subspan(info.offset, info.size);
}

bool is_node(const NifFile& nif, const std::string& name) {
  return std::any_of(nif.nodes().begin(), nif.nodes().end(), [&](const auto& node) { return node.name == name; });
}

nlohmann::json set_op(std::uint32_t block, std::size_t index, const std::string& field, nlohmann::json value) {
  return {{"op", "set"}, {"block", block}, {"index", index}, {"field", field}, {"value", std::move(value)}};
}

// Constraint: the ops rewrite every field with its current value, so an unchanged block must come back byte for byte.
nlohmann::json current_value_ops(const NifFile& nif, std::uint32_t block) {
  auto ops = nlohmann::json::array();
  if (nif.blocks()[block].type == "BSConnectPoint::Parents") {
    const auto value = fo4::nif::read_connect_parents(nif, block);
    for (std::size_t i = 0; i < value.points.size(); ++i) {
      const auto& point = value.points[i];
      EXPECT_TRUE(point.parent.empty() || point.parent == k_workshop_parent || is_node(nif, point.parent))
          << "block " << block << " point " << i << " parent '" << point.parent << "' is not a node";
      EXPECT_TRUE(std::isfinite(point.scale));
      ops.push_back(set_op(block, i, "parent", point.parent));
      ops.push_back(set_op(block, i, "name", point.name));
      ops.push_back(set_op(block, i, "rotation", std::vector<float>(point.rotation, point.rotation + 4)));
      ops.push_back(set_op(block, i, "translation", std::vector<float>(point.translation, point.translation + 3)));
      ops.push_back(set_op(block, i, "scale", point.scale));
    }
    return ops;
  }
  const auto value = fo4::nif::read_connect_children(nif, block);
  ops.push_back({{"op", "set"}, {"block", block}, {"field", "skinned"}, {"value", value.skinned}});
  for (std::size_t i = 0; i < value.names.size(); ++i) ops.push_back(set_op(block, i, "name", value.names[i]));
  return ops;
}

}  // namespace

TEST(NifConnectPointsRealData, VanillaConnectPointBlocksRoundTripByteIdentically) {
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
  std::size_t checked = 0;
  std::size_t parents_blocks = 0;
  std::size_t children_blocks = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && checked < k_max_blocks; ++i) {
      if (!is_nif(reader.entries()[i].path)) continue;
      const auto bytes = reader.extract(i);
      std::optional<NifFile> parsed;
      try {
        parsed.emplace(fo4::nif::read_nif(bytes));
      } catch (const fo4::parse_error&) {
        continue;
      }
      const auto& nif = *parsed;
      for (const auto block : fo4::nif::connect_point_blocks(nif)) {
        if (checked >= k_max_blocks) break;
        ++checked;
        const auto& type = nif.blocks()[block].type;
        if (type == "BSConnectPoint::Parents") {
          ++parents_blocks;
        } else {
          ++children_blocks;
        }
        const auto edited = fo4::nif::apply_connect_point_edits_json(nif, current_value_ops(nif, block).dump());
        const auto after = fo4::nif::read_nif(edited);
        const auto original = block_span(nif, block);
        const auto rewritten = block_span(after, block);
        EXPECT_TRUE(std::equal(original.begin(), original.end(), rewritten.begin(), rewritten.end()))
            << reader.entries()[i].path << " block " << block << " (" << type << ")";
      }
    }
  }
  EXPECT_GT(parents_blocks + children_blocks, 0U) << "no BSConnectPoint blocks found in the general archives";
}
