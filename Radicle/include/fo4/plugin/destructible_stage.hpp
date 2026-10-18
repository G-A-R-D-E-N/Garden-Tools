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

enum class DestructibleStageMemberStatus {
  updated,
  record_missing,
  record_mismatch,
  header_missing,
  stage_index_out_of_range,
  not_optional_member,
  no_default,
  member_present,
  member_absent,
};

DestructibleStageMemberStatus insert_destructible_stage_member(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::size_t stage_index,
                                                               Signature member);

DestructibleStageMemberStatus remove_destructible_stage_member(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::size_t stage_index,
                                                               Signature member);

}  // namespace fo4
