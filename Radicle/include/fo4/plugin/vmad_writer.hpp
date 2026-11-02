#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/vmad.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace fo4 {

struct VmadObjectValue {
  FormKey target;
  std::int16_t alias{-1};
};

// Constraint: remap_scene_phase_indices treats this entry as the phase the edit removes.
inline constexpr std::uint32_t k_scene_phase_removed = 0xFFFFFFFFu;

using VmadValue = std::variant<VmadObjectValue, std::string, std::int32_t, float, bool>;

ScalarMutationStatus set_vmad_property(Plugin& plugin, const FormKey& record,
                                      std::size_t script, std::size_t property,
                                      const VmadValue& value, std::size_t element = 0);

enum class VmadStructureMutationStatus {
  updated,
  unchanged,
  already_present,
  record_missing,
  subrecord_missing,
  index_out_of_range,
  not_authorable,
  binding_missing,
};

VmadStructureMutationStatus add_vmad(Plugin& plugin, const FormKey& record);

VmadStructureMutationStatus add_vmad_script(Plugin& plugin, const FormKey& record,
                                            std::size_t index, std::string_view name,
                                            std::uint8_t flags);

VmadStructureMutationStatus remove_vmad_script(Plugin& plugin, const FormKey& record,
                                               std::size_t index);

VmadStructureMutationStatus add_vmad_property(Plugin& plugin, const FormKey& record,
                                              std::size_t script, std::size_t index,
                                              const VmadProperty& property);

VmadStructureMutationStatus remove_vmad_property(Plugin& plugin, const FormKey& record,
                                                 std::size_t script, std::size_t index);

VmadStructureMutationStatus replace_vmad_property(Plugin& plugin, const FormKey& record,
                                                  std::size_t script, std::size_t index,
                                                  const VmadProperty& property);

VmadStructureMutationStatus add_quest_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                    std::uint16_t stage, std::int32_t stage_index,
                                                    std::string_view script,
                                                    std::string_view function);

VmadStructureMutationStatus set_quest_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                    std::uint16_t stage, std::string_view script,
                                                    std::string_view function);

VmadStructureMutationStatus set_quest_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                    std::uint16_t stage, std::int32_t stage_index,
                                                    std::string_view script,
                                                    std::string_view function);

VmadStructureMutationStatus remove_quest_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                       std::uint16_t stage);

VmadStructureMutationStatus remove_quest_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                       std::uint16_t stage,
                                                       std::int32_t stage_index);

VmadStructureMutationStatus add_vmad_fragment(Plugin& plugin, const FormKey& record,
                                              std::uint32_t key, std::string_view script,
                                              std::string_view function);

VmadStructureMutationStatus set_vmad_fragment(Plugin& plugin, const FormKey& record,
                                              std::uint32_t key, std::string_view script,
                                              std::string_view function);

VmadStructureMutationStatus remove_vmad_fragment(Plugin& plugin, const FormKey& record,
                                                 std::uint32_t key);

VmadStructureMutationStatus add_scene_vmad_phase_fragment(Plugin& plugin, const FormKey& record,
                                                          std::uint32_t phase_index,
                                                          std::uint8_t phase_flag,
                                                          std::string_view script,
                                                          std::string_view function);

VmadStructureMutationStatus set_scene_vmad_phase_fragment(Plugin& plugin, const FormKey& record,
                                                          std::uint32_t phase_index,
                                                          std::uint8_t phase_flag,
                                                          std::string_view script,
                                                          std::string_view function);

VmadStructureMutationStatus remove_scene_vmad_phase_fragment(Plugin& plugin,
                                                             const FormKey& record,
                                                             std::uint32_t phase_index,
                                                             std::uint8_t phase_flag);

// Constraint: rewrites every scene phase fragment index through old_to_new; a removed phase is not representable.
VmadStructureMutationStatus remap_scene_phase_indices(Plugin& plugin,
                                                      const FormKey& record,
                                                      std::span<const std::uint32_t> old_to_new);

}
