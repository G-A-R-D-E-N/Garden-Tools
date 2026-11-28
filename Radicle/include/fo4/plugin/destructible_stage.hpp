#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4 {

enum class DestructibleHeaderStatus {
  updated,
  record_missing,
  record_mismatch,
  header_present,
  header_missing,
  unknown_order,
  too_many_stages,
};

DestructibleHeaderStatus insert_destructible_header(Plugin& plugin, const FormKey& record_key);

DestructibleHeaderStatus remove_destructible_header(Plugin& plugin, const FormKey& record_key);

enum class DestructibleStageMutationStatus {
  updated,
  record_missing,
  record_mismatch,
  header_missing,
  index_out_of_range,
  too_many_stages,
  // Invariant: a stage member after the stage's DSTF belongs to no stage, so the run cannot be
// Constraint: re-cut without guessing where the reader meant it to sit.
  interleaved_stages,
  // Invariant: the DEST stage count is the only owner of the run length; a count that disagrees
// Constraint: with the DSTD runs means the record already reads differently from how it was written.
  malformed_stages,
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
  model_required,
  interleaved_stages,
  malformed_stages,
};

DestructibleStageMemberStatus insert_destructible_stage_member(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::size_t stage_index,
                                                               Signature member);

DestructibleStageMemberStatus remove_destructible_stage_member(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::size_t stage_index,
                                                               Signature member);

// Invariant: one borrowed view of the whole DEST group. Member payloads point into the plugin's
// Constraint: record storage, so the view stays readable until that plugin is edited; the C ABI hands the same borrowed pointers to its caller.
struct DestructibleGroupMember {
  Signature signature{};
  std::span<const std::byte> data{};
};

struct DestructibleGroupStage {
  std::vector<DestructibleGroupMember> members{};
};

struct DestructibleGroupResistance {
  FormKey damage_type{};
  std::uint32_t value{0};
};

struct DestructibleGroupView {
  std::int32_t health{0};
  std::uint8_t stage_count{0};
  std::uint8_t flags{0};
  std::vector<DestructibleGroupResistance> resistances{};
  std::vector<DestructibleGroupStage> stages{};
};

enum class DestructibleGroupReadStatus {
  present,
  record_missing,
  record_mismatch,
  header_missing,
  interleaved_stages,
  malformed_stages,
};

// Constraint: a refusal leaves out_group untouched, so a caller never sees half a group.
DestructibleGroupReadStatus read_destructible_group(const Plugin& plugin,
                                                    const FormKey& record_key,
                                                    DestructibleGroupView& out_group);

}  // namespace fo4
