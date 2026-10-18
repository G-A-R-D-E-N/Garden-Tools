#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>

namespace fo4 {

enum class DestructibleStageMutationStatus {
  updated,
  record_missing,
  record_mismatch,
  header_missing,
  index_out_of_range,
  too_many_stages,
};

DestructibleStageMutationStatus insert_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index);

DestructibleStageMutationStatus remove_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index);

}  // namespace fo4
