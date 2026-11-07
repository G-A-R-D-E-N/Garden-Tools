#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

constexpr std::string_view k_connect_parents_type = "BSConnectPoint::Parents";
constexpr std::string_view k_connect_children_type = "BSConnectPoint::Children";
// Invariant: vanilla workshop meshes name this parent with no node of that name in the file.
constexpr std::string_view k_workshop_connect_parent = "WorkshopConnectPoints";

// Invariant: field order follows nif.xml:BSConnectPoint; rotation is w, x, y, z (nif.xml:Quaternion).
struct ConnectPoint {
  std::string parent;
  std::string name;
  float rotation[4]{1.0f, 0.0f, 0.0f, 0.0f};
  float translation[3]{};
  float scale{1.0f};
};

struct ConnectParents {
  std::string name;
  std::vector<ConnectPoint> points;
};

struct ConnectChildren {
  std::string name;
  bool skinned{false};
  std::vector<std::string> names;
};

std::vector<std::uint32_t> connect_point_blocks(const NifFile& nif);
// Requires a BSConnectPoint::Parents block; throws parse_error for any other block.
ConnectParents read_connect_parents(const NifFile& nif, std::uint32_t block);
// Requires a BSConnectPoint::Children block; throws parse_error for any other block.
ConnectChildren read_connect_children(const NifFile& nif, std::uint32_t block);
// Invariant: an empty, WorkshopConnectPoints or unknown parent uses the point translation alone.
std::array<float, 3> connect_point_world_position(const NifFile& nif, const ConnectPoint& point);

// Requires a JSON array of ops {op, block, index?, field?, value?}; see docs/NIF.md for the op table.
// Invariant: refuses a missing block, a bad index, a non-finite number and a parent that is not a node in the file, unless the same name already sits on a point in that block (set parent ops only).
std::vector<std::byte> apply_connect_point_edits_json(const NifFile& nif, std::string_view text);

}  // namespace fo4::nif
