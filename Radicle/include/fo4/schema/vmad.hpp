#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fo4 {

std::vector<std::size_t> vmad_formid_offsets(std::span<const std::byte> data,
                                              Signature record_signature);

struct VmadCoverage {
  bool unsupported{false};
  bool failed{false};
  bool trailing_data{false};
  bool fully_consumed{false};

  bool complete() const {
    return !unsupported && !failed && !trailing_data && fully_consumed;
  }
};

VmadCoverage vmad_coverage(std::span<const std::byte> data,
                           Signature record_signature);

std::optional<std::vector<std::size_t>> vmad_formid_offsets_for_remap(
    std::span<const std::byte> data,
    Signature record_signature);

struct QuestVmadBindings {
  bool binds_stages{false};
  bool binds_aliases{false};
  bool unparseable{false};
};

QuestVmadBindings quest_vmad_bindings(std::span<const std::byte> data);

struct QuestVmadAliasBinding {
  std::size_t alias_id_offset{0};
  std::int16_t alias_id{0};
};

struct QuestVmadFragmentBinding {
  std::size_t stage_offset{0};
  std::uint16_t stage{0};
  std::size_t stage_index_offset{0};
  std::int32_t stage_index{0};
};

struct QuestVmadLayout {
  bool unparseable{false};
  bool unsupported{false};
  std::int16_t object_format{0};
  std::vector<QuestVmadFragmentBinding> fragments;
  std::vector<QuestVmadAliasBinding> aliases;
};

QuestVmadLayout inspect_quest_vmad(std::span<const std::byte> data);

}  // namespace fo4
