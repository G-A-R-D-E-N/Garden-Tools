#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/placed_decode.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

using fo4::StaticCollectionError;
using fo4::StaticPlacementView;
using fo4::StaticCollectionPartView;
using fo4::placed_decode::decode_static_collection_parts;

using StaticPartTargetResolver = std::function<const Record*(const FormKey&)>;

bool add_static_collection_part(Plugin& plugin,
                                const FormKey& scol,
                                const FormKey& static_record,
                                std::span<const StaticPlacementView> placements,
                                const StaticPartTargetResolver& resolve_target = {});

bool remove_static_collection_part(Plugin& plugin, const FormKey& scol, std::size_t part_index);

bool set_static_collection_part_placements(Plugin& plugin,
                                           const FormKey& scol,
                                           std::size_t part_index,
                                           std::span<const StaticPlacementView> placements);

}  // namespace fo4::forms
