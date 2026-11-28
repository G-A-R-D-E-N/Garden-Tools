#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4 {

class PlacedReferenceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class PlacedReferenceKind {
  object,
  npc,
  variant,
};

struct PositionRotation {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float rx{0.0f};
  float ry{0.0f};
  float rz{0.0f};
};

struct PlacedReferenceView {
  const Record* record{nullptr};
  PlacedReferenceKind kind{PlacedReferenceKind::object};
  std::optional<FormKey> base;
  std::string editor_id;
  PositionRotation transform{};
  float scale{1.0f};
  bool persistent{false};
  bool deleted{false};
};

class StaticCollectionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline constexpr std::size_t k_static_placement_size = 28;

struct StaticPlacementView {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float rx{0.0f};
  float ry{0.0f};
  float rz{0.0f};
  float scale{0.0f};
};

struct StaticCollectionPartView {
  FormKey static_record;
  std::vector<StaticPlacementView> placements;
};

namespace placed_decode {

std::optional<PlacedReferenceView> decode_placed_reference(
    const Plugin& plugin,
    const Record& record,
    bool persistent = false);

std::vector<StaticCollectionPartView> decode_static_collection_parts(
    const Plugin& plugin,
    const Record& record);

}  // namespace placed_decode
}  // namespace fo4
