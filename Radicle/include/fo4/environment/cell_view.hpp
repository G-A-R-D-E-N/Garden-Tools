#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/forms/static_collection.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::env {

class CellViewError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct CellSummary {
  FormKey key;
  std::string editor_id;
  RecordName name;
  bool interior{false};
  std::optional<CellGrid> grid;
  std::optional<FormKey> worldspace;
  std::size_t winning_plugin{0};
  bool localized{false};
};

struct DecalInfo {
  std::string diffuse;
  std::optional<float> width;
  std::optional<float> height;
};

struct ScolPart {
  std::string model_path;
  std::vector<forms::StaticPlacementView> placements;
};

struct BaseObjectInfo {
  FormKey key;
  Signature signature{};
  std::string editor_id;
  std::string type_name;
  std::string model_path;
  std::optional<DecalInfo> decal;
  std::vector<ScolPart> scol_parts;
  bool editor_marker{false};
};

struct CellReferenceListing {
  FormKey key;
  forms::PlacedReferenceKind kind{forms::PlacedReferenceKind::object};
  std::string record_type;
  std::string editor_id;
  std::optional<FormKey> base;
  std::optional<BaseObjectInfo> base_info;
  forms::PositionRotation transform{};
  float scale{1.0f};
  bool persistent{false};
  bool initially_disabled{false};
  std::size_t winning_plugin{0};
};

struct CellReferences {
  CellSummary cell;
  std::vector<CellReferenceListing> references;
  std::size_t with_model_count{0};
};

std::optional<FormKey> resolve_cell_id(const Environment& env, std::string_view id);
std::optional<FormKey> resolve_worldspace_id(const Environment& env, std::string_view id);
std::optional<FormKey> find_cell_at_grid(const Environment& env,
                                         const FormKey& world,
                                         std::int32_t x,
                                         std::int32_t y);
std::optional<CellSummary> describe_cell(const Environment& env, const FormKey& cell);

std::string base_type_name(Signature signature);
std::optional<BaseObjectInfo> describe_base_object(const Environment& env, const FormKey& key);

CellReferences list_cell_references(const Environment& env, const FormKey& cell);

}  // namespace fo4::env
