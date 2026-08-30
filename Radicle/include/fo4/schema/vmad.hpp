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

// True when the VMAD uses a property layout Radicle cannot scan (Variable or
// Variable Array property types). vmad_formid_offsets returns no offsets for
// such payloads, so callers that need complete link coverage must consult
// this flag and report the gap instead of silently under-reporting.
bool vmad_has_unsupported_properties(std::span<const std::byte> data,
                                      Signature record_signature);

// Returns nullopt when the VMAD uses a property layout Radicle cannot safely
// remap yet. Malformed/truncated VMAD remains tolerant and returns an empty
// vector so read-only inspection retains its previous behavior.
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
  std::vector<QuestVmadFragmentBinding> fragments;
  std::vector<QuestVmadAliasBinding> aliases;
};

QuestVmadLayout inspect_quest_vmad(std::span<const std::byte> data);

}  // namespace fo4
