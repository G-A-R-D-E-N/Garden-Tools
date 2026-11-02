#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class StaticCollectionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

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

// Decodes the SCOL Parts surface used by the cell viewer. Mutagen's Fallout 4
// oracle serializes each part as ONAM (nullable Static FormLink) followed by an
// optional DATA array of 28-byte StaticPlacement structs. Parts with a null
// Static link are omitted because they cannot contribute a renderable model.
std::vector<StaticCollectionPartView> decode_static_collection_parts(
    const Plugin& plugin,
    const Record& record);

bool add_static_collection_part(Plugin& plugin,
                                const FormKey& scol,
                                const FormKey& static_record,
                                std::span<const StaticPlacementView> placements);

bool remove_static_collection_part(Plugin& plugin, const FormKey& scol, std::size_t part_index);

bool set_static_collection_part_placements(Plugin& plugin,
                                           const FormKey& scol,
                                           std::size_t part_index,
                                           std::span<const StaticPlacementView> placements);

}  // namespace fo4::forms
